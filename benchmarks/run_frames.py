"""Record serial, real-window demo frame profiles on Windows (stdlib only).

Build the demos first. Example:
  python benchmarks/run_frames.py --output out/frame-profile --sdl-root C:/libs/SDL3-3.4.10
  python benchmarks/run_frames.py --output out/frame-edit --scenario edit --trials 3

No CPU affinity is changed unless --cpu-zero is supplied. Windows remain visible;
do not minimize or interact with them during scripted captures. CPU encoding times
are not GPU execution times. Initialization before the frame loop is not measured.
"""

import argparse
import csv
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parent.parent
SCENARIOS = ("idle", "text-idle", "edit", "scroll", "resize", "images", "images-grouped", "tearout")
FRAME_TIMINGS = ("frame_interval_ms", "event_ms", "ui_ms", "platform_ms", "lifecycle_ms", "frame_work_ms")
RENDER_TIMINGS = ("prepare_ms", "stage_upload_ms", "encode_ms", "swapchain_wait_ms",
                  "draw_encode_ms", "submit_ms", "render_total_ms")
RENDER_COUNTERS = ("submitted", "presented", "draw_commands", "draw_calls", "dispatches",
                   "geometry_vertices", "text_instances", "items", "run_upload_bytes",
                   "cache_upload_bytes", "geometry_upload_bytes", "full_cache_upload",
                   "cache_hits", "cache_misses", "cache_evictions", "cache_bypasses",
                   "dropped_runs", "dropped_glyphs", "renderer_diagnostics")
FRAME_COUNTERS = ("frame", "warmup", "width", "height", "diagnostic_flags", "actions")
TIMING_COLUMNS = FRAME_TIMINGS + tuple(f"{prefix}_{field}" for prefix in ("main", "secondary")
                                     for field in RENDER_TIMINGS)
COUNTER_COLUMNS = FRAME_COUNTERS + tuple(f"{prefix}_{field}" for prefix in ("main", "secondary")
                                       for field in RENDER_COUNTERS)
EXPECTED_COLUMNS = set(TIMING_COLUMNS + COUNTER_COLUMNS + ("scenario",))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def describe_file(path):
    path = path.resolve()
    return {"path": str(path), "sha256": digest(path), "size": path.stat().st_size,
            "modified_ns": path.stat().st_mtime_ns}


def nearest_rank(values, percentile):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("Cannot summarize an empty frame sample")
    return ordered[max(0, math.ceil(len(ordered) * percentile / 100) - 1)]


def distribution(values):
    return {"p50": nearest_rank(values, 50), "p95": nearest_rank(values, 95),
            "p99": nearest_rank(values, 99), "max": max(values), "min": min(values),
            "mean": statistics.fmean(values)}


def requested_present_mode(args):
    # Native lifecycle comparisons also call read_capture with a legacy
    # SimpleNamespace that only carries the vsync flag.
    return getattr(args, "present", None) or ("vsync" if args.vsync else "immediate")


def demo_present_arguments(args):
    if getattr(args, "present", None):
        return ["--present", args.present]
    return [] if args.vsync else ["--no-vsync"]


def cpu_metadata(pin):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
    kernel.SetProcessAffinityMask.restype = ctypes.c_int
    kernel.GetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                                             ctypes.POINTER(ctypes.c_size_t))
    kernel.GetProcessAffinityMask.restype = ctypes.c_int
    handle = kernel.GetCurrentProcess()
    if pin and not kernel.SetProcessAffinityMask(handle, 1):
        raise ctypes.WinError(ctypes.get_last_error())
    process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(handle, ctypes.byref(process_mask), ctypes.byref(system_mask)):
        raise ctypes.WinError(ctypes.get_last_error())
    model = platform.processor()
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            model = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    except OSError:
        pass
    return {"model": model, "logical_processors": os.cpu_count(),
            "affinity_policy": "CPU 0; inherited by children" if pin else "unchanged; OS scheduling",
            "process_affinity_mask": process_mask.value, "system_affinity_mask": system_mask.value}


