#!/usr/bin/env python3
"""Keep historical network wire captures immutable across protocol upgrades."""

from hashlib import sha256
from pathlib import Path
import sys


EXPECTED_SHA256 = {
    30: "3846367e22fce66b7d4ccb411b2ae21112e50180a78672841d9e8923fa01ccac",
    31: "d63eaa24cb5b3509fc4f02200cf64d0452b7e0af46df3f2f3d9f710ee9353649",
    32: "2bc7acc78a45edca5275823e6a4a5dd9ce10a95bbdec2a9a6481c4c55fd5f861",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check-network-protocol-golden.py PROTOCOL PATH", file=sys.stderr)
        return 2

    try:
        protocol = int(sys.argv[1])
    except ValueError:
        print(f"invalid protocol version: {sys.argv[1]}", file=sys.stderr)
        return 2
    expected = EXPECTED_SHA256.get(protocol)
    if expected is None:
        print(f"unsupported historical protocol version: {protocol}", file=sys.stderr)
        return 2

    path = Path(sys.argv[2])
    try:
        actual = sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        print(f"could not read Protocol {protocol} golden {path}: {error}", file=sys.stderr)
        return 2

    if actual != expected:
        print(
            f"Protocol {protocol} raw-wire golden changed: "
            f"expected sha256={expected}, actual sha256={actual}",
            file=sys.stderr,
        )
        return 1

    print(f"Protocol {protocol} raw-wire golden preserved: sha256={actual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
