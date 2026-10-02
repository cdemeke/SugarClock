#!/usr/bin/env python3
"""
LibreLinkUp probe — verifies a LibreLinkUp follower account works with the
same API flow the SugarClock firmware uses (src/libre_client.cpp).

Usage:
    python3 scripts/libre_probe.py
    LIBRE_EMAIL=you@example.com python3 scripts/libre_probe.py

The password is read with a hidden prompt (or LIBRE_PASSWORD) and is never
printed. Output is redacted so it is safe to share.
"""
import getpass
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request

# Keep in sync with src/libre_client.cpp
LLU_VERSION = "4.16.0"
LLU_PRODUCT = "llu.android"
DEFAULT_HOST = "api.libreview.io"

TREND_NAMES = {1: "SingleDown", 2: "FortyFiveDown", 3: "Flat",
               4: "FortyFiveUp", 5: "SingleUp"}


def host_for_region(region):
    region = region.lower()
    if region == "ru":
        return "api.libreview.ru"
    if region == "cn":
        return "api-cn.myfreestyle.cn"
    return f"api-{region}.libreview.io"


def request(method, host, path, body=None, token=None, account_id=None):
    headers = {
        "product": LLU_PRODUCT,
        "version": LLU_VERSION,
        "content-type": "application/json",
        "accept": "application/json",
        "cache-control": "no-cache",
    }
    if token:
        headers["authorization"] = f"Bearer {token}"
    if account_id:
        headers["account-id"] = account_id
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"https://{host}{path}", data=data,
                                 headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            raw = resp.read()
            return resp.status, json.loads(raw), len(raw)
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw), len(raw)
        except ValueError:
            return e.code, {"raw": raw[:200].decode(errors="replace")}, len(raw)


def main():
    email = os.environ.get("LIBRE_EMAIL") or input("LibreLinkUp email: ").strip()
    password = os.environ.get("LIBRE_PASSWORD") or getpass.getpass("LibreLinkUp password (hidden): ")

    host = DEFAULT_HOST
    for _ in range(2):
        print(f"\n[1] POST https://{host}/llu/auth/login")
        code, doc, size = request("POST", host, "/llu/auth/login",
                                  {"email": email, "password": password})
        status = doc.get("status")
        data = doc.get("data") or {}
        print(f"    HTTP {code}, status={status}, {size} bytes")
        if data.get("redirect") and data.get("region"):
            host = host_for_region(data["region"])
            print(f"    -> redirect to region '{data['region']}' ({host})")
            continue
        break
    else:
        print("    Too many redirects"); return 1

    if status == 4:
        step = (data.get("step") or {}).get("type")
        print(f"    Account needs action in the LibreLinkUp app (step: {step}).")
        print("    Open LibreLinkUp, log in, and accept the terms / privacy policy.")
        return 1
    if status != 0 or "authTicket" not in data:
        err = (doc.get("error") or {}).get("message") or doc
        print(f"    Login failed: {err}")
        return 1

    token = data["authTicket"]["token"]
    user_id = data["user"]["id"]
    account_id = hashlib.sha256(user_id.encode()).hexdigest()
    print(f"    Login OK. user country={data['user'].get('country')}, "
          f"token len={len(token)}, expires={data['authTicket'].get('expires')}")

    print(f"\n[2] GET https://{host}/llu/connections")
    code, doc, size = request("GET", host, "/llu/connections",
                              token=token, account_id=account_id)
    print(f"    HTTP {code}, status={doc.get('status')}, {size} bytes")
    conns = doc.get("data") or []
    if code != 200 or not isinstance(conns, list):
        print(f"    Unexpected response: {str(doc)[:300]}")
        return 1
    print(f"    {len(conns)} connection(s)")
    if not conns:
        print("    No one is sharing with this account. In the FreeStyle LibreLink /")
        print("    Libre 3 app: Connected Apps -> LibreLinkUp -> add this email.")
        return 1

    for i, c in enumerate(conns):
        gm = c.get("glucoseMeasurement") or {}
        trend = gm.get("TrendArrow")
        print(f"    [{i}] {c.get('firstName', '?')} {c.get('lastName', '')[:1]}.  "
              f"{gm.get('ValueInMgPerDl')} mg/dL  trend={trend} ({TREND_NAMES.get(trend, '?')})  "
              f"FactoryTimestamp={gm.get('FactoryTimestamp')!r}  "
              f"sensor type={(c.get('sensor') or {}).get('pt')}")

    print("\nAll good — SugarClock can use this account.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
