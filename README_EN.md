# HueLeaf v2 · BLE Remote Gallery Firmware (Community Edition)

Offline photo-frame firmware for the OSPTEK HueLeaf (ESP32-C5 · 3.7″ 6-color e-paper), designed to hang on your bag or sit on your desk.

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

## 📖 Overview

This project provides an offline photo album firmware tailored for the **OSPTEK HueLeaf** development kit (powered by ESP32-C5 with a 3.7-inch 480×720 6-color E-Paper display).

Photos are stored on a TF / MicroSD card, and you can easily manage and upload images from your PC over USB. You can flip pages using physical buttons or by gently tapping the frame, or use the dedicated Android companion app for Bluetooth Low Energy (BLE) wireless control—no Wi-Fi or router needed.

<p align="center"><img src="docs/images/device_photo.jpg" alt="HueLeaf" width="420"></p>

> [!NOTE]
> - **Version Notice**: Starting from v2.0, development focuses exclusively on the BLE remote architecture. The legacy stock-preserving edition is frozen at v1.5.0 (see [Branches and Archive Policy](firmware/hueleaf-gallery/docs/branches.md)).
> - **Partition Layout**: v2 uses a dedicated partition layout (`remote-v1`) to maximize storage for gallery and BLE remote features. First-time installation replaces the factory app area; the installer automatically backs up the entire flash beforehand for easy recovery.
> - **Disclaimer**: This is a community open-source project and is not affiliated with or endorsed by OSPTEK.

---

## ✨ Key Features

- 🎨 **Vibrant 6-Color E-Paper Display**: Optimized dithering algorithms for the 3.7″ 480×720 display (black, white, yellow, red, blue, green).
- ⚡ **MonoFast Quick Refresh**: Black & white partial refresh (~0.2s) for image lists and status screens, eliminating frequent full-screen blinking.
- 📱 **BLE Wireless Remote Control**: Modern Android companion app (Material 3) supporting wireless gallery browsing (with CJK label support), remote photo switching, and battery/status monitoring.
- 🖥️ **Cross-Platform PC Tools**: Driver-free Web GUI and Python CLI supporting drag-and-drop crop, live preview, batch upload, and RTC time sync.
- ⏱️ **Scheduled Deep-Sleep Playback**: Automatic photo rotation at configurable intervals with ultra-low-power deep sleep between switches. Screen retains images with zero power consumption.
- 📳 **Motion / Tap Interaction**: Onboard 3-axis accelerometer allows double-tapping the frame to change pictures or wake from sleep.
- 🛡️ **Safe Flashing & Upgrades**: Cross-platform installer with automatic flash backup; supports automatic TF card OTA firmware updates on boot.

---

## 🚀 Quick Start

### 1. Download Installer and Firmware

Head to the GitHub Releases page and download:
- **Installer Package**: `hueleaf-installer.zip`
- **Firmware Binary**: `hueleaf-firmware-<version>.bin`
- **Android App** (optional): APK available in companion Releases.

### 2. Flash Firmware

Extract `hueleaf-installer.zip` and connect the HueLeaf frame to your PC via USB (Python 3.12+ required):

- **Windows**: Double-click `install.bat`
- **macOS**: Double-click `install.command`
- **Linux**: Run `bash hueleaf.sh`

Follow the interactive prompts: choose "Install Firmware", select your downloaded `.bin` file and serial port. The installer will automatically take a backup of the flash and flash the firmware.

Or install directly via CLI:
```bash
bash hueleaf.sh install ./hueleaf-firmware-<version>.bin --port /dev/ttyACM0
# Windows: install.bat install .\hueleaf-firmware-<version>.bin --port COM5
```

For full details, see the [Installer & Workspace Guide](tools/hueleaf-workspace/README.md).

---

## 📂 TF Card Layout (FAT32)

Format your MicroSD / TF card as FAT32. The layout is organized as follows:

```text
/img/*.bin           172,800-byte 6-color image frames (converted via PC tool)
/img/index.txt       (Optional) Custom display order, one file name per line; otherwise alphabetical
/img/slideshow.txt   (Optional) Playlist for scheduled rotation (configured via Web GUI)
/img/.labels/        (Optional) Rendered name labels for fast MonoFast menu display
/update/firmware.bin (Optional) Firmware update file; applied on boot and renamed to firmware.bin.done
/hueleaf.cfg         (Optional) Configuration file for hardware switches
```

- **Power-Off Memory**: Remembers the current photo by file name across power cuts and battery replacements.
- **Upload Protection**: Physical buttons are locked during USB file transfer to prevent accidental interruptions.

