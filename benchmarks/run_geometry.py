"""Compare portable C and optional SSE2 image packing on Windows (stdlib only)."""

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

sys.dont_write_bytecode = True
import run as common

SOURCE = Path(__file__).with_name("bench_geometry.c")
VARIANTS = ("portable", "sse2")
CASES = tuple(f"{mode}_{count}" for mode in ("pack", "prepare") for count in (1, 128, 1024))


def checked_rows(stdout):
    rows = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    if len(rows) != len(CASES) or {row["case"] for row in rows} != set(CASES):
        raise RuntimeError("Expected exactly six distinct geometry benchmark cases")
    signature = {}
    for row in rows:
        mode, count = row["case"].split("_")
        count = int(count)
        times = [row[key] for key in ("min_ns", "median_ns", "max_ns")]
        if any(not isinstance(x, (int, float)) or not math.isfinite(x) or x <= 0 for x in times):
            raise RuntimeError(f"Invalid timings for {row['case']}")
        if times != sorted(times) or row["vertices"] != count * 6 or row["items"] != (count if mode == "prepare" else 0):
            raise RuntimeError(f"Invalid workload or timing range for {row['case']}")
        if not isinstance(row["geometry"], str) or len(row["geometry"]) != 16:
            raise RuntimeError(f"Invalid geometry checksum for {row['case']}")
        int(row["geometry"], 16)
        signature[row["case"]] = {key: row[key] for key in ("geometry", "vertices", "items")}
    return rows, signature


