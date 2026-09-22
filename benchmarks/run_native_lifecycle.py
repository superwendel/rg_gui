"""Compare two prebuilt tear-out demos with serial, paired real-window profiles.

Example (Windows, stdlib only):
  python benchmarks/run_native_lifecycle.py --before-bin out/before/rg_gui_demo_tearout.exe \
      --after-bin rg_gui_demo_tearout.exe --sdl-root C:/libs/SDL3-3.4.10 --output out/native-ab

Both immediate and VSync presentation are measured by default. Process order
alternates within each mode across trials. Do not interact with or minimize the
windows. Build both binaries beforehand; this runner never builds or edits them.

Optional --before-provenance/--after-provenance JSON files must contain the
corresponding executable hash as a top-level "executable_sha256" string. Other
fields (source/dependency hashes, build command and flags) are preserved verbatim.
These are supplied build records, not compiler provenance inferred by the runner.
"""

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace

import run_frames as frames


REPO = frames.REPO
MODES = ("immediate", "vsync")
VARIANTS = ("before", "after")
ACTION_NAMES = {0: "activate", 180: "redock_close"}
ACTION_TIMINGS = ("lifecycle_ms", "frame_work_ms", "platform_ms", "ui_ms",
                  "main_swapchain_wait_ms", "secondary_swapchain_wait_ms")


def executable_path(value):
    path = value.resolve()
    if path.is_dir():
        path /= "rg_gui_demo_tearout.exe"
    if not path.is_file():
        raise RuntimeError(f"Tear-out executable does not exist: {path}")
    return path


def provenance_record(path, binary):
    if path is None:
        return None
    path = path.resolve()
    contents = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(contents, dict) or contents.get("executable_sha256") != binary["sha256"]:
        raise RuntimeError(f"Provenance must bind executable_sha256 to its binary: {path}")
    return {"file": frames.describe_file(path), "contents": contents}


def shared_inputs():
    paths = [Path(__file__).resolve(), Path(frames.__file__).resolve()]
    paths += sorted(path for path in (REPO / "examples/assets").glob("*inter_medium_16*") if path.is_file())
    paths += sorted(path for path in (REPO / "shaders/Compiled").rglob("*") if path.is_file())
    return {str(path): frames.describe_file(path) for path in paths}


def child_environment(binary, sdl_root):
    # Normalize Windows' case-insensitive environment names. This also avoids
    # duplicate PATH/Path entries when the parent tool environment contains both.
    env = {name.upper(): value for name, value in os.environ.items()}
    folders = [binary.parent]
    if sdl_root is not None:
        folders += [sdl_root.resolve() / suffix for suffix in ("lib/x64", "bin", "lib")]
    if env.get("SDL3_BIN_DIR"):
        folders.append(Path(env["SDL3_BIN_DIR"]).resolve())
    folders = [folder for folder in folders if folder.is_dir()]
    env["PATH"] = os.pathsep.join(map(str, folders)) + os.pathsep + env.get("PATH", "")
    runtime = next((folder / "SDL3.dll" for folder in folders if (folder / "SDL3.dll").is_file()), None)
    return env, frames.describe_file(runtime) if runtime else None


def action_summary(rows):
    result = {}
    by_frame = {row["frame"]: row for row in rows}
    for offset, name in ACTION_NAMES.items():
        selected = [row for row in rows if row["frame"] % 240 == offset]
        if not selected:
            raise RuntimeError(f"Measurement range must contain at least one {name} action")
        result[name] = {
            "count": len(selected),
            "timings_ms": {field: frames.distribution([row[field] for row in selected])
                           for field in ACTION_TIMINGS},
            "events": [{field: row[field] for field in ("frame", "main_presented", "secondary_submitted",
                        "secondary_presented") + ACTION_TIMINGS} for row in selected],
        }
        if name == "redock_close":
            for event in result[name]["events"]:
                following = [by_frame.get(event["frame"] + offset) for offset in range(1, 6)]
                if any(row is None for row in following):
                    raise RuntimeError("Each measured close needs five subsequent frames to check for shifted stalls")
                event["following_frames"] = [dict(offset=offset, **{field: row[field] for field in
                    ("frame", "frame_work_ms", "lifecycle_ms", "main_swapchain_wait_ms", "secondary_swapchain_wait_ms")})
                    for offset, row in enumerate(following, 1)]
                event["close_plus_five_frame_work_ms"] = event["frame_work_ms"] + sum(row["frame_work_ms"] for row in following)
            result[name]["close_plus_five_frame_work_ms"] = frames.distribution([
                event["close_plus_five_frame_work_ms"] for event in result[name]["events"]])
    return result


def compare_values(before, after):
    return {"before": before, "after": after, "after_minus_before": after - before,
            "after_over_before": after / before if before else None}


