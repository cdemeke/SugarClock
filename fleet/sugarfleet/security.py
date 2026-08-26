"""Credential hashing and constant-time verification."""

import base64
import hashlib
import hmac
import os


PBKDF2_ITERATIONS = 600_000


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


def hash_device_credential(credential):
    salt = os.urandom(16)
    digest = hashlib.pbkdf2_hmac(
        "sha256", credential.encode("ascii"), salt, PBKDF2_ITERATIONS, dklen=32
    )
    return f"pbkdf2-sha256${PBKDF2_ITERATIONS}${_b64(salt)}${_b64(digest)}"


def verify_device_credential(credential, encoded):
    try:
        algorithm, iterations, salt, expected = encoded.split("$")
        if algorithm != "pbkdf2-sha256":
            return False
        actual = hashlib.pbkdf2_hmac(
            "sha256", credential.encode("ascii"), _unb64(salt), int(iterations), dklen=32
        )
        return hmac.compare_digest(actual, _unb64(expected))
    except (AttributeError, UnicodeError, ValueError):
        return False
