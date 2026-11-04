# HueLeaf · TF-card gallery firmware (community fork)

Offline photo-frame firmware for the OSPTEK HueLeaf (ESP32-C5 · 3.7″ 6-color e-paper), made to hang on a bag

<p align="center">
  <img src="docs/images/banner_en.png" alt="OSPTEK HueLeaf E-Paper Development Kit, Base Edition" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--C5-1E1E1E?style=for-the-badge" alt="MCU">
  <img src="https://img.shields.io/badge/PMU-AXP2101-D93A2B?style=for-the-badge" alt="PMU">
  <img src="https://img.shields.io/badge/E--Paper-3.7%22%206--Color-F0BE32?style=for-the-badge" alt="Display">
  <img src="https://img.shields.io/badge/Wi--Fi%206-2.4%20%2F%205%20GHz-2058C4?style=for-the-badge" alt="Wi-Fi">
  <img src="https://img.shields.io/badge/RTC-PCF8563-2E8B4A?style=for-the-badge" alt="RTC">
</p>

<p align="center">
  <a href="README.md">简体中文</a> &nbsp;|&nbsp; <b>English</b>
</p>

<img src="docs/images/divider.svg" alt="" width="100%" height="6">

## Overview

Images live on the TF card. Flip with BOOT and the power button, or double-tap the frame; upload from a PC over USB. No Wi-Fi, router or app needed.

- **Factory firmware stays intact** in its own partition: **boot without a TF card and you are back on the factory firmware.**
- Community firmware, not official OSPTEK. Items marked "untested" have not been verified on real hardware yet.

<p align="center"><img src="docs/images/device_photo.jpg" alt="HueLeaf" width="420"></p>

## 1. Back up (required)

```bash
pip install esptool
# hold BOOT while plugging in USB if download mode is not entered automatically
espefuse.py --chip esp32c5 -p /dev/ttyACM0 summary      # make sure Secure Boot / flash encryption are off
esptool.py --chip esp32c5 -p /dev/ttyACM0 read_flash 0 0x400000 backup.bin
```
Keep `backup.bin` safe. If `summary` shows Secure Boot or Flash Encryption enabled, **stop**: this firmware does not support it.

## 2. Flash

Get `dist/` from the Actions artifacts or build it:
```bash
cd firmware/hueleaf-gallery && idf.py set-target esp32c5 build && ./tools/package.sh
./dist/install/flash_install.sh /dev/ttyACM0     # writes bootloader/partition table/otadata/ota_0 only
# Windows: dist\install\flash_install.bat COM5
```
The installer backs up the whole flash and checks the factory app size before writing anything; details in the [developer notes](firmware/hueleaf-gallery/README.md).

## 3. TF card layout (FAT32)

```
/img/*.bin            images (172800 bytes each, made by hueleaf-push)
/img/index.txt        optional order, one file name per line; otherwise sorted by name
/update/firmware.bin  optional firmware update, applied on boot
/hueleaf.cfg          optional switches (section 6)
```
The current image is remembered by file name across power loss.

## 4. Adding images (hueleaf-push)

GUI and full command list: [tools/hueleaf-push/README.md](tools/hueleaf-push/README.md). You can also make a `.bin` with `--save-bin` and copy it to `/img/`.

```bash
cd tools/hueleaf-push && pip install -r requirements.txt
python hueleaf_push.py -p /dev/ttyACM0 photo.jpg             # convert + upload + show
python hueleaf_push.py -p /dev/ttyACM0 --list | --delete NAME | --show NAME | --log
python hueleaf_push.py -p /dev/ttyACM0 --firmware build/hueleaf_gallery.bin   # update via TF card
```

## 5. Buttons and double-tap

### Viewing images

| Action | Result |
|---|---|
| BOOT short | Next image (wraps) |
| Power short | Previous image (wraps) |
| BOOT hold (~1 s) | Open the image list |
| Double-tap the frame (`tap_next=1`, untested) | Next image |
| Power hold | **Don't**: hardware power-off |
| Reset | Reboot |

- One step at a time: presses and taps during an image refresh (10+ s) are **ignored**, so nothing queues up.
- The blue charge LED blinks when an input is accepted, not when it is ignored (untested: whether it lights on battery and is visible in daylight).

### In the image list

Names only, black and white, 14 per page; opens on the current image. The bottom hint follows `tap_next`.

