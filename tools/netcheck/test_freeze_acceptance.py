import tempfile
import unittest
from pathlib import Path

import freeze_acceptance


def observation_line(**overrides: int) -> str:
    values = {
        "slot": 1,
        "applied-frames": 150,
        "applied-episodes": 1,
        "applied-complete": 1,
        "applied-min": 150,
        "applied-max": 150,
        "applied-local-complete": 1,
        "applied-local-min": 150,
        "applied-local-max": 150,
        "applied-remaining": 0,
        "applied-start-frame": 20,
        "applied-thaw-frame": 170,
        "applied-open": 0,
        "applied-interrupted": 0,
        "authority-snapshots": 151,
        "authority-episodes": 1,
        "authority-thaws": 1,
        "authority-min": 150,
        "authority-max": 150,
        "authority-frozen-samples": 150,
        "authority-remaining-delta-max": 0,
        "authority-frozen-excluded": 2,
        "authority-remaining-delta-max-excluded": 60,
        "authority-start-frame": 100,
        "authority-thaw-frame": 250,
        "authority-interrupted": 0,
        "authority-open": 0,
        "generation-max": 2,
        "occupant-changes": 0,
        "authority-frame": 250,
        "authority-remaining": 0,
    }
    values.update(overrides)
    fields = " ".join(f"{key}={value}" for key, value in values.items())
    return f"  netcheck-freeze Rejoined mine {fields}\n"


class FreezeAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.requirement = freeze_acceptance.Requirement("Rejoined", 1, 2, 0)
        self.observation = freeze_acceptance.Observation(
            "Rejoined",
            "mine",
            {
                "slot": 1,
                "applied-episodes": 1,
                "applied-complete": 1,
                "applied-min": 150,
                "applied-max": 150,
                "applied-local-complete": 1,
                "applied-local-min": 150,
                "applied-local-max": 150,
                "applied-remaining": 0,
                "applied-open": 0,
                "applied-interrupted": 0,
                "authority-episodes": 1,
                "authority-thaws": 1,
                "authority-min": 150,
                "authority-max": 150,
                "authority-frozen-samples": 150,
                "authority-remaining-delta-max": 0,
                "authority-frozen-excluded": 2,
                "authority-remaining-delta-max-excluded": 60,
                "authority-remaining": 0,
                "authority-open": 0,
                "authority-interrupted": 0,
                "generation-max": 2,
                "occupant-changes": 0,
            },
        )

    def test_rejoined_generation_thaws_with_matching_snapshot_timer(self):
        self.assertEqual(
            freeze_acceptance.check_observation(self.observation, self.requirement, 4),
            freeze_acceptance.CheckResult(),
        )

    def test_still_frozen_at_report_is_rejected(self):
        values = dict(
            self.observation.values,
            **{
                "applied-complete": 0,
                "applied-min": 0,
                "applied-max": 0,
                "applied-open": 12,
                "applied-remaining": 5,
            },
        )
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        problems = freeze_acceptance.check_observation(observation, self.requirement, 4)
        self.assertTrue(any("still open" in problem for problem in problems.incomplete))

    def test_episode_duration_ranges_are_diagnostic(self):
        values = dict(self.observation.values, **{"authority-min": 160, "authority-max": 160})
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        self.assertEqual(
            freeze_acceptance.check_observation(observation, self.requirement, 4),
            freeze_acceptance.CheckResult(),
        )

    def test_applied_timer_snapshot_mismatch_is_rejected(self):
        values = dict(self.observation.values, **{"authority-remaining-delta-max": 5})
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        problems = freeze_acceptance.check_observation(observation, self.requirement, 4)
        self.assertTrue(any("snapshot remaining" in problem for problem in problems.failures))

    def test_lifecycle_excluded_samples_do_not_inflate_the_eligible_timer_delta(self):
        values = dict(
            self.observation.values,
            **{
                "authority-frozen-excluded": 12,
                "authority-remaining-delta-max-excluded": 89,
                "authority-remaining-delta-max": 1,
            },
        )
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        self.assertEqual(
            freeze_acceptance.check_observation(observation, self.requirement, 4),
            freeze_acceptance.CheckResult(),
        )

    def test_missing_lifecycle_sample_diagnostics_is_rejected(self):
        values = dict(self.observation.values)
        del values["authority-frozen-excluded"]
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        problems = freeze_acceptance.check_observation(observation, self.requirement, 4)
        self.assertTrue(any("authority-frozen-excluded" in problem for problem in problems.failures))

    def test_raw_local_duration_is_reported_but_not_compared_across_clock_domains(self):
        values = dict(
            self.observation.values,
            **{
                "applied-local-min": 143,
                "applied-local-max": 144,
                "authority-remaining-delta-max": 0,
            },
        )
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        self.assertEqual(
            freeze_acceptance.check_observation(observation, self.requirement, 4),
            freeze_acceptance.CheckResult(),
        )

    def test_projected_and_raw_local_episode_counts_must_match(self):
        values = dict(self.observation.values, **{"applied-local-complete": 0})
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        problems = freeze_acceptance.check_observation(observation, self.requirement, 4)
        self.assertTrue(any("episode counts differ" in problem for problem in problems.failures))

    def test_slot_replacement_count_is_enforced(self):
        requirement = freeze_acceptance.Requirement("Rejoined", 1, 2, 1)
        problems = freeze_acceptance.check_observation(self.observation, requirement, 4)
        self.assertTrue(any("occupant changes" in problem for problem in problems.failures))

    def test_no_freeze_observed_is_incomplete_not_a_pass(self):
        values = dict(
            self.observation.values,
            **{
                "applied-episodes": 0,
                "applied-complete": 0,
                "authority-episodes": 0,
                "authority-thaws": 0,
                "authority-frozen-samples": 0,
            },
        )
        observation = freeze_acceptance.Observation("Rejoined", "mine", values)
        result = freeze_acceptance.check_observation(observation, self.requirement, 4)
        self.assertTrue(result.incomplete)

    def test_report_parser_and_command_line_requirement(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "rejoined.txt"
            report.write_text(observation_line(), encoding="utf-8")
            observations, errors = freeze_acceptance.read_observations([report])
            self.assertEqual(errors, [])
            self.assertEqual(observations[("Rejoined", 1)].values["generation-max"], 2)
            self.assertEqual(
                freeze_acceptance.main(
                    [
                        str(report),
                        "--require",
                        "Rejoined,1,2,0",
                    ]
                ),
                0,
            )


if __name__ == "__main__":
    unittest.main()
