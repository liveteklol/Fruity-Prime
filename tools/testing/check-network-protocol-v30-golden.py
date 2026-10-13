#!/usr/bin/env python3
"""Keep the Protocol 30 wire capture immutable after the Protocol 31 upgrade."""

from hashlib import sha256
from pathlib import Path
import sys


EXPECTED_SHA256 = "3846367e22fce66b7d4ccb411b2ae21112e50180a78672841d9e8923fa01ccac"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check-network-protocol-v30-golden.py PATH", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    try:
        actual = sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        print(f"could not read Protocol 30 golden {path}: {error}", file=sys.stderr)
        return 2

    if actual != EXPECTED_SHA256:
        print(
            "Protocol 30 raw-wire golden changed: "
            f"expected sha256={EXPECTED_SHA256}, actual sha256={actual}",
            file=sys.stderr,
        )
        return 1

    print(f"Protocol 30 raw-wire golden preserved: sha256={actual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
