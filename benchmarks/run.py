"""Build and compare the fixed-workload Linux/Windows CPU benchmark (stdlib only)."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import shlex
import statistics
import subprocess
import sys
import tempfile

import platform_support


REPO = Path(__file__).resolve().parent.parent
SOURCE = Path(__file__).with_name("bench_gui.c")
EXTENDED_SOURCE = Path(__file__).with_name("bench_gui_extended.c")
FONTS = ("inter95_ascii", "synthetic4096_mixed_utf8")
CASES = (
    "frontend_measured_labels_static_warm", "frontend_measured_labels_dynamic",
    "renderer_static_warm", "renderer_content_warm", "renderer_cold", "renderer_init",
)
EXTENDED_CASES = CASES + (
    "gpu_prepare_static_warm", "gpu_prepare_content_warm", "gpu_prepare_cold",
    "textarea_wrap_300", "textarea_wrap_1200", "textarea_click_drag",
)
SIGNATURE_FIELDS = ("width_checksum", "geometry_checksum", "cache_hits", "cache_misses", "glyphs")
TRIALS = 5


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def include_dir(root, header):
    if not root:
        raise RuntimeError(f"Specify the dependency root containing {header}")
    root = Path(root).resolve()
    for candidate in (root / "src", root):
        if (candidate / header).is_file():
            return candidate
    raise RuntimeError(f"{header} not found under {root}")


def compiler_environment():
    env = os.environ.copy()
    if os.name != "nt":
        return env
    if shutil.which("cl.exe", path=env.get("PATH")):
        return env
    installer = Path(env.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer"
    vswhere = shutil.which("vswhere.exe") or str(installer / "vswhere.exe")
    if not Path(vswhere).is_file():
        raise RuntimeError("Use an x64 Developer Command Prompt, or install Visual Studio C++ tools and vswhere")
    result = subprocess.run([
        vswhere, "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
    ], check=True, capture_output=True, text=True)
    installation = result.stdout.strip()
    if not installation:
        raise RuntimeError("vswhere found no Visual Studio C++ installation")
    developer = Path(installation) / "Common7" / "Tools" / "VsDevCmd.bat"
    if not developer.is_file():
        raise RuntimeError(f"Missing {developer}")
    # Paths are passed through environment variables, never interpolated as commands.
    env["RG_GUI_BENCH_VSDEVCMD"] = str(developer)
    with tempfile.TemporaryDirectory(prefix="rg_gui_bench_") as temporary:
        bootstrap = Path(temporary) / "environment.bat"
        bootstrap.write_text(
            '@echo off\ncall "%RG_GUI_BENCH_VSDEVCMD%" -arch=x64 -host_arch=x64 >nul\n'
            'if errorlevel 1 exit /b 1\nset\n', encoding="utf-8")
        env["RG_GUI_BENCH_ENV_SCRIPT"] = str(bootstrap)
        result = subprocess.run(
            [env.get("COMSPEC", "cmd.exe"), "/d", "/c", '"%RG_GUI_BENCH_ENV_SCRIPT%"'],
            env=env, check=True, capture_output=True, text=True)
    for line in result.stdout.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            env[name] = value
    if not shutil.which("cl.exe", path=env.get("PATH")):
        raise RuntimeError("VsDevCmd did not provide cl.exe")
    return env


def sdl_paths(args):
    root = Path(args.sdl_root).resolve() if args.sdl_root else None
    include = Path(args.sdl_include).resolve() if args.sdl_include else root / "include" if root else None
    library = Path(args.sdl_lib).resolve() if args.sdl_lib else None
    if library is None and root:
        library = next((p for p in (root / "lib/x64/SDL3.lib", root / "lib/SDL3.lib") if p.is_file()), None)
    if not include or not (include / "SDL3/SDL.h").is_file() or not library or not library.is_file():
        raise RuntimeError("Set --sdl-root, or --sdl-include and --sdl-lib (path to SDL3.lib)")
    return include, library


def linux_sdl_flags(args, env):
    if args.sdl_root:
        root = Path(args.sdl_root).resolve()
        folders = [root / suffix for suffix in ("lib/pkgconfig", "lib64/pkgconfig", "share/pkgconfig")]
        env["PKG_CONFIG_PATH"] = os.pathsep.join([*(str(p) for p in folders if p.is_dir()),
                                                 *([env["PKG_CONFIG_PATH"]] if env.get("PKG_CONFIG_PATH") else [])])
    def flags(kind):
        result = subprocess.run(["pkg-config", kind, "sdl3"], env=env, check=True, text=True, capture_output=True)
        return shlex.split(result.stdout)
    cflags = ["-isystem", str(Path(args.sdl_include).resolve())] if args.sdl_include else flags("--cflags")
    libs = [str(Path(args.sdl_lib).resolve())] if args.sdl_lib else flags("--libs")
    return cflags, libs


def build(gui_root, args, output):
    source = EXTENDED_SOURCE if args.suite == "extended" else SOURCE
    gui = include_dir(gui_root, "rg_gui_renderer.h")
    core = include_dir(args.core_root, "rg_defs.h")
    text = include_dir(args.text_root, "rg_text.h")
    env = compiler_environment()
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        sdl_include, sdl_lib = sdl_paths(args)
        compiler = [shutil.which("cl.exe", path=env.get("PATH"))]
        command = [
            *compiler, "/nologo", "/std:c11", "/W4", "/WX", "/O2", "/DNDEBUG", "/D_CRT_SECURE_NO_WARNINGS",
            "/I", str(gui), "/I", str(core), "/I", str(text), "/I", str(sdl_include),
            *["/D" + define for define in args.define], str(source), str(sdl_lib),
            "/Fo:" + str(output.with_suffix(".obj")), "/Fe:" + str(output),
        ]
    else:
        compiler = shlex.split(getattr(args, "cc", None) or env.get("CC", "gcc"))
        if not compiler or not shutil.which(compiler[0], path=env.get("PATH")):
            raise RuntimeError("C compiler unavailable; install GCC or set CC")
        compiler[0] = shutil.which(compiler[0], path=env.get("PATH"))
        cflags, libs = linux_sdl_flags(args, env)
        command = [*compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2", "-DNDEBUG",
                   "-I", str(gui), "-isystem", str(core), "-isystem", str(text), *cflags,
                   *["-D" + define for define in args.define], str(source), *libs, "-lm", "-o", str(output)]
    result = subprocess.run(command, cwd=REPO, env=env, text=True, capture_output=True)
    output.with_suffix(".build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Build failed; see {output.with_suffix('.build.log')}\n{result.stdout}{result.stderr}")
    compiler_info = subprocess.run(compiler if os.name == "nt" else [*compiler, "--version"],
                                   env=env, capture_output=True, text=True)
    harness_sources = [source]
    if source.name in ("bench_gui_extended.c", "bench_unicode.c"):
        harness_sources.append(Path(__file__).with_name("bench_gui.c"))
    write_json(output.with_suffix(".build.json"), {
        "command": command, "compiler_version": (compiler_info.stdout + compiler_info.stderr).strip(),
        "source_sha256": digest(source), "executable_sha256": digest(output),
        "harness_sources": {p.name: digest(p) for p in harness_sources},
        "sdl_runtime_candidate": runtime_record(args, output.parent),
        "runner_sources": {p.name: digest(p) for p in (Path(__file__), Path(platform_support.__file__))},
        "headers": {str(folder): {p.name: digest(p) for p in sorted(folder.glob("*.h"))}
                    for folder in (gui, core, text)},
    })
    print(f"Built {output}", flush=True)
    return output


def pin_cpu_zero():
    # Legacy entry point; restricted Linux CPU sets need not contain CPU zero.
    return platform_support.cpu_metadata(pin=True)


def runtime_environment(args, bin_dir=REPO):
    return platform_support.runtime_environment(getattr(args, "sdl_bin", None) or bin_dir,
                                                getattr(args, "sdl_root", None))[0]


def runtime_record(args, bin_dir=REPO):
    _, runtime = platform_support.runtime_environment(getattr(args, "sdl_bin", None) or bin_dir,
                                                       getattr(args, "sdl_root", None))
    return {"path": str(runtime), "sha256": digest(runtime)} if runtime else None


def has_define(command, define):
    return any(prefix + define in command for prefix in ("/D", "-D"))


def comparable_build_command(command, varying_define):
    result = []
    skip_next = False
    for value in command:
        if skip_next:
            skip_next = False
        elif value == "-o":
            skip_next = True
        elif not value.startswith(("/D" + varying_define + "=", "-D" + varying_define + "=", "/Fo:", "/Fe:")):
            result.append(value)
    return result


def checked_rows(stdout, cases=CASES):
    rows = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    fonts, samples, results = {}, {}, {}
    for row in rows:
        kind = row.get("kind")
        if kind == "font":
            key = row["font"]
            if key in fonts:
                raise RuntimeError(f"Duplicate font metadata: {key}")
            fonts[key] = row
        elif kind in ("samples", "result"):
            key = (row["font"], row["case"])
            target = samples if kind == "samples" else results
            if key in target:
                raise RuntimeError(f"Duplicate {kind}: {key}")
            target[key] = row
        else:
            raise RuntimeError(f"Unknown JSON row: {row}")
    expected = {(font, case) for font in FONTS for case in cases}
    if set(fonts) != set(FONTS) or set(samples) != expected or set(results) != expected:
        raise RuntimeError(f"Incomplete benchmark output: expected two fonts and {len(expected)} sample/result pairs")
    for key in expected:
        sample = samples[key]
        values = sample["ns_per_frame"]
        labels = 1 if key[1].startswith("textarea_") else 128
        if sample["labels"] != labels or len(values) != 7 or sample["frames_per_sample"] < 2:
            raise RuntimeError(f"Unexpected workload size: {key}")
        if key[1] == "frontend_measured_labels_dynamic" and sample["frames_per_sample"] % 16:
            raise RuntimeError("Dynamic labels must complete the entire fixed 16-frame cycle")
        for value in values + [results[key]["median_ns_per_frame"]]:
            if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                raise RuntimeError(f"Invalid timing: {key}")
        if abs(statistics.median(values) - results[key]["median_ns_per_frame"]) > 0.002:
            raise RuntimeError(f"Median does not match raw samples: {key}")
    signature = {
        "fonts": fonts,
        "cases": {"/".join(key): {name: row[name] for name in SIGNATURE_FIELDS} for key, row in results.items()},
    }
    return list(results.values()), signature


def compare(args):
    source = EXTENDED_SOURCE if args.suite == "extended" else SOURCE
    cases = tuple(args.case) if args.case else EXTENDED_CASES if args.suite == "extended" else CASES
    if args.case and (args.suite != "extended" or len(set(cases)) != len(cases)):
        raise RuntimeError("Case selection requires the extended suite and distinct case names")
    output = args.output.resolve()
    # Refuse to mix old and new samples, including when a previous run failed.
    output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    for variant in ("before", "after"):
        supplied = getattr(args, variant + "_bin")
        binaries[variant] = Path(supplied).resolve() if supplied else build(
            getattr(args, variant + "_root"), args, output / "build" / platform_support.executable_name(variant))
        if not binaries[variant].is_file():
            raise RuntimeError(f"Missing benchmark executable: {binaries[variant]}")
    env = runtime_environment(args)
    env.pop("RG_GUI_BENCH_CASES", None)
    env.pop("RG_GUI_BENCH_VERIFY", None)
    if args.case:
        env["RG_GUI_BENCH_CASES"] = ",".join(cases)
    manifest = {
        "status": "incomplete", "started_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "processor": platform.processor(),
        "trials": TRIALS, "suite": args.suite, "cases": cases,
        "source_sha256": digest(source), "harness_sources": {p.name: digest(p) for p in (SOURCE, source)},
        "font_sha256": digest(REPO / "examples/assets/inter_medium_16.font"),
        "binaries": {name: {"path": str(path), "sha256": digest(path)} for name, path in binaries.items()},
    }
    for name, path in binaries.items():
        metadata = path.with_suffix(".build.json")
        if metadata.is_file():
            build_metadata = json.loads(metadata.read_text(encoding="utf-8"))
            if build_metadata.get("executable_sha256") != manifest["binaries"][name]["sha256"]:
                raise RuntimeError(f"Build metadata does not match executable: {path}")
            manifest["binaries"][name]["build"] = build_metadata
    manifest["environment"] = platform_support.environment_metadata()
    manifest["sdl_runtime_candidate"] = runtime_record(args)
    manifest["cpu"] = pin_cpu_zero()
    manifest["cpu_affinity_mask"] = manifest["cpu"]["process_affinity_mask"]
    write_json(output / "manifest.json", manifest)
    results, expected_signature = [], None
    for trial in range(TRIALS):
        order = ("before", "after") if trial % 2 == 0 else ("after", "before")
        for variant in order:
            process = subprocess.run([str(binaries[variant])], cwd=REPO, env=env, capture_output=True, text=True)
            (output / f"{variant}_{trial + 1}.jsonl").write_text(process.stdout, encoding="utf-8")
            (output / f"{variant}_{trial + 1}.stderr.txt").write_text(process.stderr, encoding="utf-8")
            if process.returncode:
                raise RuntimeError(f"{variant} trial {trial + 1} exited {process.returncode}: {process.stderr}")
            rows, signature = checked_rows(process.stdout, cases)
            if expected_signature is not None and signature != expected_signature:
                raise RuntimeError(f"Workload/output signature mismatch in {variant} trial {trial + 1}; inspect saved JSONL")
            expected_signature = signature
            results.extend(dict(variant=variant, trial=trial + 1, **row) for row in rows)
            write_json(output / "results.json", results)
            print(f"Completed {variant} trial {trial + 1}/{TRIALS}", flush=True)
    summary = []
    for font in FONTS:
        for case in cases:
            row = {"font": font, "case": case}
            for variant in ("before", "after"):
                values = [r["median_ns_per_frame"] for r in results
                          if r["font"] == font and r["case"] == case and r["variant"] == variant]
                row[variant] = {"median_ns": statistics.median(values), "min_ns": min(values), "max_ns": max(values)}
            before, after = row["before"]["median_ns"], row["after"]["median_ns"]
            row.update(speedup=before / after, time_reduction_percent=(1.0 - after / before) * 100.0)
            summary.append(row)
    write_json(output / "summary.json", summary)
    write_json(output / "signatures.json", expected_signature)
    lines = ["CPU-only benchmark: median of five process medians; times in microseconds.", "",
             "| Font | Case | Before | After | Speedup | Time reduction |",
             "| --- | --- | ---: | ---: | ---: | ---: |"]
    for row in summary:
        lines.append(f"| {row['font']} | {row['case']} | {row['before']['median_ns']/1000:.3f} | "
                     f"{row['after']['median_ns']/1000:.3f} | {row['speedup']:.3f}x | {row['time_reduction_percent']:.1f}% |")
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest["status"] = "complete"
    manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
    write_json(output / "manifest.json", manifest)
    print("\n".join(lines))
    print(f"\nRaw samples, signatures, build provenance, and summary: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="Compile one variant without running it")
    build_parser.add_argument("--gui-root", required=True, type=Path)
    build_parser.add_argument("--output", required=True, type=Path, help="Output executable path")
    compare_parser = commands.add_parser("compare", help="Build if needed, then run five alternating trials")
    for variant in ("before", "after"):
        group = compare_parser.add_mutually_exclusive_group(required=True)
        group.add_argument(f"--{variant}-root", type=Path, help="GUI repository or src directory")
        group.add_argument(f"--{variant}-bin", type=Path, help="Previously compiled benchmark executable")
    compare_parser.add_argument("--output", type=Path, required=True, help="New directory for this comparison")
    compare_parser.add_argument("--case", action="append", choices=EXTENDED_CASES,
                                help="Run only this extended-suite case; repeat for several cases")
    for command in (build_parser, compare_parser):
        command.add_argument("--cc", default=os.environ.get("CC", "gcc"), help="Linux C compiler (default CC or gcc)")
        command.add_argument("--suite", choices=("labels", "extended"), default="labels")
        command.add_argument("--define", action="append", default=[], help="Additional build macro, e.g. RG_GUI_BENCH_LOOKUP=1")
        command.add_argument("--core-root", default=os.environ.get("RG_CORE_DIR", REPO.parent / "rg_core"), type=Path)
        command.add_argument("--text-root", default=os.environ.get("RG_TEXT_DIR", REPO.parent / "rg_text"), type=Path)
        command.add_argument("--sdl-root", default=os.environ.get("SDL3_DIR"), type=Path)
        command.add_argument("--sdl-include", default=os.environ.get("SDL3_INCLUDE_DIR"), type=Path)
        command.add_argument("--sdl-lib", type=Path, help="Full path to SDL3 library (Linux defaults to pkg-config sdl3)")
        command.add_argument("--sdl-bin", default=os.environ.get("SDL3_BIN_DIR"), type=Path, help="SDL3 runtime library directory if needed")
    args = parser.parse_args()
    try:
        if args.command == "build":
            build(args.gui_root, args, args.output)
        else:
            compare(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"benchmark error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
