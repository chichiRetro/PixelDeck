# Third-party notices

This project uses the following open-source software. Their license texts are in `third_party_licenses/`.

| Software | Used in | License |
|---|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | firmware (display) | FreeBSD license (and the licenses of the code it includes) — `third_party_licenses/LovyanGFX_license.txt` |
| [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) by Larry Bank / BitBank Software | firmware (GIF decoding) | Apache License 2.0 — `third_party_licenses/AnimatedGIF_LICENSE.txt` |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) and ESP-IDF by Espressif Systems | firmware | LGPL-2.1 (core) / Apache License 2.0 (ESP-IDF). Source code is available at the linked repository. |
| [esptool-js](https://github.com/espressif/esptool-js) by Espressif Systems | app (firmware flashing; bundled inside `PixelDeck_Control.html`) | Apache License 2.0 — `third_party_licenses/esptool-js_LICENSE.txt` |
| Google Fonts: Orbitron, M PLUS Rounded 1c, DotGothic16 | app (loaded from fonts.googleapis.com, not bundled) | SIL Open Font License 1.1 |

The firmware binaries in `firmware/` are built from `firmware-src/` with the above libraries.
