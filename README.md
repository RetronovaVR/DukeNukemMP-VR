# Duke Nukem: Manhattan Project - OpenXR VR Mod

A full 6DoF stereoscopic Virtual Reality modification for **Duke Nukem: Manhattan Project** (2002), built as an OpenXR Direct3D 8 wrapper.

Experience Duke Nukem's classic platformer in fully immersive 3D directly on modern VR headsets (Meta Quest 2 / 3 / Pro, Valve Index, HTC Vive, Pico 4, and other OpenXR runtimes).

---

## Features

- **Full 360° Stereoscopic VR**: Native stereoscopic dual-viewport projection directly mapped to your headset's physical lens geometry and IPD.
- **True 6DoF Positional Tracking**: Accurate 1:1 real-world to in-game unit conversion (1 meter = 24 game units, matching Duke's 48-unit height). Lean forward, peer around corners, duck, and inspect enemies with natural head motion.
- **Engine Frustum Culling Bypass**: Dynamic runtime memory patch for Prism3D that eliminates edge culling, keeping distant skyscrapers and surrounding environments permanently rendered.
- **Zero-Latency DMA Pipeline**: High-performance double-buffered AVX2 transfer pipeline for 4K+ VR rendering at a rock-solid 90 FPS.
- **DXVK Vulkan Integration**: Bypasses legacy DirectX driver overhead via Vulkan for minimal queue latency and stutter-free tracking.
- **Comfortable 3D HUD**: Stereo-converged 2D HUD projection placed at a relaxed virtual depth, avoiding eye strain.
- **In-Game Hotkeys**: Instant on-the-fly adjustment of camera distance, world scale, gamma, and view recentering.

---

## Installation (For Players)

1. Ensure you have a working copy of **Duke Nukem: Manhattan Project** (GOG or original CD release).
2. Download the latest release package (`DukeNukemMP_VR_v1.0.zip`) from the [Releases](https://github.com/RetronovaVR/DukeNukemMP-VR/releases) page.
3. Extract all files from the zip directly into your game's root directory (alongside `DukeNukemMP.exe`).
4. Start your VR headset connection (Quest Link / AirLink / Virtual Desktop with VDXR / SteamVR).
5. Launch `DukeNukemMP.exe`. The game will automatically output to your headset in full 3D VR.

---

## In-Game Controls & Hotkeys

| Hotkey | Function |
|---|---|
| **F12** | **Recenter VR View** (sets current head position as neutral origin) |
| **Page Up** | Move camera further back from Duke |
| **Page Down** | Move camera closer to Duke |
| **Home** | Reset camera pullback distance to default (3.0) |
| **Numpad 9** | Increase World Scale (+1.0, or +5.0 with Shift) |
| **Numpad 7** | Decrease World Scale (-1.0, or -5.0 with Shift) |
| **Numpad 8** | Reset World Scale to natural 1:1 scale (24.0) |
| **F11 / Shift+F11** | Fine-tune head translation sensitivity (`TrackingPosScale`) |
| **F10** | Reset head tracking scale to 1.0 |
| **F8** | Toggle Frustum Culling patch on / off |
| **[ / ]** | Shrink / Enlarge 2D HUD size |
| **Insert / Delete** | Move 2D HUD closer / further away in 3D space |
| **Numpad \* / /** | Increase / Decrease VR lens gamma |

---

## Configuration (`vr_config.ini`)

All VR parameters can be adjusted via `vr_config.ini` in the game directory:

```ini
[VR]
EnableVR=1                 ; 1 = Enable VR mode, 0 = Standard 2D game
DisableCulling=1           ; 1 = Disable engine frustum culling
WorldScale=24.0            ; 24.0 = Natural 1:1 real-to-game unit scale
TrackingPosScale=1.0       ; 1.0 = Natural 1:1 head movement
CameraDistance=3           ; Camera distance pullback offset
HudDepth=1.0               ; Virtual HUD distance in 3D space
HudScale=0.65              ; HUD width relative to field of view

[Graphics]
MirrorMode=1               ; 1 = Fullscreen 16:9 monitor companion window
ForceWidescreen=1          ; Force high-res VR rendering
Width=4128                 ; Per-frame rendering width (split into Width/2 per eye)
Height=2208                ; Per-frame rendering height
FPSLimit=90                ; Target FPS limit
Gamma=0.8                  ; Lens gamma correction
```

---

## Building from Source

### Requirements
- **Visual Studio 2022** with the *Desktop development with C++* workload.
- Windows 10 / 11 SDK.
- MSVC v143 toolset with AVX2 instruction set support enabled.

### Compilation
1. Clone the repository:
   ```bash
   git clone https://github.com/RetronovaVR/DukeNukemMP-VR.git
   ```
2. Open `d3d8to9.sln` in Visual Studio 2022.
3. Select **Release** and **Win32** (x86) configuration.
4. Build the solution (`Ctrl + Shift + B`).
5. The compiled output `d3d8.dll` will be generated in `bin/Release/`.

---

## Legal Disclaimer

- This modification is an independent, non-commercial open-source project created strictly for educational, interoperability, and preservation purposes.
- This project is **NOT** affiliated with, endorsed by, sponsored by, or associated with **3D Realms**, **Sunstorm Interactive**, **Arush Entertainment**, or **Gearbox Software**.
- **No copyrighted game assets, executables, textures, audio, or game data are included in this repository.** Users must legally own a licensed copy of *Duke Nukem: Manhattan Project* to play.
- All game titles, trademarks, character names, and copyrights are the property of their respective owners.

---

## License

- The D3D8 proxy wrapper is based on `d3d8to9` by Patrick Mours, licensed under the **BSD 2-Clause License** (see [LICENSE.md](LICENSE.md)).
- OpenXR integration and loader components are provided under the **Apache License 2.0** (The Khronos Group Inc.).
- DXVK compatibility binaries are licensed under the **zlib License** (Philip Rebohle).
