#!/usr/bin/env python3
"""Self-test for compare-homing-trace.py: synthetic logs from three machines
that agree, and the ways they can disagree, each with the verdict it must get.
Needs nothing but Python; CTest runs it as FruityPrime.HomingTraceCompare."""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, "compare-homing-trace.py")


def spawn(role, seq=7, row="MP:11", charge=90, charged=1, prime=0, homing="81.000", target="P1:1:2",
          acquired="chosen", continuous=0, intent=100, slot=0, muzzleblocked=0):
    return ("[12:00:00.000] [homing] spawn role=%s owner=Player slot=%d gen=1 life=1 seq=%d frame=100 intent=%d "
            "proj=0 beam=Missile row=%s charge=%d charged=%d prime=%d homing=%s speed=6144.000 steer=%d "
            "continuous=%d target=%s acquired=%s presentation=0 retarget=0.000 muzzleblocked=%d"
            % (role, slot, seq if not continuous else 0, intent, row, charge, charged, prime, homing,
               1 if float(homing) > 0 else 0, continuous, target, acquired, muzzleblocked))


def run(case, owner, authority, observer, options=()):
    with tempfile.TemporaryDirectory() as folder:
        paths = []
        for name, lines in (("client0.log", owner), ("server.log", authority), ("client1.log", observer)):
            path = os.path.join(folder, name)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("\n".join(lines) + "\n")
            paths.append(path)
        result = subprocess.run([sys.executable, "-I", COMPARE] + paths + list(options), capture_output=True, text=True)
        return result.returncode


def main():
    failures = 0

    def expect(name, code, wanted):
        nonlocal failures
        ok = code == wanted
        failures += 0 if ok else 1
        print(("PASS " if ok else "FAIL ") + name + " (exit %d, wanted %d)" % (code, wanted))

    agree = [spawn("owner")], [spawn("authority", acquired="received")], [spawn("observer", acquired="received")]
    expect("three copies that agree pass", run("agree", *agree), 0)
    expect("a Prime cell without a Prime shot fails",
           run("missing-prime", *agree, options=("--require-prime",)), 1)
    prime = [spawn("owner", prime=1)], [spawn("authority", prime=1)], [spawn("observer", prime=1)]
    expect("a Prime cell with agreeing Prime shots passes",
           run("prime", *prime, options=("--require-prime",)), 0)

    expect("a different weapon row on the authority fails",
           run("row", [spawn("owner")], [spawn("authority", row="MP:2", acquired="received")],
               [spawn("observer", acquired="received")]), 1)

    expect("a different target on an observer fails",
           run("target", [spawn("owner")], [spawn("authority", acquired="received")],
               [spawn("observer", target="P3:1:1", acquired="received")]), 1)

    expect("a target the authority's history refused is counted, not failed",
           run("refused", [spawn("owner")], [spawn("authority", target="-", acquired="refused")],
               [spawn("observer", acquired="received")]), 0)

    expect("a lock the observer took that the owner did not fails",
           run("continuous", [spawn("owner", continuous=1, target="-", acquired="none")],
               [spawn("authority", continuous=1, target="-", acquired="received-none")],
               [spawn("observer", continuous=1, target="P1:1:2", acquired="received")]), 1)

    expect("a recorded muzzle contact before acquisition is counted separately",
           run("muzzle", *agree[:2],
               [spawn("observer", target="-", acquired="-", muzzleblocked=1)]), 0)
    expect("an unexplained missing lock still fails",
           run("missing-lock", *agree[:2],
               [spawn("observer", target="-", acquired="-")]), 1)
    expect("a muzzle flag cannot hide selection of a different target",
           run("wrong-lock", *agree[:2],
               [spawn("observer", target="P3:1:1", acquired="received", muzzleblocked=1)]), 1)

    expect("logs with no shot seen by owner and authority fail", run("empty", [], [], []), 1)

    claim = "[12:00:00.000] [homing] claim shooter=0 claim=5 seq=7 beam=2 victim=1 row=MP:11 straight=0"
    expect("a homing shot refused for leaving its straight ray fails",
           run("geometry", [spawn("owner")],
               [spawn("authority", acquired="received"), claim,
                "[12:00:00.000] [homing] verdict shooter=0 claim=5 result=Geometry"],
               [spawn("observer", acquired="received")]), 1)
    expect("a homing shot's applied claim passes",
           run("applied", [spawn("owner")],
               [spawn("authority", acquired="received"), claim,
                "[12:00:00.000] [homing] verdict shooter=0 claim=5 result=Applied"],
               [spawn("observer", acquired="received")]), 0)

    expect("drain credited for the plain Shock Coil row fails",
           run("drain", [spawn("owner")],
               [spawn("authority", acquired="received"),
                "[12:00:00.000] [homing] drain role=authority shooter=0 victim=1 claim=3 amount=4 row=MP:7"],
               [spawn("observer", acquired="received")]), 1)

    print("compare-homing-trace self-test: " + ("PASS" if failures == 0 else "FAIL %d" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
