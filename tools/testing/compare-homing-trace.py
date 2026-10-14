#!/usr/bin/env python3
"""Cross-check -homingtrace logs from one networked match.

Every machine writes a `[homing] spawn ...` line for each projectile it
spawns, keyed the same way everywhere: the shooter's slot, generation, life
and shot sequence (a continuous weapon's ticks by the shooter's intent
frame). The authority also writes the claims it judged and the verdicts it
gave, and both ends write the Shock Coil drain they credited.

    compare-homing-trace.py LOG [LOG ...] [--json OUT]

Exit 0 when every copy of every shot agrees on its weapon row, charge, Prime
Hunter role, steering strength, speed and target, and no homing shot's claim
was refused for being off its straight ray.
"""

import argparse
import collections
import json
import re
import sys

LINE = re.compile(r"\[homing\] (spawn|claim|verdict|drain) (.*)$")
FIELDS = ("row", "charge", "charged", "prime", "homing", "speed", "steer", "target")
ACCEPTED = {"applied", "duplicate"}


def parse(path):
    records = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            match = LINE.search(raw.rstrip("\r\n"))
            if not match:
                continue
            kind, rest = match.groups()
            values = dict(token.split("=", 1) for token in rest.split() if "=" in token)
            values["kind"] = kind
            values["log"] = path
            records.append(values)
    return records


def shot_key(record):
    if record.get("continuous") == "1":
        return ("continuous", record["slot"], record["gen"], record["life"], record["intent"])
    return ("shot", record["slot"], record["gen"], record["life"], record["seq"], record["proj"])


