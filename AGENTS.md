# MagicUSB project guidance

- Target the LILYGO T-Dongle-S3 (ESP32-S3, 16 MB flash, 8 MB PSRAM) first.
- Use ESP-IDF and the Espressif TinyUSB component. Keep board-specific GPIOs under `main/board/`.
- Never expose a filesystem to USB while firmware can write to that same filesystem.
- The POS-facing MSC device is read-only by default.
- Updates use two complete FAT disk-image files. Download only to the inactive slot, verify size and SHA-256, then activate through durable metadata and USB re-enumeration.
- Preserve the last known-good image on every failure and power-loss path.
- Never commit credentials, tokens, private keys, certificates, company identifiers, or production URLs.
- Hardware facts must cite an official schematic, vendor repository, or Espressif documentation. Mark unverified behavior as requiring bench testing.
- GPIO 3/4/5 are shared by the TFT and microSD interfaces on T-Dongle-S3. Do not assume concurrent access; coordinate ownership in the board layer.
- Keep release parsing/version selection host-testable and independent from ESP-IDF where practical.
