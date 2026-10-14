#!/usr/bin/env bash
# ROVER//RC laptop bridge:  Cube <-USB-> MAVProxy <-> rc_link.py <-WiFi-> RC
#
#   ./start_bridge.sh           laptop and RC are on the same network (RC in STA mode)
#   ./start_bridge.sh --rc-ap   join the RC's own AP "ROVER-RC" first (RC in AP mode)
#
# MAVProxy runs in the foreground (its console is yours); type "exit" to stop everything.
# Extra outputs: udp 127.0.0.1:14550 for rc_monitor.py or QGroundControl.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="$HOME/venv-ardupilot"
CUBE="/dev/serial/by-id/usb-CubePilot_CubeOrange+_310030001751313437363430-if00"
WIFI_DEV="wlp0s20f3"
RC_SSID="ROVER-RC"
RC_PASS="${RC_PASS:-roverrc123}"   # export RC_PASS=... if you changed it on the web page

USE_AP=0
for a in "$@"; do
  case "$a" in
    --rc-ap) USE_AP=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown argument: $a (try --help)"; exit 2 ;;
  esac
done

if [ ! -e "$CUBE" ]; then
  echo "ERROR: Cube Orange+ not found at $CUBE"
  echo "       Check the USB cable. (Not falling back to another serial port.)"
  exit 1
fi
if command -v fuser >/dev/null && fuser "$CUBE" >/dev/null 2>&1; then
  echo "WARN: something already has the Cube port open (QGC / another MAVProxy?):"
  fuser -v "$CUBE" 2>&1 | sed 's/^/      /'
fi

PIDS=()
cleanup() {
  for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
  wait 2>/dev/null
  if [ "$USE_AP" = 1 ]; then
    echo
    echo "[bridge] laptop WiFi is still on $RC_SSID. Back to your usual network:"
    echo "         nmcli con up UAS-DTU_5G"
  fi
}
trap cleanup EXIT

join_rc_ap() {
  while ! nmcli -t -f active,ssid dev wifi 2>/dev/null | grep -qx "yes:$RC_SSID"; do
    nmcli dev wifi rescan ifname "$WIFI_DEV" >/dev/null 2>&1
    if nmcli dev wifi connect "$RC_SSID" password "$RC_PASS" ifname "$WIFI_DEV" >/dev/null 2>&1; then
      # never auto-join the RC AP later, and never route the internet through it
      nmcli con modify "$RC_SSID" connection.autoconnect no ipv4.never-default yes >/dev/null 2>&1
      echo "[bridge] joined $RC_SSID: $(ip -4 -br addr show "$WIFI_DEV" | awk '{print $3}')"
    else
      echo "[bridge] WARN: $RC_SSID not reachable yet (RC powered? in AP mode?) - retrying in 5 s"
      sleep 5
    fi
  done
}

if [ "$USE_AP" = 1 ]; then
  ( trap '' INT; join_rc_ap ) &   # Ctrl-C belongs to MAVProxy, not the helpers
  PIDS+=($!)
else
  echo "[bridge] staying on current network: $(nmcli -t -f active,ssid dev wifi 2>/dev/null | sed -n 's/^yes://p') $(ip -4 -br addr show "$WIFI_DEV" | awk '{print $3}')"
fi

# SIGINT ignored: Ctrl-C at the MAVProxy prompt must not kill the relay.
( trap '' INT; exec "$VENV/bin/python3" "$HERE/rc_link.py" ) &
PIDS+=($!)

mkdir -p "$HERE/logs"
cd "$HERE/logs" || exit 1
"$VENV/bin/mavproxy.py" \
  --master="$CUBE" --baudrate 115200 \
  --out=udp:127.0.0.1:14561 \
  --out=udp:127.0.0.1:14550
