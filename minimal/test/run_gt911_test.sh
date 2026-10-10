#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" "$root/minimal/test/gt911_test.c" "$root/minimal/drivers/x4pro_gt911/driver.c" -o "$build/test"
for scenario in validation source-equivalence startup fallback no-chip subscriptions parallel-consumers events boundaries gaps overflow ack-failure read-failure owner-reentry release-retry pin-release-retry off-retry hold-retry hold-retained retire-retry destroy-retry claim-retained irq-claim-retained strap-release-retry reset-failure probe-release-retry bus-claim-retained unlock-retained; do "$build/test" "$scenario"; done
for scenario in power-subscriptions power-owner power-neutral power-prepare-release power-off-retained power-hold-refusal power-hold-retained power-unhold-refusal power-unhold-retained power-alternate-release power-second-release power-no-chip power-claim-refusal power-bus-retained power-empty-claim power-unlock-prepare power-unlock-resume power-zero power-timeout power-clock-reverse power-clock-stalled power-clock-overrun power-expiry-matrix power-final-prepared power-final-recovered power-final-partial power-primary-ack power-custody power-prepare-expiry-matrix power-final-expiry-matrix power-first-failure; do "$build/test" "$scenario"; done
for n in {1..7}; do "$build/test" "power-write-$n"; done
for n in {1..4}; do "$build/test" "power-claim-$n"; "$build/test" "power-pin-release-$n"; done
for n in {1..3}; do "$build/test" "power-transfer-$n"; done
# Optional target check uses the same canonical SDK and production flags.
if [[ -n "${NATIVE_DRIVER_CC:-}" ]]; then
  elf="$build/driver.elf"
  "$NATIVE_DRIVER_CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
    -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
    -I"$build/sdk" -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
    "$root/minimal/drivers/x4pro_gt911/driver.c" -lgcc -o "$elf"
  "${PYTHON:-python3}" "$RISCRTE_READER_ROOT/scripts/normalize_xtensa_relocations.py" "$elf"
  "${PYTHON:-python3}" "$RISCRTE_READER_ROOT/scripts/validate_xtensa_relative_targets.py" "$elf"
  "${NATIVE_DRIVER_CC%gcc}readelf" --dyn-syms --wide "$elf" > "$build/symbols.txt"
  "${NATIVE_DRIVER_CC%gcc}objdump" -d "$elf" > "$build/disassembly.txt"
  "${PYTHON:-python3}" - "$elf" "$build/symbols.txt" "$build/disassembly.txt" <<'PY'
import hashlib
from pathlib import Path
import sys
elf, symbols, disassembly = map(Path, sys.argv[1:])
blob = elf.read_bytes()
assert blob[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(blob[18:20], 'little') == 94
rows = [line.split() for line in symbols.read_text().splitlines()]
imports = {r[7] for r in rows if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] == 'UND'}
exports = {r[7] for r in rows if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] != 'UND' and r[3] == 'FUNC'}
assert imports <= {'strcmp', 'memcpy', 'memset'}, imports
assert exports == {'t5_driver_get'}, exports
assert 's32c1i' not in disassembly.read_text().lower(), 'PSRAM-unsafe compare-and-set'
print('X4 ordinary GT911 Xtensa PASS:', len(blob), hashlib.sha256(blob).hexdigest(), 'imports:', ','.join(sorted(imports)))
PY
fi