def compare(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"status": "incomplete", "started_utc": datetime.now(timezone.utc).isoformat(),
                "platform": platform.platform(), "processor": platform.processor(),
                "cpu_affinity_mask": 1, "trials": 0 if args.build_only else 5,
                "source_sha256": common.digest(SOURCE), "cases": CASES, "binaries": {}}
    common.write_json(output / "manifest.json", manifest)
    # Reuse the label runner's compiler discovery, dependency resolution, and build provenance.
    common.SOURCE = SOURCE
    args.suite = "labels"
    binaries = {}
    for variant, enabled in zip(VARIANTS, (0, 1)):
        supplied = getattr(args, variant + "_bin")
        args.define = [f"RG_GUI_GPU_USE_SSE2={enabled}"]
        binary = Path(supplied).resolve() if supplied else common.build(
            args.gui_root, args, output / "build" / (variant + ".exe"))
        build = json.loads(binary.with_suffix(".build.json").read_text(encoding="utf-8"))
        digest = common.digest(binary)
        if build.get("executable_sha256") != digest or build.get("source_sha256") != manifest["source_sha256"]:
            raise RuntimeError(f"Executable or harness does not match build provenance: {binary}")
        if "/D" + args.define[0] not in build["command"]:
            raise RuntimeError(f"Build provenance does not select the expected {variant} backend")
        binaries[variant] = binary
        manifest["binaries"][variant] = {"path": str(binary), "sha256": digest, "build": build}
    if manifest["binaries"]["portable"]["build"]["headers"] != manifest["binaries"]["sse2"]["build"]["headers"]:
        raise RuntimeError("Both backends must use the identical GUI and dependency headers")
    commands = []
    for variant in VARIANTS:
        command = manifest["binaries"][variant]["build"]["command"]
        commands.append([arg for arg in command if not arg.startswith((
            "/DRG_GUI_GPU_USE_SSE2=", "/Fo:", "/Fe:"))])
    if commands[0] != commands[1]:
        raise RuntimeError("Both backends must use the same compiler, source, dependencies, and flags")
    common.write_json(output / "manifest.json", manifest)
    env = os.environ.copy()
    dll_dirs = [args.sdl_bin] if args.sdl_bin else []
    if args.sdl_root:
        dll_dirs += [args.sdl_root / subdir for subdir in ("bin", "lib/x64", "lib")]
    env["PATH"] = os.pathsep.join(str(p.resolve()) for p in dll_dirs if p.is_dir()) + os.pathsep + env.get("PATH", "")
    common.pin_cpu_zero()
    for variant, binary in binaries.items():
        process = subprocess.run([str(binary), "check"], cwd=common.REPO, env=env, capture_output=True, text=True)
        (output / f"{variant}.verify.log").write_text(process.stdout + process.stderr, encoding="utf-8")
        if process.returncode:
            raise RuntimeError(f"{variant} correctness verification failed; see saved verify log")
    if args.build_only:
        manifest.update(status="verified", finished_utc=datetime.now(timezone.utc).isoformat())
        common.write_json(output / "manifest.json", manifest)
        print(f"Both backends built and verified without timing: {output}")
        return
    results, signature = [], None
    for trial in range(5):
        for variant in VARIANTS if trial % 2 == 0 else tuple(reversed(VARIANTS)):
            process = subprocess.run([str(binaries[variant])], cwd=common.REPO, env=env, capture_output=True, text=True)
            (output / f"{variant}_{trial + 1}.jsonl").write_text(process.stdout, encoding="utf-8")
            (output / f"{variant}_{trial + 1}.stderr.txt").write_text(process.stderr, encoding="utf-8")
            if process.returncode:
                raise RuntimeError(f"{variant} trial {trial + 1} failed: {process.stderr}")
            rows, actual_signature = checked_rows(process.stdout)
            if signature is not None and actual_signature != signature:
                raise RuntimeError(f"Geometry/workload mismatch in {variant} trial {trial + 1}")
            signature = actual_signature
            results.extend(dict(variant=variant, trial=trial + 1, **row) for row in rows)
            common.write_json(output / "results.json", results)
            print(f"Completed {variant} trial {trial + 1}/5", flush=True)
    summary = []
    for case in CASES:
        row = {"case": case}
        for variant in VARIANTS:
            values = [r["median_ns"] for r in results if r["case"] == case and r["variant"] == variant]
            row[variant] = {"median_ns": statistics.median(values), "min_ns": min(values), "max_ns": max(values)}
        row["speedup"] = row["portable"]["median_ns"] / row["sse2"]["median_ns"]
        row["time_reduction_percent"] = (1.0 - 1.0 / row["speedup"]) * 100.0
        summary.append(row)
    common.write_json(output / "summary.json", summary)
    common.write_json(output / "signatures.json", signature)
    lines = ["CPU packing: median of five process medians; times in microseconds.", "",
             "| Case | Portable | SSE2 | Speedup |", "| --- | ---: | ---: | ---: |"]
    for row in summary:
        lines.append(f"| {row['case']} | {row['portable']['median_ns']/1000:.3f} | {row['sse2']['median_ns']/1000:.3f} | {row['speedup']:.3f}x |")
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest.update(status="complete", finished_utc=datetime.now(timezone.utc).isoformat())
    common.write_json(output / "manifest.json", manifest)
    print("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New output directory; existing directories are refused")
    parser.add_argument("--gui-root", type=Path, default=common.REPO / "src")
    parser.add_argument("--core-root", type=Path, default=os.environ.get("RG_CORE_DIR", common.REPO.parent / "rg_core"))
    parser.add_argument("--text-root", type=Path, default=os.environ.get("RG_TEXT_DIR", common.REPO.parent / "rg_text"))
    parser.add_argument("--sdl-root", type=Path, default=os.environ.get("SDL3_DIR"))
    parser.add_argument("--sdl-include", type=Path, default=os.environ.get("SDL3_INCLUDE_DIR"))
    parser.add_argument("--sdl-lib", type=Path, help="Full path to SDL3.lib")
    parser.add_argument("--sdl-bin", type=Path, default=os.environ.get("SDL3_BIN_DIR"))
    parser.add_argument("--portable-bin", type=Path, help="Previously built portable executable with adjacent .build.json")
    parser.add_argument("--sse2-bin", type=Path, help="Previously built SSE2 executable with adjacent .build.json")
    parser.add_argument("--build-only", action="store_true", help="Build and verify both backends without timing")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Requires Windows, MSVC, and QueryPerformanceCounter")
    if bool(args.portable_bin) != bool(args.sse2_bin):
        parser.error("Supply both --portable-bin and --sse2-bin together")
    try:
        compare(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"geometry benchmark error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
