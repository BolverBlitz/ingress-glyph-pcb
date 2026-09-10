# Ingress Glyph PCB firmware

PlatformIO firmware for an ESP32-C3-WROOM-02-N4 driving 69 SK6812RGBW LEDs.

## Hardware revisions

- Rev 1 drives the LED chain from GPIO2 and has a 4.5 W hardware limit.
- Rev 2 drives the LED chain from ESP32-C3-WROOM-02 module pad 14 (GPIO19)
  and uses GPIO5 (SDA) / GPIO6 (SCL)
  for two TMP117 temperature sensors. The first detected TMP117 address is the
  center LED sensor and the second is the bottom-board sensor. Its hardware
  limit is 8 W.
- Select the revision under **Settings → Hardware revision**. A revision change
  is saved and restarts the controller automatically.

On Rev 2, the hotter TMP117 reading limits every LED mode. The configured power
is available up to 50 °C, then linearly derated to 0 W at 70 °C. The LEDs remain
off if either sensor is missing or has not produced a valid reading for five
seconds.

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

The build automatically minifies `WebUI/` and regenerates the embedded
`src/web_ui.h` header. The resulting OTA image is located at
`.pio/build/esp32-c3-devkitm-1/firmware.bin`.

## Buttons

- Button 1: ESP32-C3-WROOM-02 pad 17 / GPIO1, active-low with internal pull-up.
- Button 2: ESP32-C3-WROOM-02 pad 3 / GPIO4, active-low with internal pull-up.
- Double-press Button 1: cycle Glyph Sequence, Random Glyphs, and Draw. The new
  mode is saved after 10 seconds without further button input.
- Hold Button 1 for 2 seconds: enter Flashlight mode.
- In Flashlight mode, press Button 2: cycle 25%, 60%, and 100% brightness.
- Hold Button 2 on two nearby devices for 5 seconds: add each other as friends
  and exchange the Draw page whose name exactly matches each Agent username.
- Hold both buttons for 10 seconds: enter Emergency mode, pulse white SOS, and
  broadcast the Emergency message to nearby devices. No Flashlight command is
  triggered while both buttons are held.
- In Emergency mode, hold both buttons for 2 seconds: disable Emergency mode.
