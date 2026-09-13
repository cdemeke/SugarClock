"""Bound frames in OTA task sources; hardware high-water checks still cover TLS."""

from pathlib import Path

MAX_FRAME_BYTES = 2048
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
                raise ValueError(f"unbounded OTA stack frame in {filename}: {function}")
            if size > MAX_FRAME_BYTES:
                raise ValueError(
                    f"OTA frame uses {size} bytes (limit {MAX_FRAME_BYTES}) in {filename}: {function}")
            frames.append((function, size))
        if not any(required_function in function for function, _ in frames):
            raise ValueError(f"{required_function} is missing from {filename}")
        largest_frames[filename] = max(size for _, size in frames)
    return largest_frames
