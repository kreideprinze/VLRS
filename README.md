# ROVER//RC: two UNO R4 WiFi boards as an RC transmitter and receiver

```
handheld R4 (TX) --WiFi UDP 14570--> rover R4 (RX) --SBUS on D1--> Cube RC IN
```

The rover R4 replaces the SBUS receiver. ArduPilot sees a normal **SBUS** receiver: no MAVLink, no laptop, no OBC.
The SBUS output matches an ExpressLRS receiver: inverted, 100000 8E2, 16 channels, every 9 ms.
The R4's UART can't invert (SCI2 has no TINV), so a GPT timer generates the line directly (`rover_rc_rx/wave.h`).
- **Other outputs:** `out ppm` (8 ch) or `out ibus` (14 ch, for a serial port with `SERIALx_PROTOCOL=23`), over serial or the web page.
The RC **never arms or disarms**.

## Wiring (rover)
| Rover R4 | Cube |
|---|---|
| D1 (TX) | RC IN signal |
| GND | RC IN GND |

**Check signal vs GND on the RC IN header:** having them swapped is exactly why nothing decoded during bring-up.

- **Power:** feed the rover R4 from its own 5 V (USB or a BEC to the 5V pin). Don't use the RC IN 5 V pin, because the R4's WiFi draws up to ~250 mA.
- **Detection:** ArduPilot auto-detects SBUS (`RC_PROTOCOLS=1`).

## Controls (handheld)
| Control | Channel | |
|---|---|---|
| Right stick fwd/back | ch2 | throttle |
| Right stick left/right | ch1 | steering |
| D2 (2-pos) | ch5 | ON = MANUAL, OFF = HOLD |
| D3 (2-pos) | ch14 | motor E-stop |
| SW2 (3-pos) | ch10 | spare; hold DOWN at power-on = handheld config WiFi |
| Left stick | ch3/ch4 | spare |
| ch6–9, ch11–13 | | fixed at 1501 (middle) |

ch6/ch9/ch16 are Arm/Disarm on the rover Cube. The middle position is "no action", so the RC can never arm or disarm, not even when RC first appears.

## Two ways to connect
- **Direct (default):**
  - The rover R4 makes its own WiFi, `ROVER-LINK-<id>` (password `roverrc123`, IP 192.168.5.1).
  - The handheld joins it automatically. Nothing to set up.
- **External network:**
  - Put **both** boards on the same 2.4 GHz network (e.g. UAS-DTU), using each board's web page (Net → Scan → Join) or USB serial `join`.
  - They find each other by broadcast.
  - Networks that block devices from talking to each other (DTU-WiFi) won't work.

**Fallbacks:**
- **Rover R4:** saved network → its own `ROVER-LINK-<id>`.
- **Handheld:** saved network → `ROVER-LINK-<id>` → its own config WiFi `ROVER-RC-<id>` at 192.168.4.1, so its web page is always reachable.

## Bind ID: several pairs at once
- **Pairing:** a handheld and a rover R4 pair up only if they have the **same bind ID (1–99)**. Default 1.
- **Setting it:** on each board's web page (Net → Bind ID) or over serial with `id <n>`. The board reboots.
- **Several pairs:** each pair gets its own `ROVER-LINK-<id>` on WiFi channel 1, 6 or 11 (picked from the ID).

## LED matrix (both boards)
```
 0 7 . M     top left: bind ID       top right: M MANUAL / H HOLD / X E-stop
             ! interlock (centre throttle)  F failsafe  ? (blinking) searching
 ■ ▬▬▬▬▬     bottom left: solid = direct link, "v" = external network, "n" = handheld config WiFi
             bottom right: link quality bar (8 px = 100 %)
```
- **Network change:** the tower ↔ WiFi-fan animation plays.
- **After boot:** `ID07 EXT 192.168.1.104` scrolls once.

## Web pages
Open the board's IP (it scrolls on the LED). Both pages use the same black/white/blue style.
- **Handheld:** sticks, channels, switches, a rover-receiver panel (link quality, RSSI, what the rover is outputting).
- **Rover R4:** link state, the channels going into RC IN, failsafe flag, output selector (SBUS / PPM / iBus).
- **Net panel (both):** scan / join / forget, bind ID, partner IP (blank = broadcast discovery), static IP / gateway / mask (blank = DHCP), direct-link password (must match on both).

## Every session
1. Power the rover R4 and the handheld. **Leave the sticks alone while the handheld boots**: it re-measures stick centres.
2. Wait for both LEDs to show the bind ID, a mode letter and a link bar.
3. Centre the throttle (`!` clears). Arm from your GCS.
4. **Flip D2 to HOLD and back to MANUAL.** After power-up or any dropout the rover R4 holds ch5 at HOLD until it sees the switch at HOLD, so the rover never drives off by itself.

## Failsafe (built into the rover R4; no ArduPilot params needed)
| Situation | SBUS output |
|---|---|
| Rover R4 booted, no handheld yet | neutral, HOLD, failsafe flag |
| Handheld interlocked or packets > 300 ms late | neutral sticks, HOLD |
| No handheld packets > 1.5 s | neutral, HOLD, failsafe flag |
| Link back | sticks after the throttle is centred; MANUAL only after D2 has been seen at HOLD |

- **E-stop (ch14)** is never forced on, so RC IN can't block your autonomy stack while the handheld is off.
- **Optional:** `FS_THR_ENABLE=1` makes ArduPilot also react to the SBUS failsafe flag (FS_ACTION → Hold).

## Range
The R4 has a PCB antenna at about +20 dBm.
- **Line of sight:** expect ~100–200 m.
- **Rover low to the ground:** ~50–100 m.
- **Measuring it:** walk out with the handheld's web page on a phone and watch link quality; below ~90 % is the edge.

## Flashing
```
./flash_rc.sh tx     # handheld
./flash_rc.sh rx     # rover R4 (first time: plug in only that board, its serial is recorded in .rx_board)
```
- **"No device found":** double-tap RESET on that R4 and rerun.
- **Serial commands (both boards):** `stat`, `join <ssid><TAB><pass>`, `ap`, `id <n>`, `peer <ip|->`, `ip <ip> [gw] [mask]`, `ip -`, `appass <p>`. The handheld also has `c` (calibration).
- **Calibrate the sticks:** power the handheld with both 2-pos switches ON: centre 5 s, sweep corners 15 s, tick = saved.
- **Stick direction wrong:** see `rover_rc_tx/config.h` (`SWAP_RIGHT_STICK`, `INVERT_*`).

## Files
- `rover_rc_tx/`, `rover_rc_rx/`: the two sketches.
- `libraries/RoverLink/`: shared packet format, WiFi bring-up, web server, NET panel, LED screens.
- `rc_monitor.py`: what the Cube receives on its channels (needs MAVProxy or a direct USB link).

## Legacy: MAVLink teleop (previous design)
`rover_rc_tx_mavlink/` (handheld firmware), `rc_link.py`, `start_bridge.sh`, `rover_rc.parm`.
The handheld sends `RC_CHANNELS_OVERRIDE` as sysid 200 through MAVProxy.
- It needs `MAV_GCS_SYSID=200`, which conflicts with an autonomy stack sending overrides as sysid 1.
- Param backup from before any change: `params_backup_20261012_062525.parm`.