def paired_summary(results, modes, trials):
    pairs = []
    for mode in modes:
        for trial in range(1, trials + 1):
            pair = {row["variant"]: row for row in results if row["mode"] == mode and row["trial"] == trial}
            if set(pair) != set(VARIANTS):
                continue
            before, after = pair["before"], pair["after"]
            comparisons = {}
            for field in ("frame_work_ms", "frame_interval_ms", "lifecycle_ms", "combined_swapchain_wait_ms"):
                comparisons[field] = {percentile: compare_values(before["summary"]["timings_ms"][field][percentile],
                                      after["summary"]["timings_ms"][field][percentile])
                                      for percentile in ("p50", "p95", "p99", "max")}
            actions = {}
            for name in ACTION_NAMES.values():
                old = {event["frame"]: event for event in before["actions"][name]["events"]}
                new = {event["frame"]: event for event in after["actions"][name]["events"]}
                if old.keys() != new.keys():
                    raise RuntimeError(f"Action frame identities differ in {mode} trial {trial}")
                actions[name] = [{"frame": index,
                                  "timings_ms": {field: compare_values(old[index][field], new[index][field])
                                                 for field in ACTION_TIMINGS}}
                                 for index in sorted(old)]
                if name == "redock_close":
                    for event in actions[name]:
                        index = event["frame"]
                        event["close_plus_five_frame_work_ms"] = compare_values(
                            old[index]["close_plus_five_frame_work_ms"], new[index]["close_plus_five_frame_work_ms"])
                        event["following_frame_work_ms"] = [{"offset": offset, **compare_values(
                            old[index]["following_frames"][offset - 1]["frame_work_ms"],
                            new[index]["following_frames"][offset - 1]["frame_work_ms"])} for offset in range(1, 6)]
            pairs.append({"mode": mode, "trial": trial,
                          "process_order": sorted(VARIANTS, key=lambda variant: pair[variant]["execution_index"]),
                          "timings_ms": comparisons, "matched_action_frames": actions})
    return pairs


def aggregate_summary(results, modes):
    aggregate = {}
    for mode in modes:
        aggregate[mode] = {}
        for variant in VARIANTS:
            selected = [row for row in results if row["mode"] == mode and row["variant"] == variant]
            if not selected:
                continue
            timings = {}
            for field in ("frame_work_ms", "frame_interval_ms", "lifecycle_ms", "combined_swapchain_wait_ms"):
                timings[field] = {}
                for percentile in ("p50", "p95", "p99", "max"):
                    values = [row["summary"]["timings_ms"][field][percentile] for row in selected]
                    timings[field][percentile] = {"median_of_trials": statistics.median(values),
                                                 "minimum_trial": min(values), "maximum_trial": max(values)}
                timings[field]["global_max"] = max(row["summary"]["timings_ms"][field]["max"] for row in selected)
            actions = {}
            for name in ACTION_NAMES.values():
                events = [event for row in selected for event in row["actions"][name]["events"]]
                actions[name] = {"count": len(events), "pooled_event_timings_ms": {
                    field: frames.distribution([event[field] for event in events]) for field in ACTION_TIMINGS}}
                if name == "redock_close":
                    actions[name]["pooled_close_plus_five_frame_work_ms"] = frames.distribution([
                        event["close_plus_five_frame_work_ms"] for event in events])
            aggregate[mode][variant] = {"trials": len(selected),
                                       "frames": sum(row["summary"]["frames"] for row in selected),
                                       "timings_ms": timings, "actions": actions}
    return aggregate


