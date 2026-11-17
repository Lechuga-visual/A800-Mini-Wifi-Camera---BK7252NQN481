# Changelog

## 0.2.0, experimental source revision

- Generate a per-device CA and server key on first boot.
- Persist identity and password hashes in alternating flash banks.
- Limit initial admin/admin access to setup Wi-Fi; require both account passwords before video or home Wi-Fi setup.
- Add CA download, password changes, secure UI sessions and ES/EN interface.
- Reissue the server certificate for station IP changes while keeping the same CA.
- Add pinned dependencies, SDK patch, owner-backup sensor extraction, guarded UART tooling and tests.

ARM compilation and host tests pass. Physical first-boot onboarding and new-version FPS remain pending. No OTA, RTSP or ONVIF implementation.

## 0.1.0, private prototype

Validated on one BK7252N/GC0312 unit: standalone HTTPS/MJPEG, saved Wi-Fi, viewer/admin Basic authentication, and Home Assistant OS with certificate verification enabled. Identity was generated on the PC and embedded privately. Observed HTTPS rate: about 5 FPS at 640×480.

Private keys, unit firmware images, dumps and private prototype logs are not part of this repository.
