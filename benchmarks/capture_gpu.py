"""Capture GPU timestamp counters using qrenderdoc --python (Python 3.6+).

Required environment: RG_GUI_CAPTURE_EXE, RG_GUI_CAPTURE_ARGS,
RG_GUI_CAPTURE_OUTPUT (a new directory). Optional: RG_GUI_CAPTURE_FRAME=180,
RG_GUI_CAPTURE_COUNT=3, RG_GUI_CAPTURE_TIMEOUT=180. Run from the repository.
RG_GUI_CAPTURE_REPLAY_DIR can instead reuse captures from a previous run.

This script runs inside RenderDoc, which already initializes the replay API.
It always raises SystemExit, including on failure, to suppress the main UI.
Check status.json, not qrenderdoc's exit code: qrenderdoc may return zero after
SystemExit(1). Native driver calls cannot be preempted by this polling timeout;
the caller should also impose an overall process timeout.

API definitions verified against RenderDoc v1.44:
https://github.com/baldurk/renderdoc/blob/v1.44/renderdoc/api/replay/renderdoc_replay.h
https://github.com/baldurk/renderdoc/blob/v1.44/renderdoc/api/replay/control_types.h
https://github.com/baldurk/renderdoc/blob/v1.44/renderdoc/api/replay/data_types.h
"""

import ctypes
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time
import traceback


REPLAYS = 5
# qrenderdoc compiles this file into an existing namespace without __file__.
# Its code object's filename still contains the exact --python path.
SCRIPT_PATH = Path(globals().get("__file__", sys._getframe().f_code.co_filename)).resolve()
REPO = SCRIPT_PATH.parent.parent
LOG = None
OUTPUT = None


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def log(message):
    line = "{} {}\n".format(utc_now(), message)
    if LOG is not None:
        LOG.write(line)
        LOG.flush()
    else:
        sys.stderr.write(line)


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    os.replace(str(temporary), str(path))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def describe_file(path):
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": sha256(path)}


def positive_environment(name, default, maximum):
    value = int(os.environ.get(name, str(default)))
    if not 1 <= value <= maximum:
        raise ValueError("{} must be between 1 and {}".format(name, maximum))
    return value


