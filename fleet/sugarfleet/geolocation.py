"""Privacy-limited, best-effort source-IP geolocation."""

import ipaddress
import json
from urllib.parse import quote
from urllib.request import Request, urlopen


LOOKUP_BASE_URL = "https://ipwho.is"
MAX_RESPONSE_BYTES = 4096
MAX_LOCATION_PART_LENGTH = 120

# The lookup provider returns full US state names. Keep summaries compact while
# retaining the supplied region for countries without a US postal abbreviation.
US_REGIONS = dict(zip(
    ('Alabama|Alaska|Arizona|Arkansas|California|Colorado|Connecticut|Delaware|'
     'District of Columbia|Florida|Georgia|Hawaii|Idaho|Illinois|Indiana|Iowa|'
     'Kansas|Kentucky|Louisiana|Maine|Maryland|Massachusetts|Michigan|Minnesota|'
     'Mississippi|Missouri|Montana|Nebraska|Nevada|New Hampshire|New Jersey|'
     'New Mexico|New York|North Carolina|North Dakota|Ohio|Oklahoma|Oregon|'
     'Pennsylvania|Rhode Island|South Carolina|South Dakota|Tennessee|Texas|'
     'Utah|Vermont|Virginia|Washington|West Virginia|Wisconsin|Wyoming|'
     'American Samoa|Guam|Northern Mariana Islands|Puerto Rico|U.S. Virgin Islands').lower().split('|'),
    ('AL AK AZ AR CA CO CT DE DC FL GA HI ID IL IN IA KS KY LA ME MD MA MI MN '
     'MS MO MT NE NV NH NJ NM NY NC ND OH OK OR PA RI SC SD TN TX UT VT VA '
     'WA WV WI WY AS GU MP PR VI').split(),
))


def location_summary(city, region, country_code):
    """A region-aware grouping key shared by overview links and device filters."""
    region = (region or '').strip()
    if (country_code or '').upper() == 'US':
        region = US_REGIONS.get(region.casefold(), region)
        if region.upper() in US_REGIONS.values():
            region = region.upper()
    return location_label(city, region, country_code) or 'Unknown'


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
