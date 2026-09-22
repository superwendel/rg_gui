"""Profile loaded-font Unicode CPU costs, or compare two header trees on Windows."""

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

SOURCE = Path(__file__).with_name("bench_unicode.c")
SOURCES = (SOURCE, SOURCE.with_name("bench_gui.c"))
FONTS = ("synthetic4096_cjk", "synthetic16384_cjk")
CORPORA = ("repeated32", "dispersed")
CASES = ("frontend_dynamic", "renderer_warm", "renderer_cold", "lookup_pairs")
EXPECTED = {(font, corpus, case) for font in FONTS for corpus in CORPORA for case in CASES}
SIGNATURE_FIELDS = ("cycle_checksum", "geometry_checksum", "cache_hits", "cache_misses", "glyphs")
TRIALS = 5


def checked_rows(stdout, verify=False):
    fonts, samples, results = {}, {}, {}
    for line in stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        kind = row.get("kind")
        if kind == "font":
            key, target = row["font"], fonts
        elif kind in ("samples", "result"):
            key = (row["font"], row["corpus"], row["case"])
            target = samples if kind == "samples" else results
        else:
            raise RuntimeError(f"Unknown benchmark output: {row}")
        if key in target:
            raise RuntimeError(f"Duplicate {kind}: {key}")
        target[key] = row
    if set(fonts) != set(FONTS) or set(results) != EXPECTED or set(samples) != (set() if verify else EXPECTED):
        raise RuntimeError("Expected two font records and exactly 16 complete benchmark cases")
    for font, count in zip(FONTS, (4096, 16384)):
        expected = dict(glyph_count=count, kerning_count=count, labels=128, codepoints_per_label=40,
                        non_ascii_per_label=36, bytes_per_label=112, dynamic_cycle=16)
        if any(fonts[font].get(key) != value for key, value in expected.items()):
            raise RuntimeError(f"Unexpected workload metadata for {font}")
        if fonts[font].get("lookup_enabled") not in (0, 1):
            raise RuntimeError(f"Invalid lookup configuration for {font}")
    for key, row in results.items():
        case = key[2]
        if (row["glyphs"] != 5120 or row["cache_hits"] != (128 if case == "renderer_warm" else 0)
                or row["cache_misses"] != (128 if case == "renderer_cold" else 0)):
            raise RuntimeError(f"Invalid glyph/cache counts for {key}")
        for field in ("cycle_checksum", "geometry_checksum"):
            if not isinstance(row[field], str) or not 0 <= int(row[field]) < 2**64:
                raise RuntimeError(f"Invalid {field} for {key}")
        if verify:
            if row["median_ns_per_frame"] != 0:
                raise RuntimeError(f"Verification unexpectedly timed {key}")
            continue
        sample = samples[key]
        values = sample["ns_per_frame"]
        if len(values) != 7 or sample["frames_per_sample"] < 16:
            raise RuntimeError(f"Invalid calibrated sample for {key}")
        if case == "frontend_dynamic" and sample["frames_per_sample"] % 16:
            raise RuntimeError("Changing frontend labels must complete the fixed 16-frame cycle")
        if any(not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0
               for value in values + [row["median_ns_per_frame"]]):
            raise RuntimeError(f"Invalid timing for {key}")
        if abs(statistics.median(values) - row["median_ns_per_frame"]) > 0.002:
            raise RuntimeError(f"Median does not match raw samples for {key}")
    signature = {
        "fonts": {font: {key: value for key, value in row.items() if key != "lookup_enabled"}
                  for font, row in fonts.items()},
        "cases": {"/".join(key): {field: row[field] for field in SIGNATURE_FIELDS}
                  for key, row in results.items()},
    }
    return results, samples, signature, fonts


