"""Bound OTA's own stack frames; hardware high-water checks still cover TLS."""

from pathlib import Path

MAX_FRAME_BYTES = 2048


def check_ota_stack(report):
    lines = Path(report).read_text(encoding="utf-8").splitlines()
    frames = []
    for line in lines:
        function, size, kind = line.rsplit("\t", 2)
        size = int(size)
        if "dynamic" in kind and "bounded" not in kind:
            raise ValueError(f"unbounded OTA stack frame: {function}")
        if size > MAX_FRAME_BYTES:
            raise ValueError(f"OTA frame uses {size} bytes (limit {MAX_FRAME_BYTES}): {function}")
        frames.append((function, size))
    if not any("ota_worker(" in function for function, _ in frames):
        raise ValueError("OTA worker is missing from the compiler stack report")
    return max(size for _, size in frames)
