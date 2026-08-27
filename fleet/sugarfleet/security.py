"""Credential hashing and constant-time verification."""

import base64
import hashlib
import hmac


PBKDF2_ITERATIONS = 600_000
HMAC_DOMAIN = b"sugarclock-device-credential-v1\0"


def _b64(data):
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def _unb64(value):
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def valid_device_credential(value):
    if not isinstance(value, str) or len(value) != 43:
        return False
    try:
        return len(_unb64(value)) == 32
    except (ValueError, TypeError):
        return False


def _pepper_bytes(pepper):
    if isinstance(pepper, str):
        pepper = pepper.encode("utf-8")
    if not isinstance(pepper, bytes) or len(pepper) < 32:
        raise ValueError("device credential pepper must contain at least 32 bytes")
    return pepper


def hash_device_credential(credential, pepper):
    """Fast keyed digest for a uniformly random 256-bit device credential."""
    digest = hmac.new(
        _pepper_bytes(pepper), HMAC_DOMAIN + credential.encode("ascii"), hashlib.sha256
    ).digest()
    return f"hmac-sha256${_b64(digest)}"


def verify_device_credential(credential, encoded, pepper):
    try:
        parts = encoded.split("$")
        if parts[0] == "hmac-sha256" and len(parts) == 2:
            actual = hmac.new(
                _pepper_bytes(pepper), HMAC_DOMAIN + credential.encode("ascii"), hashlib.sha256
            ).digest()
            return hmac.compare_digest(actual, _unb64(parts[1]))
        # Transitional compatibility: upgrade these on the first successful
        # authentication after deployment.
        if parts[0] == "pbkdf2-sha256" and len(parts) == 4:
            _algorithm, iterations, salt, expected = parts
            actual = hashlib.pbkdf2_hmac(
                "sha256", credential.encode("ascii"), _unb64(salt), int(iterations), dklen=32
            )
            return hmac.compare_digest(actual, _unb64(expected))
        return False
    except (AttributeError, UnicodeError, ValueError):
        return False


def credential_hash_needs_upgrade(encoded):
    return not isinstance(encoded, str) or not encoded.startswith("hmac-sha256$")
