"""Latest allowlisted configuration snapshots and bounded daily fleet totals."""

import datetime
import json
from flask import current_app
from .util import ApiError, json_text, now_epoch
from .geolocation import location_summary

BOOLEAN_FEATURES = {
    "companion_enabled",
    "weather_enabled",
    "timer_enabled",
    "stopwatch_enabled",
    "notifications_enabled",
    "sysmon_enabled",
    "auto_cycle_enabled",
    "countdown_enabled",
    "night_mode_enabled",
    "auto_brightness",
    "time_display_enabled",
}


def validate_telemetry(value):
    if "capabilities" in value:
        capabilities = value["capabilities"]
        if (
            not isinstance(capabilities, list)
            or len(capabilities) > 8
            or any(
                not isinstance(x, str) or x not in {"fleet_rollout_v1"}
                for x in capabilities
            )
        ):
            raise ApiError("invalid_capabilities", "unsupported capabilities")
    if "features" not in value:
        return
    features = value["features"]
    allowed = BOOLEAN_FEATURES | {
        "schema_version",
        "data_source",
        "companion_character",
    }
    if (
        not isinstance(features, dict)
        or set(features) - allowed
        or type(features.get("schema_version")) is not int
        or features["schema_version"] != 1
    ):
        raise ApiError(
            "invalid_features", "features must use the allowlisted schema version 1"
        )
    for key, val in features.items():
        if key in BOOLEAN_FEATURES and type(val) is not bool:
            raise ApiError("invalid_features", key + " must be boolean")
    if "data_source" in features and (
        not isinstance(features["data_source"], str)
        or features["data_source"] not in {"dexcom", "libre", "custom", "demo"}
    ):
        raise ApiError("invalid_features", "unsupported data source")
    if "companion_character" in features and (
        type(features["companion_character"]) is not int
        or not 0 <= features["companion_character"] <= 6
    ):
        raise ApiError("invalid_features", "unsupported companion character")


def record(connection, device_id, value, now):
    connection.execute(
        "UPDATE devices SET last_checkin_at=?,capabilities_json=? WHERE id=?",
        (now, json_text(value.get("capabilities", [])), device_id),
    )
    if "features" in value:
        connection.execute(
            "UPDATE devices SET features_json=?,features_reported_at=? WHERE id=?",
            (json_text(value["features"]), now, device_id),
        )
    else:
        # Older firmware must not leave a misleading current snapshot after downgrade.
        connection.execute(
            "UPDATE devices SET features_json=NULL,features_reported_at=NULL WHERE id=?",
            (device_id,),
        )


def _summary_counts(rows, now):
    active = [r for r in rows if r["retired_at"] is None and r["blocked_at"] is None]
    recent_cutoff = now - 30 * 86400
    week_cutoff = now - 7 * 86400
    online_cutoff = now - 600
    counts = {
        "total": len(rows),
        "active_30d": 0,
        "active_7d": 0,
        "online": 0,
        "retired": sum(
            r["retired_at"] is not None and r["blocked_at"] is None for r in rows
        ),
        "blocked": sum(r["blocked_at"] is not None for r in rows),
        "dormant": 0,
        "registration_only": 0,
        "feature_reporting": 0,
        "legacy": 0,
    }
    for r in active:
        ts = r["last_checkin_at"]
        if not ts:
            counts["registration_only"] += 1
            continue
        if ts >= online_cutoff:
            counts["online"] += 1
        if ts >= week_cutoff:
            counts["active_7d"] += 1
        if ts >= recent_cutoff:
            counts["active_30d"] += 1
            if r["features_json"] is not None:
                counts["feature_reporting"] += 1
            if "fleet_rollout_v1" not in json.loads(r["capabilities_json"]):
                counts["legacy"] += 1
        else:
            counts["dormant"] += 1
    return counts, active


