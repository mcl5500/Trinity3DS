# Trinity3DS

<p align="center">
  <img src="assets/icon.png" width="96" height="96" alt="Trinity3DS Logo" />
</p>

<p align="center">
  <strong>The Definitive Doom, Heretic & Hexen Source Port for the Nintendo 3DS</strong><br>
  Powered by the modern <strong>dsda-doom v0.27.5</strong> engine, featuring native <strong>Hardware Stereoscopic 3D</strong>, a <strong>Dual-Screen Tactical Dashboard</strong>, integrated <strong>WAD Manager & Over-The-Air Downloader</strong>, and full <strong>Raven Software Game Support</strong>.
</p>

<p align="center">
  <a href="https://github.com/mcl5500/Trinity3DS/releases/tag/v1.5.2"><img src="https://img.shields.io/badge/Release-v1.5.2-blue.svg?style=flat-square" alt="Version 1.5.2"></a>
  <a href="https://github.com/mcl5500/Trinity3DS/blob/dsda/COPYING"><img src="https://img.shields.io/badge/License-GPLv2-green.svg?style=flat-square" alt="GPL-2.0 License"></a>
  <a href="https://github.com/mcl5500/Trinity3DS"><img src="https://img.shields.io/badge/Platform-Nintendo%203DS%20%7C%202DS-red.svg?style=flat-square" alt="Platform"></a>
  <a href="https://github.com/kraflab/dsda-doom"><img src="https://img.shields.io/badge/Engine-dsda--doom%20v0.27.5-orange.svg?style=flat-square" alt="dsda-doom"></a>
</p>

---

## 📥 Downloads & Quick QR Code Installation (v1.5.2)

You can install Trinity3DS directly onto your 3DS over WiFi using **FBI** (*Remote Install -> Scan QR Code*) or **3dsDL**:

