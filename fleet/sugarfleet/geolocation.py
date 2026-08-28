"""Privacy-limited, best-effort source-IP geolocation."""

import ipaddress
import json
from urllib.parse import quote
from urllib.request import Request, urlopen


LOOKUP_BASE_URL = "https://ipwho.is"
MAX_RESPONSE_BYTES = 4096
MAX_LOCATION_PART_LENGTH = 120


def public_ip(value):
    """Return a normalized globally routable IP, or None for local/special addresses."""
    try:
        address = ipaddress.ip_address(value or "")
    except ValueError:
        return None
    return address.compressed if address.is_global else None


def _location_part(value, limit=MAX_LOCATION_PART_LENGTH):
    if not isinstance(value, str):
        return ""
    value = value.strip()
    return value if 0 < len(value) <= limit else ""


def lookup_approximate_location(source_ip, *, timeout=2.0):
    """Resolve only city/region/country. The caller must never persist source_ip."""
    address = public_ip(source_ip)
    if not address:
        return None
    fields = "success,city,region,country_code"
    url = f"{LOOKUP_BASE_URL}/{quote(address, safe='')}?fields={fields}"
    request = Request(url, headers={"Accept": "application/json", "User-Agent": "SugarClock-Fleet/1"})
    try:
        with urlopen(request, timeout=timeout) as response:
            payload = response.read(MAX_RESPONSE_BYTES + 1)
    except (OSError, TimeoutError):
        return None
    if len(payload) > MAX_RESPONSE_BYTES:
        return None
    try:
        value = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(value, dict) or value.get("success") is not True:
        return None
    city = _location_part(value.get("city"))
    region = _location_part(value.get("region"))
    country_code = _location_part(value.get("country_code"), 2).upper()
    if len(country_code) != 2:
        country_code = ""
    if not any((city, region, country_code)):
        return None
    return {"city": city, "region": region, "country_code": country_code}


def location_label(city, region, country_code):
    """Format a compact label without repeating identical city/region names."""
    parts = []
    for value in (city, region, country_code):
        value = (value or "").strip()
        if value and value.casefold() not in {part.casefold() for part in parts}:
            parts.append(value)
    return ", ".join(parts)
