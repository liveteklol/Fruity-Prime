#!/usr/bin/env python3
"""Check completed freeze observations in one or more -netcheck reports.

Each --require is CLIENT,SLOT,MIN_GENERATION,MIN_OCCUPANT_CHANGES. For example,
require a rejoined client and a monitor that observed its slot replacement:

  python tools/netcheck/freeze_acceptance.py monitor.txt rejoined.txt \
      --require monitor,1,2,1 --require rejoined,1,2,0

The default remaining-frame tolerance is four 60 Hz frames. The checker
compares each applied local timer with the authority's `FrozenFrames` value
from that received snapshot, counting only snapshots where both states are in
play and the slot occupant is unchanged. Excluded lifecycle samples and their
largest timer difference are retained separately in the report. Episode
duration ranges remain diagnostic because the remote freeze start can arrive
after it began on the authority. The checker only judges the requested
observer/slot pairs, leaving ordinary -netcheck exit behavior and unrelated
freeze observations unchanged.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


FREEZE_LINE = re.compile(
    r"^\s*netcheck-freeze\s+(?P<client>\S+)\s+(?P<subject>\S+)\s+(?P<fields>.*)$"
)


@dataclass(frozen=True)
class Observation:
    client: str
    subject: str
    values: dict[str, int]


@dataclass(frozen=True)
class Requirement:
    client: str
    slot: int
    minimum_generation: int
    minimum_occupant_changes: int


@dataclass(frozen=True)
class CheckResult:
    failures: tuple[str, ...] = ()
    incomplete: tuple[str, ...] = ()


def parse_requirement(value: str) -> Requirement:
    parts = value.split(",")
    if len(parts) != 4 or not parts[0]:
        raise argparse.ArgumentTypeError(
            "expected CLIENT,SLOT,MIN_GENERATION,MIN_OCCUPANT_CHANGES"
        )
    try:
        slot, generation, occupant_changes = (int(part) for part in parts[1:])
    except ValueError as error:
        raise argparse.ArgumentTypeError("slot and minimums must be integers") from error
    if slot < 0 or generation < 0 or occupant_changes < 0:
        raise argparse.ArgumentTypeError("slot and minimums must be non-negative")
    return Requirement(parts[0], slot, generation, occupant_changes)


def read_observations(paths: list[Path]) -> tuple[dict[tuple[str, int], Observation], list[str]]:
    observations: dict[tuple[str, int], Observation] = {}
    errors: list[str] = []
    for path in paths:
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as error:
            errors.append(f"{path}: {error}")
            continue
        for line_number, line in enumerate(lines, start=1):
            match = FREEZE_LINE.match(line)
            if match is None:
                continue
            client = match.group("client")
            subject = match.group("subject")
            values: dict[str, int] = {}
            malformed = False
            for item in match.group("fields").split():
                key, separator, raw_value = item.partition("=")
                if not separator:
                    continue
                try:
                    values[key] = int(raw_value)
                except ValueError:
                    errors.append(
                        f"{path}:{line_number}: invalid integer for {key}: {raw_value}"
                    )
                    malformed = True
            if malformed or "slot" not in values:
                if not malformed:
                    errors.append(f"{path}:{line_number}: freeze line has no slot")
                continue
            observations[(client, values["slot"])] = Observation(client, subject, values)
    return observations, errors


def check_observation(
    observation: Observation,
    requirement: Requirement,
    remaining_tolerance: int,
) -> CheckResult:
    values = observation.values
    label = f"{observation.client} slot {requirement.slot}"
    problems: list[str] = []

    expected_values = (
        "applied-episodes",
        "applied-complete",
        "applied-min",
        "applied-max",
        "applied-local-complete",
        "applied-local-min",
        "applied-local-max",
        "applied-remaining",
        "applied-open",
        "applied-interrupted",
        "authority-episodes",
        "authority-thaws",
        "authority-min",
        "authority-max",
        "authority-frozen-samples",
        "authority-remaining-delta-max",
        "authority-frozen-excluded",
        "authority-remaining-delta-max-excluded",
        "authority-remaining",
        "authority-open",
        "authority-interrupted",
        "generation-max",
        "occupant-changes",
    )
    missing = [key for key in expected_values if key not in values]
    if missing:
        return CheckResult(failures=(f"{label}: missing fields {', '.join(missing)}",))

    if values["generation-max"] < requirement.minimum_generation:
        problems.append(
            f"highest generation {values['generation-max']} < required {requirement.minimum_generation}"
        )
    if values["occupant-changes"] < requirement.minimum_occupant_changes:
        problems.append(
            "occupant changes "
            f"{values['occupant-changes']} < required {requirement.minimum_occupant_changes}"
        )

    if values["applied-episodes"] == 0 and values["authority-episodes"] == 0:
        return CheckResult(incomplete=(f"{label}: no freeze episode was observed",))

    incomplete: list[str] = []

    if values["applied-open"] != 0 or values["applied-remaining"] != 0:
        incomplete.append(
            "applied freeze is still open "
            f"(open={values['applied-open']}, remaining={values['applied-remaining']})"
        )
    if values["authority-open"] != 0 or values["authority-remaining"] != 0:
        incomplete.append(
            "authority freeze is still open "
            f"(open={values['authority-open']}, remaining={values['authority-remaining']})"
        )

    applied_accounted = (
        values["applied-complete"]
        + values["applied-interrupted"]
        + int(values["applied-open"] != 0)
    )
    authority_accounted = (
        values["authority-thaws"]
        + values["authority-interrupted"]
        + int(values["authority-open"] != 0)
    )
    if values["applied-episodes"] != applied_accounted:
        problems.append(
            "applied episode accounting differs: "
            f"started={values['applied-episodes']} accounted={applied_accounted}"
        )
    if values["applied-complete"] != values["applied-local-complete"]:
        problems.append(
            "projected/local applied episode counts differ: "
            f"projected={values['applied-complete']} local={values['applied-local-complete']}"
        )
    if values["authority-episodes"] != authority_accounted:
        problems.append(
            "authority episode accounting differs: "
            f"started={values['authority-episodes']} accounted={authority_accounted}"
        )

    if values["authority-frozen-samples"] == 0:
        incomplete.append("no spawned authority-frozen snapshot was observed")

    if incomplete:
        return CheckResult(tuple(problems), tuple(f"{label}: {problem}" for problem in incomplete))

    if values["authority-thaws"] == 0:
        problems.append("no authority freeze thaw was observed")
    if values["applied-complete"] == 0:
        problems.append("no applied freeze thaw was observed")
    if values["authority-remaining-delta-max"] > remaining_tolerance:
        problems.append(
            "applied timer differs from authority snapshot remaining beyond tolerance: "
            f"max-delta={values['authority-remaining-delta-max']} frames "
            f"samples={values['authority-frozen-samples']} "
            f"tolerance={remaining_tolerance}"
        )

    return CheckResult(tuple(problems))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path, help="captured -netcheck output files")
    parser.add_argument(
        "--require",
        action="append",
        type=parse_requirement,
        required=True,
        metavar="CLIENT,SLOT,MIN_GENERATION,MIN_OCCUPANT_CHANGES",
        help="require one completed/thawed freeze observation for this observer and slot",
    )
    parser.add_argument(
        "--remaining-tolerance",
        dest="remaining_tolerance",
        type=int,
        default=4,
        metavar="FRAMES",
        help="maximum applied-vs-authority remaining-frame delta on a newly applied snapshot (default: 4)",
    )
    args = parser.parse_args(argv)
    if args.remaining_tolerance < 0:
        parser.error("--remaining-tolerance must be non-negative")

    observations, errors = read_observations(args.reports)
    failures = list(errors)
    incomplete: list[str] = []
    for requirement in args.require:
        observation = observations.get((requirement.client, requirement.slot))
        if observation is None:
            incomplete.append(
                f"missing netcheck-freeze report for {requirement.client} slot {requirement.slot}"
            )
            continue
        current = check_observation(observation, requirement, args.remaining_tolerance)
        if current.failures:
            failures.extend(f"FAIL: {requirement.client}: {problem}" for problem in current.failures)
        if current.incomplete:
            incomplete.extend(f"INCOMPLETE: {problem}" for problem in current.incomplete)
        if not current.failures and not current.incomplete:
            values = observation.values
            print(
                f"PASS: {requirement.client} slot {requirement.slot} "
                f"generation-max={values['generation-max']} occupant-changes={values['occupant-changes']} "
                f"applied={values['applied-complete']}/{values['applied-episodes']} "
                f"authority={values['authority-thaws']}/{values['authority-episodes']} "
                f"remaining-max-delta={values['authority-remaining-delta-max']} "
                f"over {values['authority-frozen-samples']} authority-frozen snapshots; "
                f"excluded-lifecycle-samples={values['authority-frozen-excluded']} "
                f"excluded-max-delta={values['authority-remaining-delta-max-excluded']}; "
                f"episode-duration diagnostic={values['applied-min']}..{values['applied-max']} / "
                f"{values['authority-min']}..{values['authority-max']} frames; "
                f"raw-local={values['applied-local-min']}..{values['applied-local-max']} "
                f"(remaining tolerance={args.remaining_tolerance})"
            )
    if failures:
        for problem in failures:
            print(problem, file=sys.stderr)
        for problem in incomplete:
            print(problem, file=sys.stderr)
        print(f"RESULT: FAIL -- {len(failures)} failure(s), {len(incomplete)} incomplete", file=sys.stderr)
        return 1
    if incomplete:
        for problem in incomplete:
            print(problem, file=sys.stderr)
        print(f"RESULT: INCOMPLETE -- {len(incomplete)} observation(s) not complete", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
