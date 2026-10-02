#!/usr/bin/env python3
"""Verify the checked-in demo assets without depending on PowerShell."""
from hashlib import sha256
from pathlib import Path

EXPECTED = {
    "examples/assets/inter_medium_16.font": "e31e494cee22d7a7b03355d70862e6631993a55dc75f23b3e0aa896bb1369e83",
    "examples/assets/inter_medium_16.rgba": "9d67b0f6edfe34294fecd4888e8058a9c5e21e1fa2589b8ff873496b6230e626",
}


def main():
    root = Path(__file__).resolve().parent.parent
    for relative, expected in EXPECTED.items():
        path = root / relative
        if not path.is_file():
            raise SystemExit(f"Required demo asset is missing: {relative}")
        actual = sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f"SHA-256 mismatch for {relative}: expected {expected}, got {actual}")
    print("Demo asset SHA-256 checks passed.")


if __name__ == "__main__":
    main()
