"""Headless regression coverage for real-window capture validation."""

import csv
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import run_frames as frames
import run_native_lifecycle as lifecycle


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "capture.csv"
        self.args = SimpleNamespace(frames=2, warmup=0, vsync=True)
        self.stdout = ("Profile interactions: text_edits=0 scroll_changes=0\n"
                       "Profile configuration: backend=vulkan scenario=idle present=vsync\n")

    def capture(self, extended=True, update=None, missing=None):
        columns = sorted(frames.EXPECTED_COLUMNS)
        if extended:
            columns += list(frames.OPTIONAL_COUNTER_COLUMNS)
        if missing:
            columns.remove(missing)
        with self.path.open("w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(output, fieldnames=columns)
            writer.writeheader()
            for index in range(self.args.frames):
                row = dict.fromkeys(columns, 0)
                row.update(frame=index, scenario="idle", width=1280, height=800,
                           main_submitted=1, main_presented=1)
                if extended:
                    row.update(main_copy_calls=3, main_cache_upload_ranges=2)
                if update:
                    row.update(update)
                writer.writerow(row)

    def test_extended_counters_retain_measured_values(self):
        self.capture()
        rows, included, validation = frames.read_capture(self.path, "idle", self.args, self.stdout)
        summary = frames.summarize_frames(included)
        self.assertEqual(summary["counters"]["combined_copy_calls"]["total"], 6)
        self.assertEqual(summary["counters"]["main_cache_upload_ranges"]["p50"], 2)
        self.assertEqual(validation["unavailable_counters"], [])
        self.assertEqual(len(rows), 2)

    def test_legacy_capture_does_not_invent_missing_counters(self):
        self.capture(extended=False)
        _, included, validation = frames.read_capture(self.path, "idle", self.args, self.stdout)
        self.assertEqual(validation["unavailable_counters"], sorted(frames.OPTIONAL_COUNTER_COLUMNS))
        self.assertNotIn("combined_copy_calls", frames.summarize_frames(included)["counters"])

    def test_partial_extended_schema_fails(self):
        self.capture(missing="secondary_cache_upload_ranges")
        with self.assertRaisesRegex(RuntimeError, "CSV columns"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)

    def test_negative_upload_counter_fails(self):
        self.capture(update={"main_copy_calls": -1})
        with self.assertRaisesRegex(RuntimeError, "Negative main_copy_calls"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)

    def test_runtime_records_actual_sdl_display_and_buffers(self):
        self.capture()
        self.args.frame_buffers = 3
        stdout = self.stdout + ("Profile runtime: sdl=3.4.10 video=x11 frame_buffers=3\n"
                                "Profile display: id=1 size=1920x1080 refresh_hz=60.000000 scale=1.000000\n")
        _, _, validation = frames.read_capture(self.path, "idle", self.args, stdout)
        self.assertEqual(validation["video_driver"], "x11")
        self.assertEqual(validation["sdl_version"], "3.4.10")
        self.assertEqual(validation["frame_buffers"], 3)
        self.assertEqual(validation["display"]["refresh_hz"], 60.0)
        self.args.frame_buffers = 1
        with self.assertRaisesRegex(RuntimeError, "frame-buffer count"):
            frames.read_capture(self.path, "idle", self.args, stdout)

    def test_physical_gpu_comes_from_process_backend_log(self):
        self.capture()
        _, _, validation = frames.read_capture(self.path, "idle", self.args, self.stdout,
            "INFO: Vulkan Device: Intel Test GPU\nINFO: Vulkan Driver: Mesa 26.0\n")
        self.assertEqual(validation["gpu_device"], "Intel Test GPU")
        self.assertEqual(validation["gpu_driver"], "Mesa 26.0")

    def test_requested_buffers_require_actual_configuration(self):
        self.capture()
        self.args.frame_buffers = 2
        with self.assertRaisesRegex(RuntimeError, "frame-buffer count"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)

    def test_duplicate_columns_fail(self):
        self.capture()
        lines = self.path.read_text().splitlines()
        lines[0] += ",frame"
        self.path.write_text("\n".join(lines) + "\n")
        with self.assertRaisesRegex(RuntimeError, "CSV columns"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)

    def test_presentation_coverage_gate_remains_enforced(self):
        self.capture(update={"main_presented": 0})
        with self.assertRaisesRegex(RuntimeError, "measurement frames presented"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)

    def test_external_maximize_cannot_change_the_measured_workload(self):
        self.capture(update={"width": 1920, "height": 1011})
        with self.assertRaisesRegex(RuntimeError, "Unexpected measurement window size"):
            frames.read_capture(self.path, "idle", self.args, self.stdout)


class ProvenanceTests(unittest.TestCase):
    def test_hash_bound_sidecar_and_platform_executable_name(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / frames.host.executable_name("rg_gui_demo_tearout")
            binary.write_bytes(b"example executable")
            self.assertEqual(lifecycle.executable_path(Path(folder)), binary)
            description = frames.describe_file(binary)
            self.assertIsNone(frames.build_provenance(binary, description))
            sidecar = binary.with_name(binary.name + ".build.json")
            contents = {"executable_sha256": description["sha256"], "compile_command": ["cc", "-O2"]}
            sidecar.write_text(json.dumps(contents))
            self.assertEqual(frames.build_provenance(binary, description)["contents"], contents)
            sidecar.write_text(json.dumps({"executable_sha256": "wrong"}))
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                frames.build_provenance(binary, description)


class LifecycleTests(unittest.TestCase):
    def test_close_summary_includes_five_following_frames(self):
        rows = []
        for index in range(120, 486):
            row = dict.fromkeys(lifecycle.ACTION_TIMINGS, 0)
            row.update(frame=index, frame_work_ms=float(index), main_presented=1,
                       secondary_submitted=0, secondary_presented=0)
            rows.append(row)
        summary = lifecycle.action_summary(rows)
        self.assertEqual(summary["activate"]["count"], 2)
        self.assertEqual(summary["redock_close"]["count"], 2)
        event = summary["redock_close"]["events"][0]
        self.assertEqual(event["close_plus_five_frame_work_ms"], sum(range(180, 186)))
        self.assertEqual([row["offset"] for row in event["following_frames"]], list(range(1, 6)))

    def test_close_without_following_frames_fails(self):
        rows = [{**dict.fromkeys(lifecycle.ACTION_TIMINGS, 0), "frame": index,
                 "main_presented": 1, "secondary_submitted": 0, "secondary_presented": 0}
                for index in range(240, 423)]
        with self.assertRaisesRegex(RuntimeError, "five subsequent frames"):
            lifecycle.action_summary(rows)


if __name__ == "__main__":
    unittest.main()
