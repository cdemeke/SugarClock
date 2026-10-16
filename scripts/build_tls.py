"""Rebuild the pinned SDK's TLS record library, without altering shared packages.

Arduino 2.0.17 ships precompiled libmbedtls_2.a. A -D flag alone cannot change
its allocations. Rebuild every member of that archive from its exact IDF
sources with the application's profile. Crypto, X.509 and certificate-bundle
archives stay unchanged: record-capacity macros do not change their code or
the public SSL context layout in this pinned version.
"""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

IDF_COMMIT = "38eeba213aa695aabfd6d89aa9f5078dbe5a94c3"
MBEDTLS_COMMIT = "2b8e772fc1cb0732cda3bae7d1e9d6f4cfaf63d9"
ARCHIVES = {
    "esp-idf": (IDF_COMMIT, "397194be939889b8daefae7b1d797efa1bf9adbdc79b8bf78a248216aaf65920"),
    "mbedtls": (MBEDTLS_COMMIT, "7c47e87902c9153706fc469a5f7b609bd67c02e00e4ac3c977137bc74b5507d9"),
}
TLS_SOURCES = (
    "debug.c", "ssl_cache.c", "ssl_ciphersuites.c", "ssl_cli.c", "ssl_cookie.c",
    "ssl_msg.c", "ssl_srv.c", "ssl_ticket.c", "ssl_tls.c", "ssl_tls13_keys.c",
)
PORT_SOURCES = ("mbedtls_debug.c", "net_sockets.c")


def checked_archive(cache, repo):
    commit, digest = ARCHIVES[repo]
    archive = cache / (repo + ".tar.gz")
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        try:
            url = "https://codeload.github.com/espressif/{}/tar.gz/{}".format(repo, commit)
            with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as out:
                shutil.copyfileobj(response, out)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != digest:
                raise RuntimeError("TLS source download checksum mismatch: " + repo)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError("TLS source cache checksum mismatch: " + str(archive))
    return archive


def member_bytes(archive, name):
    member = archive.getmember(name)
    if not member.isfile():
        raise RuntimeError("Expected a regular TLS source file: " + name)
    return archive.extractfile(member).read()


def write_if_changed(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def prepare_sources(cache, sdk):
    cache.mkdir(parents=True, exist_ok=True)
    source = cache / "src"
    with tarfile.open(checked_archive(cache, "mbedtls")) as archive:
        root = "mbedtls-" + MBEDTLS_COMMIT + "/"
        # Fail closed if a framework upgrade changes the ABI/configuration.
        for header in ("ssl.h", "ssl_internal.h", "config.h", "check_config.h", "version.h"):
            data = member_bytes(archive, root + "include/mbedtls/" + header)
            installed = sdk / "include/mbedtls/mbedtls/include/mbedtls" / header
            if installed.read_bytes() != data:
                raise RuntimeError("Installed TLS header differs from pinned source: " + header)
        for name in TLS_SOURCES:
            write_if_changed(source / name, member_bytes(archive, root + "library/" + name))
        # Private headers are included by the upstream C sources.
        for member in archive.getmembers():
            relative = member.name.removeprefix(root + "library/")
            if member.name.startswith(root + "library/") and "/" not in relative and relative.endswith(".h"):
                write_if_changed(source / relative, member_bytes(archive, member.name))
    with tarfile.open(checked_archive(cache, "esp-idf")) as archive:
        root = "esp-idf-" + IDF_COMMIT + "/components/mbedtls/port/"
        config = member_bytes(archive, root + "include/mbedtls/esp_config.h")
        if config != (sdk / "include/mbedtls/port/include/mbedtls/esp_config.h").read_bytes():
            raise RuntimeError("Installed ESP TLS configuration differs from pinned source")
        for name in PORT_SOURCES:
            write_if_changed(source / name, member_bytes(archive, root + name))
    return source


def configure(env):
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    sdk = framework / "tools/sdk/esp32"
    versions = (framework / "tools/sdk/versions.txt").read_text()
    if "esp-idf: v4.4.7 38eeba213a" not in versions or env.BoardConfig().get("build.mcu") != "esp32":
        raise RuntimeError("TLS rebuild is pinned to Arduino ESP32 / IDF 4.4.7, ESP32 target")
    original = sdk / "lib/libmbedtls_2.a"
    members = subprocess.check_output([env.subst("$AR"), "t", str(original)], text=True).splitlines()
    expected = sorted(name + ".obj" for name in TLS_SOURCES + PORT_SOURCES)
    if sorted(members) != expected:
        raise RuntimeError("SDK TLS archive members changed; re-audit the rebuild integration")
    cache = Path(env.subst("$PROJECT_DIR")) / ".pio/tls-source" / MBEDTLS_COMMIT
    source = prepare_sources(cache, sdk)
    tls_env = env.Clone()
    tls_env.Append(CFLAGS=["-std=gnu99"])
    objects = [tls_env.Object("$BUILD_DIR/tls/" + name + ".o", str(source / name))
               for name in TLS_SOURCES + PORT_SOURCES]
    rebuilt = tls_env.StaticLibrary("$BUILD_DIR/tls/sugarclock_mbedtls", objects)[0]
    libraries = list(env["LIBS"])
    if "-lmbedtls_2" not in libraries:
        raise RuntimeError("SDK TLS link entry missing; refusing a header-only optimization")
    env.Replace(LIBS=[rebuilt if lib == "-lmbedtls_2" else lib for lib in libraries])
    print("[TLS BUILD] Rebuilding IDF 4.4.7 TLS archive; keeping crypto/X.509 and incoming capacity")


try:
    Import("env")
except NameError:
    pass
else:
    configure(env)
