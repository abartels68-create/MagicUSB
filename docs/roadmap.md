# Implementation roadmap

## Phase 1 — hardware proof

- [x] ESP-IDF/TinyUSB repository scaffold
- [x] Read-only, in-memory FAT MSC proof
- [x] APA102 state indication
- [x] Wi-Fi subsystem startup without embedded secrets
- [x] Compile with ESP-IDF 6.0.2 and resolve component API differences
- [x] Verify USB enumeration and read-only behavior on Windows
- [x] Verify dedicated 4-bit SDMMC card detection on a physical T-Dongle-S3
- [x] Add and bench-test card/display/LED/button diagnostics
- [x] Mount a private SD management filesystem without automatic formatting
- [x] Seed, hash, cold-boot verify, and serve a read-only slot-A FAT image
- [x] Retain an immutable RAM image as recovery fallback

## Phase 2 — safe downloads

- [x] Load two NVS-provisioned WPA2-Personal profiles in priority order.
- [x] Verify connection to the home fallback while USB remains available.
- [x] Add reconnect handling and periodic preferred-network discovery.
- [ ] Bench-test automatic promotion to the preferred company network onsite.
- [ ] Migrate prototype credentials to encrypted NVS and define rotation/revocation.
- [x] Parse and validate schema-v1 manifests.
- [x] Implement strict release parsing, ordering, identical-image suppression, and downgrade rejection.
- [ ] Add range-resumable downloads. CA/hostname validation, bounded retry/backoff, byte limits, and streaming SHA-256 are complete.
- [x] Complete alternating metadata selection, bidirectional inactive-slot validation, and v1→v2 compatibility.

## Phase 3 — activation and fault testing

- [x] Replace normal RAM-disk reads with a read-only image-file backend.
- [x] Track active MSC reads and require an explicit button activation.
- [x] Implement controlled TinyUSB disconnect/reconnect and older-record boot fallback.
- Fault-inject power loss at every storage transition.
- Measure Windows compatibility, boot-to-drive time, read speed, and thermal/power behavior.

## Phase 4 — publisher and fleet controls

- Build Drive-to-object-storage publisher with malware/content policy checks.
- Add signed scoped manifests, per-device credentials, revocation, audit logs, and telemetry.
- Add signed firmware OTA, staged rollout, and recovery documentation.