def describe_key(key):
    return " ".join(str(part) for part in key)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--json")
    parser.add_argument("--require-prime", action="store_true")
    args = parser.parse_args()

    spawns = collections.defaultdict(list)
    claims = {}
    verdicts = {}
    drains = collections.defaultdict(lambda: collections.Counter())
    for path in args.logs:
        for record in parse(path):
            kind = record["kind"]
            if kind == "spawn":
                if record.get("owner") != "Player" or record.get("role") == "offline":
                    continue
                if record.get("continuous") != "1" and record.get("seq") == "0":
                    continue
                spawns[shot_key(record)].append(record)
            elif kind == "claim":
                claims[(record["shooter"], record["claim"])] = record
            elif kind == "verdict":
                verdicts[(record["shooter"], record["claim"])] = record["result"].lower()
            elif kind == "drain":
                drains[(record["role"], record["shooter"], record["row"])]["amount"] += int(record["amount"])
                drains[(record["role"], record["shooter"], record["row"])]["events"] += 1

    mismatches = collections.Counter()
    examples = collections.defaultdict(list)
    coverage = collections.Counter()
    by_row = collections.defaultdict(collections.Counter)
    target_refusals = collections.Counter()
    for key, copies in spawns.items():
        roles = collections.Counter(copy["role"] for copy in copies)
        owner = next((copy for copy in copies if copy["role"] == "owner"), None)
        authority = next((copy for copy in copies if copy["role"] == "authority"), None)
        kind = key[0]
        coverage[(kind, "copies")] += 1
        if owner and authority:
            coverage[(kind, "owner+authority")] += 1
        if roles.get("observer"):
            coverage[(kind, "observed")] += 1
        if owner:
            row = owner["row"] + (" charged" if owner["charged"] == "1" else "") + (" prime" if owner["prime"] == "1" else "")
            by_row[row]["shots"] += 1
            by_row[row]["steer"] += owner["steer"] == "1"
            by_row[row]["target"] += owner["target"] != "-"
            if owner["prime"] == "1" and int(owner["row"].split(":")[1]) < 9:
                mismatches["prime shot on a base row"] += 1
                examples["prime shot on a base row"].append(describe_key(key))
            if (owner["steer"] == "1") != (float(owner["homing"]) > 0):
                mismatches["steer flag disagrees with homing strength"] += 1
        reference = owner or authority or copies[0]
        for copy in copies:
            if copy is reference:
                continue
            for field in FIELDS:
                if copy.get(field) == reference.get(field):
                    continue
                if (field == "target" and copy["target"] == "-"
                        and copy.get("muzzleblocked") == "1" and copy.get("acquired") == "-"):
                    # This copy hit a local obstacle before target acquisition.
                    # It did not choose a different lock. Keep this distinct
                    # from an unexplained missing or changed target.
                    target_refusals[(copy["role"], "muzzle-contact")] += 1
                    continue
                if field == "target" and copy["target"] == "-" and reference["target"] != "-"                         and copy.get("acquired") in ("refused", "received-ineligible"):
                    # The shooter's choice reached this copy and was turned
                    # down here: by the authority's history of the fire frame,
                    # or because the entity it names cannot be locked on this
                    # machine now (already down). Counted on its own.
                    target_refusals[(copy["role"], copy["acquired"])] += 1
                    continue
                label = "%s %s/%s %s" % (kind, reference["role"], copy["role"], field)
                mismatches[label] += 1
                if len(examples[label]) < 5:
                    examples[label].append("%s: %s=%s vs %s" % (describe_key(key), field, reference.get(field), copy.get(field)))

    claim_outcomes = collections.defaultdict(collections.Counter)
    false_refusals = []
    owner_shots = {}
    for key, copies in spawns.items():
        if key[0] != "shot":
            continue
        owner = next((copy for copy in copies if copy["role"] == "owner"), None)
        if owner:
            owner_shots[(owner["slot"], owner["seq"])] = owner
    for claim_key, claim in claims.items():
        result = verdicts.get(claim_key, "pending")
        shot = owner_shots.get((claim["shooter"], claim["seq"]))
        homing = shot is not None and shot.get("steer") == "1"
        row = claim["row"] + (" homing" if homing else "")
        claim_outcomes[row][result] += 1
        if homing and result == "geometry":
            false_refusals.append(claim)

    drain_problems = []
    for (role, shooter, row), counter in drains.items():
        if row == "MP:7" and counter["amount"] > 0:
            drain_problems.append("%s credited %d drain to slot %s with the plain Shock Coil row" % (role, counter["amount"], shooter))
    owners = {(shooter, row): c["amount"] for (role, shooter, row), c in drains.items() if role == "owner"}
    authorities = {(shooter, row): c["amount"] for (role, shooter, row), c in drains.items() if role == "authority"}

    print("== copies")
    for (kind, what), count in sorted(coverage.items()):
        print("  %-10s %-16s %6d" % (kind, what, count))
    print("== shots by weapon row (owner's copy)")
    for row, counter in sorted(by_row.items()):
        print("  %-24s shots %5d  homing %5d  with target %5d" % (row, counter["shots"], counter["steer"], counter["target"]))
    print("== disagreements between copies")
    if not mismatches:
        print("  none")
    for label, count in sorted(mismatches.items()):
        print("  %-48s %6d   e.g. %s" % (label, count, "; ".join(examples[label][:3])))
    for (role, outcome), count in sorted(target_refusals.items()):
        print("  shooter's target turned down by %s (%s): %d" % (role, outcome, count))
    print("== claims judged by the authority")
    for row, counter in sorted(claim_outcomes.items()):
        print("  %-18s %s" % (row, ", ".join("%s %d" % item for item in sorted(counter.items()))))
    print("  homing shots refused for leaving their straight ray: %d" % len(false_refusals))
    print("== Shock Coil drain credited")
    for key in sorted(set(owners) | set(authorities)):
        print("  slot %s %-6s owner %5d  authority %5d" % (key[0], key[1], owners.get(key, 0), authorities.get(key, 0)))
    for problem in drain_problems:
        print("  PROBLEM " + problem)

    # A cell that compared nothing proves nothing: some shot must have been
    # seen by its owner and by the authority.
    empty = coverage[("shot", "owner+authority")] + coverage[("continuous", "owner+authority")] == 0
    if empty:
        print("  PROBLEM no shot was seen by both its owner and the authority")
    missing_prime = args.require_prime and not any(" prime" in row for row in by_row)
    if missing_prime:
        print("  PROBLEM no Prime Hunter shot was observed")
    failed = bool(mismatches) or bool(false_refusals) or bool(drain_problems) or empty or missing_prime
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({
                "coverage": {"%s %s" % key: value for key, value in coverage.items()},
                "mismatches": dict(mismatches),
                "targetRefusals": {"%s %s" % key: value for key, value in target_refusals.items()},
                "claims": {row: dict(counter) for row, counter in claim_outcomes.items()},
                "falseRefusals": len(false_refusals),
                "drain": {"owner": {"%s %s" % k: v for k, v in owners.items()},
                          "authority": {"%s %s" % k: v for k, v in authorities.items()}},
                "failed": failed,
            }, handle, indent=2)
    print("RESULT " + ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
