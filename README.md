# Ingress Glyph PCB firmware

Ingress Glyph PCB firmware for controlling LED chains and managing device features.

## Hardware revisions

- Rev 1 drives the LED chain from GPIO2 and has a 4.5 W hardware limit.
- Rev 2 drives the LED chain from GPIO19 and uses GPIO5 (SDA) / GPIO6 (SCL)
  for two TMP117 temperature sensors. Its hardware limit is 8 W with dimming* applied as needed.
- Select the revision under **Settings → Hardware revision**. A revision change
  is saved and restarts the controller automatically.

*On Rev 2, the hotter TMP117 reading limits every LED mode. The configured power
is available up to 50 °C, then linearly derated to 0 W at 70 °C. The LEDs remain
off if either sensor is missing or has not produced a valid reading for five
seconds.

## Buttons

- Button 1: GPIO1, active-low with internal pull-up.
- Button 2: GPIO4, active-low with internal pull-up.
- Double-press Button 1: cycle Glyph Sequence, Random Glyphs, and Draw. The new
  mode is saved after 10 seconds without further button input.
- Hold Button 1 for 2 seconds: toggle Flashlight mode on or off.
- In Flashlight mode, press Button 2: cycle 25%, 60%, and 100% brightness.
- Hold Button 2 on two nearby devices for 5 seconds: add each other as friends
  and exchange the Draw page whose name exactly matches each Agent username.
- Hold both buttons for 10 seconds: enter Emergency mode, pulse white SOS, and
  broadcast the Emergency message to nearby devices.
- In Emergency mode, hold both buttons for 2 seconds: disable Emergency mode.


## Build and flash

Install Python and PlatformIO, then run these commands from the repository root:

```powershell
python -m platformio run
python -m platformio run -e esp32-c3-devkitm-1-en --target upload --upload-port COM11
```

Open the serial monitor with:

```powershell
python -m platformio device monitor --port COM6 --baud 115200
```

At 115200 baud, the serial configuration console accepts:

```text
show
ip
rev 2
wifi 1 "Network name" "Network password"
wifi 2 "Backup network" "Network password"
wifi-clear 1
```

Passwords are never printed by `show`. An empty password configures an open
network. Revision and Wi-Fi changes are saved and restart the controller.

To display the IP addresses of all controllers connected over USB serial, run:

```powershell
python scripts/find_device_ips.py
```

Specific ports can be supplied when other serial equipment is connected:

```powershell
python scripts/find_device_ips.py COM11 COM12
```

The default build produces three independent firmware images: English (`en`),
German (`de`), and Japanese (`ja`). Each image contains only its own UI language;
language is selected when flashing, not in Settings. The build automatically
translates and minifies `WebUI/` into a `localized_web_ui.h` header inside each
PlatformIO build directory. For example, the English OTA
image is `.pio/build/esp32-c3-devkitm-1-en/firmware.bin`. Use `-e
esp32-c3-devkitm-1-de` or `-e esp32-c3-devkitm-1-ja` to flash another language.

Keep WebUI JavaScript readable, with indented functions and comments. The Python
pre-build script translates the sources, minifies HTML, JavaScript and CSS, and
embeds the result in firmware before flashing. It uses the pinned `minify-html`
dependency from `scripts/requirements-web-ui.txt`, automatically installed into
`.pio/web-ui-deps` on the first build if needed. This first build requires
internet access; later builds reuse the dependency. Node.js is not needed to
build or flash. Generated files do not overwrite the sources in `WebUI/`.

Run the minification regression checks with `python -m unittest discover -s
tests -v`. If Node.js is installed, they also check JavaScript behavior and
syntax for all three languages; otherwise those checks are skipped.

Link-time optimization reduces firmware size. A final binary-size check rejects
images that exceed the OTA slot, including image headers and alignment padding.

The source build disables C++ exceptions and unwind-table generation because
the application and its source dependencies use explicit error returns. Unwind
tables in the precompiled ESP-IDF/C++ libraries are retained. New dependencies
that require exceptions need these build flags reviewed.

NimBLE Security Manager (legacy pairing and Secure Connections), bonding, and
bond persistence are disabled through the library's `MYNEWT_VAL_*` overrides.
BLE discovery and public username/glyph reads remain available. Button-based
friend pairing stores an application friend record, independently of BLE bonds.
Private or authenticated BLE features would require restoring security support.

For an interactive build, flash, hardware-revision setup, and optional Wi-Fi
setup, run one of the helper scripts from the repository root:

```powershell
.\flash.ps1
```

```bash
bash ./flash.sh
```

When Wi-Fi setup is requested, the script waits
for the connection and prints the controller's assigned IP address.
On Windows, the port prompt accepts either `COM11` or just `11`.

## Display effects

The Main page's Effects section is available in Glyph Sequence, Random Glyphs,
and Draw modes. Choose Off or Fade. Fade also applies to friend glyph playback
when enabled, with six moving directions, a Global fade, and three transition types:

- Quickfade: each LED reaches black before fading into the next frame, while
  outgoing and incoming waves overlap across the display.
- Slowfade: the entire display reaches black before the incoming wave starts.
- Mix: directly blend the old and new frames without inserting a black step.

Global fades all LEDs together without a moving wave.
Fade duration (100–60000 ms) covers the entire transition. The switch interval
is the hold time after the transition finishes. Effects support live preview and
are stored with the display settings when saved. Existing settings default to
Off. Flashlight, SOS, and pairing feedback retain their immediate controls.

## Releases

Push a semantic-version tag matching `FIRMWARE_VERSION` in `src/config.h` to
build and publish a GitHub release automatically. For example:

```bash
git tag v0.9.8
git push origin v0.9.8
```

The release contains an OTA-ready binary and SHA-256 checksum for each language,
named `ingress-glyph-pcb-vX.Y.Z-en.bin`, `-de.bin`, and `-ja.bin`. The Settings
page offers the download matching the language currently installed.
Once the repository is public, the Settings page checks the latest release once
when it loads. Network and GitHub errors are ignored so hotspot mode continues
to work normally.
