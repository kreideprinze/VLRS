#!/usr/bin/env bash
# Compile + flash ROVER//RC firmware, then show the board's serial log.
#
#   ./flash_rc.sh tx    handheld   (rover_rc_tx)
#   ./flash_rc.sh rx    rover RX   (rover_rc_rx)
#
# Boards are told apart by USB serial number. The handheld's is fixed below;
# the RX's is recorded in .rx_board on its first flash (when it is the only
# other R4 plugged in). It never guesses between two unknown boards.
# If upload says "No device found": double-tap RESET on that R4 (LED pulses) and rerun.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
CLI="$HOME/.local/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli"
FQBN="arduino:renesas_uno:unor4wifi"
TX_BYID="/dev/serial/by-id/usb-Arduino_UNO_WiFi_R4_CMSIS-DAP_34CDB03AD040-if01"
RX_FILE="$HERE/.rx_board"

role="${1:-}"
case "$role" in
  tx) SKETCH="rover_rc_tx" ;;
  rx) SKETCH="rover_rc_rx" ;;
  *) echo "usage: $0 tx|rx"; exit 2 ;;
esac

mapfile -t R4S < <(ls /dev/serial/by-id/ 2>/dev/null | grep -E 'UNO_WiFi_R4|UNO_R4_WiFi' | grep -E 'if0[01]$' | sed 's|^|/dev/serial/by-id/|')

if [ "$role" = tx ]; then
  BYID="$TX_BYID"
  [ -e "$BYID" ] || { echo "ERROR: handheld R4 not found at $BYID"; printf '  plugged in: %s\n' "${R4S[@]:-none}"; exit 1; }
else
  if [ -f "$RX_FILE" ]; then
    BYID="$(cat "$RX_FILE")"
    [ -e "$BYID" ] || { echo "ERROR: rover RX R4 not found at $BYID (recorded in $RX_FILE)"; printf '  plugged in: %s\n' "${R4S[@]:-none}"; exit 1; }
  else
    OTHERS=()
    for d in "${R4S[@]}"; do [ "$d" != "$TX_BYID" ] && OTHERS+=("$d"); done
    if [ "${#OTHERS[@]}" -ne 1 ]; then
      echo "ERROR: need exactly one R4 that is not the handheld to record as the RX; found ${#OTHERS[@]}:"
      printf '  %s\n' "${OTHERS[@]:-none}"
      echo "Plug in only the rover R4 (the handheld may stay connected) and rerun."
      exit 1
    fi
    BYID="${OTHERS[0]}"
    echo "$BYID" > "$RX_FILE"
    echo "recorded rover RX board: $BYID"
  fi
fi
PORT="$(readlink -f "$BYID")"          # bossac wants the real ttyACMx name
echo "flashing $SKETCH -> $BYID ($PORT)"

"$CLI" compile --fqbn "$FQBN" --libraries "$HERE/libraries" "$HERE/$SKETCH"
if ! "$CLI" upload --fqbn "$FQBN" -p "$PORT" "$HERE/$SKETCH"; then
  echo
  echo "Upload failed. Double-tap RESET on that R4 (the L LED starts pulsing) and run this again."
  exit 1
fi

echo "--- serial log (Ctrl-C to stop) ---"
for _ in $(seq 1 30); do [ -e "$BYID" ] && break; sleep 0.5; done
sleep 0.5
exec "$HOME/venv-ardupilot/bin/python3" -c "
import serial
s = serial.Serial('$BYID', 115200, timeout=1)
while True:
    l = s.readline()
    if l: print(l.decode(errors='replace').rstrip(), flush=True)
"
