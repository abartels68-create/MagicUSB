"""Dependency-free checks for repository artifacts; run with Python 3."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    schema = json.loads((ROOT / "schemas/release-manifest.schema.json").read_text())
    config = json.loads((ROOT / "config/device.example.json").read_text())

    assert schema["properties"]["schema_version"]["const"] == 1
    assert config["update_endpoint"].startswith("https://")
    assert "example" in config["update_endpoint"]

    source = (ROOT / "main/ram_disk.c").read_text()
    assert "b[510] = 0x55; b[511] = 0xaa" in source
    assert 'memcpy(root, "README  TXT", 11)' in source
    assert "tud_msc_is_writable_cb" in (ROOT / "main/usb_msc.c").read_text()

    print("Scaffold checks passed")


if __name__ == "__main__":
    main()