def overview(connection, now=None):
    now = now if now is not None else now_epoch()
    rows = connection.execute(
        "SELECT retired_at, blocked_at, last_checkin_at, capabilities_json, "
        "features_json, firmware_version, detected_city, detected_region, detected_country_code "
        "FROM devices"
    ).fetchall()
    counts, active = _summary_counts(rows, now)
    windows = (
        ("7", now - 7 * 86400),
        ("30", now - 30 * 86400),
        ("90", now - 90 * 86400),
        ("all", None),
    )
    sorted_features = sorted(BOOLEAN_FEATURES)
    accum = {
        key: {
            "count": 0,
            "features": {f: {"enabled": 0, "known": 0} for f in sorted_features},
            "categories": {"data_source": {}, "companion_character": {}},
            "versions": {},
            "locations": {},
        }
        for key, _ in windows
    }
    for row in active:
        ts = row["last_checkin_at"]
        if not ts:
            continue
        location = location_summary(
            row["detected_city"], row["detected_region"], row["detected_country_code"]
        )
        fw = row["firmware_version"]
        snapshot = json.loads(row["features_json"] or "{}")
        ds = str(snapshot.get("data_source", "unknown"))
        cc = str(snapshot.get("companion_character", "unknown"))
        for key, cutoff in windows:
            if cutoff is not None and ts < cutoff:
                continue
            bucket = accum[key]
            bucket["count"] += 1
            bucket["locations"][location] = bucket["locations"].get(location, 0) + 1
            bucket["versions"][fw] = bucket["versions"].get(fw, 0) + 1
            for f in sorted_features:
                if f in snapshot:
                    bucket["features"][f]["enabled"] += int(snapshot[f])
                    bucket["features"][f]["known"] += 1
            bucket["categories"]["data_source"][ds] = bucket["categories"]["data_source"].get(ds, 0) + 1
            bucket["categories"]["companion_character"][cc] = bucket["categories"]["companion_character"].get(cc, 0) + 1
    breakdowns = {}
    for key, _ in windows:
        bucket = accum[key]
        total = bucket["count"]
        features = {
            f: {
                "enabled": bucket["features"][f]["enabled"],
                "known": bucket["features"][f]["known"],
                "unknown": total - bucket["features"][f]["known"],
            }
            for f in sorted_features
        }
        breakdowns[key] = {
            "feature_counts": features,
            "category_counts": bucket["categories"],
            "version_counts": bucket["versions"],
            "location_counts": bucket["locations"],
            "source_counts": bucket["categories"]["data_source"],
            "feature_denominator": total,
        }
    distribution = breakdowns["30"]
    ceiling = current_app.config["ENROLLMENT_MAX_DEVICES"]
    return {
        "counts": counts,
        **distribution,
        "breakdowns": breakdowns,
        "activity_days": 30,
        "capacity": {
            "records": len(rows),
            "ceiling": ceiling,
            "warning": len(rows) >= ceiling * 0.8,
        },
        "daily": [
            dict(r, **json.loads(r["counts_json"]))
            for r in connection.execute(
                "SELECT * FROM daily_metrics ORDER BY day DESC LIMIT 90"
            )
        ],
    }


def maintenance(connection, now):
    """Refresh hourly; store one row per UTC day, never every heartbeat."""
    day = datetime.datetime.fromtimestamp(now, datetime.timezone.utc).date().isoformat()
    if connection.execute(
        "SELECT 1 FROM daily_metrics WHERE day=? AND captured_at>?", (day, now - 3600)
    ).fetchone():
        return
    cleanup(connection, now)
    prune_history(connection, now)
    connection.execute(
        "UPDATE devices SET features_json=NULL,features_reported_at=NULL,health_json='[]',detected_city='',detected_region='',detected_country_code='' WHERE last_seen<?",
        (now - 90 * 86400,),
    )
    connection.execute(
        "DELETE FROM daily_metrics WHERE captured_at<?", (now - 365 * 86400,)
    )
    rows = connection.execute(
        "SELECT retired_at, blocked_at, last_checkin_at, capabilities_json, features_json FROM devices"
    ).fetchall()
    counts, _ = _summary_counts(rows, now)
    connection.execute(
        "INSERT INTO daily_metrics VALUES(?,?,?) ON CONFLICT(day) DO UPDATE SET captured_at=excluded.captured_at,counts_json=excluded.counts_json",
        (day, now, json_text(counts)),
    )


def cleanup(connection, now):
    ids = [
        r[0]
        for r in connection.execute(
            "SELECT id FROM devices WHERE last_checkin_at IS NULL AND registration_cleanup_exempt=0 AND retired_at IS NULL AND blocked_at IS NULL AND first_seen<?",
            (now - 86400,),
        )
    ]
    if ids:
        placeholders = ",".join("?" for _ in ids)
        connection.execute(f"DELETE FROM commands WHERE device_id IN ({placeholders})", ids)
        connection.execute(
            f"UPDATE audit_events SET target_device_id=NULL WHERE target_device_id IN ({placeholders})",
            ids,
        )
        connection.execute(f"DELETE FROM devices WHERE id IN ({placeholders})", ids)
    return len(ids)


def prune_history(connection, now):
    """Bound history while preserving live stable fallback and candidate assignments."""
    keep = set()
    latest = connection.execute(
        "SELECT id,previous_stable_id FROM rollouts WHERE kind='stable' ORDER BY id DESC LIMIT 1"
    ).fetchone()
    if latest:
        keep.update(item for item in latest if item is not None)
    keep.update(
        row[0]
        for row in connection.execute(
            "SELECT MAX(ro.id) FROM rollouts ro JOIN rollout_targets t ON t.rollout_id=ro.id JOIN devices d ON d.installation_id=t.installation_id WHERE ro.kind='candidate' GROUP BY t.installation_id"
        )
    )
    obsolete = [
        row[0]
        for row in connection.execute(
            "SELECT id FROM rollouts WHERE created_at<?", (now - 180 * 86400,)
        )
        if row[0] not in keep
    ]
    for rollout_id in obsolete:
        connection.execute(
            "UPDATE rollouts SET previous_stable_id=NULL WHERE previous_stable_id=?",
            (rollout_id,),
        )
        connection.execute("DELETE FROM rollouts WHERE id=?", (rollout_id,))
    connection.execute(
        "DELETE FROM commands WHERE created_at<? AND status IN ('succeeded','failed','expired')",
        (now - 90 * 86400,),
    )
    connection.execute(
        "DELETE FROM audit_events WHERE created_at<? AND id NOT IN (SELECT audit_event_id FROM commands)",
        (now - 180 * 86400,),
    )