class ChildProcess:
    """Hold a handle to the process this invocation created, never a reused PID."""

    def __init__(self, pid):
        if os.name != "nt":
            raise RuntimeError("This capture runner currently requires Windows")
        from ctypes import wintypes
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
        self.api.GetExitCodeProcess.restype = wintypes.BOOL
        self.api.TerminateProcess.argtypes = (wintypes.HANDLE, wintypes.UINT)
        self.api.TerminateProcess.restype = wintypes.BOOL
        self.api.CloseHandle.argtypes = (wintypes.HANDLE,)
        self.api.CloseHandle.restype = wintypes.BOOL
        self.handle = self.api.OpenProcess(0x00100000 | 0x1000 | 0x0001, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        self.pid = pid

    def exited(self):
        result = self.api.WaitForSingleObject(self.handle, 0)
        if result == 0:
            return True
        if result == 258:
            return False
        raise ctypes.WinError(ctypes.get_last_error())

    def exit_code(self):
        from ctypes import wintypes
        code = wintypes.DWORD()
        if not self.api.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value

    def close(self):
        if not self.exited():
            log("Stopping only this runner's unfinished child PID {}".format(self.pid))
            if not self.api.TerminateProcess(self.handle, 1):
                log("Child termination failed: {}".format(ctypes.get_last_error()))
        self.api.CloseHandle(self.handle)


def capture_frames(rd, executable, arguments, output, frame, count, timeout):
    log("Launching {} {}".format(executable, arguments))
    options = rd.GetDefaultCaptureOptions()
    result = rd.ExecuteAndInject(str(executable), str(REPO), arguments, [],
                                str(output / "capture"), options, False)
    if result.result != rd.ResultCode.Succeeded or not result.ident:
        raise RuntimeError("ExecuteAndInject failed: {}".format(result.result))
    target = None
    child = None
    captures = {}
    expected = set(range(frame, frame + count))
    deadline = time.monotonic() + timeout
    try:
        # This is a freshly created target; do not evict another controller.
        target = rd.CreateTargetControl("", result.ident, "rg_gui GPU benchmark", False)
        if target is None or not target.Connected():
            raise RuntimeError("Cannot connect to newly launched RenderDoc target")
        child = ChildProcess(int(target.GetPID()))
        log("Connected to PID {}; queuing frames {}..{}".format(child.pid, frame, frame + count - 1))
        target.QueueCapture(frame, count)
        while True:
            if time.monotonic() >= deadline:
                raise RuntimeError("Capture/child-exit timeout after {} seconds; received {} of {} frames".format(
                    timeout, len(captures), count))
            if not target.Connected():
                break
            message = target.ReceiveMessage(None)
            if message.type == rd.TargetControlMessageType.NewCapture:
                data = message.newCapture
                number = int(data.frameNumber)
                path = Path(str(data.path)).resolve()
                if not data.local or path.parent != output:
                    raise RuntimeError("Unexpected nonlocal capture path: {}".format(path))
                if number not in expected or number in captures:
                    raise RuntimeError("Unexpected or duplicate captured frame {}".format(number))
                requested_api = os.environ.get("SDL_GPU_DRIVER", "").lower()
                if requested_api in ("vulkan", "d3d12") and str(data.api).lower() != requested_api:
                    raise RuntimeError("Requested SDL GPU backend {} but captured {}".format(requested_api, data.api))
                if not path.is_file() or path.stat().st_size == 0:
                    raise RuntimeError("Capture notification has no complete file: {}".format(path))
                captures[number] = {"frame_number": number, "capture_id": int(data.captureId),
                                    "api": str(data.api), "file": describe_file(path)}
                log("Captured frame {}: {}".format(number, path.name))
                write_json(output / "captures.json", list(captures.values()))
            elif message.type == rd.TargetControlMessageType.Busy:
                raise RuntimeError("Target busy: {}".format(message.busy.clientName))
            elif message.type == rd.TargetControlMessageType.RegisterAPI:
                log("Target API {}: supported={}".format(message.apiUse.name, message.apiUse.supported))
                if not message.apiUse.supported:
                    raise RuntimeError("Unsupported capture API: {}".format(message.apiUse.supportMessage))
            elif message.type == rd.TargetControlMessageType.Disconnected:
                break
            # ReceiveMessage pumps the target connection and has a bounded wait.
        while not child.exited():
            if time.monotonic() >= deadline:
                raise RuntimeError("Target disconnected without exiting before the timeout")
            time.sleep(0.01)
        code = child.exit_code()
        log("Target exited with code {}".format(code))
        if code:
            raise RuntimeError("Captured executable failed with exit code {}".format(code))
        if set(captures) != expected:
            raise RuntimeError("Missing capture frames {}; QueueCapture may have arrived after the requested frame".format(
                sorted(expected - set(captures))))
        return [captures[number] for number in sorted(captures)]
    finally:
        if target is not None:
            target.Shutdown()
        if child is not None:
            child.close()


def actions_by_event(rd, controller):
    events = {}
    structured = controller.GetStructuredFile()

    def visit(action, groups):
        name = str(action.GetName(structured))
        children = list(action.children)
        flags = action.flags
        category = "other"
        if children:
            category = "parent_excluded"
        elif flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch):
            category = "draw_dispatch"
        elif flags & (rd.ActionFlags.Copy | rd.ActionFlags.Resolve):
            category = "copy_resolve"
        elif flags & rd.ActionFlags.Clear:
            category = "clear"
        event_id = int(action.eventId)
        if event_id in events:
            raise RuntimeError("Duplicate action event {}".format(event_id))
        events[event_id] = {"event_id": event_id, "name": name, "groups": groups,
                            "flags": int(flags), "category": category,
                            "child_count": len(children)}
        for child in children:
            visit(child, groups + [name])

    for root in controller.GetRootActions():
        visit(root, [])
    return events


def distribution(values):
    return {"samples": values, "median": statistics.median(values),
            "minimum": min(values), "maximum": max(values)}