def source_snapshot(args, scenarios):
    files = [Path(__file__), REPO / "src/rg_gui.h", REPO / "src/rg_gui_renderer.h",
             REPO / "src/rg_gui_gpu.h", REPO / "examples/rg_gui_demo_common.h",
             REPO / "examples/rg_gui_demo_profile.h", REPO / "examples/rg_gui_demo_stats.h",
             REPO / "examples/rg_gui_demo_present.h"]
    if any(case != "tearout" for case in scenarios):
        files.append(REPO / "examples/rg_gui_demo_full.c")
    if "tearout" in scenarios:
        files.append(REPO / "examples/rg_gui_demo_tearout.c")
    result = {path.relative_to(REPO).as_posix(): describe_file(path) for path in files}
    # These are the caller-declared build dependencies, not an inferred linker record.
    for name, root in (("rg_core", args.core_root), ("rg_text", args.text_root)):
        if root is None:
            continue
        root = root.resolve()
        folder = root / "src" if (root / "src").is_dir() else root
        if not folder.is_dir():
            raise RuntimeError(f"Declared {name} dependency directory does not exist: {root}")
        for path in sorted(folder.glob("*.h")):
            result[f"dependencies/{name}/{path.name}"] = describe_file(path)
    for path in sorted((REPO / "examples/assets").glob("*")):
        if path.is_file() and "inter_medium_16" in path.name:
            result[f"examples/assets/{path.name}"] = describe_file(path)
    for path in sorted((REPO / "shaders/Compiled").rglob("*")):
        if path.is_file():
            result[path.relative_to(REPO).as_posix()] = describe_file(path)
    return result


def child_environment(args):
    env = os.environ.copy()
    folders = [args.bin_dir.resolve()]
    if args.sdl_root:
        folders.extend(args.sdl_root.resolve() / suffix for suffix in ("lib/x64", "bin", "lib"))
    if env.get("SDL3_BIN_DIR"):
        folders.append(Path(env["SDL3_BIN_DIR"]).resolve())
    folders = [folder for folder in folders if folder.is_dir()]
    env["PATH"] = os.pathsep.join(map(str, folders)) + os.pathsep + env.get("PATH", "")
    runtime = next((folder / "SDL3.dll" for folder in folders if (folder / "SDL3.dll").is_file()), None)
    return env, describe_file(runtime) if runtime else None


