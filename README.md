# Raising Hell — Cardputer ADV Edition

A Tamagotchi-style virtual pet game for the M5Stack Cardputer ADV (ESP32).

Raise your infernal companion through multiple life stages, feed it, play mini-games, manage sleep cycles, survive decay, and maybe… resurrect what should not be resurrected.
[![Discord](https://img.shields.io/discord/1495288087104721038?label=Join%20Discord&logo=discord&color=5865F2)](https://discord.gg/RH7dwZqaxV)
![Latest Release](https://img.shields.io/github/v/release/acpayers-alt/raising-hell-cardputer)
![Platform](https://img.shields.io/badge/platform-Cardputer%20ADV-orange)
![License](https://img.shields.io/github/license/acpayers-alt/raising-hell-cardputer)

![Raising Hell](media/rh_multicards.JPG)

## Community

Follow live development, report issues, and help shape Raising Hell:
https://discord.gg/RH7dwZqaxV

------------------------------------------------------------
Hardware Target
------------------------------------------------------------

- M5Stack Cardputer ADV
- ESP32 (240 MHz)
- SD card (required for assets)


------------------------------------------------------------
Controls
------------------------------------------------------------

Arrow Keys  - Navigate

Enter/G       - Confirm

Esc         - Menu

Del/Q

GO          - Screen Off/On

Shake Your cardputer to wake the screen

Hold GO     - Power Menu

/           - Console

Keyz Z-M are hotkeys for all the tabs

(Some mini-games may use alternate input behavior.)

Alternate Navigation - E,A,S,D and O,J,K,L - allows for one handed navigation


------------------------------------------------------------
Project Structure
------------------------------------------------------------

src/        (all .cpp and .h files)

assets/     Image Files

docs/       Licensing, Changelog, Contrib

tools/      Asset Manifest Generator and other Dev Tools


------------------------------------------------------------
Installation
------------------------------------------------------------

Raising Hell runs on the **M5Stack Cardputer ADV**.

There are three ways to install the game depending on your setup.

---

# 1. Install via M5Launcher (Recommended)

This is the easiest way to install Raising Hell directly from the Cardputer.

### Steps

1. Install **M5Launcher** on your Cardputer if it is not already installed.
2. Connect the device to **Wi-Fi**.
3. Open **M5Launcher**.
4. Browse the application list and locate **Raising Hell**.
5. Select the game and choose **Install**.

After installation:

- Launch the game from the launcher.
- On first boot the game will **automatically provision required assets via OTA**.

No manual asset downloads are required.

---

# 2. Install using M5Burner

You can install the firmware from a computer using **M5Burner**.

### Requirements

- M5Burner installed on your computer
- USB connection to the Cardputer ADV

### Steps

1. Connect the Cardputer ADV to your computer via USB.
2. Open **M5Burner**.
3. Search for **Raising Hell**.
4. Select the application.
5. Click **Burn**.
6. Wait for flashing to complete.

After the first launch:

- The game will **download required assets automatically via OTA**.

---

# 3. Manual Firmware Install (Advanced)

Advanced users can manually flash the firmware using **PlatformIO or esptool**.

### Step 1 — Download firmware

Download the latest firmware binary from the **GitHub Releases page**:

https://github.com/acpayers-alt/raising-hell-cardputer/releases

## Step 2 - Install Assets/Manifest
- Do **not** rename asset files or folders (except the manifest file, see below)  
- Make sure the path is exactly: /raising_hell/assets/

### Manifest Requirement (Important)

Manual installs require the **v2 asset manifest**.

You must use the correct manifest file and rename it to:

manifest_local.json

1. Locate the **v2 manifest file** from the assets package  
   (for example: `manifest-dev-v2.json` or `manifest-public-v2.json`)

2. Rename it to:
   manifest_local.json

3. Place it in:
   /raising_hell/assets/

If the wrong manifest is used, or if it is not renamed correctly, the game will not detect the local asset pack and may:
- attempt OTA download
- report missing assets
- fail to load assets properly

### Step 3 — Flash firmware

Flash the firmware to the device.

Example using PlatformIO:

```bash
pio run -t upload
```

Or using esptool:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x10000 firmware.bin
```

### Step 4 — Launch the game

After booting the firmware:

- The game will automatically **provision its asset pack via OTA**.

---

# First Boot Behavior

On first launch Raising Hell will:

- Check for required game assets  
- Download missing assets automatically  
- Store assets on the SD card  

This process only occurs once.

---

# Hardware Support

Currently supported hardware:

M5Stack Cardputer ADV

The original first-generation Cardputer has not been tested and may not work correctly.

---

# Troubleshooting

If the game fails to start:

- Ensure an **SD card is installed**
- Ensure the device has **Wi-Fi connectivity** for asset provisioning
- Restart the device after flashing

------------------------------------------------------------
Development Direction
------------------------------------------------------------

This project is under active architectural cleanup and refactor toward:

- Modular state architecture
- Strict include hygiene
- Removal of legacy globals
- Separation of platform and gameplay logic
- Open-source readiness


------------------------------------------------------------
Arduino IDE Settings
------------------------------------------------------------

Recommended configuration:

Board: M5Cardputer
Flash Mode: QIO 80MHz
Flash Size: 4MB (32Mb)
Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
CPU Frequency: 240MHz (WiFi)
Upload Speed: 921600


------------------------------------------------------------
Building From Source
------------------------------------------------------------

1. Clone the repository.
2. Copy the assets folder contents to an SD card.
3. Open raising_hell_cpADV.ino in the Arduino IDE.
4. Select the board settings listed above.
5. Compile and upload.

   
------------------------------------------------------------
Known Limitations
------------------------------------------------------------

- Requires SD card
- Designed specifically for Cardputer ADV hardware
- Not optimized for alternate ESP32 boards


------------------------------------------------------------
License
------------------------------------------------------------

Code is licensed under the MIT License.
See the LICENSE file for details.

Assets licensing is described in ASSETS_LICENSE.md.


------------------------------------------------------------
Author
------------------------------------------------------------

Aaron Ayers

If you build this, fork it, improve it, or port it — I’d love to see it.


------------------------------------------------------------
Screenshots
------------------------------------------------------------
![Raising Hell](media/Raising_Hell_Credits.JPG)
![Choose Your Pet](media/Raising_Hell_Choose.JPG)
![Hatch Your Pet](media/Raising_Hell_Hatch.JPG)
![NameYour Pet](media/Raising_Hell_Name.JPG)
![Meet Your Pet](media/Raising_Hell_Meet.JPG)
![Care for Your Pet](media/Raising_Hell_Care.JPG)
![Raise Your Pet](media/Raising_Hell_Raise.JPG)
![Neglect Your Pet](media/Raising_Hell_Neglect.JPG)
![Mourn Your Pet](media/Raising_Hell_Mourn.JPG)
