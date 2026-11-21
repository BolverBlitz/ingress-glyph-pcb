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
- Hold Button 2 on two nearby devices until the shared five-second rainbow
  finishes: add each other as friends and exchange the Draw page whose name
  exactly matches each Agent username. Connection setup happens before the
  rainbow starts, so total button-hold time can be longer than five seconds.
- On the Nearby page, send a Friendship Request instead. Requests expire after
  five minutes; the other board can accept while nearby, after which both boards
  run the normal five-second rainbow pairing animation. Pending requests can be
  cancelled and existing friends can be removed. Friendship is checked once when
  a new visit is recorded, regardless of Friend Mode. Removing a friend on one
  board notifies the other board immediately when nearby; if it is offline, the
  removal is reconciled at the next recorded visit. A nearby entry expires after 10 minutes without seeing the board,
  allowing a later visit to rediscover the board. Requests and nearby state
  are volatile and bounded to 32 entries; an unavailable board or older firmware
  does not delete a friend record. Physical Button 2 pairing remains supported.
- Hold both buttons for 10 seconds: enter Emergency mode, pulse white SOS, and
  broadcast the Emergency message to nearby devices.
- In Emergency mode, hold both buttons for 2 seconds: disable Emergency mode.

## Encounter history

Encounter history records `FIRST`/`LAST` pairs with UTC timestamps. Before NTP or
Browser Sync changes the clock, active encounters are closed on the old timeline
using elapsed uptime, then reopened on the corrected timeline. A 17-minute visit
therefore retains 17 minutes even if UTC jumps forward or backward. Time spent
nearby before the first usable UTC synchronization is recovered from uptime.
Clock corrections do not trigger extra friendship checks.

After reboot, the clock is restored to at least the newest saved encounter
timestamp or UTC checkpoint. An interrupted encounter without a `LAST` is
estimated from the next `FIRST`, capped at ten minutes; if that timestamp is
equal or earlier, the ten-minute fallback is used. These reboot estimates cannot
recover the actual encounter duration or time spent powered off. NTP and Browser
Sync can still correct an inaccurate clock backward. The normal encounter end
includes the existing ten-minute absence timeout.

## Coordinated friendship animation

Both devices need this firmware for the new friendship protocol. Discovery,
Emergency broadcasts and public identity reads still work with older firmware;
friend pairing does not fall back to the old animation timing.

The device with the smaller device ID opens a single BLE connection. It sends
an offer containing a random session ID and its identity. The other device
accepts only while Button 2 is held, at close range, and with no active emergency
or other session. Both identities are frozen for the exchange. After READY, the
coordinator measures the peer's `millis()` offset with three GATT reads and uses
the sample with the shortest round-trip time. Samples over 200 ms are rejected.
An acknowledged start is normally scheduled 350 ms ahead, in each device's own
clock. Slower links automatically receive up to 700 ms to complete the START
write/read acknowledgement. Rainbow progress and the success animation use this scheduled timeline,
including when the main loop delivers a frame slightly late.

Early release, emergency, an eight-second preparation timeout or 1.2 seconds
without peer contact cancel the session. Setup failures before the animation
starts retry automatically after 250 ms, with at most three attempts in an
eight-second window. Release and press again after an animation is cancelled
or the retry budget is exhausted. The incoming offer supplies the coordinator's
intent even if its request advertisement has not arrived; local button consent,
device-ID ordering, freshness and the RSSI threshold are still required.
Heartbeats continue through the hold; the completed status remains available
for six seconds so the peer can finish its acknowledgement. A new gesture after
the success animation can replace that retained status immediately; it does not
wait for the six-second cleanup or become blocked by it. Wireless cancellation
is best effort: loss of contact right at the completion boundary can still leave
only one device with a saved friendship. There is no atomic distributed commit.

All outgoing BLE connects, discovery and identity reads run in one FreeRTOS
worker. Queues transfer snapshots to the main task, which owns LED rendering,
settings and proximity storage. Disabling BLE invalidates pending work and
defers NimBLE destruction until the worker is finished. The HTTP Wi-Fi scan
returns a retryable error during pairing because its synchronous scan would
otherwise stall animation and temperature sampling.