def replay_capture(rd, capture, output):
    path = Path(capture["file"]["path"])
    log("Opening frame {} for replay".format(capture["frame_number"]))
    cap = rd.OpenCaptureFile()
    controller = None
    try:
        result = cap.OpenFile(str(path), "", None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot open capture: {}".format(result))
        if not cap.LocalReplaySupport():
            raise RuntimeError("Capture does not support local replay")
        result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        if result != rd.ResultCode.Succeeded or controller is None:
            raise RuntimeError("Cannot replay capture: {}".format(result))
        counter = rd.GPUCounter.EventGPUDuration
        available = controller.EnumerateCounters()
        if counter not in available:
            raise RuntimeError("EventGPUDuration is not supported on this replay device")
        description = controller.DescribeCounter(counter)
        if description.resultType != rd.CompType.Float or description.resultByteWidth != 8 or description.unit != rd.CounterUnit.Seconds:
            raise RuntimeError("Unexpected GPU duration counter type/width/unit: {}/{}/{}".format(
                description.resultType, description.resultByteWidth, description.unit))
        events = actions_by_event(rd, controller)
        write_json(output / "frame-{}-actions.json".format(capture["frame_number"]),
                   {"actions": [events[key] for key in sorted(events)],
                    "counter": {"id": int(counter), "name": str(description.name),
                                "description": str(description.description),
                                "unit": str(description.unit), "type": str(description.resultType),
                                "byte_width": int(description.resultByteWidth)}})
        expected_draws = set(key for key, event in events.items() if event["category"] == "draw_dispatch")
        if not expected_draws:
            raise RuntimeError("Capture has no leaf draw or dispatch actions")
        samples = []
        baseline_event_ids = None
        for replay in range(REPLAYS):
            log("Frame {} GPU counter replay {}/{}".format(capture["frame_number"], replay + 1, REPLAYS))
            values = {}
            invalid = []
            seen = set()
            raw_results = list(controller.FetchCounters([counter]))
            write_json(output / "frame-{}-replay-{}-raw.json".format(capture["frame_number"], replay + 1),
                       [{"event_id": int(value.eventId), "counter": int(value.counter),
                         "seconds": float(value.value.d) if math.isfinite(float(value.value.d)) else str(value.value.d)}
                        for value in raw_results])
            fatal_status = controller.GetFatalErrorStatus()
            write_json(output / "frame-{}-replay-{}-status.json".format(capture["frame_number"], replay + 1),
                       {"fatal_error_status": str(fatal_status), "result_count": len(raw_results),
                        "renderdoc_log": rd.GetLogFile()})
            if fatal_status != rd.ResultCode.Succeeded:
                raise RuntimeError("GPU counter collection failed: {}".format(fatal_status))
            for value in raw_results:
                event_id = int(value.eventId)
                seconds = float(value.value.d)
                if int(value.counter) != int(counter) or event_id in seen:
                    raise RuntimeError("Unexpected or duplicate GPU counter result")
                seen.add(event_id)
                if event_id not in events:
                    raise RuntimeError("GPU duration references unknown event {}".format(event_id))
                if not math.isfinite(seconds) or seconds < 0:
                    invalid.append({"event_id": event_id, "invalid_value": str(seconds)})
                    continue
                values[event_id] = seconds
            if expected_draws - set(values):
                raise RuntimeError("GPU timestamps missing for draw/dispatch events {}".format(sorted(expected_draws - set(values))))
            if baseline_event_ids is None:
                baseline_event_ids = set(values)
            elif set(values) != baseline_event_ids:
                raise RuntimeError("GPU counter coverage changed between replay passes")
            totals = {}
            for category in ("draw_dispatch", "copy_resolve", "clear", "other"):
                members = [seconds for key, seconds in values.items() if events[key]["category"] == category]
                totals[category] = {"measured_events": len(members), "sum_seconds": sum(members) if members else None}
            samples.append({"replay": replay + 1, "totals": totals,
                            "invalid_results": invalid,
                            "event_seconds": {str(key): values[key] for key in sorted(values)}})
            write_json(output / "frame-{}-partial.json".format(capture["frame_number"]), samples)
        by_group = {}
        for event in events.values():
            if event["category"] != "parent_excluded":
                group = " / ".join(event["groups"]) or "(ungrouped)"
                key = (group, event["category"])
                by_group.setdefault(key, []).append(event["event_id"])
        groups = []
        for (name, category), ids in sorted(by_group.items()):
            measured = [key for key in ids if key in baseline_event_ids]
            groups.append({"name": name, "category": category, "action_count": len(ids),
                           "measured_count": len(measured),
                           "sum_seconds": distribution([sum(s["event_seconds"][str(key)] for key in measured)
                                                        for s in samples]) if measured else None})
        summaries = {}
        for category in ("draw_dispatch", "copy_resolve", "clear", "other"):
            numbers = [sample["totals"][category]["sum_seconds"] for sample in samples]
            summaries[category] = distribution(numbers) if numbers[0] is not None else None
        capture.update({"counter": {"id": int(counter), "name": str(description.name),
                                    "description": str(description.description), "unit": "seconds",
                                    "result_type": "float64"},
                        "available_counter_ids": [int(item) for item in available],
                        "actions": [events[key] for key in sorted(events)],
                        "untimed_event_ids": sorted(set(events) - baseline_event_ids),
                        "groups": groups, "replays": samples, "summary_seconds": summaries,
                        "replay_api": str(controller.GetAPIProperties().pipelineType)})
        write_json(output / "frame-{}.json".format(capture["frame_number"]), capture)
        return capture
    finally:
        if controller is not None:
            controller.Shutdown()
        cap.Shutdown()


def main():
    global LOG, OUTPUT
    output_value = os.environ.get("RG_GUI_CAPTURE_OUTPUT")
    if not output_value:
        raise ValueError("RG_GUI_CAPTURE_OUTPUT must name a new output directory")
    output = Path(output_value).resolve()
    output.mkdir(parents=True, exist_ok=False)
    OUTPUT = output
    LOG = (output / "progress.log").open("w", encoding="utf-8", buffering=1)
    log("GPU capture runner started")
    write_json(output / "status.json", {"status": "running", "started_utc": utc_now()})
    if "pyrenderdoc" not in globals():
        raise RuntimeError("Launch with qrenderdoc --python benchmarks/capture_gpu.py")
    import renderdoc as rd
    rd.SetDebugLogFile(str(output / "renderdoc.log"))
    executable = Path(os.environ["RG_GUI_CAPTURE_EXE"]).resolve()
    if not executable.is_file():
        raise ValueError("Executable not found: {}".format(executable))
    arguments = os.environ["RG_GUI_CAPTURE_ARGS"]
    frame = positive_environment("RG_GUI_CAPTURE_FRAME", 180, 1000000)
    count = positive_environment("RG_GUI_CAPTURE_COUNT", 3, 20)
    timeout = positive_environment("RG_GUI_CAPTURE_TIMEOUT", 180, 3600)
    source_paths = [REPO / "src" / name for name in ("rg_gui.h", "rg_gui_renderer.h", "rg_gui_gpu.h")]
    source_paths += sorted((REPO / "examples").glob("*.c"))
    source_paths += sorted((REPO / "examples").glob("*.h"))
    source_paths.append(SCRIPT_PATH)
    manifest = {"created_utc": utc_now(), "renderdoc_version": rd.GetVersionString(),
                "renderdoc_commit": rd.GetCommitHash(), "python": sys.version,
                "executable": describe_file(executable), "arguments": arguments,
                "working_directory": str(REPO), "capture_frame": frame, "capture_count": count,
                "environment": {name: os.environ.get(name) for name in
                                ("SDL_GPU_DRIVER", "SDL_VIDEODRIVER", "SDL_GPU_DEBUG")},
                "replays_per_capture": REPLAYS, "capture_timeout_seconds": timeout,
                "source_snapshot": [describe_file(path) for path in source_paths],
                "provenance_note": "Source hashes describe files present at capture time; executable hash identifies the actual binary.",
                "measurement": "RenderDoc replay GPU timestamps per event, not native frame time, CPU time, fence latency, or presentation wait.",
                "aggregation": "Only leaf actions are summed; draw/dispatch, copy/resolve, clear, and other are separate. Parent events excluded. Missing copy counters are unavailable, never zero.",
                "limitations": "Replay instruments GPU events and can alter scheduling/caches. Sum of event durations is not an end-to-end GPU frame duration, especially across queues."}
    write_json(output / "manifest.json", manifest)
    replay_directory = os.environ.get("RG_GUI_CAPTURE_REPLAY_DIR")
    if replay_directory:
        prior = Path(replay_directory).resolve()
        manifest["capture_manifest"] = json.loads((prior / "manifest.json").read_text(encoding="utf-8"))
        captures = json.loads((prior / "captures.json").read_text(encoding="utf-8"))
        if len(captures) != count:
            raise RuntimeError("Replay directory capture count does not match RG_GUI_CAPTURE_COUNT")
        for capture in captures:
            capture_path = Path(capture["file"]["path"]).resolve()
            if capture_path.parent != prior or sha256(capture_path) != capture["file"]["sha256"]:
                raise RuntimeError("Replay capture path or hash mismatch")
        log("Replaying {} existing captures from {}".format(len(captures), prior))
        write_json(output / "manifest.json", manifest)
        write_json(output / "captures.json", captures)
    else:
        captures = capture_frames(rd, executable, arguments, output, frame, count, timeout)
    # The native child has exited before any replay measurement begins.
    results = [replay_capture(rd, capture, output) for capture in captures]
    write_json(output / "measurements.json", {"manifest": manifest, "captures": results})
    write_json(output / "status.json", {"status": "complete", "finished_utc": utc_now(),
                                         "capture_count": len(results), "replays_per_capture": REPLAYS})
    log("Complete: {} captures, {} GPU counter replays each".format(len(results), REPLAYS))


try:
    main()
except BaseException:
    error = traceback.format_exc()
    log(error)
    if OUTPUT is not None:
        write_json(OUTPUT / "status.json", {"status": "failed", "finished_utc": utc_now(), "error": error})
    if LOG is not None:
        LOG.close()
    sys.exit(1)
else:
    if LOG is not None:
        LOG.close()
    sys.exit(0)
