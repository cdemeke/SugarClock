"""Conservatively bound whole OTA-related sources, not a task-specific call graph."""

from pathlib import Path

MAX_FRAME_BYTES = 2048
# Include every frame, even functions used only by the separate Fleet task.
# This source-file budget is deliberately stricter than a per-task analysis.
REQUIRED_REPORTS = {
    "ota_manager.cpp.su": "ota_worker(",
    "ota_manifest.cpp.su": "ota_manifest_verify_signature(",
    "fleet_manager.cpp.su": "fleet_authorize_update(",
}


def check_ota_stack(report_directory):
    largest_frames = {}
    for filename, required_function in REQUIRED_REPORTS.items():
        lines = (Path(report_directory) / filename).read_text(encoding="utf-8").splitlines()
        frames = []
        for line in lines:
            function, size, kind = line.rsplit("\t", 2)
            size = int(size)
            if "dynamic" in kind.split(",") and "bounded" not in kind.split(","):
                raise ValueError(f"unbounded stack frame in {filename}: {function}")
            if size > MAX_FRAME_BYTES:
                raise ValueError(
                    f"Source frame uses {size} bytes (OTA source-file limit {MAX_FRAME_BYTES}) "
                    f"in {filename}: {function}")
            frames.append((function, size))
        if not any(required_function in function for function, _ in frames):
            raise ValueError(f"{required_function} is missing from {filename}")
        largest_frames[filename] = max(size for _, size in frames)
    return largest_frames
