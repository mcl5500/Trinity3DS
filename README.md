# Trinity3DS

An enhanced, high-performance source port of **Doom (PrBoom+)** built specifically for the **Nintendo 3DS** family of systems.  
Featuring a custom **Dual-Screen UI**, hardware-accelerated **PICA200 OpenGL rendering with Stereoscopic 3D**, an in-game **Live Tactical Automap**, touch-driven **Instant Weapon Selector**, built-in **WAD Manager & Downloader**, and **Software Keyboard (`swkbd`)** integration.

---

## 📥 Quick Download & Over-the-Air Installation

Scan either QR code using **3dsDL** or **FBI** (*Remote Install -> Scan QR Code*) on your 3DS to download and install directly over WiFi:

| **Installable CIA** (HOME Menu) | **Standalone 3DSX** (Homebrew Launcher) |
| :---: | :---: |
| ![Installable CIA QR Code](assets/qrcode_cia.png) | ![Standalone 3DSX QR Code](assets/qrcode_3dsx.png) |
| [Download Trinity3DS.cia (v1.2.2)](https://github.com/mcl5500/Trinity3DS/releases/download/v1.2.2/Trinity3DS.cia) | [Download Trinity3DS.3dsx (v1.2.2)](https://github.com/mcl5500/Trinity3DS/releases/download/v1.2.2/Trinity3DS.3dsx) |

---

## 🌟 Key Features

* **Dual-Screen Architecture**:
  * **Top Screen (400×240)**: Pure, widescreen Doom rendering in GPU-accelerated OpenGL mode with full **Stereoscopic 3D** support via the 3DS depth slider.
  * **Bottom Screen (320×240)**: Dedicated real-time tactical dashboard:
    * **Live Tactical Automap**: Seamless line-of-sight map with real-time player direction, color-coded locked doors, keys, discovered secrets, enemy radar blips, and pickups.
    * **Interactive Weapon Selector**: Instant touch slots (1–7) on the right edge for quick weapon switching without cycling.
    * **Authentic Doom Status Bar**: Gap-free, authentic Doom status bar with classic font numbers, ammo counters, and dynamic Doomguy reaction face.
    * **Tactical Header Bar**: Live kill/item/secret trackers, quick touch buttons for **`[WADS]` (Soft Reset)**, **`[SAVE]`**, **`[LOAD]`**, and **`[ZOOM]`**.

* **Integrated WAD Manager & Launcher**:
  * Automatically scans and organizes all your IWADs and PWADs from `sdmc:/3ds/Trinity3DS/wads/`.
  * Visual card browser with game metadata, box art / TITLEPIC covers, author details, and base game requirements.
  * Fast binary cache for instantaneous boot without rescanning SD folders every launch.
  * Companion `.txt` and `.png` support for custom descriptions and high-res cover art.

* **In-Game WAD Store & Downloader**:
  * Browse and download community megawads and mods directly on your 3DS over WiFi!
  * Built-in catalog of community classics and idgames archive integration.
  * Background download worker thread with progress bar and instant cancel.
  * Automatic zip extraction, metadata parser, and TITLEPIC cover extraction.
  * Full TLS/SSL certificate bundle support (`cacert.pem`) for secure HTTPS downloads.

* **Soft-Reset Back to Launcher**:
  * Switch between megawads and campaigns without quitting back to the 3DS HOME Menu.
  * Tap the **`[WADS]`** touch button on the bottom screen or hold **`L + R + START + SELECT`** for ~1.5s to cleanly relaunch the WAD Manager.

* **Nintendo 3DS Software Keyboard (`swkbd`)**:
  * Native 3DS system keyboard for typing custom savegame descriptions, cheat codes, and console commands.

---

## 🎮 Controls

### In-Game Controls
| Button | Action |
| :--- | :--- |
| **Circle Pad** | **Analog Movement** (Walk Forward / Backward, **Strafe** Left / Right) |
| **C-Stick** *(New 3DS / CPP)* | **Analog Camera Look** (Turn Left / Right) |
| **D-Pad** | Classic Digital Movement & Turning |
| **A / R / ZR** | Fire Weapon |
| **B / Y** | Use / Open Doors / Activate Switches |
| **X** | Next Weapon |
| **L / ZL** | Run / Speed modifier |
| **START** | In-Game Pause / Doom Options Menu |
| **SELECT** | Toggle Automap Overlay on Top Screen |
| **Touch: Weapon Grid (Right)** | Instant Weapon Switch (Slot 1 to 7) |
| **Touch: [WADS] Button** | **Soft-Reset** back to WAD Manager |
| **Touch: [SAVE] Button** | Quick Save |
| **Touch: [LOAD] Button** | Quick Load |
| **Touch: [ZOOM] Button** | Cycle Automap Zoom Level (1x, 2x, 3x, 4x) |
| **L + R + START + SELECT (Hold 1.5s)** | **Hardware Soft-Reset** to WAD Manager |

### WAD Manager & Store Controls
| Button | Action |
| :--- | :--- |
| **D-Pad Up / Down** | Select WAD / Megawad |
| **D-Pad Left / Right** | Page Scroll (±5 entries) |
| **L / R** | Alphabetical Jump (A–Z) |
| **A** | Launch Selected Game / Download WAD |
| **X** | WAD Details & Management (View Info, Delete from SD) |
| **Y** | Open WAD Store (or Fetch Online Cover) |
| **B** | Back / Close Modal |
| **START** | Instant Resume Last Played Game |

---

## 📂 Installation & Setup

> [!IMPORTANT]
> **No copyrighted game files are included with this repository.** You must provide your own legitimate Doom IWAD files (e.g. `doom.wad`, `doom2.wad`, `tnt.wad`, `plutonia.wad`).  
> Free alternatives such as [Freedoom](https://freedoom.github.io/) (`freedoom1.wad`, `freedoom2.wad`) are fully supported and can be downloaded directly inside the built-in WAD Store!

### Step 1: Install Trinity3DS
Choose one of the following methods:
* **Via QR Code (Recommended)**: Open **3dsDL** or **FBI** on your 3DS, select *Remote Install* -> *Scan QR Code*, and scan the CIA QR code above.
* **Via CIA**: Download `Trinity3DS.cia` from the [Releases](https://github.com/mcl5500/Trinity3DS/releases) page, copy it to your SD card, and install it with **FBI**.
* **Via 3DSX**: Download `Trinity3DS.3dsx` and copy it to `sdmc:/3ds/Trinity3DS/Trinity3DS.3dsx` to launch via the **Homebrew Launcher**.

### Step 2: SD Card Folder Setup
Ensure the following directory structure exists on your SD card:
```text
sdmc:/
└── 3ds/
    └── Trinity3DS/
        ├── Trinity3DS.3dsx          (if using Homebrew Launcher)
        ├── prboom-plus.wad          (bundled Boom support data)
        ├── cacert.pem               (SSL certificate bundle for WAD store)
        └── wads/
            ├── doom.wad             (Your game IWADs go here!)
            ├── doom2.wad
            ├── plutonia.wad
            └── sigil.wad
```

### Step 3: Add Your Game WADs
Copy any commercial or community WAD files into:
`sdmc:/3ds/Trinity3DS/wads/`

Supported games include:
* **DOOM / The Ultimate DOOM** (`doom.wad` / `doomu.wad`)
* **DOOM II: Hell on Earth** (`doom2.wad`)
* **Final Doom: TNT Evilution** (`tnt.wad`)
* **Final Doom: The Plutonia Experiment** (`plutonia.wad`)
* **Freedoom: Phase 1 & 2** (`freedoom1.wad`, `freedoom2.wad`)
* **Heretic: Shadow of the Serpent Riders** (`heretic.wad`)
* **Hexen: Beyond Heretic** (`hexen.wad`)
* Thousands of community PWADs and Megawads (SIGIL, Eviternity, Scythe, Going Down, Alien Vendetta, etc.).

---

## 🛠️ Building from Source

This project uses the official **devkitARM / devkitPro** 3DS toolchain. An automated build script with Docker support is included:

```bash
# Clone the repository
git clone https://github.com/mcl5500/Trinity3DS.git
cd Trinity3DS

# Compile both .3dsx and .cia binaries
./build_cia.sh
```

---

## 📜 Credits & License

* **id Software** for creating DOOM.
* **Team TNT**, **Lee Killough**, **Jim Flynn**, **Rand Phares**, and the **Boom / PrBoom / PrBoom+ Team** for the legendary source port.
* **Voxel9** for the initial Nintendo 3DS PrBoom+ port base.
* **mcl5500** for Trinity3DS enhancements: dual-screen tactical HUD, WAD Manager & Downloader, soft-reset architecture, VBO GPU batching, and localization.
* Licensed under the **GNU General Public License v2 (GPL-2.0)**.
