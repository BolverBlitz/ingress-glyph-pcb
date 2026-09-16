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
