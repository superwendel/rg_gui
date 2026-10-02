"""Shared, stdlib-only Linux/Windows benchmark environment helpers."""

import ctypes
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess


def executable_name(stem):
    return stem + (".exe" if os.name == "nt" else "")


def _command_output(command, env=None):
    try:
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
        return result.stdout.strip() if result.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


def _read(path):
    try:
        return Path(path).read_text(encoding="utf-8").strip()
    except OSError:
        return None


def runtime_environment(bin_dir, sdl_root=None):
    """Return child environment and an SDL runtime candidate (not a module trace)."""
    env = ({name.upper(): value for name, value in os.environ.items()}
           if os.name == "nt" else os.environ.copy())
    folders = [Path(bin_dir).resolve()]
    if sdl_root:
        folders += [Path(sdl_root).resolve() / suffix for suffix in ("lib/x64", "bin", "lib", "lib64")]
    if env.get("SDL3_BIN_DIR"):
        folders.append(Path(env["SDL3_BIN_DIR"]).resolve())
    folders = list(dict.fromkeys(folder for folder in folders if folder.is_dir()))
    key = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
    env[key] = os.pathsep.join([*(str(folder) for folder in folders), *([env[key]] if env.get(key) else [])])
    if os.name == "nt":
        return env, next((folder / "SDL3.dll" for folder in folders if (folder / "SDL3.dll").is_file()), None)
    candidates = folders + [Path(folder) for folder in env.get(key, "").split(os.pathsep) if folder]
    libdir = _command_output(["pkg-config", "--variable=libdir", "sdl3"], env)
    if libdir:
        candidates.append(Path(libdir))
    for folder in dict.fromkeys(candidates):
        for pattern in ("libSDL3.so.0", "libSDL3.so", "libSDL3.so.*"):
            runtime = next((path for path in sorted(folder.glob(pattern)) if path.is_file()), None)
            if runtime:
                return env, runtime.resolve()
    cache = _command_output([shutil.which("ldconfig") or "/sbin/ldconfig", "-p"], env) or ""
    for line in cache.splitlines():
        match = re.match(r"\s*libSDL3\.so(?:\.\d+)*\s.*=>\s*(.+)$", line)
        if match and Path(match[1]).is_file():
            return env, Path(match[1]).resolve()
    return env, None


def cpu_metadata(pin=False):
    """Optionally pin this process and inherited children to its first allowed CPU."""
    model = platform.processor()
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.GetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t))
        kernel.GetProcessAffinityMask.restype = ctypes.c_int
        kernel.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        kernel.SetProcessAffinityMask.restype = ctypes.c_int
        handle = kernel.GetCurrentProcess()
        process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
        if not kernel.GetProcessAffinityMask(handle, ctypes.byref(process_mask), ctypes.byref(system_mask)):
            raise ctypes.WinError(ctypes.get_last_error())
        allowed = [cpu for cpu in range(ctypes.sizeof(process_mask) * 8) if process_mask.value & (1 << cpu)]
        selected = min(allowed) if pin else None
        if pin and not kernel.SetProcessAffinityMask(handle, 1 << selected):
            raise ctypes.WinError(ctypes.get_last_error())
        mask = (1 << selected) if pin else process_mask.value
        system = system_mask.value
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                model = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
        except OSError:
            pass
    else:
        if not hasattr(os, "sched_getaffinity"):
            if pin:
                raise RuntimeError("CPU affinity requires Linux or Windows")
            allowed = list(range(os.cpu_count() or 1))
        else:
            allowed = sorted(os.sched_getaffinity(0))
        selected = min(allowed) if pin else None
        if pin:
            os.sched_setaffinity(0, {selected})
        mask = sum(1 << cpu for cpu in ([selected] if pin else allowed))
        system = None
        for line in (_read("/proc/cpuinfo") or "").splitlines():
            name, separator, value = line.partition(":")
            if separator and name.strip() in ("model name", "Hardware"):
                model = value.strip()
                break
    return {"model": model, "logical_processors": os.cpu_count(), "selected_cpu": selected,
            "allowed_cpus_before": allowed, "process_affinity_mask": mask, "system_affinity_mask": system,
            "affinity_policy": f"CPU {selected}; inherited by children" if pin else "unchanged; OS scheduling"}


def environment_metadata():
    """Best-effort host context; unavailable details are explicitly left empty."""
    result = {"platform": platform.platform(), "machine": platform.machine(),
              "session": {key: os.environ.get(key) for key in (
                  "XDG_SESSION_TYPE", "DISPLAY", "WAYLAND_DISPLAY", "SDL_VIDEODRIVER", "SDL_GPU_DRIVER")},
              "sdl_pkg_config_version": _command_output(["pkg-config", "--modversion", "sdl3"])}
    if os.name != "nt":
        result["cpu_frequency"] = {path.name: {name: _read(path / name) for name in (
            "scaling_driver", "scaling_governor", "energy_performance_preference", "scaling_min_freq", "scaling_max_freq")}
            for path in sorted(Path("/sys/devices/system/cpu/cpufreq").glob("policy*"))}
        result["power_supply"] = {path.name: {name: _read(path / name) for name in ("type", "online", "status")}
                                  for path in sorted(Path("/sys/class/power_supply").glob("*"))}
        result["power_profile"] = _command_output(["powerprofilesctl", "get"])
        result["display_configuration"] = _command_output(["xrandr", "--current"])
        result["drm_devices"] = {path.name: {"vendor": _read(path / "device/vendor"), "device": _read(path / "device/device"),
                                           "driver": str((path / "device/driver").resolve())}
                                 for path in sorted(Path("/sys/class/drm").glob("card[0-9]*")) if "-" not in path.name}
    return result
