# Architecture and validated assumptions

## Verified facts

LILYGO documents T-Dongle-S3 variants with 16 MB flash, native USB-A, an 80×160 ST7735 TFT, APA102 LED, button, and TF slot. The Plus variant has 8 MB PSRAM. The current official pin map and LILYGO's original USB-MSC example agree on these assignments:

| Function | Pins |
| --- | --- |
| Native USB | D- GPIO19, D+ GPIO20 (ESP32-S3 fixed USB pins) |
| TFT | BL 38, CS 4, SCK 5, MOSI 3, DC 2, RST 1 |
| TF/SD 4-bit SDMMC | CLK 12, CMD 16, D0 14, D1 17, D2 21, D3 18 |
| APA102 | data 40, clock 39 |
| Button | GPIO0 |

ESP-IDF supports TinyUSB MSC on ESP32-S3 and supports SD cards as backing media. Custom TinyUSB MSC callbacks can instead map block reads to a disk-image file, which is required for A/B isolation.

## Corrected assumptions and risks

1. **Board revisions must be bench-verified.** Earlier project notes incorrectly treated the TFT GPIOs as the TF interface. LILYGO's current pin map, schematic, and original USB-MSC example instead use a dedicated 4-bit SDMMC bus. The board layer follows those official assignments; card detection and display behavior still require physical testing on the exact shipped revision.
2. **The ESP-IDF convenience MSC storage layer is not the final backend.** Exposing the entire SD card would let Windows see management data and defeats A/B disk images. Final MSC callbacks read sectors from the selected image file only.
3. **A background switch cannot be entirely invisible.** Windows caches removable-media state. Activation requires a TinyUSB disconnect/reconnect and should happen only after an MSC-idle window, an explicit button action, or the next power cycle.
4. **USB full speed limits throughput.** Practical transfer rates and SD image read latency require hardware measurement.
5. **Corporate Wi-Fi may require WPA2-Enterprise certificates, enrollment, or MAC approval.** Provisioning must match the approved network configuration.

## Exact storage layout

Use internal encrypted NVS for device configuration and small durable state. Use a normal FAT32 management filesystem on the microSD, never directly exposed over USB:

```text
/magicusb/
  metadata.0.json       previous durable activation record
  metadata.1.json       current durable activation record
  image-a.fat           complete POS-facing FAT image
  image-b.fat           complete POS-facing FAT image
  download.partial      resumable inactive download only
  logs/events-YYYYMM.bin
```

Metadata records contain a monotonically increasing generation, active slot, release, image size, SHA-256, and CRC. On boot, choose the valid record with the highest generation. Each image is a complete filesystem image whose size is a multiple of 512 bytes.

The current hardware proof implements alternating binary metadata records, newest-valid-record selection, complete-image SHA-256 verification on every boot, fallback to the older valid record, read-only file access for MSC, and RAM fallback. It derives the inactive A/B slot from current metadata, streams the candidate to that slot's `.partial` file, verifies exact size and SHA-256, flushes it, and replaces only the inactive image. The updater runs on a dedicated 12 KiB task because the HTTP client and manifest buffer exceed the default 3,584-byte app-main stack.

An explicit button press activates a verified pending image. Firmware first marks the medium unavailable and disconnects TinyUSB, waits for any active read callback to exit, re-verifies the pending slot, writes and flushes the alternate metadata record with an incremented generation, swaps the read-only file handle, and reconnects USB. A failed verification or metadata commit reconnects the previous active image. Signed manifests, directory-level durability testing, and fault injection remain before this is production-safe.

Metadata v2 adds the activated `YYYY.MM.DD.build` release while continuing to read v1 records. Before downloading an image, firmware skips an exact active size/hash match and rejects a candidate release that is equal to or older than a known active release. The strict release parser/comparator is independent of ESP-IDF.

HTTPS requests attach Espressif's full built-in CA bundle, so certificate-chain and hostname validation are performed by `esp_http_client`. Manifest and image transfers each use at most three attempts with 500 ms then 1,000 ms backoff; every image retry truncates the inactive `.partial` file and restarts streaming SHA-256. Plain HTTP remains behind the explicit prototype-only `allow_http` NVS flag.

Bench validation on 2026-08-20 confirmed A→B activation, B→A activation, Windows disconnect/reconnect, cold-boot persistence, and active-image hash suppression. A metadata-v2 migration then activated release `2026.08.20.3`; when the server advertised different-content release `2026.08.20.2`, the dongle requested only the manifest, retained `.3`, and did not download the older FAT image. The lab endpoint used plain HTTP behind the explicit prototype-only NVS gate.

HTTPS bench validation fetched release `2026.08.20.5` from public GitHub raw content with `allow_http=0`, verified and activated it, and confirmed the expected file through Windows after USB re-enumeration.

Activation sequence:

1. Download to `download.partial`, never the active image.
2. Stream SHA-256 and verify final size; validate the FAT image's basic geometry.
3. Rename/replace the inactive slot and flush it to the card.
4. Write and flush the next alternating metadata record.
5. Wait for a configurable MSC idle interval and no open firmware reads.
6. Tell TinyUSB the medium is absent, disconnect, close the old image, open the new image read-only, reconnect.
7. Keep the old slot intact until a later release is fully verified.

Power loss before step 4 selects the old metadata. Power loss after step 4 selects the new, already flushed image. If the selected image fails validation at boot, fall back to the other valid slot and record the rollback.

## Cloud recommendation

Use a publishing bridge, not Google OAuth on each dongle. The bridge watches or polls an approved Google Drive folder, builds the complete FAT image, computes its hash, signs a small canonical manifest, and stores immutable release objects behind HTTPS. Devices receive narrowly scoped, revocable credentials and only implement manifest fetch, signature verification, ranged download, and hashing.

Google Apps Script is acceptable for a low-volume prototype but is a poor long-term binary publisher because of execution/runtime quotas and deployment controls. A Cloud Run or Cloud Function publisher plus object storage is the recommended production shape. Drive change notifications can wake the publisher; periodic reconciliation remains necessary.

Provisioning recommendation: use a factory flashing station to assign device ID, site, approved Wi-Fi profiles, per-device credential, and trust anchor into NVS. Add BLE provisioning later for credential rotation. Avoid a persistent setup access point on deployed units.

The prototype currently loads two WPA2-Personal profiles from the `magicusb` NVS namespace, tries them in priority order, reconnects to the active profile after loss, and scans every 60 seconds for the preferred profile while on fallback. Credentials are generated and flashed from saved Windows profiles using a temporary artifact that is deleted after provisioning. This prototype NVS is not yet encrypted and must not be treated as production credential protection.

## Security posture

- USB is read-only and implements storage only; no USB network interface.
- TLS server validation and signed manifests are required before production.
- Per-device credentials must be revocable and least-privileged.
- Enable secure boot v2, flash encryption, encrypted NVS, and signed OTA only after the development recovery workflow is established; these controls are difficult to reverse.
- The publishing service should scan/allowlist release contents before image creation and retain an audit record of uploader, scope, hashes, and publication time.
