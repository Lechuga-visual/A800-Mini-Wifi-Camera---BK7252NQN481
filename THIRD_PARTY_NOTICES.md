# Third-party material

The MIT license covers original A800 Local code and documentation. It does not grant rights to vendor firmware, extracted register data, SDK libraries, toolchains or seller images.

| Dependency | Source | Notice |
|---|---|---|
| Beken FreeRTOS SDK | [NonPIayerCharacter/beken_freertos_sdk](https://github.com/NonPIayerCharacter/beken_freertos_sdk) | Per-file licenses and precompiled vendor libraries. No blanket relicensing under this repository's MIT license. |
| ARM GCC toolchain | [openshwprojects/OpenBK7231N](https://github.com/openshwprojects/OpenBK7231N) | GCC and bundled runtime components retain their own licenses. Downloaded separately. |
| Mbed TLS 3.6.8 | [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 OR GPL-2.0-or-later; see the downloaded release license. |
| w64devkit 2.11.0 | [skeeto/w64devkit](https://github.com/skeeto/w64devkit) | Bundled tools retain their own licenses; see upstream. |
| bk7231tools | [Bk7231tools on PyPI](https://pypi.org/project/bk7231tools/) | Upstream package license applies. |

The GC0312 initialization sequence was recovered from the owner's factory image. Redistribution rights have not been established. The sequence and factory image are deliberately excluded. `tools/extract_sensor.py` validates and extracts the supported sequence locally from a backup supplied by the device owner.

Listing screenshots were supplied by the repository owner for identification and historical context. Seller/platform artwork and text remain third-party material. The listing's AI summary is not a datasheet, certification report or verified feature list.

No compiled release image is supplied. Review the SDK and vendor-data terms before redistributing a binary built with those components.
