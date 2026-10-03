"""Compare opt-in production scene descriptor counters from identical harnesses.

Shell loops draw a timing-dependent number of frames. Normalize by scene
draws instead of claiming an FPS improvement from these command counters.
The stream with most draws is the match; other streams include window/preview.
"""
import argparse
import json
import re
from pathlib import Path


def read(path):
    streams = [dict((name, int(value)) for name, value in re.findall(r'(\w+)=(\d+)', line))
               for line in path.read_text(encoding='utf-8').splitlines()
               if line.startswith('[vulkan-metrics]')]
    if not streams:
        raise ValueError(f'No production counters: {path}')
    result = max(streams, key=lambda item: item['draws'])
    if not result['draws']:
        raise ValueError(f'No scene draws: {path}')
    return dict(path=str(path.resolve()), counters=result,
                per_draw={name: value / result['draws'] for name, value in result.items() if name != 'draws'})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = json.dumps([read(path) for path in args.logs], indent=2) + '\n'
    if args.output:
        args.output.write_text(report, encoding='utf-8')
    print(report, end='')
