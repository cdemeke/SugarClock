# Isolated legacy OTA test fixture

This signed stable-channel manifest exists only for the temporary USB test clock firmware. It offers the exact immutable v0.2.11 preview binary already tested on Chris Office. It does not change GitHub Latest, publish a stable release, or authorize a Fleet rollout. Do not use this fixture as the public release manifest.

The test starts from v0.2.6 with only its manifest URL changed. Signature verification, TLS roots, updater logic, partition layout, and firmware version remain unchanged. Hardware results must distinguish this test build from the exact published v0.2.6 binary.
