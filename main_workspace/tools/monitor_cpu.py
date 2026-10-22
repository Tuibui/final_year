#!/usr/bin/env python3
"""Monitor the separate camera and light-curtain processes without starting them."""

import argparse
import csv
from datetime import datetime
import os
from pathlib import Path
import time


def snapshot():
    processes = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            args = [p.decode(errors='replace') for p in
                    (entry / 'cmdline').read_bytes().split(b'\0') if p]
            if not args:
                continue
            executable = Path(args[0]).name
            script = Path(args[1]).name if len(args) > 1 else ''
            if executable == 'hand_detect_node':
                group = 'camera'
            elif executable == 'gc_modbus_driver' or (
                    executable.startswith('python') and script == 'gc_modbus_driver'):
                group = 'curtain'
            else:
                continue
            raw = (entry / 'stat').read_text()
            fields = raw[raw.rfind(')') + 2:].split()
            processes[int(entry.name)] = {
                'group': group,
                'ticks': int(fields[11]) + int(fields[12]),
                'threads': int(fields[17]),
                'start': int(fields[19]),
                'rss': int(fields[21]) * os.sysconf('SC_PAGE_SIZE') / (1024 * 1024),
            }
        except (OSError, ValueError, IndexError):
            # Processes may exit between directory listing and reading their files.
            continue
    with Path('/proc/stat').open() as stream:
        cpu = [int(v) for v in stream.readline().split()[1:9]]
    return time.monotonic(), sum(cpu), cpu[3] + cpu[4], processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interval', type=float, default=1.0)
    parser.add_argument('--duration', type=float, default=0,
                        help='Seconds to monitor; 0 runs until Ctrl+C')
    parser.add_argument('--output', type=Path,
                        help='CSV path inside the repository; optional')
    args = parser.parse_args()
    if not 0.1 <= args.interval <= 60 or not 0 <= args.duration < float('inf'):
        parser.error('Use interval 0.1..60 seconds and a finite nonnegative duration')
    repo = Path(__file__).resolve().parents[2]
    stream = None
    writer = None
    fields = ['timestamp', 'system_cpu_pct', 'camera_cpu_pct', 'curtain_cpu_pct',
              'camera_rss_mb', 'curtain_rss_mb', 'camera_threads', 'curtain_threads',
              'camera_pids', 'curtain_pids']
    if args.output:
        path = args.output.expanduser().resolve()
        if not path.is_relative_to(repo):
            parser.error('The CSV output must be inside the repository')
        path.parent.mkdir(parents=True, exist_ok=True)
        stream = path.open('x', newline='')
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        print(f'CSV: {path}', flush=True)
    print('Process CPU: 100% = one CPU core. System CPU: 100% = all CPU cores.', flush=True)
    print('A dash means stopped or waiting for the first sample after startup.', flush=True)
    print('TIME       SYS%   CAM%   CURTAIN%  CAM_MB  CURTAIN_MB  CAM_PID  CURTAIN_PID', flush=True)
    previous = snapshot()
    started = previous[0]
    ticks_per_second = os.sysconf('SC_CLK_TCK')
    try:
        while True:
            remaining = args.duration - (time.monotonic() - started) if args.duration else args.interval
            if remaining <= 0:
                break
            time.sleep(min(args.interval, remaining))
            current = snapshot()
            elapsed = current[0] - previous[0]
            total = current[1] - previous[1]
            idle = current[2] - previous[2]
            row = {'timestamp': datetime.now().astimezone().isoformat(timespec='seconds'),
                   'system_cpu_pct': round(100 * (total - idle) / total, 2) if total > 0 else ''}
            for group in ('camera', 'curtain'):
                members = [(pid, proc) for pid, proc in current[3].items() if proc['group'] == group]
                comparable = [(proc, previous[3][pid]) for pid, proc in members
                              if pid in previous[3] and proc['start'] == previous[3][pid]['start']]
                row[f'{group}_cpu_pct'] = round(100 * sum(
                    proc['ticks'] - before['ticks'] for proc, before in comparable
                ) / ticks_per_second / elapsed, 2) if comparable and elapsed > 0 else ''
                row[f'{group}_rss_mb'] = round(sum(proc['rss'] for _, proc in members), 1)
                row[f'{group}_threads'] = sum(proc['threads'] for _, proc in members)
                row[f'{group}_pids'] = ','.join(str(pid) for pid, _ in members)
            if writer:
                writer.writerow(row)
                stream.flush()
            camera_cpu = '-' if row['camera_cpu_pct'] == '' else str(row['camera_cpu_pct'])
            curtain_cpu = '-' if row['curtain_cpu_pct'] == '' else str(row['curtain_cpu_pct'])
            print(f"{row['timestamp'][11:19]}  {row['system_cpu_pct']:>5}  "
                  f"{camera_cpu:>6}  {curtain_cpu:>8}  "
                  f"{row['camera_rss_mb']:>6}  {row['curtain_rss_mb']:>10}  "
                  f"{row['camera_pids'] or '-':>7}  {row['curtain_pids'] or '-'}", flush=True)
            previous = current
    except KeyboardInterrupt:
        pass
    finally:
        if stream:
            stream.close()


if __name__ == '__main__':
    main()
