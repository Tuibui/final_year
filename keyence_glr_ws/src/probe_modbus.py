#!/usr/bin/env python3
"""Probe a Keyence GC-1000 over Modbus/TCP — the fallback path after the
GC-1000 rejected EtherNet/IP explicit reads ("Service not supported").

Dumps a block of a Modbus data area and, in watch mode, highlights which
registers/bits change between reads. Wave your hand through the curtain: the
bits that toggle ARE the beam bitmap, and their index tells you the register
+ bit offset to read in the driver.

Modbus has four areas — the beam status could live in any of them, so try each:
  holding  (FC3, 16-bit r/w)      input    (FC4, 16-bit read-only)
  coils    (FC1, 1-bit r/w)       discrete (FC2, 1-bit read-only)

Prerequisites:
  * pip3 install --user 'pymodbus>=3'
  * Modbus/TCP enabled on the GC-1000 (GC Configurator) — and the beam data
    actually assigned to the comm area.

Stop the ROS driver first so two clients don't poll at once.

Examples:
  ./probe_modbus.py                              # watch holding regs 0..63
  ./probe_modbus.py --area input --count 64
  ./probe_modbus.py --area discrete --count 128  # 52 beams likely as bits
  ./probe_modbus.py --area holding --once
"""
from __future__ import annotations

import argparse
import inspect
import sys
import time

try:
    from pymodbus.client import ModbusTcpClient            # pymodbus 3.x
except ImportError:
    try:
        from pymodbus.client.sync import ModbusTcpClient   # pymodbus 2.x
    except ImportError:
        sys.exit("pymodbus not installed.  pip3 install --user 'pymodbus>=3'")


class C:
    use = sys.stdout.isatty()
    R = '\033[31m' if use else ''
    G = '\033[32m' if use else ''
    Y = '\033[33m' if use else ''
    DIM = '\033[2m' if use else ''
    OFF = '\033[0m' if use else ''


# (client method, kind) per area
AREAS = {
    'holding':  ('read_holding_registers', 'reg'),
    'input':    ('read_input_registers',   'reg'),
    'coils':    ('read_coils',             'bit'),
    'discrete': ('read_discrete_inputs',   'bit'),
}


def _id_kwarg(fn, unit):
    """`slave=` on pymodbus 3.x, `unit=` on 2.x."""
    params = inspect.signature(fn).parameters
    if 'slave' in params:
        return {'slave': unit}
    if 'unit' in params:
        return {'unit': unit}
    return {}


def read_area(client, area, start, count, unit):
    """Return (values_list, error_str). values = ints (reg) or 0/1 (bit)."""
    fn = getattr(client, AREAS[area][0])
    try:
        try:
            rr = fn(start, count=count, **_id_kwarg(fn, unit))
        except TypeError:
            rr = fn(start, count, **_id_kwarg(fn, unit))   # older positional API
    except Exception as exc:                               # noqa: BLE001
        return None, f'{type(exc).__name__}: {exc}'
    if rr is None or (hasattr(rr, 'isError') and rr.isError()):
        return None, str(rr)
    if AREAS[area][1] == 'reg':
        return list(rr.registers), None
    return [1 if b else 0 for b in rr.bits][:count], None


def reg_set_bits(values: list[int], kind: str) -> set[int]:
    """Global '1' bit indices. reg: idx*16+bit (LSB). bit: the address itself."""
    out = set()
    if kind == 'bit':
        return {i for i, v in enumerate(values) if v}
    for i, v in enumerate(values):
        for b in range(16):
            if v & (1 << b):
                out.add(i * 16 + b)
    return out


def dump(values: list[int], kind: str, start: int, changed: set[int]) -> str:
    lines = []
    if kind == 'reg':
        per = 8
        for off in range(0, len(values), per):
            cells = []
            for j, v in enumerate(values[off:off + per]):
                cell = f'{v:04x}'
                if (off + j) in changed:
                    cell = f'{C.Y}{cell}{C.OFF}'
                cells.append(cell)
            lines.append(f'  {C.DIM}{start + off:4d}{C.OFF}  ' + ' '.join(cells))
    else:
        per = 32
        for off in range(0, len(values), per):
            cells = []
            for j, v in enumerate(values[off:off + per]):
                ch = str(v)
                if (off + j) in changed:
                    ch = f'{C.Y}{ch}{C.OFF}'
                cells.append(ch)
            lines.append(f'  {C.DIM}{start + off:4d}{C.OFF}  ' + ''.join(cells))
    return '\n'.join(lines)


def main() -> None:
    p = argparse.ArgumentParser(
        description='Probe a Keyence GC-1000 over Modbus/TCP.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument('--ip', default='192.168.0.10', help='GC-1000 IP (default 192.168.0.10)')
    p.add_argument('--port', type=int, default=502, help='Modbus TCP port (default 502)')
    p.add_argument('--unit', type=int, default=1, help='unit/slave id (default 1)')
    p.add_argument('--area', choices=list(AREAS), default='holding', help='data area (default holding)')
    p.add_argument('--start', type=int, default=0, help='start address (default 0)')
    p.add_argument('--count', type=int, default=64, help='how many regs/bits to read (default 64)')
    p.add_argument('--interval', type=float, default=0.3, help='seconds between reads in watch mode')
    p.add_argument('--once', action='store_true', help='read once and exit')
    args = p.parse_args()

    kind = AREAS[args.area][1]
    client = ModbusTcpClient(args.ip, port=args.port)
    if not client.connect():
        sys.exit(f'{C.R}cannot connect to {args.ip}:{args.port} — is Modbus/TCP '
                 f'enabled on the GC-1000?{C.OFF}')
    print(f'== {"single read" if args.once else "live watch"} {args.area} '
          f'[{args.start}..{args.start + args.count - 1}] unit {args.unit} '
          f'on {args.ip}:{args.port} ==')
    if not args.once:
        print(f'{C.DIM}   wave your hand through the curtain; toggled bits = the beam '
              f'bitmap. Ctrl-C to stop.{C.OFF}')

    prev_vals: list[int] | None = None
    prev_bits: set[int] = set()
    try:
        while True:
            vals, err = read_area(client, args.area, args.start, args.count, args.unit)
            if vals is None:
                print(f'{C.R}read failed: {err}{C.OFF}')
            else:
                changed = {i for i in range(len(vals))
                           if prev_vals is None or i >= len(prev_vals) or vals[i] != prev_vals[i]}
                bits = reg_set_bits(vals, kind)
                print(f'\n[{time.strftime("%H:%M:%S")}] {len(vals)} {args.area}')
                print(dump(vals, kind, args.start, changed if prev_vals is not None else set()))
                print(f'  bits=1 (global idx): {sorted(bits) if bits else "[]"}')
                if prev_vals is not None:
                    on, off = sorted(bits - prev_bits), sorted(prev_bits - bits)
                    if on:
                        print(f'  {C.G}+ ON : {on}{C.OFF}')
                    if off:
                        print(f'  {C.R}- OFF: {off}{C.OFF}')
                prev_vals, prev_bits = vals, bits
            if args.once:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print('\nstopped.')
    finally:
        client.close()


if __name__ == '__main__':
    main()