---

## 🖥️ PC Tools & Web GUI (hueleaf-push)

The repository provides a Web GUI and command-line tools under `tools/hueleaf-push`. See [tools/hueleaf-push/README.md](tools/hueleaf-push/README.md) for full documentation.

### Launch the Web GUI

Run the GUI from the workspace or installer:
```bash
./hueleaf.sh gui
# Or in tools/hueleaf-push: python -m gui
```

In the browser GUI, you can:
- Drag and drop pictures for instant 6-color conversion with crop, rotate, and dithering adjustments;
- Batch upload photos to the TF card with automatically rendered labels;
- Manage the photo gallery and easily check/uncheck photos for the slideshow playlist;
- Sync your PC clock to the onboard RTC and configure the 4-digit BLE remote PIN.

### Command Line Examples

```bash
cd tools/hueleaf-push && pip install -r requirements.txt

# Convert and preview locally
python hueleaf_push.py photo.jpg --preview out.png

# Convert, upload with progress bar, and display
python hueleaf_push.py -p /dev/ttyACM0 photo.jpg --rotate 90 --fit pad

# Manage files and query status
python hueleaf_push.py -p /dev/ttyACM0 --list            # List images on TF card
python hueleaf_push.py -p /dev/ttyACM0 --show photo.bin  # Display specific image
python hueleaf_push.py -p /dev/ttyACM0 --set-time        # Synchronize RTC time
python hueleaf_push.py -p /dev/ttyACM0 --info            # Check battery and status
python hueleaf_push.py -p /dev/ttyACM0 --log             # Read firmware logs

# Offline conversion: generate .bin to manually copy into /img/
python hueleaf_push.py photo.jpg --save-bin output.bin
```

---

## 📱 Android BLE Remote App

HueLeaf v2 features native Bluetooth Low Energy remote control:

1. **Set PIN**: In the PC Web GUI under "Display Settings", enter a 4-digit PIN (e.g. `1234`) and save.
2. **Connect**: Open the Android App, grant Bluetooth permissions, enter the same PIN, and tap "Scan". Tap your HueLeaf device to pair.
3. **Control**:
   - **Switch Photos**: Previous, Next, or jump directly to any photo;
   - **Browse Gallery**: View all images with labels, identify the active picture, and inspect slideshow status;
   - **Device Status**: Monitor real-time battery percentage and device state;
   - **Power Saving**: Put the device to sleep with one tap.

See the [Android Remote Documentation](android/hueleaf-remote/README.md) for details.

---

## 🎮 Physical Controls

Buttons are inactive during initial boot until the first picture finishes rendering.

### 1. Photo Viewing Mode

| Control | Action |
| :--- | :--- |
| **BOOT Short Press** | Previous image (wraps around) |
| **Power Short Press** | Next image (wraps around) |
| **BOOT Long Press** (~1s) | Open image selection list (MonoFast black & white) |
| **Double-Tap Frame** | Next image (requires `tap_next=1` in config) |
| **Power Long Press** | Hardware power off (stops timer; requires manual power on) |
| **Reset (RST)** | Reboot device |

- **Refresh Guard**: Full-color e-paper refreshing takes 10+ seconds. Inputs during refresh are ignored to avoid queueing.
- **Feedback**: The blue charging LED blinks once when an input is accepted. The bottom status bar shows battery %, index, and RTC time.

### 2. MonoFast Image Picker

Long-press BOOT to open the fast 14-row list (~0.2s per scroll):

| Control | Default Mode (`tap_next=0`) | Tap Mode (`tap_next=1`) |
| :--- | :--- | :--- |
| **BOOT Short Press** | Move cursor down | Move cursor down |
| **Power Short Press** | Next page | Move cursor up |
| **Double-Tap** | — | Next page |
| **BOOT Long Press** | Confirm and display selected image | Confirm and display selected image |

- Automatically exits and returns to the current image after 60 seconds of inactivity.

---

## ⚙️ Configuration & Slideshow

### 1. Scheduled Deep-Sleep Slideshow

Automatically cycles through photos at set intervals while putting the MCU into deep sleep between refreshes. The e-paper display retains the image without consuming power.

- **How to enable**:
  - Configure **Scheduled Rotation (Deep Sleep)** and interval (e.g., 60 min) in the Web GUI;
  - Or edit `hueleaf.cfg` in the TF card root:
    ```ini
    slideshow_enabled=1
    slideshow_interval_min=60
    tap_wake=0   # Set to 1 to allow double-tap wake from deep sleep
    ```