The GATT status and command characteristics use UUID suffixes `03` and `04` in
the existing `44424700-696e-6772-6573-732d676c7970` service. Packets start with
`FP`, version 1 and a phase/command byte, followed by a little-endian 32-bit
session ID, the sender's six-byte device ID and a little-endian timestamp.
OFFER adds a glyph-present byte, a name-length byte, 64 name bytes and 69 RGBW
pixels. Other commands are START, CANCEL and HEARTBEAT. Status phases are IDLE,
READY and SCHEDULED; status reads stamp the server's current `millis()`.

Run `python -m unittest discover -s tests -v` for host checks of the timing,
release/timeout rules and malformed or stale packets.


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
faction enlightened
wifi 1 "Network name" "Network password"
wifi 2 "Backup network" "Network password"
wifi-clear 1
```

Passwords are never printed by `show`. An empty password configures an open
network. Revision, faction and Wi-Fi changes are saved and restart the controller.
Use `faction res` / `faction resistance` for Resistance (blue), or
`faction enl` / `faction enlightened` for Enlightened (green). Faction names
and aliases are case-insensitive. The `show` command includes the saved faction.

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
Both flash helpers prompt for a faction: `res` / `resistance` (blue) or
`enl` / `enlightened` (green). Leave it blank to keep the board's current faction. On Windows, you
can also supply it directly:

```powershell
.\flash.ps1 -Port COM12 -Revision 2 -Language en -Faction enl
```

Serial configuration waits up to 60 seconds for the controller to answer a
`show` query before sending settings, and again after each saved change restarts
the board. Saved Wi-Fi profiles can delay startup while connection attempts
finish. If flashing succeeded but configuration failed, retry configuration
without rebuilding or uploading:

```powershell
python scripts/configure_serial.py --port COM11 --revision 2 --faction enl
```

### Backup and restore (Windows)

Close serial monitors, then save a complete snapshot of a board:

```powershell
.\backup.ps1 -Port COM12
.\backup.ps1 -Port COM12 -Output .\backups\my-board.zip
```

Without `-Output`, a unique timestamped ZIP is saved in `backups/`. Existing
backup files are never overwritten. The snapshot includes the entire 4 MB
flash: firmware and UI language, partition table, NVS, settings, Wi-Fi,
faction, drawings, friends, history, clock checkpoints, and backup files on
the board. Reading flash temporarily stops the firmware and restarts the board
afterward. These scripts use the ESP32 tools installed by PlatformIO; run a
firmware build once if the tools are not installed yet.

To put that board back exactly as it was at backup time:

```powershell
.\restore.ps1 -Port COM12 -Backup .\backups\my-board.zip
```

Restore validates the archive format, chip type, image size, and SHA-256 checksum
before accessing the board. It then replaces all flash, verifies the written
image, and restarts the board. This also restores the backed-up firmware;
changes made after the backup are replaced. Use these images with the original
ESP32-C3 4 MB board. Hardware MAC addresses and eFuses are not backed up or changed.
If a restore is interrupted, rerun it with the same backup.

Backups contain Wi-Fi passwords and personal history. Keep them private;
the default `backups/` directory is ignored by Git. No board is erased merely
by creating a backup.

The same operations are available from Python on other platforms:

```bash
python scripts/board_backup.py backup --port /dev/ttyUSB0 --output backups/my-board.zip
python scripts/board_backup.py restore --port /dev/ttyUSB0 --backup backups/my-board.zip
```

### Factory reset (Windows)

To remove all saved board data and reinstall firmware with factory defaults:

```powershell
.\factory-reset.ps1 -Port COM12
```

Use `-Language de` or `-Language ja` for German or Japanese (default: English).
The script asks for hardware revision **1** or **2** before building, then saves
your selection after flashing. Use `-Revision 2` to supply it directly.
Close serial monitors before running. The script builds first, then erases the
entire flash and uploads fresh firmware. This permanently removes settings,
Wi-Fi credentials, drawings, display preferences, friends, encounter history,
clock checkpoints, backup files, and ESP32 NVS data. No previous configuration
is restored; only the selected hardware revision is configured afterward.
The board's hardware MAC address remains unchanged.

If upload fails after erasing, rerun the script to restore the firmware.

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
