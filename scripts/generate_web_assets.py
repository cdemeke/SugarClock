#!/usr/bin/env python3
import os
import gzip
import hashlib
import re

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_WWW_DIR = os.path.join(ROOT_DIR, "data", "www")
INCLUDE_DIR = os.path.join(ROOT_DIR, "include")
SRC_DIR = os.path.join(ROOT_DIR, "src")

MIME_MAP = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".ico": "image/x-icon",
    ".svg": "image/svg+xml",
    ".txt": "text/plain; charset=utf-8",
}

def sanitize_ident(name):
    return re.sub(r'[^a-zA-Z0-9_]', '_', name)

def deterministic_gzip(data):
    compressed = bytearray(gzip.compress(data, compresslevel=9, mtime=0))
    # Python 3.11 delegates mtime=0 compression to zlib, whose gzip OS byte
    # varies by platform (3 on Linux, 255 on macOS). RFC 1952 allows 255 for
    # an unknown OS, so normalize it to keep generated firmware byte-identical.
    compressed[9] = 255
    return bytes(compressed)

def generate_web_assets():
    if not os.path.exists(DATA_WWW_DIR):
        print(f"Warning: {DATA_WWW_DIR} does not exist.")
        return

    assets = []
    for root, _, files in os.walk(DATA_WWW_DIR):
        for f in sorted(files):
            if f.startswith('.'):
                continue
            full_path = os.path.join(root, f)
            rel_path = "/" + os.path.relpath(full_path, DATA_WWW_DIR)
            ext = os.path.splitext(f)[1].lower()
            mime = MIME_MAP.get(ext, "application/octet-stream")

            with open(full_path, "rb") as fp:
                raw_data = fp.read()

            # mtime=0 makes identical source assets produce byte-identical
            # firmware. SHA-256 is also used for the cache tag so MD5 is not
            # present anywhere in the OTA implementation.
            gz_data = deterministic_gzip(raw_data)
            etag = hashlib.sha256(gz_data).hexdigest()[:16]
            ident = "asset_" + sanitize_ident(rel_path.lstrip("/")) + "_gz"

            assets.append({
                "rel_path": rel_path,
                "ident": ident,
                "mime": mime,
                "raw_size": len(raw_data),
                "gz_data": gz_data,
                "etag": etag
            })

    # Generate header
    header_content = """#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <stdint.h>
#include <stddef.h>

struct WebAsset {
    const char* path;
    const uint8_t* data;
    size_t size;
    const char* mime_type;
    const char* etag;
};

const WebAsset* find_web_asset(const char* path);
size_t get_web_assets_count();
const WebAsset* get_web_asset_at(size_t index);

#endif // WEB_ASSETS_H
"""

    # Generate source
    cpp_lines = [
        '#include "web_assets.h"',
        '#include <pgmspace.h>',
        '#include <string.h>',
        '',
        '// Generated gzipped web assets',
    ]

    for a in assets:
        cpp_lines.append(f'// {a["rel_path"]} (raw {a["raw_size"]} bytes, gzip {len(a["gz_data"])} bytes)')
        cpp_lines.append(f'static const uint8_t {a["ident"]}[] PROGMEM = {{')
        bytes_hex = [f'0x{b:02x}' for b in a["gz_data"]]
        for i in range(0, len(bytes_hex), 16):
            chunk = ", ".join(bytes_hex[i:i+16])
            if i + 16 < len(bytes_hex):
                chunk += ","
            cpp_lines.append("    " + chunk)
        cpp_lines.append("};\n")

    cpp_lines.append("static const WebAsset WEB_ASSETS[] = {")
    for a in assets:
        cpp_lines.append(f'    {{ "{a["rel_path"]}", {a["ident"]}, {len(a["gz_data"])}, "{a["mime"]}", "{a["etag"]}" }},')
        if a["rel_path"] == "/index.html":
            # Also map root "/" to index.html
            cpp_lines.append(f'    {{ "/", {a["ident"]}, {len(a["gz_data"])}, "{a["mime"]}", "{a["etag"]}" }},')
    cpp_lines.append("};\n")

    cpp_lines.append("static const size_t WEB_ASSETS_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);\n")

    cpp_lines.append("""const WebAsset* find_web_asset(const char* path) {
    if (!path) return nullptr;
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (strcmp(WEB_ASSETS[i].path, path) == 0) {
            return &WEB_ASSETS[i];
        }
    }
    return nullptr;
}

size_t get_web_assets_count() {
    return WEB_ASSETS_COUNT;
}

const WebAsset* get_web_asset_at(size_t index) {
    if (index < WEB_ASSETS_COUNT) {
        return &WEB_ASSETS[index];
    }
    return nullptr;
}
""")

    os.makedirs(INCLUDE_DIR, exist_ok=True)
    os.makedirs(SRC_DIR, exist_ok=True)

    header_path = os.path.join(INCLUDE_DIR, "web_assets.h")
    src_path = os.path.join(SRC_DIR, "web_assets.cpp")

    with open(header_path, "w") as fp:
        fp.write(header_content)

    with open(src_path, "w") as fp:
        fp.write("\n".join(cpp_lines))

    print(f"Generated web assets from {len(assets)} files in {DATA_WWW_DIR}")

if __name__ == "__main__":
    generate_web_assets()