def write_results(output, results, modes, args):
    pairs = paired_summary(results, modes, args.trials)
    aggregate = aggregate_summary(results, modes)
    frames.write_json(output / "summary.json", {"trials": results, "paired_comparisons": pairs, "aggregate": aggregate})
    lines = ["# Native-window lifecycle comparison", "",
             "Serial trials alternate the before/after process order within each presentation mode. Each executable runs the same scripted tear-out workload.",
             f"The first {args.warmup} of {args.frames} frames per process are warmup. All raw frames remain in CSV.",
             "Whole-frame and action summaries below retain every post-warmup frame, including suppressed presentations; the JSON also provides fully-presented-only distributions and suppression counts.",
             "Activation occurs at frame modulo 240 = 0; redocking and closing occur at offset 180. An activation may create or reuse a native window; a close may destroy or park it. Physical lifecycle counts and final cleanup are validated separately.",
             "Startup and final cleanup outside the frame loop are not included in frame distributions. A candidate's separate cleanup duration is retained in validation metadata when reported.",
             "Frame work and lifecycle durations are CPU wall times including driver/OS/GPU waits, not CPU busy time or GPU timestamps. A frame interval spans the preceding frame's work and must not be attributed to the current action.",
             "Percentiles use nearest rank over individual frames. Process trials are paired by mode and trial index; frame-number matching aligns scripted actions, not external scheduling noise.",
             "", "| Mode | Trial | Binary | Frames | Work p50 ms | Work p95 ms | Work p99 ms | Work max ms | Suppressed frames |",
             "| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for result in sorted(results, key=lambda row: (modes.index(row["mode"]), row["trial"], VARIANTS.index(row["variant"]))):
        work = result["summary"]["timings_ms"]["frame_work_ms"]
        lines.append(f"| {result['mode']} | {result['trial']} | {result['variant']} | {result['summary']['frames']} | "
                     f"{work['p50']:.4f} | {work['p95']:.4f} | {work['p99']:.4f} | {work['max']:.4f} | "
                     f"{result['validation']['suppressed_presentation_frames']} |")
    lines += ["", "## Action samples", "",
              "These action events are pooled within each mode and binary. Sample counts are small; their maxima are observed spikes, not reliable high-percentile estimates for a general application.",
              "", "| Mode | Binary | Action | Events | Lifecycle median ms | Lifecycle max ms | Frame median ms | Frame max ms |",
              "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    for mode in modes:
        for variant, item in aggregate[mode].items():
            for name, action in item["actions"].items():
                timing = action["pooled_event_timings_ms"]
                lines.append(f"| {mode} | {variant} | {name} | {action['count']} | "
                             f"{timing['lifecycle_ms']['p50']:.4f} | {timing['lifecycle_ms']['max']:.4f} | "
                             f"{timing['frame_work_ms']['p50']:.4f} | {timing['frame_work_ms']['max']:.4f} |")
    lines += ["", "See summary.json for stage/counter distributions, per-event before/after differences, median-of-trial percentile estimates, ranges, and the true global maximum across trials.",
              "Each close event also records the next five individual frames and the six-frame work sum, to expose stalls shifted beyond the close itself. These work sums are not presentation intervals or GPU times.",
              "The runner validates workload identity and reports measurements without asserting that either binary is faster. Manifest status must be complete before using a run.",
              "Executable hashes, exact commands, supplied build provenance, runtime candidates, and shared resource hashes are recorded in manifest.json. Runtime paths are candidates, not a loaded-module trace.", ""]
    (output / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def run(args):
    modes = args.mode or list(MODES)
    if len(modes) != len(set(modes)):
        raise RuntimeError("Do not repeat the same --mode")
    binaries = {variant: executable_path(getattr(args, variant + "_bin")) for variant in VARIANTS}
    binary_records = {variant: frames.describe_file(path) for variant, path in binaries.items()}
    provenance = {variant: provenance_record(getattr(args, variant + "_provenance"), binary_records[variant])
                  for variant in VARIANTS}
    environments, runtimes = {}, {}
    for variant in VARIANTS:
        environments[variant], runtimes[variant] = child_environment(binaries[variant], args.sdl_root)
    runtime_hashes = {record["sha256"] for record in runtimes.values() if record is not None}
    if len(runtime_hashes) > 1:
        raise RuntimeError("Before/after executables resolve different SDL runtime candidates")
    shared = shared_inputs()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"status": "incomplete", "started_utc": datetime.now(timezone.utc).isoformat(),
                "platform": platform.platform(), "python": sys.version, "cpu": frames.cpu_metadata(False),
                "arguments": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                "modes": modes, "binaries": binary_records, "supplied_build_provenance": provenance,
                "sdl_runtime_candidates": runtimes, "shared_runtime_inputs": shared,
                "presentation_environment": {key: environments["before"][key] for key in ("SDL_GPU_DRIVER", "SDL_VIDEODRIVER")
                                             if key in environments["before"]},
                "provenance_note": "Prebuilt executable comparison. Supplied sidecars are hash-bound build records; absent sidecars mean build/source provenance is unavailable. Current source is not assumed to match either executable.",
                "measurement_note": "CPU wall time with actual presentation; not GPU timestamps. All post-warmup frames retained in primary/action summaries. Alternating serial process order; unchanged CPU affinity.",
                "commands": []}
    frames.write_json(output / "manifest.json", manifest)
    results = []
    try:
        for trial_index in range(args.trials):
            mode_order = modes[trial_index % len(modes):] + modes[:trial_index % len(modes)]
            for mode in mode_order:
                order = VARIANTS if (trial_index + modes.index(mode)) % 2 == 0 else tuple(reversed(VARIANTS))
                for variant in order:
                    stem = f"{mode}_{trial_index + 1}_{variant}"
                    csv_path = output / (stem + ".csv")
                    command = [str(binaries[variant]), "--profile", str(csv_path), "--profile-warmup", str(args.warmup),
                               "--frames", str(args.frames), "--scenario", "tearout"]
                    if mode == "immediate":
                        command.append("--no-vsync")
                    execution_index = len(manifest["commands"])
                    manifest["commands"].append({"mode": mode, "trial": trial_index + 1, "variant": variant,
                                                 "execution_index": execution_index, "argv": command, "cwd": str(REPO)})
                    frames.write_json(output / "manifest.json", manifest)
                    print(f"Running {mode} trial {trial_index + 1}/{args.trials}: {variant}", flush=True)
                    started = time.perf_counter()
                    with (output / (stem + ".stdout.txt")).open("w", encoding="utf-8") as stdout_file, \
                         (output / (stem + ".stderr.txt")).open("w", encoding="utf-8") as stderr_file:
                        process = subprocess.run(command, cwd=REPO, env=environments[variant], stdout=stdout_file,
                                                 stderr=stderr_file, timeout=args.timeout, check=False)
                    elapsed = time.perf_counter() - started
                    if process.returncode:
                        raise RuntimeError(f"{stem} exited {process.returncode}; see {stem}.stderr.txt")
                    stdout = (output / (stem + ".stdout.txt")).read_text(encoding="utf-8", errors="replace")
                    capture_args = SimpleNamespace(frames=args.frames, warmup=args.warmup, vsync=mode == "vsync")
                    rows, included, validation = frames.read_capture(csv_path, "tearout", capture_args, stdout)
                    measured = [row for row in rows if not row["warmup"]]
                    result = {"mode": mode, "trial": trial_index + 1, "variant": variant,
                              "execution_index": execution_index, "process_elapsed_seconds": elapsed,
                              "csv": csv_path.name, "csv_sha256": frames.digest(csv_path), "validation": validation,
                              "summary": frames.summarize_frames(measured),
                              "fully_presented_summary": frames.summarize_frames(included),
                              "actions": action_summary(measured)}
                    results.append(result)
                    write_results(output, results, modes, args)
                    work = result["summary"]["timings_ms"]["frame_work_ms"]
                    print(f"Completed {stem}: work p50={work['p50']:.4f} ms; p99={work['p99']:.4f} ms; "
                          f"max={work['max']:.4f} ms", flush=True)
        if any(frames.describe_file(binaries[variant]) != binary_records[variant] for variant in VARIANTS):
            raise RuntimeError("A demo executable changed during the comparison")
        if shared_inputs() != shared:
            raise RuntimeError("Runner or shared assets/shaders changed during the comparison")
        for record in provenance.values():
            if record is not None and frames.describe_file(Path(record["file"]["path"])) != record["file"]:
                raise RuntimeError("A build provenance record changed during the comparison")
        for record in runtimes.values():
            if record is not None and frames.describe_file(Path(record["path"])) != record:
                raise RuntimeError("An SDL runtime candidate changed during the comparison")
        manifest.update(status="complete", finished_utc=datetime.now(timezone.utc).isoformat())
    except Exception as error:
        manifest.update(status="failed", error=str(error), finished_utc=datetime.now(timezone.utc).isoformat())
        frames.write_json(output / "manifest.json", manifest)
        raise
    frames.write_json(output / "manifest.json", manifest)
    print(f"Validated native lifecycle comparison saved to {output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--before-bin", required=True, type=Path, help="Before tear-out executable, or its directory")
    parser.add_argument("--after-bin", required=True, type=Path, help="After tear-out executable, or its directory")
    parser.add_argument("--before-provenance", type=Path, help="Optional JSON build record with executable_sha256")
    parser.add_argument("--after-provenance", type=Path, help="Optional JSON build record with executable_sha256")
    parser.add_argument("--output", required=True, type=Path, help="New output directory; existing directories are refused")
    parser.add_argument("--sdl-root", type=Path, default=os.environ.get("SDL3_DIR"))
    parser.add_argument("--mode", action="append", choices=MODES, help="Repeat to select modes; default immediate and vsync")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=120.0, help="Maximum seconds per process")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Real-window profiling runner currently requires Windows")
    if not (1 <= args.trials <= 100 and 0 <= args.warmup < args.frames <= 100000):
        parser.error("Require 1 <= trials <= 100 and 0 <= warmup < frames <= 100000")
    if not all(any(frame % 240 == offset for frame in range(args.warmup, args.frames)) for offset in ACTION_NAMES):
        parser.error("Post-warmup frames must contain both an activation (modulo 240 = 0) and a redock/close action (= 180)")
    if any(frame + 5 >= args.frames for frame in range(args.warmup, args.frames) if frame % 240 == 180):
        parser.error("Include at least five frames after every measured close action")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be positive and finite")
    try:
        run(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"native lifecycle comparison error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
