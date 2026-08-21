# MagicUSB

MagicUSB is firmware for a remotely managed, read-only USB mass-storage dongle. The first target is the LILYGO T-Dongle-S3. A POS sees an ordinary USB drive immediately; network updates are staged separately and can never alter the disk currently exposed to the host.

## Current proof of concept

The current hardware proof:

- boots as an ESP32-S3 ESP-IDF application;
- exposes a valid 64 KiB read-only FAT12 disk containing `README.TXT`;
- loads two provisioned Wi-Fi profiles from NVS without embedding credentials;
- prioritizes the company network, reconnects after signal loss, and periodically checks for promotion from the home fallback;
- reports state through the onboard TFT and APA102 LED;
- shows the active release alongside SD/cache and Wi-Fi status on the normal ready screen;
- verifies the BOOT button input;
- mounts the microSD without automatic formatting and keeps it private to firmware;
- seeds and SHA-256-verifies slot A using durable generation metadata;
- fetches a versioned JSON manifest and streams a complete candidate image into whichever A/B slot is inactive;
- verifies the candidate's exact byte count and SHA-256 before replacing the inactive slot;
- activates the verified inactive slot only on an explicit button press, using durable alternating metadata and USB disconnect/reconnect;
- selects the newest valid metadata/image pair after a cold boot and falls back to the older record if validation fails;
- skips an identical active image and rejects equal or older `YYYY.MM.DD.build` releases before downloading their image;
- validates public HTTPS servers with Espressif's full CA bundle and retries manifest/image transfers with bounded exponential backoff;
- requires ECDSA P-256/SHA-256 signatures on HTTPS manifests and rejects invalid signatures before image download;
- enforces signed minimum-firmware, site, and device targeting before image download;
- serves USB reads from the verified SD image with an immutable RAM fallback;
- records the verified T-Dongle-S3 pin assignments in one board module.

The management filesystem itself is never exposed to Windows. Only the selected complete FAT image is visible, and all USB writes are rejected. See [architecture.md](docs/architecture.md).

Provisioned credentials are intentionally absent from this repository. The current lab device has locally flashed NVS profiles; production devices still require encrypted-NVS provisioning and credential-rotation procedures.

## Build

Install ESP-IDF 5.5 or 6.0 and activate its environment, then run:

```text
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

On Windows, the device should enumerate as `MAGICUSB` and contain a read-only `README.TXT`. Programming/monitoring may require the board's separate UART connector because the native USB pins are used by MSC.

The project is verified with ESP-IDF 6.0.2, `espressif/esp_tinyusb` 2.2.1, `espressif/tinyusb` 0.21.0~1, and `espressif/cjson` 1.7.19~2. The current build produces `build/magicusb.bin` at approximately 938 KiB.

The lab publisher in `tools/lab_publisher.py` creates a deterministic 64 KiB FAT12 release and ECDSA P-256-signed manifest for isolated bench testing. Plain HTTP is accepted only when the separately provisioned `allow_http` NVS flag is set. Public HTTPS certificate and hostname validation is implemented, and HTTPS manifests must have a valid provisioned trust anchor.

Metadata v2 records the activated release while remaining backward-compatible with v1 records. The release comparator is kept independent of ESP-IDF in `main/release_version.c` so it can be host-tested.

The manifest eligibility rules are isolated in `main/manifest_policy.c`. A manifest can be global, site-scoped, device-scoped, or constrained by both site and device. Any nonempty constraint must match the corresponding provisioned NVS value, and `minimum_firmware` must not exceed the running semantic firmware version.

## Documentation

- [Architecture and risk decisions](docs/architecture.md)
- [State machine](docs/state-machine.md)
- [Roadmap](docs/roadmap.md)
- [Release manifest schema](schemas/release-manifest.schema.json)

## Authoritative references

- [LILYGO T-Dongle-S3 documentation and pin map](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/docs/en/t-dongle-s3/REAMDE.MD)
- [LILYGO original USB-MSC example](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/examples/usb_mass_storage/usb_mass_storage.ino)
- [Espressif ESP32-S3 USB Device Stack](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_device.html)
- [Espressif TinyUSB MSC example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_msc)
- [Google Drive change notifications](https://developers.google.com/workspace/drive/api/guides/push)
