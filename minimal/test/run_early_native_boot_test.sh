#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
exec python3 "$root/minimal/test/run_pinned_early_boot_test.py" "$@"
