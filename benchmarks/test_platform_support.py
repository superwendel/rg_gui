"""Protect affinity selection and equivalent-build checks across platforms."""

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import platform_support
import run as cpu
import run_geometry


class PlatformSupportTests(unittest.TestCase):
    @unittest.skipUnless(hasattr(os, "sched_getaffinity"), "Linux affinity API")
    def test_pin_selects_first_available_cpu_in_restricted_cpuset(self):
        with mock.patch.object(os, "sched_getaffinity", return_value={6, 3}), \
             mock.patch.object(os, "sched_setaffinity") as set_affinity:
            result = platform_support.cpu_metadata(pin=True)
        set_affinity.assert_called_once_with(0, {3})
        self.assertEqual(result["selected_cpu"], 3)
        self.assertEqual(result["process_affinity_mask"], 8)
        self.assertEqual(result["allowed_cpus_before"], [3, 6])

    @unittest.skipUnless(hasattr(os, "sched_getaffinity"), "Linux affinity API")
    def test_frame_metadata_leaves_affinity_unchanged(self):
        with mock.patch.object(os, "sched_getaffinity", return_value={6, 3}), \
             mock.patch.object(os, "sched_setaffinity") as set_affinity:
            result = platform_support.cpu_metadata()
        set_affinity.assert_not_called()
        self.assertIsNone(result["selected_cpu"])
        self.assertEqual(result["process_affinity_mask"], 72)

    @unittest.skipIf(os.name == "nt", "Linux runtime library semantics")
    def test_runtime_preserves_case_sensitive_environment_and_library_search(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "lib"
            library.mkdir()
            runtime = library / "libSDL3.so.0"
            runtime.write_bytes(b"candidate")
            original = {"PATH": "/usr/bin", "LD_LIBRARY_PATH": "/existing", "mixedName": "value"}
            with mock.patch.dict(os.environ, original, clear=True), \
                 mock.patch.object(platform_support, "_command_output", return_value=None):
                env, found = platform_support.runtime_environment(root, root)
            self.assertEqual(found, runtime)
            self.assertEqual(env["PATH"], "/usr/bin")
            self.assertEqual(env["mixedName"], "value")
            self.assertEqual(env["LD_LIBRARY_PATH"].split(os.pathsep), [str(root), str(library), "/existing"])

    def test_equivalent_commands_keep_actual_flags_and_remove_output_paths(self):
        linux = ["gcc", "-O2", "-DRG_GUI_GPU_USE_SSE2=0", "bench.c", "-lSDL3", "-o", "/tmp/portable"]
        sse = ["gcc", "-O2", "-DRG_GUI_GPU_USE_SSE2=1", "bench.c", "-lSDL3", "-o", "/tmp/sse2"]
        normalize = lambda command: cpu.comparable_build_command(command, "RG_GUI_GPU_USE_SSE2")
        self.assertEqual(normalize(linux), normalize(sse))
        self.assertNotEqual(normalize(linux), normalize(["-O3" if value == "-O2" else value for value in sse]))
        windows = ["cl.exe", "/O2", "/DRG_GUI_GPU_USE_SSE2=0", "bench.c", "/Fo:a.obj", "/Fe:a.exe"]
        self.assertEqual(normalize(windows), ["cl.exe", "/O2", "bench.c"])
        self.assertTrue(cpu.has_define(linux, "RG_GUI_GPU_USE_SSE2=0"))
        self.assertTrue(cpu.has_define(windows, "RG_GUI_GPU_USE_SSE2=0"))
        self.assertFalse(cpu.has_define(linux, "RG_GUI_GPU_USE_SSE2=1"))

    def test_geometry_parser_rejects_changed_workload_and_duplicate_cases(self):
        rows = []
        for name in run_geometry.CASES:
            mode, count = name.split("_")
            rows.append(dict(case=name, min_ns=1, median_ns=2, max_ns=3, geometry="0" * 16,
                             vertices=int(count) * 6, items=int(count) if mode == "prepare" else 0))
        serialize = lambda values: "\n".join(json.dumps(row) for row in values)
        results, signature = run_geometry.checked_rows(serialize(rows))
        self.assertEqual(len(results), 6)
        self.assertEqual(signature["prepare_1024"]["vertices"], 6144)
        with self.assertRaises(RuntimeError):
            run_geometry.checked_rows(serialize(rows + rows[:1]))
        rows[0]["vertices"] = 5
        with self.assertRaises(RuntimeError):
            run_geometry.checked_rows(serialize(rows))


if __name__ == "__main__":
    unittest.main()