def read_capture(path, scenario, args, stdout):
    with path.open(newline="", encoding="utf-8-sig") as capture:
        reader = csv.DictReader(capture)
        if reader.fieldnames is None or set(reader.fieldnames) != EXPECTED_COLUMNS or len(reader.fieldnames) != len(EXPECTED_COLUMNS):
            raise RuntimeError(f"Unexpected or duplicated CSV columns: {path}")
        rows = []
        for raw in reader:
            if None in raw or any(value is None for value in raw.values()):
                raise RuntimeError(f"Malformed CSV row {len(rows)} in {path}")
            row = {"scenario": raw["scenario"]}
            for column in COUNTER_COLUMNS:
                row[column] = int(raw[column])
                if row[column] < 0:
                    raise RuntimeError(f"Negative {column} at frame {len(rows)}")
            for column in TIMING_COLUMNS:
                row[column] = float(raw[column])
                if not math.isfinite(row[column]) or row[column] < 0:
                    raise RuntimeError(f"Invalid {column} at frame {len(rows)}")
            if row["frame"] != len(rows) or row["scenario"] != scenario or row["warmup"] != int(len(rows) < args.warmup):
                raise RuntimeError(f"Incorrect frame/scenario/warmup identity in {path}")
            # Text-area mouse release uses end_frame's documented active-ID
            # recovery. Accept only that setup event; capacity/stack errors fail.
            allowed_diagnostics = 128 if scenario in ("edit", "scroll") and row["frame"] == 3 else 0
            if row["diagnostic_flags"] & ~allowed_diagnostics:
                raise RuntimeError(f"GUI diagnostics at frame {row['frame']}: {row['diagnostic_flags']}")
            for prefix in ("main", "secondary"):
                for field in ("renderer_diagnostics", "dropped_runs", "dropped_glyphs"):
                    if row[f"{prefix}_{field}"]:
                        raise RuntimeError(f"{prefix} {field} at frame {row['frame']}")
                if row[f"{prefix}_submitted"] not in (0, 1) or row[f"{prefix}_presented"] not in (0, 1):
                    raise RuntimeError(f"Invalid {prefix} submission/presentation flags")
                if row[f"{prefix}_presented"] > row[f"{prefix}_submitted"]:
                    raise RuntimeError(f"{prefix} presented without submission")
                stage_sum = sum(row[f"{prefix}_{stage}"] for stage in RENDER_TIMINGS[:-1])
                if stage_sum > row[f"{prefix}_render_total_ms"] + 0.00002:
                    raise RuntimeError(f"Overlapping {prefix} render timings at frame {row['frame']}")
            if row["main_submitted"] != 1:
                raise RuntimeError(f"Main frame was not submitted: {row['frame']}")
            if row["main_presented"] and (not row["width"] or not row["height"]):
                raise RuntimeError(f"Presented main frame has zero dimensions: {row['frame']}")
            rows.append(row)
    if len(rows) != args.frames:
        raise RuntimeError(f"Expected {args.frames} frames; got {len(rows)} in {path}")
    measured = [row for row in rows if not row["warmup"]]
    main_presented = sum(row["main_presented"] for row in measured)
    if main_presented / len(measured) < 0.95:
        raise RuntimeError(f"Only {main_presented}/{len(measured)} main measurement frames presented")
    secondary_submitted = sum(row["secondary_submitted"] for row in measured)
    secondary_presented = sum(row["secondary_presented"] for row in measured)
    if secondary_submitted and secondary_presented / secondary_submitted < 0.95:
        raise RuntimeError(f"Only {secondary_presented}/{secondary_submitted} secondary measurement frames presented")
    included = [row for row in measured if row["main_presented"] and
                (not row["secondary_submitted"] or row["secondary_presented"])]
    if not included:
        raise RuntimeError("No fully presented measurement frames")
    checks = {"raw_frames": len(rows), "warmup_frames": args.warmup,
              "setup_active_id_recoveries": sum(row["diagnostic_flags"] == 128 for row in rows),
              "measurement_frames": len(measured), "included_frames": len(included),
              "suppressed_presentation_frames": len(measured) - len(included),
              "main_presented": main_presented, "secondary_submitted": secondary_submitted,
              "secondary_presented": secondary_presented,
              "measurement_actions": sum(row["actions"] for row in measured),
              "measurement_sizes": sorted({(row["width"], row["height"]) for row in included})}
    interactions = re.search(r"Profile interactions: text_edits=(\d+) scroll_changes=(\d+)", stdout)
    if scenario != "tearout":
        if not interactions:
            raise RuntimeError("Full demo did not report verified interaction counts")
        checks["observed_edits_all_frames"] = int(interactions[1])
        checks["observed_scrolls_all_frames"] = int(interactions[2])
        if scenario == "edit" and checks["observed_edits_all_frames"] <= 2:
            raise RuntimeError("Editing scenario did not change text more than twice")
        if scenario == "scroll" and checks["observed_scrolls_all_frames"] <= 2:
            raise RuntimeError("Scrolling scenario did not change scroll position more than twice")
        if any(row["secondary_submitted"] for row in rows):
            raise RuntimeError("Full demo unexpectedly submitted a secondary window")
    if scenario == "resize" and len(checks["measurement_sizes"]) < 2:
        raise RuntimeError("Resize scenario did not render at least two actual pixel sizes")
    if scenario in ("images", "images-grouped"):
        if min(row["main_geometry_vertices"] for row in included) < 512 * 6:
            raise RuntimeError("Image scenario did not prepare the 512 image quads")
        if min(row["main_draw_commands"] for row in included) < 512:
            raise RuntimeError("Image scenario did not emit the 512 image commands")
        calls = [row["main_draw_calls"] for row in included]
        if scenario == "images" and min(calls) < 512:
            raise RuntimeError("Alternating image scenario did not issue 512 image draws")
        if scenario == "images-grouped" and nearest_rank(calls, 50) >= 512:
            raise RuntimeError("Grouped image control did not batch the image draws")
    if scenario == "tearout":
        created = re.search(r"(\d+) native windows? created", stdout)
        presentations = re.search(r"\((\d+) main and (\d+) tear-out presentations", stdout)
        expected_activations = (args.frames - 1) // 240 + 1
        lifecycle = re.search(r"^Native lifecycle: created=(\d+) reused=(\d+) parked=(\d+) destroyed=(\d+)\s*$",
                              stdout, re.MULTILINE)
        cleanup = re.search(r"^Native cleanup: destroyed=(\d+) retained=(\d+) duration_ms=(\S+)\s*$",
                            stdout, re.MULTILINE)
        if "Native lifecycle:" in stdout and not lifecycle:
            raise RuntimeError("Malformed native lifecycle report")
        if "Native cleanup:" in stdout and not cleanup:
            raise RuntimeError("Malformed native cleanup report")
        if lifecycle:
            native = dict(zip(("created", "reused", "parked", "destroyed"), map(int, lifecycle.groups())))
            expected_closes = sum(frame % 240 == 180 for frame in range(args.frames))
            if (native["created"] != 1 or native["reused"] != expected_activations - 1 or
                    native["parked"] != expected_closes or native["destroyed"] != 0):
                raise RuntimeError("Tear-out native-window reuse counts do not match the action schedule")
            if not created or int(created[1]) != native["created"]:
                raise RuntimeError("Tear-out physical creation reports do not agree")
            if not cleanup:
                raise RuntimeError("Tear-out native-window reuse report is missing final cleanup")
            cleanup_ms = float(cleanup[3])
            if (int(cleanup[1]) != native["created"] or int(cleanup[2]) != 0 or
                    not math.isfinite(cleanup_ms) or cleanup_ms < 0):
                raise RuntimeError("Tear-out final native-window cleanup is incomplete or invalid")
            checks["native_lifecycle"] = native
            checks["native_cleanup"] = {"destroyed": int(cleanup[1]), "retained": int(cleanup[2]),
                                        "duration_ms": cleanup_ms}
        elif not created or int(created[1]) != expected_activations:
            raise RuntimeError(f"Tear-out scenario did not create the expected {expected_activations} windows")
        if not presentations or int(presentations[1]) != sum(row["main_presented"] for row in rows) or int(presentations[2]) != sum(row["secondary_presented"] for row in rows):
            raise RuntimeError("Tear-out console and CSV presentation counts do not agree")
        if not secondary_presented:
            raise RuntimeError("Tear-out scenario has no measured secondary presentation")
        for row in rows:
            if row["actions"] != int(row["frame"] % 240 in (0, 180)):
                raise RuntimeError(f"Unexpected tear-out action schedule at frame {row['frame']}")
        checks["native_windows_created_all_frames"] = int(created[1])
        checks["native_activations_all_frames"] = expected_activations
    config = re.search(r"Profile configuration: backend=(\S+) scenario=(\S+) present=(\S+)", stdout)
    if config:
        if config[2] != scenario or config[3] != requested_present_mode(args):
            raise RuntimeError("Demo presentation configuration does not match requested profile")
        checks["gpu_backend"] = config[1]
        checks["present_mode"] = config[3]
    elif getattr(args, "present", None):
        raise RuntimeError("Demo did not report its actual presentation configuration")
    return rows, included, checks