- **Custom Playlist**: In the Web GUI, check/uncheck images to select which photos participate in the rotation.
- **Wakeup**: Connecting USB pauses sleep. Press Reset to wake up immediately, or enable `tap_wake=1` to wake by double-tapping the casing.

### 2. `hueleaf.cfg` Options

Place `hueleaf.cfg` in the TF card root (`#` indicates comments):

| Option | Default | Description |
| :--- | :---: | :--- |
| `slideshow_enabled` | `0` | `1` to enable scheduled rotation, `0` to disable |
| `slideshow_interval_min` | `60` | Rotation interval in minutes (1–10080) |
| `tap_wake` | `0` | `1` to allow double-tap to wake from deep sleep |
| `tap_next` | `0` | `1` to enable double-tap to flip images and remap menu keys |
| `mono_fast` | `1` | `1` for fast black/white menu refresh (~0.2s), `0` for full refresh |

---

## 🛠️ Troubleshooting & Recovery

### 1. Serial Port Not Detected / Download Mode
- The ESP32-C5 native USB typically enters download mode automatically.
- If not recognized, **hold the BOOT button while plugging in USB** (or hold BOOT and press the RST button), then release to enter ROM download mode.

### 2. Restore Factory or Unbrick
- **Restore from Backup**: The installer automatically creates a backup (e.g., `backup.bin`) on first install. To restore:
  ```bash
  esptool.py --chip esp32c5 write_flash 0 backup.bin
  ```
- **Restore Factory Firmware**:
  ```bash
  esptool.py --chip esp32c5 erase_flash
  esptool.py --chip esp32c5 write_flash 0x0 examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
  ```

### 3. Why is the screen refresh slow?
This is normal. Six-color full-color e-paper requires multiple voltage-driving phases for particle movement, taking 10–15 seconds for a complete refresh. The firmware locks key inputs during this period to protect screen integrity.

---

## 📋 Hardware Specifications

| Item | Specification |
| :--- | :--- |
| **MCU** | ESP32-C5 (ESP32-C5HF4, RISC-V) |
| **Display** | 3.7″ 6-color E-Paper EPD037A02, 480 × 720 (black, white, red, yellow, blue, green) |
| **Wireless** | Dual-band Wi-Fi 6 (2.4 / 5 GHz) + Bluetooth 5 (LE) |
| **Power Management** | AXP2101 PMIC (charging, fuel gauge, power rails) |
| **Real-Time Clock** | PCF8563 RTC |
| **Motion Sensor** | MSA321 3-axis accelerometer |
| **Storage** | 4 MB Flash, MicroSD / TF card expansion |
| **I/O & Buttons** | Type-C USB (Native USB-Serial-JTAG), RST, Power, BOOT buttons |
| **LEDs** | Charging (Blue), Power/Running (Green) |

Full schematics: [hardware/HueLeaf_Base_Schematic_V1.0.pdf](hardware/HueLeaf_Base_Schematic_V1.0.pdf). Component placement: [hardware/HueLeaf_Base_Placement.pdf](hardware/HueLeaf_Base_Placement.pdf).

---

## 📚 Developer References

- **Firmware Architecture & Partitions**: [firmware/hueleaf-gallery/README.md](firmware/hueleaf-gallery/README.md)
- **BLE Protocol & Sleep Flow**: [firmware/hueleaf-gallery/docs/ble-protocol.md](firmware/hueleaf-gallery/docs/ble-protocol.md) · [remote-control.md](firmware/hueleaf-gallery/docs/remote-control.md)
- **Hardware Validation Checklist**: [firmware/hueleaf-gallery/docs/real_hw_test.md](firmware/hueleaf-gallery/docs/real_hw_test.md)
- **Branch and Release Archive**: [firmware/hueleaf-gallery/docs/branches.md](firmware/hueleaf-gallery/docs/branches.md)
- **Upstream Repository**: [osptek/devkit-hueleaf_base](https://github.com/osptek/devkit-hueleaf_base)

---

## 📄 License

This repository is distributed under the [GPL-3.0](LICENSE) license.
- Black/white fast refresh (MonoFast) waveform routines in `firmware/hueleaf-gallery/main/screen_driver/mono_fast.c` are ported from [FrameFilm](https://github.com/kirmisaki/FrameFilm) by kirmisaki (GPL-3.0-or-later);
- Upstream code (`examples/`, etc.) retains its original [Apache-2.0](LICENSE-APACHE-2.0) license;
- Hardware design files and documentation (`hardware/`, `docs/`) are licensed under [CC BY-NC-SA 4.0](LICENSE-CC-BY-NC-SA-4.0);
- Other third-party libraries remain under their respective licenses.