def build(gui_root, args, output):
    common.SOURCE = SOURCE
    args.suite = "labels"
    args.define = [f"RG_GUI_BENCH_LOOKUP={args.lookup}"]
    binary = common.build(gui_root, args, output)
    sidecar = binary.with_suffix(".build.json")
    metadata = json.loads(sidecar.read_text(encoding="utf-8"))
    metadata["harness_sources"] = {path.name: common.digest(path) for path in SOURCES}
    common.write_json(sidecar, metadata)
    return binary


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    comparing = bool(args.before_root or args.before_bin)
    variants = ("before", "after") if comparing else ("current",)
    manifest = {
        "status": "incomplete", "started_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "processor": platform.processor(),
        "cpu_affinity_mask": 1, "trials": 0 if args.build_only else TRIALS,
        "harness_sources": {path.name: common.digest(path) for path in SOURCES},
        "runner_sha256": common.digest(Path(__file__)), "compiler_runner_sha256": common.digest(Path(common.__file__)),
        "variants": variants, "fonts": FONTS, "corpora": CORPORA, "cases": CASES, "binaries": {},
        "measurement": "Seven calibrated approximately 35 ms batch averages per process; five serial process trials. "
                       "Summary p95 uses the 35 batch averages, not individual frame latency. "
                       "Font loading, allocations, lookup initialization and cold cache clearing are excluded. "
                       "Cold per-frame timer overhead is included; no GPU work or language shaping.",
        "corpus": "128 labels, each 36 CJK codepoints plus four changing ASCII digits (40 codepoints/112 UTF-8 bytes). "
                  "Repeated corpus uses a 32-codepoint CJK alphabet; dispersed corpus uses 36-codepoint spans "
                  "distributed across the loaded font. The same 16-frame digit cycle is measured in each frontend batch. "
                  "Lookup-only case uses predecoded codepoints and performs 5120 glyph plus 4992 kerning queries.",
    }
    common.write_json(output / "manifest.json", manifest)
    binaries = {}
    for variant in variants:
        supplied = getattr(args, variant + "_bin")
        root = getattr(args, variant + "_root") if comparing else args.gui_root
        binary = supplied.resolve() if supplied else build(root, args, output / "build" / (variant + ".exe"))
        metadata = json.loads(binary.with_suffix(".build.json").read_text(encoding="utf-8"))
        digest = common.digest(binary)
        if (metadata.get("executable_sha256") != digest or
                metadata.get("harness_sources") != manifest["harness_sources"]):
            raise RuntimeError(f"Executable or harness does not match build provenance: {binary}")
        if f"/DRG_GUI_BENCH_LOOKUP={args.lookup}" not in metadata["command"]:
            raise RuntimeError(f"Executable does not match --lookup={args.lookup}: {binary}")
        binaries[variant] = binary
        manifest["binaries"][variant] = {"path": str(binary), "sha256": digest, "build": metadata}
    common.write_json(output / "manifest.json", manifest)
    env = os.environ.copy()
    dll_dirs = [args.sdl_bin] if args.sdl_bin else []
    if args.sdl_root:
        dll_dirs += [args.sdl_root / subdir for subdir in ("bin", "lib/x64", "lib")]
    env["PATH"] = os.pathsep.join(str(path.resolve()) for path in dll_dirs if path.is_dir()) + os.pathsep + env.get("PATH", "")
    common.pin_cpu_zero()
    signature = None
    for variant, binary in binaries.items():
        process = subprocess.run([str(binary), "check"], cwd=common.REPO, env=env, capture_output=True, text=True)
        (output / f"{variant}.verify.jsonl").write_text(process.stdout, encoding="utf-8")
        (output / f"{variant}.verify.stderr.txt").write_text(process.stderr, encoding="utf-8")
        if process.returncode:
            raise RuntimeError(f"{variant} correctness verification failed: {process.stderr}")
        _, _, actual, fonts = checked_rows(process.stdout, verify=True)
        if signature is not None and actual != signature:
            raise RuntimeError(f"Correctness/workload signatures differ for {variant}")
        signature = actual
        manifest["binaries"][variant]["font_metadata"] = fonts
    common.write_json(output / "signatures.json", signature)
    if args.build_only:
        manifest.update(status="verified", finished_utc=datetime.now(timezone.utc).isoformat())
        common.write_json(output / "manifest.json", manifest)
        print(f"Built and verified Unicode benchmark without timing: {output}")
        return
    results = []
    for trial in range(TRIALS):
        for variant in variants if trial % 2 == 0 else tuple(reversed(variants)):
            process = subprocess.run([str(binaries[variant])], cwd=common.REPO, env=env, capture_output=True, text=True)
            (output / f"{variant}_{trial + 1}.jsonl").write_text(process.stdout, encoding="utf-8")
            (output / f"{variant}_{trial + 1}.stderr.txt").write_text(process.stderr, encoding="utf-8")
            if process.returncode:
                raise RuntimeError(f"{variant} trial {trial + 1} failed: {process.stderr}")
            rows, samples, actual, _ = checked_rows(process.stdout)
            if actual != signature:
                raise RuntimeError(f"Workload/output signature mismatch in {variant} trial {trial + 1}")
            results.extend(dict(variant=variant, trial=trial + 1, samples=samples[key], **row) for key, row in rows.items())
            common.write_json(output / "results.json", results)
            print(f"Completed {variant} Unicode trial {trial + 1}/{TRIALS}", flush=True)
    summary = []
    for font in FONTS:
        for corpus in CORPORA:
            for case in CASES:
                row = {"font": font, "corpus": corpus, "case": case}
                for variant in variants:
                    trials = [result for result in results if
                              (result["font"], result["corpus"], result["case"], result["variant"]) == (font, corpus, case, variant)]
                    medians = [result["median_ns_per_frame"] for result in trials]
                    batch_means = sorted(value for result in trials for value in result["samples"]["ns_per_frame"])
                    row[variant] = {
                        "median_ns": statistics.median(medians), "min_process_median_ns": min(medians),
                        "max_process_median_ns": max(medians), "process_medians_ns": medians,
                        "p95_batch_mean_ns": batch_means[math.ceil(0.95 * len(batch_means)) - 1],
                    }
                if comparing:
                    row["speedup"] = row["before"]["median_ns"] / row["after"]["median_ns"]
                    row["time_reduction_percent"] = (1.0 - 1.0 / row["speedup"]) * 100.0
                summary.append(row)
    common.write_json(output / "summary.json", summary)
    lines = ["Unicode CPU profile: median of five process medians; times in microseconds.",
             "p95 is over 35 batch averages per case, not individual frames. No GPU work or shaping.", "",
             "| Font | Corpus | Case | Variant | Median | p95 batch mean |",
             "| --- | --- | --- | --- | ---: | ---: |"]
    for row in summary:
        for variant in variants:
            values = row[variant]
            lines.append(f"| {row['font']} | {row['corpus']} | {row['case']} | {variant} | "
                         f"{values['median_ns']/1000:.3f} | {values['p95_batch_mean_ns']/1000:.3f} |")
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest.update(status="complete", finished_utc=datetime.now(timezone.utc).isoformat())
    common.write_json(output / "manifest.json", manifest)
    print("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New directory; existing directories are refused")
    parser.add_argument("--gui-root", type=Path, default=common.REPO / "src")
    parser.add_argument("--core-root", type=Path, default=os.environ.get("RG_CORE_DIR", common.REPO.parent / "rg_core"))
    parser.add_argument("--text-root", type=Path, default=os.environ.get("RG_TEXT_DIR", common.REPO.parent / "rg_text"))
    parser.add_argument("--sdl-root", type=Path, default=os.environ.get("SDL3_DIR"))
    parser.add_argument("--sdl-include", type=Path, default=os.environ.get("SDL3_INCLUDE_DIR"))
    parser.add_argument("--sdl-lib", type=Path, help="Full path to SDL3.lib")
    parser.add_argument("--sdl-bin", type=Path, default=os.environ.get("SDL3_BIN_DIR"))
    parser.add_argument("--lookup", type=int, choices=(0, 1), default=1, help="Attach the optional ASCII lookup when the header supports it")
    parser.add_argument("--current-bin", type=Path, help="Profile an existing executable with adjacent .build.json")
    parser.add_argument("--before-root", type=Path, help="Compare these GUI headers against --after-root")
    parser.add_argument("--after-root", type=Path)
    parser.add_argument("--before-bin", type=Path, help="Compare existing executables with adjacent .build.json")
    parser.add_argument("--after-bin", type=Path)
    parser.add_argument("--build-only", action="store_true", help="Compile and verify all cases without timing")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Requires Windows, MSVC, and QueryPerformanceCounter")
    if bool(args.before_root) != bool(args.after_root) or bool(args.before_bin) != bool(args.after_bin):
        parser.error("Specify both before/after roots, or both before/after executables")
    if (args.before_root and args.before_bin) or (args.current_bin and (args.before_root or args.before_bin)):
        parser.error("Choose one input: current executable, paired header roots, or paired executables")
    try:
        run(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Unicode benchmark error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
