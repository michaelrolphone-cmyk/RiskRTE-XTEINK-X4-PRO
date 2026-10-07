#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime with touch.i2c v2 required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
runtime="$RISCRTE_RUNTIME_ROOT"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
c++ "${flags[@]}" -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs" \
 "$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" \
 "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" "$runtime/src/ports/esp32s3/CpuPort.cpp" \
 "$root/minimal/test/profile_materialize.cpp" -ldl -o "$build/materialize"
for panel in ssd1677 uc8279; do
 python3 "$root/minimal/scripts/generate_profile.py" --panel "$panel" --output "$build/$panel"
 "$build/materialize" "$build/$panel"
 python3 "$root/minimal/scripts/generate_profile.py" --panel "$panel" --sleep --output "$build/$panel-sleep"
 "$build/materialize" "$build/$panel-sleep" sleep
done
python3 - "$build" <<'PY'
import json,shutil,sys
from pathlib import Path
root=Path(sys.argv[1])
for name in ("overlap","missing","foreign"):
 out=root/name;shutil.copytree(root/"uc8279-sleep",out)
 p=out/"board.json";b=json.loads(p.read_text());nav=next(d for d in b["devices"] if d["instance_id"]==6)
 if name=="overlap":nav["config"]["pins"].append(3)
 elif name=="missing":b["devices"]=[d for d in b["devices"] if d["instance_id"]!=17]
 else:nav["bindings"]["x4.power"]=7
 p.write_text(json.dumps(b))
PY
for bad in overlap missing foreign;do "$build/materialize" "$build/$bad" sleep reject;done
if [[ "${X4_GRAPH_ONLY:-0}" == 1 ]];then exit 0;fi
export X4_PANEL_TYPED_CONFIG_SSD="$build/ssd1677/config-3.bin"
export X4_PANEL_TYPED_CONFIG_UC="$build/uc8279/config-3.bin"
export X4_SD_TYPED_CONFIG="$build/ssd1677/config-9.bin"
bash "$root/minimal/test/run_panel_test.sh"
bash "$root/minimal/test/run_sd_test.sh"