| **Installable CIA** (HOME Menu) | **Standalone 3DSX** (Homebrew Launcher) |
| :---: | :---: |
| ![Installable CIA QR Code](assets/qrcode_cia.png) | ![Standalone 3DSX QR Code](assets/qrcode_3dsx.png) |
| [Download Trinity3DS.cia (v1.5.2)](https://github.com/mcl5500/Trinity3DS/releases/download/v1.5.2/Trinity3DS.cia) | [Download Trinity3DS.3dsx (v1.5.2)](https://github.com/mcl5500/Trinity3DS/releases/download/v1.5.2/Trinity3DS.3dsx) |

---

## 🚀 What's New in Version 1.5.2

* **Fixed Weapon Double Vision (Diplopia Elimination)**:
  * Isolated the uncapped framerate weapon bobbing interpolation from stereoscopic pass rendering.
  * Both left and right eyes now share the identical interpolated base coordinate before applying stereo offset, ensuring pixel-perfect 0-disparity on the screen plane.
  * No more ghosting, double weapon sprites, or bobbing jitter!
* **Dynamic Near-Wall Disparity Clamping**:
  * Added active distance tracking of close geometry. When standing right against walls, pillars, or tight corners, eye separation dynamically clamps so crossed disparity never exceeds the human fusional limit ($\le 8\text{ px}$).
  * Walking right next to walls is now 100% crisp and single-imaged while retaining full, rich diorama depth across rooms and background vistas.
* **Calibrated Human Interocular Scale**:
  * Recalibrated baseline camera separation to match Doomguy's 32-unit player body dimensions, creating perfectly natural 3D depth.

---

## 🚀 What's New in Version 1.5.1

* **Super Mario 64 3DS-Calibrated Stereoscopic 3D**:
  * Implemented an authentic **Zero Parallax (Convergence) Plane** with Horizontal Image Translation (HIT).
  * The entire 3D world (rooms, corridors, monsters, outdoor vistas) sinks deep into the 3DS display, producing the beloved "window into the world / diorama" 3D depth effect.
  * Near-field objects and charging demons pop out dynamically in front of the screen.
  * The player's weapon is pre-compensated so it sits rock-solid at the screen glass (zero disparity, zero flicker, zero eye strain).
* **In-Game 3D Depth Intensity Setting**:
  * Added **Options $\rightarrow$ Video $\rightarrow$ 3D Depth Intensity** with 4 selectable presets:
    * **Subtle (Low)**: Gentle depth for sensitive eyes.
    * **Normal (SM64)**: Calibrated directly to match the popular Super Mario 64 3DS stereoscopic experience!
    * **Deep (High)**: Extra pronounced spatial separation.
    * **Extreme (Ultra)**: Maximum 3D pop and depth.
  * Scales smoothly from 0% (2D) to 100% via the physical 3DS 3D depth slider.
* **Modern Engine Migration (dsda-doom v0.27.5)**:
  * Upgraded from older PrBoom+ to modern **dsda-doom v0.27.5** engine standards.
  * Native support for **DOOM / DOOM II**, **Heretic: Shadow of the Serpent Riders**, and **Hexen: Beyond Heretic** in one unified binary!
  * Full compatibility with modern standards: **Boom**, **MBF**, **MBF21**, and **UMAPINFO**.
* **True Stereoscopic 3D Experience (Hardware 3D Depth Slider)**:
  * Parallel-camera stereoscopic depth rendering tied directly to the physical 3DS 3D slider.
  * Natural depth perception with zero eye fatigue, zero headache, and zero keystone distortion.
  * Player weapon is centered at 0 disparity (rock-solid 2D sprite, zero binocular flickering).
  * Seamless 2D-to-3D transition in menus and the WAD Manager (no black right eye).
* **Smooth Distance Colormap Shading**:
  * Overhauled the classic 1993 distance diminishing calculation.
  * Eliminated harsh concentric bands ("Balken") and flashlight falloff cliffs on floors and ceilings.
  * Traverses every single colormap level smoothly for natural atmospheric lighting.
* **Hardware Gamma Correction**:
  * Official Doom `GAMMATBL` lookup tables integrated into the 3DS display pipeline.
  * Selectable in-game via **Options $\rightarrow$ Video $\rightarrow$ Gamma Correction** (Levels 0 through 4) to eliminate LCD black crushing.
* **Redesigned Raven Software Touchscreen UI**:
  * Custom stone-carved dashboard for Heretic and Hexen.
  * 3D filled progress bars for Health, Armor, and Dual Blue/Green Mana.
  * Interactive touch Artifact Tray and weapon selectors.
* **Rock-Solid Memory Stability**:
  * Custom memory allocation manager with a dedicated **56 MB application heap**, fixing all Out-Of-Memory errors in large Hexen hubs and complex megawads.

---

## ✨ Key Features

### 📺 Dual-Screen Dashboard Architecture
* **Top Screen (400×240 Widescreen)**:
  * Pure, unobstructed view of the action with zero HUD clutter blocking your view.
  * Full stereoscopic 3D depth rendering via the 3DS depth slider.
* **Bottom Screen (320×240 Touchscreen)**:
  * **Live Tactical Automap**: Real-time line-of-sight vector map showing player orientation, keys, locked doors, discovered secrets, enemy radar blips, and item pickups. Discovered item markers persist on the map until picked up!
  * **Touch Weapon Selector**: Instant touch slots (1–7 for Doom/Heretic, 1–4 for Hexen) along the right edge for instant weapon switching without cycling.
  * **Interactive Artifact Tray**: Instant tap to use Torches, Chaos Devices, Mystic Urns, Krater of Might, and Dark Servant flechettes in Heretic & Hexen.
  * **Touch Action Buttons**: Quick-access touch buttons for **`[WADS]` (Soft Reset)**, **`[SAVE]`**, **`[LOAD]`**, and **`[ZOOM]`**.

### 📁 Integrated WAD Manager & Launcher
* Automatically scans, discovers, and categorizes all IWADs and PWADs stored in `sdmc:/3ds/Trinity3DS/wads/`.
* Visual card browser with game metadata, box art / `TITLEPIC` banners, author details, and base game requirements.
* Fast binary cache for instantaneous bootup without slow filesystem rescanning on every launch.
* Companion `.txt` and `.png` file support for custom descriptions and high-resolution cover art.

### 🌐 In-Game WAD Store & Downloader
* Browse and download community megawads, total conversions, and mods directly on your 3DS over WiFi!
* Complete TLS/SSL certificate support (`cacert.pem`) for fast, secure HTTPS downloads.
* Background download worker with real-time progress percentage bar and instant cancel option.
* Automatic ZIP archive extraction and metadata parser.

### 🔄 Soft-Reset Back to Launcher
* Switch between campaigns, episodes, and megawads without quitting back to the Nintendo 3DS HOME Menu.
* Tap the **`[WADS]`** touch button on the bottom screen or hold **`L + R + START + SELECT`** for ~1.5s to cleanly return to the WAD Manager.

### ⌨️ Nintendo 3DS Software Keyboard (`swkbd`)
* Native 3DS virtual keyboard for typing custom savegame titles, cheat codes, and console commands.

---

## 📖 Step-by-Step Installation Tutorial

> [!IMPORTANT]
> **No copyrighted commercial game files are included with Trinity3DS.** You must provide your own legitimate IWAD files from your purchased copies of Doom, Heretic, or Hexen.  
> Free and open-source games such as **Freedoom** (`freedoom1.wad`, `freedoom2.wad`) and **Blasphemer** are fully supported and can also be downloaded directly inside the built-in WAD Store!

### Prerequisites
* A Nintendo 3DS, 3DS XL, 2DS, New 3DS, New 3DS XL, or New 2DS XL console with modern **custom firmware (Luma3DS)**.
* An SD card (FAT32 formatted) with at least 50 MB of free space.
* **FBI** (for CIA installation) or the **Homebrew Launcher** (for 3DSX).
* **DSP Firmware dumped**: If you have never run homebrew audio on your 3DS, run the homebrew app **DSP1** once to dump audio DSP firmware (required for sound in all 3DS homebrew).

---

### Method A: Install CIA via QR Code (Easiest & Recommended)
1. Turn on WiFi on your 3DS and connect to your home network.
2. Launch **FBI** from your 3DS HOME Menu.
3. Select **Remote Install** $\rightarrow$ **Scan QR Code**.
4. Point your 3DS camera at the **Installable CIA QR Code** at the top of this guide.
5. Press **A** to confirm download and installation.
6. Once complete, press **HOME** to find the newly installed **Trinity3DS** gift icon on your HOME Menu!

---

### Method B: Manual CIA Installation via SD Card
1. Download `Trinity3DS.cia` from the latest [GitHub Releases](https://github.com/mcl5500/Trinity3DS/releases/tag/v1.5.2).
2. Insert your 3DS SD card into your PC or connect via MicroSD Management / FTP.
3. Copy `Trinity3DS.cia` to the `sdmc:/cias/` directory on your SD card.
4. Insert the SD card back into your 3DS and launch **FBI**.
5. Navigate to **SD** $\rightarrow$ **cias** $\rightarrow$ select `Trinity3DS.cia` $\rightarrow$ choose **Install and delete CIA**.
6. Press **HOME** to unwrap your new Trinity3DS game icon!

---

### Method C: Standalone Homebrew Launcher (.3dsx)
1. Download `Trinity3DS.3dsx` from the latest [GitHub Releases](https://github.com/mcl5500/Trinity3DS/releases/tag/v1.5.2).
2. Copy `Trinity3DS.3dsx` to:  
   `sdmc:/3ds/Trinity3DS/Trinity3DS.3dsx`
3. Launch the **Homebrew Launcher** and start **Trinity3DS** from the list.

---

### Required SD Card Directory Structure

Ensure your SD card is set up with the following directory structure:

```text
sdmc:/
├── 3ds/
│   └── Trinity3DS/
│       ├── Trinity3DS.3dsx            # (Only needed if using Homebrew Launcher)
│       ├── dsda-doom.wad              # (Required: dsda-doom core engine data)
│       ├── prboom-plus.wad            # (Required: Boom/MBF compatibility assets)
│       ├── cacert.pem                 # (Required: SSL root certificates for WAD Store)
│       ├── launcher.ogg               # (Optional: Background music in WAD Manager)
│       └── wads/                      # (Your game IWADs and PWADs go here!)
│           ├── doom.wad
│           ├── doom2.wad
│           ├── tnt.wad
│           ├── plutonia.wad
│           ├── heretic.wad
│           ├── hexen.wad
│           ├── hexdd.wad
│           └── [custom mods & megawads...]
└── cias/
    └── Trinity3DS.cia                 # (Installation file for FBI)
```

> [!TIP]
> Both `dsda-doom.wad`, `prboom-plus.wad`, and `cacert.pem` are bundled inside the release archive and embedded in the CIA. They will automatically be created on first boot if missing, but having them placed in `sdmc:/3ds/Trinity3DS/` ensures maximum compatibility!

---

### Supported Games & IWAD Filenames

Copy your game `.wad` files into `sdmc:/3ds/Trinity3DS/wads/`. Supported games include:

| Game Title | Official IWAD Filename | Notes |
| :--- | :--- | :--- |
| **DOOM (Shareware / Registered)** | `doom1.wad` / `doom.wad` | Episodes 1–3 |
| **The Ultimate DOOM** | `doomu.wad` or `doom.wad` | Includes Episode 4: *Thy Flesh Consumed* |
| **DOOM II: Hell on Earth** | `doom2.wad` | Full 30 levels + secret levels |
| **Final Doom: TNT Evilution** | `tnt.wad` | 32 levels |
| **Final Doom: The Plutonia Experiment** | `plutonia.wad` | 32 levels |
| **Heretic: Shadow of the Serpent Riders** | `heretic.wad` | Full 5 episodes, inventory, flying |
| **Hexen: Beyond Heretic** | `hexen.wad` | Full hub system, 3 character classes |
| **Hexen: Deathkings of the Dark Citadel** | `hexdd.wad` | Official Hexen expansion pack |
| **Freedoom: Phase 1 & 2** | `freedoom1.wad`, `freedoom2.wad` | Free open-source alternatives |
| **SIGIL & SIGIL II** | `sigil.wad`, `sigil_ii.wad` | John Romero's official 5th & 6th episodes |
| **Community Megawads** | `eviternity.wad`, `scythe.wad`, etc. | Thousands of Doom & Boom megawads! |

---

## 🎮 Controls

### In-Game Gameplay Controls

| Input | Action |
| :--- | :--- |
| **Circle Pad** | **Analog Movement** (Walk Forward / Backward, **Strafe** Left / Right) |
| **C-Stick** *(New 3DS / CPP)* | **Analog Freelook Camera** (Look Left / Right / Up / Down) |
| **D-Pad** | Classic Digital Movement & Turning |
| **A / R / ZR** | Fire Weapon |
| **B / Y** | Use / Open Doors / Activate Switches / Teleport |
| **X** | Next Weapon |
| **L / ZL** | Run / Speed Modifier |
| **START** | Pause / Main Options Menu |
| **SELECT** | Toggle Automap Overlay on Top Screen |
| **3D Slider** | **Adjust Stereoscopic 3D Depth** in real time (0% = 2D, 100% = Deep 3D) |
| **Touch: Weapon Grid (Right)** | Instant Weapon Switch (Slots 1–7) |
| **Touch: Artifact Bar (Bottom)** | Instant Use Selected Inventory Item (Heretic / Hexen) |
| **Touch: [WADS] Button** | **Soft-Reset** cleanly back to WAD Manager |
| **Touch: [SAVE] Button** | Quick Save |
| **Touch: [LOAD] Button** | Quick Load |
| **Touch: [ZOOM] Button** | Cycle Automap Zoom Level (1x, 2x, 3x, 4x) |
| **L + R + START + SELECT (Hold 1.5s)** | **Hardware Soft-Reset** back to WAD Manager |

---

### WAD Manager & Store Controls

| Input | Action |
| :--- | :--- |
| **D-Pad Up / Down** | Select WAD / Megawad |
| **D-Pad Left / Right** | Page Scroll (±5 entries) |
| **L / R** | Alphabetical Jump (A–Z) |
| **A** | Launch Selected Game / Download WAD in Store |
| **X** | WAD Details & Management (View Info, Delete from SD) |
| **Y** | Open In-Game WAD Store (or Fetch Online Box Art) |
| **B** | Back / Close Modal |
| **START** | Instant Resume Last Played Game |

---

## ⚙️ Recommended In-Game Settings

Trinity3DS runs out-of-the-box with optimized presets, but you can tailor your visual experience in **Options $\rightarrow$ Video**:

1. **Stereoscopic 3D**:
   * Simply push the physical 3DS depth slider up!
   * The cameras are calibrated to true parallel perspective with 0 vertical parallax.
2. **Gamma Correction**:
   * Go to **Options $\rightarrow$ Video $\rightarrow$ Gamma Correction**.
   * By default, **Level 1 (Soft)** is enabled to prevent black crushing on the 3DS LCD. If you prefer brighter visuals, select **Level 2 (Medium)** or **Level 3 (Bright)**.
3. **Automap Colors**:
   * The live automap on the bottom screen uses high-contrast vector lines. You can customize colors in **Options $\rightarrow$ Automap**.

---

## 🔧 Troubleshooting & FAQ

#### Q: The game has no sound / music!
* **Cause**: Your 3DS does not have its DSP audio firmware dumped.
* **Fix**: Download and run the homebrew app **[DSP1](https://github.com/zoogie/DSP1/releases)** once. It will automatically dump the required audio microcode in 2 seconds and delete itself. Audio will work perfectly afterward in all homebrew!

#### Q: The C-Stick on my New 3DS does not turn or aim!
* **Cause**: The infrared C-Stick service needs recalibration or has deadzone issues.
* **Fix**: Trinity3DS initializes `ir:rst` automatically on New 3DS. If the stick feels unresponsive, go to 3DS **System Settings $\rightarrow$ Other Settings $\rightarrow$ Circle Pad Pro** to test and calibrate your C-Stick nub.

#### Q: How do I load custom PWADs (e.g. Scythe or Eviternity)?
* **Fix**: Place both your IWAD (`doom2.wad`) and your PWAD (`scythe.wad`) into `sdmc:/3ds/Trinity3DS/wads/`. The WAD Manager will automatically recognize `scythe.wad`, detect that it requires DOOM II, and launch them together seamlessly!

#### Q: Where can I get legal copies of the game WADs?
* You can buy DOOM, DOOM II, Final Doom, Heretic, and Hexen on **Steam** or **GOG**. The `.wad` files are located directly in the installation folder on your computer.
* Alternatively, install **Freedoom** for free from the built-in WAD Store!

---

## 🛠️ Building from Source

Trinity3DS is built with the official **devkitARM / devkitPro** toolchain:

### Prerequisites
* `devkitARM` (r60 or newer)
* `libctru`, `citro3d`, `citro2d`
* `bannertool` and `makerom` (for CIA packaging)

### Compiling
```bash
# Clone the repository
git clone https://github.com/mcl5500/Trinity3DS.git
cd Trinity3DS
git checkout dsda

# Compile both .3dsx and .cia binaries
./build_cia.sh
```
The output binaries `Trinity3DS.3dsx` and `Trinity3DS.cia` will be generated in the root repository directory.

---

## 📜 Credits & Acknowledgements

Trinity3DS is built upon the dedication and genius of generations of source port developers and id Software legends:

* **id Software**:
  * **John Carmack**, **John Romero**, **Adrian Carmack**, **Kevin Cloud**, **Sandy Petersen**, **Tom Hall**, and **Dave Taylor** for creating DOOM, the game that revolutionized the gaming industry.
* **Raven Software**:
  * **Brian Raffel**, **Steve Raffel**, **Ben Gokey**, **Chris Rhinehart**, and the Raven team for Heretic and Hexen.
* **dsda-doom Team**:
  * **Ryan Krafnick (kraflab)**, **Fabian Greffrath**, and all dsda-doom contributors for the modern, feature-rich, high-performance engine base.
* **Boom, MBF & PrBoom-Plus Teams**:
  * **Team TNT**, **Lee Killough**, **Jim Flynn**, **Rand Phares**, **Ty Halderman**, **Colin Phipps**, **Florian Schulze**, and **Andrey Budko (entryway)** for the legendary Boom, MBF, and PrBoom+ lineage.
* **3DS Port Pioneers**:
  * **Voxel9** and **elisherer** for the original Nintendo 3DS PrBoom-Plus port foundations and PICA200 GL wrapper.
* **Trinity3DS Development**:
  * **mcl5500 / Sheldor**: Trinity3DS architecture, dual-screen tactical dashboard & automap, integrated WAD Manager & Downloader, autostereoscopic 3D camera engine, smooth distance lighting falloff, Hexen/Heretic touch UI & artifact management, 56MB memory subsystem, and version 1.5 release.
* **devkitPro & libctru Contributors**:
  * **WinterMute**, **fincs**, **smealum**, **yellow8**, and the entire 3DS homebrew scene for the exceptional toolchains and libraries.

---

## ⚖️ License

Trinity3DS is free and open-source software licensed under the **GNU General Public License v2 (GPL-2.0)**.  
See the [COPYING](COPYING) file for full license terms and conditions.
