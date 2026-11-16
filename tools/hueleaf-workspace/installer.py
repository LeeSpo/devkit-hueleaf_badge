"""One install path for Release and local BIN files; no firmware hash checks."""
from pathlib import Path
import tempfile

from firmware_image import Firmware, layout_id, ota0_data, parse_table
from support import preflight, remote_layout


def ports():
    from serial.tools import list_ports
    return [(port.device, port.description) for port in list_ports.comports()]


def select_port(port=None):
    if port:
        return port
    available = ports()
    for index, (device, description) in enumerate(available, 1):
        print(f"  {index}. {device} — {description}")
    value = input("选择串口序号或输入串口名称：").strip()
    if value.isdigit() and 1 <= int(value) <= len(available):
        return available[int(value) - 1][0]
    if not value:
        raise ValueError("未选择设备")
    return value


def plan_install(firmware, snapshot):
    """Plan from a complete backup; never performs device I/O."""
    try:
        current = parse_table(snapshot[0x8000:0x9000])
    except preflight.ImageError:
        current = []
    if not firmware.complete:
        # A plain BIN is an application by convention. Do not inspect its type
        # or reject a user-selected file based on its contents or version.
        app = next((p for p in current if p[1:3] == (0, 0x10)), None)
        ota = next((p for p in current if p[1:3] == (1, 0)), None)
        if not app or not ota:
            raise ValueError("设备没有可用的 OTA 应用分区；首次安装请选择本项目发布的安装 BIN")
        return [(ota[3], ota0_data()), (app[3], firmware.data)], False
    target = firmware.metadata["layout_id"]
    try:
        same = layout_id(current) == target
    except ValueError:
        same = False
    if target == "remote-v1":
        if not same and not remote_layout.migration_allowed(current):
            raise ValueError("当前设备的 NVS/PHY 分区无法保留，未执行迁移")
        return firmware.payloads(), not same
    if not same:
        factory = preflight.factory_of(current)
        if not factory or factory[0] != 0x10000:
            raise ValueError("当前设备不能直接切换到此固件，备份已保存")
        size = preflight.app_image_size(snapshot, 0x10000)
        if size > 0x170000:
            raise ValueError("原厂应用超出保留区域，未执行写入")
    return firmware.payloads(), False


class Device:
    def __init__(self, port):
        self.port = port

    def backup(self, directory):
        try:
            mac = preflight.check_security(self.port)
            return preflight.make_backup(self.port, str(directory), f"hueleaf_{mac or 'unknown'}_")
        except SystemExit as exc:
            raise RuntimeError("设备检查或备份失败，安装已停止") from exc

    def write(self, payloads, migrate):
        with tempfile.TemporaryDirectory(prefix="hueleaf_install_") as temporary:
            files = []
            for offset, data in payloads:
                path = Path(temporary) / f"{offset:06x}.bin"
                path.write_bytes(data)
                files.extend((hex(offset), str(path)))
            if migrate:
                rc, _ = preflight.esptool(self.port, "erase_region", "0x10000", "0x3f0000")
                if rc:
                    raise RuntimeError(f"迁移擦除失败：{rc}；请保留备份并使用同一固件重试")
            rc, _ = preflight.esptool(self.port, "write_flash", "--flash_mode", "dio", "--flash_size", "4MB", *files)
            if rc:
                raise RuntimeError(f"固件写入失败：{rc}；备份已保留")


def install(path, directory, port=None, yes=False, preflight_only=False, device_factory=Device):
    firmware = Firmware.read(path)
    port = select_port(port)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    print("固件：", firmware.path)
    print("版本：", firmware.metadata.get("firmware_version", "本地应用 BIN"))
    print("设备：", port)
    device = device_factory(port)
    backup, snapshot = device.backup(directory)
    payloads, migrate = plan_install(firmware, snapshot)
    print("备份：", backup)
    print("写入区域：", ", ".join(f"{offset:#x} + {len(data):#x}" for offset, data in payloads))
    if migrate:
        print("本次安装将替换原厂应用区，保留 NVS/PHY；后续同布局更新不会重复擦除绑定区。")
    if preflight_only:
        print("预检和备份完成。")
        return 0
    if not yes and input("确认安装？[y/N]：").strip().lower() not in ("y", "yes"):
        print("已取消，备份已保存。")
        return 0
    device.write(payloads, migrate)
    print("安装完成。备份：", backup)
    return 0
