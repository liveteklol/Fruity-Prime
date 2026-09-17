#!/usr/bin/env bash
# Check native and imported sources/packages without proprietary game files.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 tools/check-maps-shipped.py "${1:-.}/maps"
