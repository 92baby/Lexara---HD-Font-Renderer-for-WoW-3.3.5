# Lexara — HD Font Renderer for WoW 3.3.5

**Lexara** is a lightweight DLL that replaces WoW 3.3.5's native font renderer
with **MSDF** (Multi-channel Signed Distance Field) technology, delivering sharp,
resolution-independent text across the entire game UI — combat numbers, nameplates,
tooltips, chat, and more.

> Drop `dinput8.dll` into your WoW root folder. No addon, no configuration file, no setup required.

---

## Installation

**Requirements:** World of Warcraft 3.3.5 (build 12340)

1. Download and extract `Lexara.zip` from [Releases](../../releases)
2. Copy `dinput8.dll` and `skia.dll` to your WoW root folder (where `WoW.exe` is located)
3. Launch the game

**Files included**
- `dinput8.dll` — Lexara font renderer
- `skia.dll` — required graphics dependency

---

## Compatibility

Lexara coexists with ReShade and other DLL-based mods. If `dinput8.dll` is already
taken by another mod, rename Lexara to `version.dll` or `dsound.dll` — all three
names work identically.

---

## Building from Source

### Requirements

| Tool | Minimum version |
|---|---|
| Visual Studio (MSVC) | 2019 or 2022 |
| CMake | 3.15 |
| Target platform | **x86 (32-bit)** required |

### Third-party dependencies

The `third_party/` folder must contain:

```
third_party/
  Detours/
  freetype-2.14.1/
  msdfgen/
  skia/
  unordered_dense/
    unordered_dense-4.8.1/
      include/
```

### Build

```bat
build.bat
```

Output: `build\Release\Release\dinput8.dll`

---

## Differences from Transmorpher

| Transmorpher 2.0.0 | Lexara |
|---|---|
| HD fonts disabled by default | HD fonts **always active** |
| Requires `state\msdf_mode.txt` | No configuration file |
| Requires `Transmorpher` Lua addon | **No addon** |
| Includes morph system, spell visuals, render overrides | Removed entirely |

### Removed from build:

- `Hooks.cpp` / `Hooks.h`
- `Morpher.cpp` / `Morpher.h`
- `SpellMorph.cpp` / `SpellMorph.h`
- `D3DHooks.cpp` / `D3DHooks.h`
- `RenderEnables.cpp` / `RenderEnables.h`
- `RenderOverrides.cpp` / `RenderOverrides.h`
- `Utils.cpp` / `Utils.h`
- `WoWOffsets.h`
- Lua bridge and timer system

---

## Uninstall

Delete `dinput8.dll` and `skia.dll` from the WoW root folder.

---

## Credits

Built on the font subsystem of [Transmorpher 2.0.0](https://github.com/AwesomeWotLK/Transmorpher)
by AwesomeWotLK, stripped to a single focused purpose.  
MSDF engine: [msdfgen](https://github.com/Chlumsky/msdfgen) by Viktor Chlumský  
Hooking: [Microsoft Detours](https://github.com/microsoft/Detours)  
Font rasterization: [FreeType](https://freetype.org/)
