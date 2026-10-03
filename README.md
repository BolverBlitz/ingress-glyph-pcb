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
- Hold Button 1 for 2 seconds: enter Flashlight mode.
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
python -m platformio run --target upload --upload-port COM6
```

Open the serial monitor with:

```powershell
python -m platformio device monitor --port COM6 --baud 115200
```

At 115200 baud, the serial configuration console accepts:

```text
show
rev 2
wifi 1 "Network name" "Network password"
wifi 2 "Backup network" "Network password"
wifi-clear 1
```

Passwords are never printed by `show`. An empty password configures an open
network. Revision and Wi-Fi changes are saved and restart the controller.

The build automatically minifies `WebUI/` and regenerates the embedded
`src/web_ui.h` header. The resulting OTA image is located at
`.pio/build/esp32-c3-devkitm-1/firmware.bin`.
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
when enabled, with six directions and three transition types:

- Quickfade: each LED reaches black before fading into the next frame, while
  outgoing and incoming waves overlap across the display.
- Slowfade: the entire display reaches black before the incoming wave starts.
- Mix: directly blend the old and new frames without inserting a black step.

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

The release contains the OTA-ready firmware binary and its SHA-256 checksum.
Once the repository is public, the Settings page checks the latest release once
when it loads. Network and GitHub errors are ignored so hotspot mode continues
to work normally.
