#!/usr/bin/env python3
"""Check a real two-client pickup race and loss/reorder rollback run."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


CLAIMS = re.compile(r"pickup claims: (\d+) declared, (\d+) unanswered")
PREDICTIONS = re.compile(
    r"power-up pickups: (\d+) predicted, (\d+) granted, (\d+) refused \((\d+) taken back\)"
)
AUTHORITY_REFUSAL = re.compile(
    r", \d+ (?:somebody else had it first|not the spawn the claim named|"
    r"the authority never had the player within reach)"
)


def read_report(path: Path) -> tuple[str, re.Match[str], re.Match[str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise ValueError(f"cannot read {path}: {error}") from error

    claims = CLAIMS.search(text)
    predictions = PREDICTIONS.search(text)
    if claims is None or predictions is None:
        raise ValueError(f"{path}: missing pickup claim or prediction diagnostics")
    return text, claims, predictions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path, help="unlagged client report")
    parser.add_argument("contested", type=Path, help="faulted client report")
    args = parser.parse_args()

    try:
        control = read_report(args.control)
        contested = read_report(args.contested)
    except ValueError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2

    checks: list[tuple[bool, str]] = []
    control_text, control_claims, control_predictions = control
    contested_text, contested_claims, contested_predictions = contested

    control_values = tuple(int(value) for value in control_predictions.groups())
    control_declared, control_unanswered = (int(value) for value in control_claims.groups())
    checks.append(
        (control_values[0] > 0 and control_values[1] > 0, "control predicted and received an authority grant")
    )
    checks.append(
        (control_unanswered == 0 and control_declared >= control_values[0], "control claims were answered")
    )

    contested_values = tuple(int(value) for value in contested_predictions.groups())
    contested_declared, contested_unanswered = (int(value) for value in contested_claims.groups())
    claim_line = next(
        (line for line in contested_text.splitlines() if line.strip().startswith("pickup claims:")),
        "",
    )
    checks.append(
        (contested_values[0] > 0 and contested_values[2] > 0 and contested_values[3] > 0,
            "faulted client predicted, was refused, and rolled back a pickup")
    )
    checks.append(
        (contested_unanswered == 0 and contested_declared >= contested_values[0],
            "faulted client's rollback came from answered claims, not timeout expiry")
    )
    checks.append(
        (AUTHORITY_REFUSAL.search(claim_line) is not None,
            "faulted client's claim diagnostics contain an authority refusal")
    )

    line = next((item for item in contested_text.splitlines() if "SIMULATED LINE:" in item), "")
    fault_tokens = (
        r"\+250 ms round trip",
        r"5(?:\.0)?% packet loss each way",
        r"reorder 10(?:\.0)?%",
        r"duplicate 5(?:\.0)?%",
    )
    checks.append(
        (all(re.search(pattern, line) is not None for pattern in fault_tokens),
            "faulted client report records the configured latency/loss/reorder/duplicate line")
    )

    for passed, description in checks:
        print(f"{'PASS' if passed else 'FAIL'}: {description}")
    print(f"control: predicted/granted/refused/rollback={control_values}; claims={control_declared}/{control_unanswered} unanswered")
    print(f"faulted: predicted/granted/refused/rollback={contested_values}; claims={contested_declared}/{contested_unanswered} unanswered")
    return 0 if all(passed for passed, _ in checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
