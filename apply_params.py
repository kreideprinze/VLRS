#!/usr/bin/env python3
"""
apply_params.py - back up all vehicle params, then apply and verify rover_rc.parm.

  apply_params.py                     direct to the Cube over USB (MAVProxy must NOT be running)
  apply_params.py --conn udpin:127.0.0.1:14550    through a running start_bridge.sh
  apply_params.py --verify            read-only check, changes nothing

The backup lands in ~/rover_rc/params_backup_<timestamp>.parm; restore with
MAVProxy "param load <file>".
"""
import argparse
import os
import sys
import time

from pymavlink import mavutil

HERE = os.path.dirname(os.path.abspath(__file__))
CUBE = "/dev/serial/by-id/usb-CubePilot_CubeOrange+_310030001751313437363430-if00"


def read_parm(path):
    out = {}
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if line:
            name, val = line.replace(",", " ").split()[:2]
            out[name] = float(val)
    return out


def fetch_all(m):
    m.mav.param_request_list_send(m.target_system, m.target_component)
    params, total, t_last = {}, None, time.time()
    while time.time() - t_last < 3:
        msg = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
        if msg is None:
            continue
        t_last = time.time()
        params[msg.param_id] = msg.param_value
        total = msg.param_count
        if len(params) >= total:
            break
    return params, total


def verify(m, name, val, tries=3):
    got = None
    for _ in range(tries):
        m.mav.param_request_read_send(m.target_system, m.target_component, name.encode(), -1)
        t0 = time.time()
        while time.time() - t0 < 1.5:
            r = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
            if r and r.param_id == name:
                got = r.param_value
                return "ok" if abs(got - val) < 1e-3 else f"MISMATCH (vehicle has {got:g})"
    return "NO REPLY (check manually)"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--conn", default=CUBE)
    ap.add_argument("--file", default=os.path.join(HERE, "rover_rc.parm"))
    ap.add_argument("--verify", action="store_true", help="read-only: just check the vehicle matches the file")
    args = ap.parse_args()

    if args.conn == CUBE and not os.path.exists(CUBE):
        sys.exit(f"ERROR: Cube not found at {CUBE}")
    want = read_parm(args.file)
    m = mavutil.mavlink_connection(args.conn, baud=115200, source_system=254)
    hb = m.wait_heartbeat(timeout=15)
    if hb is None:
        sys.exit("ERROR: no heartbeat from vehicle")
    print(f"vehicle sysid {m.target_system} compid {m.target_component}, {mavutil.mode_string_v10(hb)}, "
          f"{'ARMED' if hb.base_mode & 128 else 'disarmed'}")

    if args.verify:
        results = {name: verify(m, name, val) for name, val in want.items()}
        for name, st in results.items():
            print(f"  {name:<18} want {want[name]:g}  {st}")
        sys.exit(0 if all(v == "ok" for v in results.values()) else 1)

    print("fetching all params for backup ...")
    params, total = fetch_all(m)
    if total is None or len(params) < total:
        sys.exit(f"ERROR: only got {len(params)}/{total} params, not changing anything without a full backup")
    backup = os.path.join(HERE, time.strftime("params_backup_%Y%m%d_%H%M%S.parm"))
    with open(backup, "w") as f:
        for k in sorted(params):
            f.write(f"{k},{params[k]:.6g}\n")
    print(f"backup: {backup} ({len(params)} params)")

    ok = True
    for name, val in want.items():
        if name not in params:
            print(f"  {name:<18} NOT ON THIS FIRMWARE - skipped")
            ok = False
            continue
        m.mav.param_set_send(m.target_system, m.target_component, name.encode(), val,
                             mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
        time.sleep(0.2)
    time.sleep(0.5)
    while m.recv_match(blocking=False):   # drop queued echoes, verify with explicit reads
        pass
    for name, val in want.items():
        if name not in params:
            continue
        status = verify(m, name, val)
        ok &= status == "ok"
        print(f"  {name:<18} {params[name]:g} -> {val:g}  {status}")

    for name, bad, note in [("ARMING_SKIPCHK", lambda v: v != 0, "all arming checks skipped"),
                            ("FS_THR_ENABLE", lambda v: v == 0, "SBUS/RC-loss failsafe is off")]:
        if name in params and bad(params[name]):
            print(f"  note: {name}={params[name]:g} ({note}) - left unchanged")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