def summarize_frames(rows):
    timings = {column: distribution([row[column] for row in rows]) for column in TIMING_COLUMNS}
    for field in RENDER_TIMINGS:
        timings[f"combined_{field}"] = distribution([row[f"main_{field}"] + row[f"secondary_{field}"] for row in rows])
    timings["cpu_work_excluding_swapchain_wait_ms"] = distribution([
        max(0.0, row["frame_work_ms"] - row["main_swapchain_wait_ms"] - row["secondary_swapchain_wait_ms"])
        for row in rows])
    # This residual can still contain driver, submission, lifecycle, and OS waits.
    counters = {}
    for prefix in ("main", "secondary"):
        for field in RENDER_COUNTERS:
            values = [row[f"{prefix}_{field}"] for row in rows]
            counters[f"{prefix}_{field}"] = dict(distribution(values), total=sum(values))
    for field in RENDER_COUNTERS:
        values = [row[f"main_{field}"] + row[f"secondary_{field}"] for row in rows]
        counters[f"combined_{field}"] = dict(distribution(values), total=sum(values))
    for field in ("actions",):
        values = [row[field] for row in rows]
        counters[field] = dict(distribution(values), total=sum(values))
    actions = [row for row in rows if row["actions"]]
    steady = [row for row in rows if not row["actions"]]
    lifecycle = {"action_frames": len(actions), "other_frames": len(steady)}
    for name, subset in (("action_frames", actions), ("other_frames", steady)):
        if subset:
            lifecycle[name + "_frame_work_ms"] = distribution([row["frame_work_ms"] for row in subset])
            lifecycle[name + "_lifecycle_ms"] = distribution([row["lifecycle_ms"] for row in subset])
    return {"frames": len(rows), "timings_ms": timings, "counters": counters,
            "action_frame_breakdown": lifecycle}


