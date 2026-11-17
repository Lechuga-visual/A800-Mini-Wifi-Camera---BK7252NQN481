# Security model

Video and administration use TLS 1.2 with ECDHE-ECDSA/AES-128-GCM. Plain HTTP is closed. The modern TLS server is namespaced separately from the SDK's older Wi-Fi crypto implementation.

## First setup

A blank supported device generates its own P-256 CA and server private keys. It stores identity and password hashes in two alternating flash sectors. No shared device CA, server key or administrator hash is embedded in the application.

The initial `admin / admin` login is accepted only through the camera's `192.168.4.1` setup interface. Streaming and home Wi-Fi changes remain blocked until both account passwords are set. The setup AP stays available until that step completes.

The setup AP password is a documented shared default. First setup uses trust on first use: the browser does not yet trust the camera CA. Perform setup near the device, in a trusted environment. An attacker able to impersonate that AP during onboarding is outside this bootstrap's protection. Do not expose setup mode to an untrusted network.

Download only the public CA from the UI. `/certificate` is intentionally public; a CA certificate is not a secret. The web API does not export private keys. Home Assistant must trust that CA and keep SSL verification enabled. For an out-of-band check, compare the CA certificate DER SHA-256 with the `LOCALCAM: CA SHA256` fingerprint printed on the physical UART at boot.

## Accounts and storage

`admin` controls settings. `ha` can request MJPEG and snapshots. Passwords use per-device salt and PBKDF2-HMAC-SHA256 with 10,000 iterations. Password changes invalidate UI sessions and stop existing video streams. UI sessions are random, expire after 15 minutes, and use Secure, HttpOnly, SameSite=Strict cookies. Settings POST requests require a matching HTTPS Origin.

Keys are stored in ordinary flash, not a secure element. Physical flash/UART access can disclose them. This project does not implement secure boot, flash encryption or a general-purpose denial-of-service defense.

## Certificates and addresses

The CA remains stable across password changes, restarts and DHCP address changes. The server certificate is reissued for the current station IPv4 address when no TLS clients are active. Existing streams may delay renewal until they close. Home Assistant's URL still needs to point to the current address; a DHCP reservation is recommended.

There is no RTC/NTP dependency. Certificate validity comes from build-time UTC bounds, defaulting to the build date minus one day through ten years after that date. An old build can therefore expire. Renew the build before expiry; first-boot key generation is not a guarantee of a fresh ten-year validity period on an arbitrarily old binary.

## Updates and reports

OTA is disabled. Update through UART after a verified backup. Keep Mbed TLS maintained: the pinned version describes the tested build, not a permanent security guarantee.

This is experimental firmware, not independently audited security software. Do not post passwords, CA private keys, full flash dumps or video containing private scenes in issues. Report a vulnerability privately to the repository owner through an available private GitHub reporting channel; otherwise open an issue requesting private contact without exploit details or secrets.
