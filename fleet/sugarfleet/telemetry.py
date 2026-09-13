"""Latest allowlisted configuration snapshots and bounded daily fleet totals."""

import datetime
import json
from flask import current_app
from .util import ApiError, json_text, now_epoch

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


def overview(connection, now=None):
    now = now if now is not None else now_epoch()
    rows = connection.execute("SELECT * FROM devices").fetchall()
    active = [r for r in rows if r["retired_at"] is None and r["blocked_at"] is None]
    recent = [
        r
        for r in active
        if r["last_checkin_at"] and r["last_checkin_at"] >= now - 30 * 86400
    ]
    counts = {
        "total": len(rows),
        "active_30d": len(recent),
        "active_7d": sum(
            bool(r["last_checkin_at"] and r["last_checkin_at"] >= now - 7 * 86400)
            for r in active
        ),
        "online": sum(
            bool(r["last_checkin_at"] and r["last_checkin_at"] >= now - 600)
            for r in active
        ),
        "retired": sum(
            r["retired_at"] is not None and r["blocked_at"] is None for r in rows
        ),
        "blocked": sum(r["blocked_at"] is not None for r in rows),
        "dormant": sum(
            bool(r["last_checkin_at"] and r["last_checkin_at"] < now - 30 * 86400)
            for r in active
        ),
        "registration_only": sum(r["last_checkin_at"] is None for r in active),
        "feature_reporting": sum(r["features_json"] is not None for r in recent),
        "legacy": sum(
            "fleet_rollout_v1" not in json.loads(r["capabilities_json"]) for r in recent
        ),
    }
    features = {
        key: {"enabled": 0, "known": 0, "unknown": len(recent)}
        for key in sorted(BOOLEAN_FEATURES)
    }
    categories = {key: {} for key in ("data_source", "companion_character")}
    versions = {}
    locations = {}
    for row in recent:
        location = (
            ", ".join(
                x for x in (row["detected_city"], row["detected_country_code"]) if x
            )
            or "Unknown"
        )
        locations[location] = locations.get(location, 0) + 1
        versions[row["firmware_version"]] = versions.get(row["firmware_version"], 0) + 1
        snapshot = json.loads(row["features_json"] or "{}")
        for key in features:
            if key in snapshot:
                features[key]["enabled"] += int(snapshot[key])
                features[key]["known"] += 1
                features[key]["unknown"] -= 1
        for key in categories:
            category = str(snapshot.get(key, "unknown"))
            categories[key][category] = categories[key].get(category, 0) + 1
    ceiling = current_app.config["ENROLLMENT_MAX_DEVICES"]
    return {
        "counts": counts,
        "feature_counts": features,
        "category_counts": categories,
        "version_counts": versions,
        "location_counts": locations,
        "source_counts": categories["data_source"],
        "feature_denominator": len(recent),
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
    connection.execute(
        "INSERT INTO daily_metrics VALUES(?,?,?) ON CONFLICT(day) DO UPDATE SET captured_at=excluded.captured_at,counts_json=excluded.counts_json",
        (day, now, json_text(overview(connection, now)["counts"])),
    )


def cleanup(connection, now):
    ids = [
        r[0]
        for r in connection.execute(
            "SELECT id FROM devices WHERE last_checkin_at IS NULL AND registration_cleanup_exempt=0 AND retired_at IS NULL AND blocked_at IS NULL AND first_seen<?",
            (now - 86400,),
        )
    ]
    for device_id in ids:
        connection.execute("DELETE FROM commands WHERE device_id=?", (device_id,))
        connection.execute(
            "UPDATE audit_events SET target_device_id=NULL WHERE target_device_id=?",
            (device_id,),
        )
        connection.execute("DELETE FROM devices WHERE id=?", (device_id,))
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
