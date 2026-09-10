# TLS send-buffer reduction

This change retains TLS encryption and the existing verification policies. It
reduces outgoing TLS record capacity from 16,384 to 4,096 bytes while preserving
16,384-byte incoming capacity. Bluetooth's network-pause policy is unchanged for
the initial memory comparison. The iOS protocol and saved configuration format
are unchanged; an iOS rebuild is not required to exercise this firmware change.

## Reproducible build

`pio run -e esp32dev` builds the reduced-buffer candidate. For a controlled
comparison, `pio run -e esp32dev_tls_baseline` builds the same source and
diagnostics with the original 16,384-byte outgoing capacity.

The pinned Arduino SDK contains precompiled TLS code. `scripts/build_tls.py`
therefore rebuilds every member of `libmbedtls_2.a` and replaces all references to
that archive in the link. It does not modify the shared PlatformIO installation.
The first build downloads checksum-verified source archives into the ignored
`.pio/tls-source` cache:

- ESP-IDF `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3` (4.4.7).
- Its Espressif Mbed TLS submodule `2b8e772fc1cb0732cda3bae7d1e9d6f4cfaf63d9` (2.28.7).

The build checks installed TLS headers, ESP configuration header, SDK identity
and original archive membership against those sources. Missing, changed or
corrupted inputs fail the build rather than silently falling back to the old
archive. A framework upgrade requires reviewing these checks and pins.

Only TLS record-layer sources use the changed capacity constants in this pinned
configuration. The public SSL context layout is unchanged. The crypto, X.509 and
certificate-bundle archives remain the SDK originals. The allocator uses internal
RAM; the alternative IRAM allocation branch that also references these constants
is not enabled. Dynamic buffers and alternative allocation modes are rejected by
the profile header pending their own integration review.

## Verification

The startup-only `[TLS MEM]` diagnostic constructs and frees an offline TLS
context before Wi-Fi/Bluetooth startup. It reports actual allocated receive/send
buffer sizes, the linked library's outgoing payload capacity, total setup heap,
cleanup delta, setup result and profile agreement. It sends no traffic and logs
no payloads or credentials. Record framing overhead means the allocation sizes
are larger than the 16,384/4,096-byte content capacities.

The maximum-fragment-length negotiation getters are not allocation-size getters.
In this version the default negotiation helper can report the smaller directional
capacity. The check therefore inspects the ESP heap allocation sizes directly and
uses `mbedtls_ssl_get_max_out_record_payload()` for outgoing capacity.

Run `python -m unittest discover -s tests -v` after resolving the PlatformIO
dependencies. The TLS transport tests compile the exact pinned upstream library
for the host, then use a local TLS 1.2 server with certificate verification. They
check a 20,000-byte outgoing payload split across multiple writes, a 32,768-byte
incoming payload, and rejection of a wrong server hostname. A separate test
rejects a corrupted source archive. CI repeats these tests after the firmware
build populates the source cache. Host tests do not qualify ESP hardware
acceleration, the embedded allocator or every provider/OTA endpoint.

Before installation, check the application slot size, capture a private recovery
backup, establish the active application slot and preserve NVS, bonds, bootloader,
partition table and filesystem. Install only the application. Compare matching
baseline/candidate cold boots and network scenarios on the physical clock;
lifetime minimum heap is not a per-request allocation measurement.

The existing HTTPClient payload path handles partial writes. Its header-writing
path expects one complete write, so future unusually large outgoing HTTP headers
need an explicit audit. Current configured provider URLs/tokens and managed
request headers are bounded well below the new outgoing capacity.

## Scope and remaining qualification

This is a development candidate, not a completed release qualification. Keep the
network-pause fallback during the first comparison. Successful allocation tests
and autonomous polling do not demonstrate continuous Bluetooth plus worst-case
TLS, phone-side save/readback reliability, provider reauthentication after expiry,
weather-enabled operation, or signed OTA/rollback acceptance. Follow
[BLE_ACCEPTANCE.md](BLE_ACCEPTANCE.md) for those physical checks.

Reference: [Espressif ESP-IDF 4.4.7 asymmetric TLS buffers](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/kconfig.html#config-mbedtls-asymmetric-content-len).

## USB measurement on September 8, 2026

The owner authorized installation on the attached TC001 (ESP32-D0WD revision
1.1, 4 MiB flash). A private full-flash backup matched the previous correction
image in the active first application slot. The original firmware was observed
for five minutes, followed by a three-minute observation of the rebuilt baseline.

The same startup diagnostic measured:

| Measurement, bytes | Rebuilt baseline | Reduced-buffer candidate |
| --- | ---: | ---: |
| Incoming content capacity | 16,384 | 16,384 |
| Outgoing content capacity | 16,384 | 4,096 |
| Actual receive allocation | 16,720 | 16,720 |
| Actual send allocation | 16,720 | 4,432 |
| Total offline TLS setup allocation | 36,124 | 23,836 |
| Heap delta after cleanup | 0 | 0 |

Both profiles returned success and passed the runtime profile check. This
isolated physical allocation comparison confirms a **12,288-byte reduction**.
It does not establish the memory floor during every encrypted handshake or
prove that Bluetooth can safely remain on during network work.

The reduced-buffer image is **1,585,008 bytes**, leaving **250,000 bytes** in the
existing application slot. Its SHA-256 is
`f37fb3d74da54bae22aced3c6491f63c319d120a9a76e69aa0d8261c943d14da`.
The rebuilt baseline is 1,585,248 bytes, SHA-256
`e37005dd3ae4d224cf1531056adbd52b4b1496babd156bf574cac45d2fc84dcd`.
Static RAM remains 93,512 bytes for both images.

Both application-only writes were hash-verified. Immediately before and after
the candidate write, the entire first 64 KiB matched byte for byte, including
bootloader, partition table, NVS/settings/bonds and OTA metadata. The initial
backup and baseline-prefix read differed only in the logical NVS value for the
routine OTA last-check timestamp, which advanced during baseline operation;
all 79 saved configuration entries and all 16 bond-storage entries matched.
The filesystem and other application slot were outside both write ranges.

Both firmware profiles and the installer filesystem build passed. All 52
repository tests passed, including the three new TLS tests, and the layout check
passed. Private backups and filtered logs are retained locally under
`/tmp/sugarclock-tls-20260908`; they are not repository artifacts.

After the candidate booted at 20:21:57 UTC, it connected to saved Wi-Fi, completed
provider login, received a reading and resumed Bluetooth. The first glucose
worker ended with a lifetime minimum heap of 41,368 bytes; after subsequent
startup network work, the first resumed-Bluetooth sample had a 36,288-byte
minimum. These are short observations with variable network work, not a
controlled measurement of peak-handshake savings. The owner was asked to test a
visible setting save; phone-side confirmation and continuous-Bluetooth testing
remain separate from the confirmed allocation reduction.
