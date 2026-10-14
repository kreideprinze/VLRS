#!/usr/bin/env python3
"""
rc_link.py - relay between the ROVER//RC handheld (WiFi, UDP 14560) and MAVProxy.

    RC  --UDP 14560-->  rc_link  --127.0.0.1:14561-->  MAVProxy --USB--> Cube
    RC  <-- vehicle HEARTBEAT only --  rc_link  <-- all telemetry --  MAVProxy

Why a relay instead of pointing MAVProxy straight at the RC: MAVProxy forwards
the full telemetry stream to every output, and the R4's WiFi modem talks to the
MCU over a 115200-baud AT link. Sending it only the vehicle heartbeat (~21 B/s)
keeps that link free for the 25 Hz control packets.

Listens on 0.0.0.0, so it works whether the laptop is on the RC's own AP or on
the same network as the RC. Prints the RC's IP when it shows up.

Started by start_bridge.sh; run it by hand with --help for options.
"""
import argparse
import socket
import select
import time

from pymavlink.dialects.v20 import ardupilotmega as mavlink

ROVER_MODES = {0: "MANUAL", 1: "ACRO", 3: "STEERING", 4: "HOLD", 5: "LOITER", 6: "FOLLOW",
               7: "SIMPLE", 8: "DOCK", 9: "CIRCLE", 10: "AUTO", 11: "RTL", 12: "SMART_RTL",
               15: "GUIDED", 16: "INIT"}


def log(msg):
    print(f"[rc_link {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rc-port", type=int, default=14560, help="UDP port the RC talks to (default 14560)")
    ap.add_argument("--mp-port", type=int, default=14561, help="local port MAVProxy --out points at (default 14561)")
    ap.add_argument("--rc-sysid", type=int, default=200, help="RC MAVLink sysid (default 200)")
    ap.add_argument("--vehicle-sysid", type=int, default=1, help="vehicle MAVLink sysid (default 1)")
    ap.add_argument("--stats", type=float, default=5.0, help="seconds between link stat lines (default 5)")
    args = ap.parse_args()

    rc_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rc_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rc_sock.bind(("0.0.0.0", args.rc_port))
    mp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    mp_sock.bind(("127.0.0.1", args.mp_port))
    log(f"listening for RC on 0.0.0.0:{args.rc_port}, MAVProxy on 127.0.0.1:{args.mp_port}")

    rc_parser = mavlink.MAVLink(None)
    rc_parser.robust_parsing = True
    mp_parser = mavlink.MAVLink(None)
    mp_parser.robust_parsing = True

    rc_addr = None
    mp_addr = None
    rc_last = 0.0
    rc_lost_reported = True
    veh_state = None
    warned_no_mp = False

    # override timing stats for the current window
    ov_count = 0
    ov_last = None
    ov_max_gap = 0.0
    t_stats = time.monotonic()
    last_ch = None

    while True:
        ready, _, _ = select.select([rc_sock, mp_sock], [], [], 0.5)
        now = time.monotonic()

        if rc_sock in ready:
            data, addr = rc_sock.recvfrom(2048)
            try:
                msgs = rc_parser.parse_buffer(data) or []
            except mavlink.MAVError:
                msgs = []
            msgs = [m for m in msgs if m.get_srcSystem() == args.rc_sysid]
            if msgs:
                if addr != rc_addr or rc_lost_reported:
                    log(f"RC found at {addr[0]}:{addr[1]}  (web page: http://{addr[0]}/)")
                    rc_addr = addr
                    rc_lost_reported = False
                rc_last = now
                for m in msgs:
                    if m.get_type() == "RC_CHANNELS_OVERRIDE":
                        ov_count += 1
                        if ov_last is not None:
                            ov_max_gap = max(ov_max_gap, now - ov_last)
                        ov_last = now
                        last_ch = (m.chan1_raw, m.chan2_raw, m.chan5_raw, m.chan14_raw)
                if mp_addr:
                    mp_sock.sendto(data, mp_addr)
                elif not warned_no_mp:
                    log("WARN: RC packets arriving but MAVProxy hasn't connected to this relay yet")
                    warned_no_mp = True

        if mp_sock in ready:
            data, addr = mp_sock.recvfrom(4096)
            if mp_addr != addr:
                log(f"MAVProxy connected from {addr[0]}:{addr[1]}")
                mp_addr = addr
                warned_no_mp = False
            try:
                msgs = mp_parser.parse_buffer(data) or []
            except mavlink.MAVError:
                msgs = []
            for m in msgs:
                if (m.get_type() == "HEARTBEAT" and m.get_srcSystem() == args.vehicle_sysid
                        and m.autopilot != mavlink.MAV_AUTOPILOT_INVALID):
                    if rc_addr and not rc_lost_reported:
                        rc_sock.sendto(m.get_msgbuf(), rc_addr)
                    st = (bool(m.base_mode & mavlink.MAV_MODE_FLAG_SAFETY_ARMED),
                          ROVER_MODES.get(m.custom_mode, str(m.custom_mode)))
                    if st != veh_state:
                        log(f"vehicle {'ARMED' if st[0] else 'disarmed'}  mode {st[1]}")
                        veh_state = st

        if rc_addr and not rc_lost_reported and now - rc_last > 3.0:
            log(f"RC LOST (no packets from {rc_addr[0]} for 3 s) - waiting for it to come back")
            rc_lost_reported = True

        if now - t_stats >= args.stats:
            dt = now - t_stats
            if rc_addr and not rc_lost_reported:
                rate = ov_count / dt
                ch = ("ch1 %d ch2 %d ch5 %d ch14 %d" % last_ch) if ov_count else \
                    "no overrides - RC interlock: centre the throttle (reason shown on the RC web page)"
                warn = "  <-- WARN gap > 250 ms" if ov_max_gap > 0.25 else ""
                log(f"RC {rc_addr[0]}  override {rate:4.1f} Hz  max gap {ov_max_gap * 1000:4.0f} ms  {ch}{warn}")
            elif not mp_addr:
                log("waiting for MAVProxy and RC")
            else:
                log("waiting for RC (is it on the same network? its IP scrolls on the LED at boot)")
            ov_count = 0
            ov_max_gap = 0.0
            t_stats = now


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
