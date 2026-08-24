#ifndef OTA_PUBLIC_KEYS_H
#define OTA_PUBLIC_KEYS_H

// Release key material. Only public keys belong in firmware. The matching
// private key is intentionally stored outside this repository and supplied to
// GitHub Actions through OTA_SIGNING_KEY_B64.
struct OtaPublicKey {
    const char* key_id;
    const char* pem;
};

static const char OTA_RELEASE_2026_01_PEM[] = R"KEY(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxEP+wWDZ9X2JSs/t7VwW
UR6/b/b2bazZMvhJVl3s1OHx3GEFEjtzBLD88AgsUpwTXyioNB03ZQVggcXU5dL/
uvvauqU/SWEbBGWeP3DRPkBWLxRxVrY/nj2OfXtPZQusaow7PhlcPwGuhBd3YWzc
ZxrXZsIy3FDNTcWEBnWbD7Tac3LOW9yEjSAqwgYwm5ftdQoognlo+Wu6l/JaMwuR
4LZjsX5+wbuA14m5aj+sjV892sWcVj/0Iy6qUBdeW4/+2uexd3I4ET5b2XLS6EnE
qMdeiPMuN4T1noB8nNQjXqPolXfKRIy+NJAAR5jr1cL0EbUQLzRM4vNMcWVbodJ5
hwIDAQAB
-----END PUBLIC KEY-----
)KEY";

static const OtaPublicKey OTA_PUBLIC_KEYS[] = {
    {"release-2026-01", OTA_RELEASE_2026_01_PEM},
    // Add a future public key here before rotating the release signing secret.
};

static const unsigned OTA_PUBLIC_KEY_COUNT =
    sizeof(OTA_PUBLIC_KEYS) / sizeof(OTA_PUBLIC_KEYS[0]);

#endif
