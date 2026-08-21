# ESP32 FlipWorld

A standalone touch firmware that runs **FlipWorld** — the top-down action game
from [Picoware](https://github.com/jblanked/Picoware) (originally
[FlipWorld](https://github.com/jblanked/FlipWorld) for the Flipper Zero) — on the
**FlipSocial** touch UI shell. No Flipper Zero attached; the game runs natively on
the ESP32-C5.

Two boards are supported (selected at build time):

| Board | MCU | Display | Touch |
|-------|-----|---------|-------|
| **Marauder Pancake** | ESP32-C5 | ST7796 320×480 | FT6336 capacitive |
| **Marauder V8** | ESP32-C5 | ILI9341 240×320 | XPT2046 resistive |

## The game

FlipWorld is rendered in **full colour**. The original sprites are 1-bit Flipper
masks; this port blits each entity in its own ink colour over a coloured world
(grass background, red enemies, blue water, green trees, etc.) with the sprite
"paper" left transparent — see `Draw::imageMaskPGM` and the `ink_color`
assignments in `src/Picoware/internal/applications/games/flipworld/`.

Because the shell is touch-only, the game is driven through the UI's tap-zones:

- **Tap a screen edge** (top / bottom / left / right) → move
- **Tap the centre** → attack
- **Press and hold anywhere (~1.2 s)** → exit back to the menu

A controls splash is shown before each run.

## Features (shell)

- **Play FlipWorld** — the game, launched from the main menu.
- **Settings** — 20 named themes, per-theme accent + font colour (with a Neon
  rainbow theme), screen brightness, status-LED brightness, WiFi setup/debug, and
  an About screen.
- **WiFi** — connects to saved networks in the background at boot; the header WiFi
  icon and the status LED show connection state. (The bundled level plays fully
  offline; WiFi is provided by the shell for future networked content.)
- **Touch keyboard** — QWERTY with tap-to-position cursor editing, used by WiFi setup.

## Build (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | ESP32C5 Dev Module |
| Flash Size | 8 MB |
| Partition Scheme | Custom → `partitions.csv` in this folder |
| Flash Frequency | 80 MHz |

Select the target board in `configs.h` (uncomment `MARAUDER_PANCAKE` **or**
`MARAUDER_V8`), and point the TFT library's `User_Setup_Select.h` at the matching
setup. CI passes `-DMARAUDER_PANCAKE` / `-DMARAUDER_V8` and builds both.

### Libraries

- **`TFT_eSPI-ESP32-C5`** (the ESP32-C5-patched fork) — install into your Arduino
  `libraries` folder and set its `User_Setup_Select.h` to one of:

  ```cpp
  #include <User_Setup_marauder_pancake.h>   // Pancake
  //#include <User_Setup_marauder_v8.h>      // V8
  ```
- **ArduinoJson** and **ArduinoHttpClient** — via Library Manager.

The ESP32-C5 Arduino core provides WiFi, SPIFFS and SD.

### Storage

- **SPIFFS** — UI settings (`/pico_ui.dat`), saved WiFi networks (`/pico_wifi.json`),
  and the resistive touch calibration on the V8 (`/pico_touch.dat`).
- **SD (FAT32)** — used by the Picoware core for its own files.

## First run

1. Flash, then optionally open **Settings → WiFi Setup → Scan**, pick your network
   and enter the password. It's saved and auto-connects on later boots.
2. On the V8 (resistive), run through the touch calibration when prompted.
3. From the main menu, tap **Play FlipWorld**.

## Layout

```
ESP32_FlipWorld.ino    Main sketch: UI shell + FlipWorld launcher/game loop
configs.h              Pancake / V8 pin + board config, firmware identity
theme.h                Themes, accents, font colours, brightness (SPIFFS)
ft6336.h               FT6336 capacitive-touch driver
TouchKeyboard.{h,cpp}  Self-contained touch QWERTY keyboard
partitions.csv         8 MB layout (nvs + ota apps + spiffs + fat)
src/Picoware/          Vendored Picoware core + FlipWorld game engine (ESP32-C5)
  internal/engine/                       game engine (game/level/entity/sprite3d)
  internal/applications/games/flipworld/ the FlipWorld game + assets + sprites
```

## Credits

- **[JBlanked](https://www.jblanked.com/)** — the **FlipWorld** game and
  **[Picoware](https://github.com/jblanked/Picoware)**, which this firmware is built on.
- UI shell adapted from **ESP32 FlipSocial** (H4W9).
