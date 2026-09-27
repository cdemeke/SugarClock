#!/usr/bin/env python3
"""Local disposable-fleet simulator; fake update results never validate firmware."""

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
    request = urllib.request.Request(
        base_url.rstrip("/") + path,
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        headers={"Authorization": "Bearer " + credential, "Content-Type": "application/json",
                 "User-Agent": "SugarClock-Simulator/2"},
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.loads(response.read(64 * 1024))


def new_clock(index):
    return {"installation_id": str(uuid.uuid4()), "credential": secrets.token_urlsafe(32),
            "firmware_version": "0.2.2", "channel": "stable", "config_revision": "sim-1",
            "completed_commands": [], "started_at": int(time.time()) - index * 30,
            "registered": False}


def save_state(path, clocks):
    # Credentials are private from the moment the temporary file is created.
    temporary = path + ".tmp"
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(descriptor, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        json.dump({"clocks": clocks}, stream, indent=2)
        stream.write("\n")
    os.replace(temporary, path)


def load_state(path, count):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as stream:
            clocks = json.load(stream)["clocks"]
    else:
        clocks = []
    while len(clocks) < count:
        clocks.append(new_clock(len(clocks)))
    return clocks  # Retain identities when running a smaller selected count.


def register(base_url, clock):
    result = api(base_url, "/device/v1/register", clock["credential"], {
        "installation_id": clock["installation_id"], "hardware": HARDWARE,
        "firmware_version": clock["firmware_version"], "timezone": TIMEZONE,
        "management_protocol": 1})
    clock["registered"] = True
    return result


def process_command(clock, command):
    command_type, payload = command["type"], command["payload"]
    if command_type in {"ota_install", "ota_rollback_previous"}:
        return "failed", "requires_rollout_target"
    if command["id"] in clock["completed_commands"]:
        return "succeeded", None
    if command_type == "set_channel":
        clock["channel"] = payload["channel"]
    elif command_type == "config_patch":
        revision = int(clock["config_revision"].split("-")[-1]) + 1
        clock["config_revision"] = f"sim-{revision}"
    elif command_type == "restart":
        clock["started_at"] = int(time.time())
    elif command_type != "ota_check":
        return "failed", "unsupported_command"
    clock["completed_commands"] = (clock["completed_commands"] + [command["id"]])[-32:]
    return "succeeded", None


def report_pending(base_url, clock):
    pending = clock.get("pending_update")
    if pending:
        try:
            api(base_url, f"/device/v1/updates/{pending['target_id']}/result", clock["credential"],
                {"installation_id": clock["installation_id"], "status": "boot_validated",
                 "firmware_version": pending["version"]})
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
        clock.pop("pending_update", None)


def process_offer(base_url, clock, offer, simulate_updates=False):
    if not offer or clock.get("pending_update"):
        return False
    if not simulate_updates:
        api(base_url, f"/device/v1/updates/{offer['target_id']}/result", clock["credential"],
            {"installation_id": clock["installation_id"], "status": "deferred",
             "reason": "simulator_updates_disabled"})
        return False
    authorization = api(base_url, f"/device/v1/updates/{offer['target_id']}/authorize", clock["credential"],
                        {"installation_id": clock["installation_id"]})
    fresh = authorization.get("update_offer", {})
    if authorization.get("authorized") is not True or authorization.get("expires_at", 0) <= time.time():
        return False
    if any(fresh.get(key) != offer.get(key) for key in
           ("target_id", "manifest_url", "version", "channel", "sha256")):
        return False
    if tuple(map(int, offer["version"].split('.'))) <= tuple(map(int, clock["firmware_version"].split('.'))):
        return False
    # Deliberately fake: no image download, signature check, flash or bootloader.
    clock["firmware_version"] = offer["version"]
    clock["channel"] = offer["channel"]
    clock["pending_update"] = {"target_id": offer["target_id"], "version": offer["version"]}
    report_pending(base_url, clock)
    return True


def check_in(base_url, clock, *, legacy=False, features=False, simulate_updates=False):
    report_pending(base_url, clock)
    payload = {"installation_id": clock["installation_id"], "firmware_version": clock["firmware_version"],
               "running_partition": "ota_0", "boot_partition": "ota_0", "channel": clock["channel"],
               "timezone": TIMEZONE, "uptime_seconds": max(0, int(time.time()) - clock["started_at"]),
               "free_heap_bucket": "75k_plus", "wifi_signal_bucket": "good", "battery_percent": 84,
               "charging": True, "config_revision": clock["config_revision"], "health_codes": []}
    if not legacy:
        payload["capabilities"] = ["fleet_rollout_v1"]
        if features:
            payload["features"] = {"schema_version": 1, "data_source": "demo",
                                   "companion_enabled": True, "companion_character": 0,
                                   "weather_enabled": False}
    result = api(base_url, "/device/v1/check-in", clock["credential"], payload)
    for command in result["commands"]:
        status, reason = process_command(clock, command)
        outcome = {"installation_id": clock["installation_id"], "status": status}
        if reason:
            outcome["reason"] = reason
        if command["type"] == "config_patch":
            outcome["config_revision"] = clock["config_revision"]
        api(base_url, f"/device/v1/commands/{command['id']}/result", clock["credential"], outcome)
    if not legacy:
        process_offer(base_url, clock, result.get("update_offer"), simulate_updates)
    requested = int(result.get("next_checkin_seconds", 300))
    clock["next_checkin_at"] = time.time() + (max(90, min(150, requested)) if legacy
                                            else max(270, min(330, requested))) + random.randint(-15, 15)
    return len(result["commands"])


def visit(base_url, clock, **options):
    try:
        if not clock.get("registered"):
            register(base_url, clock)
        result = check_in(base_url, clock, **options)
        clock["failures"] = 0
        return result
    except urllib.error.HTTPError as error:
        if error.code in {401, 404}:
            clock["registered"] = False
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default="http://127.0.0.1:8080")
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--state", default="simulator-state.json")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--legacy", action="store_true", help="omit capabilities/features and use legacy cadence")
    parser.add_argument("--features", action="store_true", help="report a fixed demo feature snapshot")
    parser.add_argument("--simulate-updates", action="store_true", help="FAKE successful boots after fresh authorization; isolated test fleets only")
    args = parser.parse_args()
    if not 1 <= args.count <= 1000:
        parser.error("--count must be 1-1000; use fleet.load_test for capacity tests (registration is rate limited)")
    clocks = load_state(args.state, args.count)
    save_state(args.state, clocks)
    while True:
        checked, delivered = 0, 0
        for clock in clocks[:args.count]:
            if not args.once and clock.get("next_checkin_at", 0) > time.time():
                continue
            try:
                delivered += visit(args.server, clock, legacy=args.legacy, features=args.features,
                                   simulate_updates=args.simulate_updates)
                checked += 1
            except (OSError, ValueError, KeyError) as error:
                failures = clock["failures"] = clock.get("failures", 0) + 1
                clock["next_checkin_at"] = time.time() + min(900, 15 * 2 ** min(failures - 1, 6))
                # Do not echo arbitrary exception bodies or credential-bearing data.
                print(f"visit failed: {type(error).__name__}; HTTP {getattr(error, 'code', 'n/a')}", flush=True)
            finally:
                save_state(args.state, clocks)
        if checked:
            print(f"checked in {checked} clocks; processed {delivered} commands", flush=True)
        if args.once:
            break
        time.sleep(max(0.1, min(30, min(c.get("next_checkin_at", 0) for c in clocks[:args.count]) - time.time())))


if __name__ == "__main__":
    main()
