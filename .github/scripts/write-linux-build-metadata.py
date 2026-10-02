#!/usr/bin/env python3
"""Bind a Linux executable to the command, sources, and dependencies that built it."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command_output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    executable = Path(sys.argv[1]).resolve()
    command = sys.argv[2:]
    root = Path(__file__).resolve().parents[2]
    headers = {}
    for name, directory in (
        ("rg_gui", root),
        ("rg_core", Path(os.environ["RG_CORE_DIR"]).resolve()),
        ("rg_text", Path(os.environ["RG_TEXT_DIR"]).resolve()),
    ):
        contents = {}
        for subtree in ("src", "examples", "tests", "shaders") if name == "rg_gui" else ("src",):
            for path in sorted((directory / subtree).rglob("*")):
                if path.is_file() and path.suffix in (".h", ".c", ".hlsl"):
                    contents[str(path.relative_to(directory))] = digest(path)
        headers[name] = {"directory": str(directory), "files": contents}
    compiler = shutil.which(command[0]) or command[0]
    metadata = {
        "executable": str(executable),
        "executable_sha256": digest(executable),
        "compiler": {"path": compiler, "version": command_output(compiler, "--version")},
        "compile_command": command,
        "working_directory": str(root),
        "source_files": headers,
        "sdl": {
            "pkg_config_version": command_output("pkg-config", "--modversion", "sdl3"),
            "cflags": command_output("pkg-config", "--cflags", "sdl3"),
            "libs": command_output("pkg-config", "--libs", "sdl3"),
        },
    }
    Path(str(executable) + ".build.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
