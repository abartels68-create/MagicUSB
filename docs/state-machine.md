# Runtime state machine

```mermaid
stateDiagram-v2
    [*] --> Boot
    Boot --> CachedReady: active image valid
    Boot --> Recovery: no valid image
    CachedReady --> Connecting: USB already available
    Connecting --> Offline: timeout or network error
    Connecting --> Checking: connected
    Offline --> Checking: later reconnect
    Checking --> CachedReady: no newer eligible release
    Checking --> Downloading: newer release
    Downloading --> Verifying: complete
    Downloading --> CachedReady: failure, retain active
    Verifying --> UpdateReady: size, hash, signature valid
    Verifying --> CachedReady: invalid, retain active
    UpdateReady --> Switching: idle window, button, or next boot
    Switching --> CachedReady: re-enumerated on new image
    Switching --> Recovery: activation validation failed
    Recovery --> CachedReady: fallback slot valid
```

Every SCSI read is counted while in progress. Button activation first disconnects the USB medium and waits for the active-read count to reach zero before changing the file handle. No automatic activation occurs. On every power cycle, firmware evaluates both committed metadata records and selects the newest record whose complete image passes size and SHA-256 verification.