| Action | `tap_next` off (default) | `tap_next` on |
|---|---|---|
| BOOT short | Cursor down | Cursor down |
| Power short | Next page | Cursor up |
| Double-tap | — | Next page |
| BOOT hold | Select and show | Select and show |
| Bottom hint | `BOOT down · PWR page · Hold select` | `BOOT down · PWR up` / `Tap×2 page · Hold select` |

- Cursor and pages wrap; the list closes after 60 s idle.
- Image names (including CJK) are rendered by the PC tool at upload; hand-copied files show their file name.

### Double-tap rules (untested)

- The frame must be still for ~1.5 s before tapping while viewing, 0.5 s in the list (usually hand-held).
- At least 1.5 s between accepted taps.
- Log lines `double tap accepted` / `double tap ignored (...)` (read with `--log`) help tune sensitivity.

## 6. `hueleaf.cfg` switches

Put `hueleaf.cfg` in the TF card root, one `key=value` per line, `#` for comments, reboot to apply.

| Switch | Default | Effect |
|---|---|---|
| `mono_fast` | `1` | Fast black/white refresh for the list and messages (ported from [FrameFilm](https://github.com/kirmisaki/FrameFilm)); `0` = factory full refresh |
| `tap_next` | `0` | `1` = double-tap to next image, and list controls switch as above |

- Settings are stored on the board: **removing the file or a line does not restore the default**; write `=0` / `=1` explicitly.

## 7. Troubleshooting

1. Boot without a TF card to get the factory firmware.
2. Download mode not entered: hold BOOT while plugging in USB, or hold BOOT and press reset.
3. Bricked: in download mode, `esptool.py --chip esp32c5 write_flash 0 backup.bin`.
4. Fell back to factory after a reset during first boot: rerun `flash_install.sh`, or `otatool.py -p PORT switch_ota_partition --slot 0`.

## Specifications

| Item | Spec |
| :--- | :--- |
| MCU | ESP32-C5 (ESP32-C5HF4) |
| Display | 3.7″ 6-color e-paper EPD037A02, 480 × 720 (black, white, red, yellow, blue, green) |
| Wireless | Dual-band Wi-Fi 6, 2.4 / 5 GHz |
| PMU | AXP2101 |
| RTC | PCF8563 |
| Accelerometer | MSA321 |
| Storage | 4 MB Flash, microSD |
| USB | Type-C, ESP32-C5 native USB |
| Buttons | Reset, Power, BOOT |
| LEDs | Charging (blue), Power on (green) |

Full schematic: [hardware/HueLeaf_Base_Schematic_V1.0.pdf](hardware/HueLeaf_Base_Schematic_V1.0.pdf). Component placement: [hardware/HueLeaf_Base_Placement.pdf](hardware/HueLeaf_Base_Placement.pdf).

## Developer notes

- How the firmware boots, partition table, `idf.py` flash guards: [firmware/hueleaf-gallery/README.md](firmware/hueleaf-gallery/README.md) (Chinese)
- Real-hardware test list: [firmware/hueleaf-gallery/docs/real_hw_test.md](firmware/hueleaf-gallery/docs/real_hw_test.md)
- Branches and archive tags: [firmware/hueleaf-gallery/docs/branches.md](firmware/hueleaf-gallery/docs/branches.md)
- Pin assignment and the factory firmware (web upload, Wi-Fi setup, factory buttons): upstream [osptek/devkit-hueleaf_base](https://github.com/osptek/devkit-hueleaf_base); this repo's `main` branch keeps the original content

## License

> **This fork (LeeSpo/devkit-hueleaf_badge) is distributed under [GPL-3.0](LICENSE).** Reason: the black/white fast refresh (MonoFast) waveform tables and command sequence in `firmware/hueleaf-gallery/main/screen_driver/mono_fast.c` are ported from [FrameFilm](https://github.com/kirmisaki/FrameFilm) by kirmisaki (GPL-3.0-or-later), so the derived firmware must be distributed under GPL-3.0. Upstream osptek code keeps its original Apache-2.0 notice, see [LICENSE-APACHE-2.0](LICENSE-APACHE-2.0); hardware/doc licensing is unchanged.

- Upstream code (`examples/` etc.): originally [Apache-2.0](LICENSE-APACHE-2.0), combined with GPL-3.0 code in this fork
- Hardware files and documentation (`hardware/`, `docs/`): [CC BY-NC-SA 4.0](LICENSE-CC-BY-NC-SA-4.0)
- Third-party components (e.g. Vue) remain under their original licenses
