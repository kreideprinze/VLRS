#!/usr/bin/env python3
"""
rc_monitor.py - watch what the Cube actually sees on its RC channels.

  rc_monitor.py                         live view via MAVProxy (udp 127.0.0.1:14550)
  rc_monitor.py --capture 60            log every channel change for 60 s, then summarise
  rc_monitor.py --conn /dev/serial/by-id/...CubeOrange+...-if00   talk to the Cube directly

Use it to compare the Arduino RC with the SBUS RC: the SBUS capture showed
ch1 = steering (left = low), ch2 = throttle (forward = high), centre 1495.
"""
import argparse
import sys
import time

from pymavlink import mavutil

SHOW = [1, 2, 3, 4, 5, 6, 9, 14]
NAMES = {1: "str", 2: "thr", 5: "mode", 9: "arm", 14: "estp"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--conn", default="udpin:127.0.0.1:14550", help="pymavlink connection string")
    ap.add_argument("--capture", type=float, metavar="SEC", help="log channel changes for SEC seconds")
    ap.add_argument("--threshold", type=int, default=60, help="change in us that counts as movement (capture)")
    args = ap.parse_args()

    m = mavutil.mavlink_connection(args.conn, source_system=254)
    print(f"connecting {args.conn} ...", flush=True)
    hb = m.wait_heartbeat(timeout=15)
    if hb is None:
        sys.exit("ERROR: no vehicle heartbeat (is start_bridge.sh / MAVProxy running?)")
    print(f"vehicle sysid {m.target_system}", flush=True)
    for s in (mavutil.mavlink.MAV_DATA_STREAM_RC_CHANNELS, mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS):
        m.mav.request_data_stream_send(m.target_system, m.target_component, s, 10, 1)

    mode, armed = "?", False
    servo = [0, 0, 0, 0]
    t0 = time.time()
    cur = mn = mx = rest = None
    last_print = 0.0

    while True:
        msg = m.recv_match(type=["RC_CHANNELS", "SERVO_OUTPUT_RAW", "HEARTBEAT"], blocking=True, timeout=1)
        now = time.time()
        if args.capture and now - t0 > args.capture:
            break
        if msg is None:
            continue
        t = msg.get_type()
        if t == "HEARTBEAT":
            if msg.get_srcSystem() == m.target_system and msg.autopilot != mavutil.mavlink.MAV_AUTOPILOT_INVALID:
                new = (mavutil.mode_string_v10(msg), bool(msg.base_mode & 128))
                if args.capture and new != (mode, armed):
                    print(f"t={now - t0:5.1f}  MODE {new[0]}  {'ARMED' if new[1] else 'disarmed'}", flush=True)
                mode, armed = new
            continue
        if t == "SERVO_OUTPUT_RAW":
            servo = [msg.servo1_raw, msg.servo2_raw, msg.servo3_raw, msg.servo4_raw]
            continue

        ch = [getattr(msg, f"chan{i}_raw") for i in range(1, 17)]
        if args.capture:
            if cur is None:
                cur, mn, mx, rest = ch[:], ch[:], ch[:], ch[:]
                print("REST " + " ".join(f"ch{i + 1}={v}" for i, v in enumerate(ch)), flush=True)
            for i in range(16):
                mn[i], mx[i] = min(mn[i], ch[i]), max(mx[i], ch[i])
                if abs(ch[i] - cur[i]) > args.threshold:
                    print(f"t={now - t0:5.1f}  CH{i + 1:<2} {cur[i]} -> {ch[i]}", flush=True)
                    cur[i] = ch[i]
        elif now - last_print > 0.2:
            last_print = now
            cols = "  ".join(f"{NAMES.get(c, 'ch' + str(c))}:{ch[c - 1]:4d}" for c in SHOW)
            line = f"{cols}  | {mode:<8} {'ARMED' if armed else 'disarm'} | servo {servo[0]} {servo[1]} {servo[2]} {servo[3]}"
            print("\r" + line + "   ", end="", flush=True)

    if args.capture and cur is not None:
        print("\nSUMMARY (moved channels)")
        for i in range(16):
            span = mx[i] - mn[i]
            if span > args.threshold:
                print(f"  CH{i + 1:<2} rest={rest[i]} min={mn[i]} max={mx[i]} span={span}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
