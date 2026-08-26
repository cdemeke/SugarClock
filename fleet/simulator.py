#!/usr/bin/env python3
"""Small persistent simulator for exercising the fleet service without hardware."""

import argparse
import json
import os
import random
import secrets
import time
import urllib.error
import urllib.request
import uuid


HARDWARE = "ulanzi-tc001-esp32-4mb"
TIMEZONE = "EST5EDT,M3.2.0,M11.1.0"


def api(base_url, path, credential, payload):
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(
        base_url.rstrip("/") + path,
        data=body,
        headers={
            "Authorization": "Bearer " + credential,
            "Content-Type": "application/json",
            "User-Agent": "SugarClock-Simulator/1",
        },
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.loads(response.read(64 * 1024))


def new_clock(index):
    return {
        "installation_id": str(uuid.uuid4()),
        "credential": secrets.token_urlsafe(32),
        "firmware_version": "0.2.2",
        "channel": "stable",
        "config_revision": "sim-1",
        "completed_commands": [],
        "started_at": int(time.time()) - index * 30,
        "registered": False,
    }


def save_state(path, clocks):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump({"clocks": clocks}, stream, indent=2)
        stream.write("\n")
    os.replace(temporary, path)
    os.chmod(path, 0o600)


def load_state(path, count):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as stream:
            clocks = json.load(stream)["clocks"]
    else:
        clocks = []
    while len(clocks) < count:
        clocks.append(new_clock(len(clocks)))
    return clocks[:count]


def register(base_url, clock):
    result = api(
        base_url,
        "/device/v1/register",
        clock["credential"],
        {
            "installation_id": clock["installation_id"],
            "hardware": HARDWARE,
            "firmware_version": clock["firmware_version"],
            "timezone": TIMEZONE,
            "management_protocol": 1,
        },
    )
    clock["registered"] = True
    return result


def process_command(clock, command):
    if command["id"] in clock["completed_commands"]:
        return "succeeded", None
    command_type = command["type"]
    payload = command["payload"]
    if command_type == "set_channel":
        clock["channel"] = payload["channel"]
    elif command_type == "config_patch":
        revision = int(clock["config_revision"].split("-")[-1]) + 1
        clock["config_revision"] = f"sim-{revision}"
    elif command_type == "ota_install":
        if payload["channel"] != clock["channel"]:
            return "failed", "channel_mismatch"
        clock["firmware_version"] = payload["version"]
    elif command_type == "restart":
        clock["started_at"] = int(time.time())
    clock["completed_commands"] = (clock["completed_commands"] + [command["id"]])[-32:]
    return "succeeded", None


def check_in(base_url, clock):
    result = api(
        base_url,
        "/device/v1/check-in",
        clock["credential"],
        {
            "installation_id": clock["installation_id"],
            "firmware_version": clock["firmware_version"],
            "running_partition": "ota_0",
            "boot_partition": "ota_0",
            "previous_partition": "ota_1",
            "previous_partition_available": True,
            "channel": clock["channel"],
            "timezone": TIMEZONE,
            "uptime_seconds": max(0, int(time.time()) - clock["started_at"]),
            "free_heap_bucket": "75k_plus",
            "wifi_signal_bucket": "good",
            "battery_percent": 84,
            "charging": True,
            "config_revision": clock["config_revision"],
            "config_hash": "simulated",
            "health_codes": [],
        },
    )
    for command in result["commands"]:
        status, reason = process_command(clock, command)
        payload = {"installation_id": clock["installation_id"], "status": status}
        if reason:
            payload["reason"] = reason
        if command["type"] == "config_patch":
            payload["config_revision"] = clock["config_revision"]
        if command["type"] == "ota_install":
            payload["firmware_version"] = clock["firmware_version"]
        api(base_url, f"/device/v1/commands/{command['id']}/result", clock["credential"], payload)
    return len(result["commands"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default="http://127.0.0.1:8080")
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--state", default="simulator-state.json")
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.count <= 250:
        parser.error("--count must be 1-250")
    clocks = load_state(args.state, args.count)
    failures = 0
    while True:
        try:
            delivered = 0
            for clock in clocks:
                if not clock.get("registered"):
                    register(args.server, clock)
                delivered += check_in(args.server, clock)
            save_state(args.state, clocks)
            failures = 0
            print(f"checked in {len(clocks)} clocks; processed {delivered} commands", flush=True)
        except (OSError, urllib.error.HTTPError, ValueError, KeyError) as error:
            failures += 1
            print(f"check-in failed: {error}", flush=True)
        if args.once:
            break
        if failures:
            delay = min(900, 15 * (2 ** min(failures - 1, 6)))
        else:
            delay = random.randint(90, 150)
        time.sleep(delay)


if __name__ == "__main__":
    main()