def write_report(output, results, args):
    lines = ["# Real-window CPU frame profiles", "",
             f"Requested presentation mode: `{requested_present_mode(args)}`.", "",
             "Each row describes one process trial. Percentiles use nearest rank over individual presented frames, not batch averages.",
             f"First {args.warmup} frames excluded from each trial; all raw frames remain in CSV. Post-warmup creation, redocking, resize, and destruction spikes remain included.",
             "Initialization before the frame loop and final process cleanup are outside frame measurements. An interval belongs to the frame that follows the work it spans.",
             "Encoding and submission are CPU wall times, not GPU execution. Removing swapchain wait does not remove other driver or OS waits.",
             "", "| Scenario | Trial | Frames | Work p50 ms | Work p95 ms | Work p99 ms | Work max ms | Interval p99 ms | UI p99 ms | Swapchain wait p99 ms |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for result in sorted(results, key=lambda value: (SCENARIOS.index(value["scenario"]), value["trial"])):
        timings = result["summary"]["timings_ms"]
        work = timings["frame_work_ms"]
        lines.append(f"| {result['scenario']} | {result['trial']} | {result['summary']['frames']} | "
                     f"{work['p50']:.4f} | {work['p95']:.4f} | {work['p99']:.4f} | {work['max']:.4f} | "
                     f"{timings['frame_interval_ms']['p99']:.4f} | {timings['ui_ms']['p99']:.4f} | "
                     f"{timings['combined_swapchain_wait_ms']['p99']:.4f} |")
    lines += ["", "Full stage distributions, counters, action-frame breakdowns and validation appear in summary.json. Raw CSVs and console logs are adjacent.",
              "Trials run serially with rotating scenario order. Default scheduling leaves CPU affinity unchanged; --cpu-zero is an explicit alternative.",
              "Frames with a suppressed main presentation or an attempted but suppressed secondary presentation are excluded from distributions and counted in validation.",
              "The runner verifies workloads and does not presume that grouped images or any other scenario is faster.", "",
              "To inspect a scene manually, run:", "", "```powershell",
              f"& '{args.bin_dir.resolve() / 'rg_gui_demo_full.exe'}' --scenario manual",
              f"& '{args.bin_dir.resolve() / 'rg_gui_demo_tearout.exe'}' --start-torn-out", "```", ""]
    (output / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def run(args):
    scenarios = args.scenario or list(SCENARIOS)
    if len(set(scenarios)) != len(scenarios):
        raise RuntimeError("Do not repeat the same --scenario")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    env, runtime = child_environment(args)
    executables = {case: args.bin_dir.resolve() / ("rg_gui_demo_tearout.exe" if case == "tearout" else "rg_gui_demo_full.exe")
                   for case in scenarios}
    binaries = {str(path): describe_file(path) for path in sorted(set(executables.values()))}
    snapshot = source_snapshot(args, scenarios)
    # This is a no-build runner. Refuse obviously stale binaries; hashes document
    # source/dependency snapshots but do not claim to reconstruct compiler flags.
    for case, binary in executables.items():
        related = [value for name, value in snapshot.items()
                   if name.startswith("src/") or name in ("examples/rg_gui_demo_common.h", "examples/rg_gui_demo_profile.h", "examples/rg_gui_demo_stats.h",
                       "examples/rg_gui_demo_present.h",
                       "examples/rg_gui_demo_tearout.c" if case == "tearout" else "examples/rg_gui_demo_full.c")]
        if binary.stat().st_mtime_ns < max(value["modified_ns"] for value in related):
            raise RuntimeError(f"Demo is older than its source headers; rebuild before profiling: {binary}")
    manifest = {"status": "incomplete", "started_utc": datetime.now(timezone.utc).isoformat(),
                "platform": platform.platform(), "python": sys.version, "cpu": cpu_metadata(args.cpu_zero),
                "gpu_label_supplied": args.gpu_label, "sdl_runtime_candidate": runtime,
                "requested_present_mode": requested_present_mode(args),
                "arguments": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                "scenarios": scenarios, "binaries": binaries, "source_and_dependency_snapshot": snapshot,
                "provenance_note": "Current source/dependency hashes and executable hashes; rebuild beforehand. Compiler flags are not inferred.",
                "measurement_note": "CPU wall time with real presentation; not GPU timestamps. Nearest-rank per-frame percentiles. Warmup retained raw but excluded from summaries.",
                "commands": []}
    write_json(output / "manifest.json", manifest)
    results = []
    try:
        for trial in range(args.trials):
            order = scenarios[trial % len(scenarios):] + scenarios[:trial % len(scenarios)]
            for scenario in order:
                stem = f"{scenario}_{trial + 1}"
                csv_path = output / (stem + ".csv")
                command = [str(executables[scenario]), "--profile", str(csv_path), "--profile-warmup", str(args.warmup),
                           "--frames", str(args.frames), "--scenario", scenario]
                command.extend(demo_present_arguments(args))
                manifest["commands"].append({"trial": trial + 1, "scenario": scenario, "argv": command})
                write_json(output / "manifest.json", manifest)
                print(f"Running {scenario} trial {trial + 1}/{args.trials}", flush=True)
                started = time.perf_counter()
                with (output / (stem + ".stdout.txt")).open("w", encoding="utf-8") as stdout_file, \
                     (output / (stem + ".stderr.txt")).open("w", encoding="utf-8") as stderr_file:
                    process = subprocess.run(command, cwd=REPO, env=env, stdout=stdout_file, stderr=stderr_file,
                                             timeout=args.timeout, check=False)
                elapsed = time.perf_counter() - started
                stdout = (output / (stem + ".stdout.txt")).read_text(encoding="utf-8", errors="replace")
                if process.returncode:
                    raise RuntimeError(f"{scenario} trial {trial + 1} exited {process.returncode}; see {stem}.stderr.txt")
                rows, included, validation = read_capture(csv_path, scenario, args, stdout)
                result = {"scenario": scenario, "trial": trial + 1, "process_elapsed_seconds": elapsed,
                          "csv": csv_path.name, "csv_sha256": digest(csv_path), "validation": validation,
                          "summary": summarize_frames(included),
                          "warmup_summary": summarize_frames(rows[:args.warmup]) if args.warmup else None}
                results.append(result)
                write_json(output / "summary.json", results)
                write_report(output, results, args)
                print(f"Completed {scenario}: work p50={result['summary']['timings_ms']['frame_work_ms']['p50']:.4f} ms; "
                      f"p99={result['summary']['timings_ms']['frame_work_ms']['p99']:.4f} ms", flush=True)
        if "images" in scenarios and "images-grouped" in scenarios:
            controls = []
            for trial in range(1, args.trials + 1):
                pair = {result["scenario"]: result for result in results if result["trial"] == trial}
                alternating = pair["images"]["summary"]["counters"]["main_draw_calls"]["p50"]
                grouped = pair["images-grouped"]["summary"]["counters"]["main_draw_calls"]["p50"]
                if alternating - grouped < 500:
                    raise RuntimeError(f"Image control did not eliminate the expected image state changes in trial {trial}")
                controls.append({"trial": trial, "alternating_draw_calls_p50": alternating,
                                 "grouped_draw_calls_p50": grouped, "draw_call_difference": alternating - grouped})
            write_json(output / "image_control.json", controls)
        if source_snapshot(args, scenarios) != snapshot or any(describe_file(Path(path)) != value for path, value in binaries.items()):
            raise RuntimeError("Sources, dependencies, or demo binaries changed during profiling")
        manifest.update(status="complete", finished_utc=datetime.now(timezone.utc).isoformat())
    except Exception as error:
        manifest.update(status="failed", error=str(error), finished_utc=datetime.now(timezone.utc).isoformat())
        write_json(output / "manifest.json", manifest)
        raise
    write_json(output / "manifest.json", manifest)
    print(f"Validated frame profiles saved to {output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", required=True, type=Path, help="New output directory; existing directories are refused")
    parser.add_argument("--scenario", action="append", choices=SCENARIOS, help="Repeat to select scenarios; default is all")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--frames", type=int, default=1320)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=120.0, help="Maximum seconds per process")
    presentation = parser.add_mutually_exclusive_group()
    presentation.add_argument("--present", choices=("vsync", "mailbox", "immediate"),
                              help="Request a presentation mode; default is immediate")
    presentation.add_argument("--vsync", action="store_true", help="Alias for --present vsync")
    parser.add_argument("--cpu-zero", action="store_true", help="Pin runner and children to logical CPU 0; default leaves affinity unchanged")
    parser.add_argument("--bin-dir", type=Path, default=REPO, help="Directory containing freshly built demo executables")
    parser.add_argument("--sdl-root", type=Path, default=os.environ.get("SDL3_DIR"))
    parser.add_argument("--core-root", type=Path, default=os.environ.get("RG_CORE_DIR"), help="Optional declared build dependency root for hashing")
    parser.add_argument("--text-root", type=Path, default=os.environ.get("RG_TEXT_DIR"), help="Optional declared build dependency root for hashing")
    parser.add_argument("--gpu-label", help="Optional GPU model label recorded verbatim; backend is also captured from demo output")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Real-window profiling runner currently requires Windows")
    if not (1 <= args.trials <= 100 and 0 <= args.warmup < args.frames <= 100000):
        parser.error("Require 1 <= trials <= 100 and 0 <= warmup < frames <= 100000")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be positive and finite")
    try:
        run(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"frame profile error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
