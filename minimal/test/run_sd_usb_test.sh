#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime required}" "${RISCRTE_READER_ROOT:?Reader required}" "${TINYUSB_SOURCE:?TinyUSB required}"
if [[ -n "${X4_SD_USB_TEST_OUT:-}" ]];then build="$X4_SD_USB_TEST_OUT";else build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT;fi
mkdir -p "$build"
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
python3 "$RISCRTE_READER_ROOT/scripts/prepare_usb_device_stack.py" --source "$TINYUSB_SOURCE" --output "$build/stack"
flags=(-O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-overflow -I"$build/sdk" -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" -I"$RISCRTE_READER_ROOT/Drivers/x4pro_board" -I"$RISCRTE_READER_ROOT" -I"$build/stack" -I"$RISCRTE_READER_ROOT/Drivers/usb_device_msc_esp32s3")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cc -std=c11 "${flags[@]}" -c "$root/minimal/test/sd_usb_test.c" -o "$build/sd.o"
cc -std=c11 "${flags[@]}" -Dt5_driver_get=usb_t5_driver_get -c "$RISCRTE_READER_ROOT/Drivers/usb_device_msc_esp32s3/driver.c" -o "$build/usb.o"
cppflags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-return-type -I"$root/minimal/test/bootlog/stubs" -I"$build/sdk" "-DX4_BOOTLOG_INTERNAL_ROOT=\"$build/appdata\"")
if [[ "${SANITIZE:-0}" == 1 ]];then cppflags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
g++ "${cppflags[@]}" -c "$root/minimal/test/bootlog/usb_fixture.cpp" -o "$build/native.o"
for source in "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ff.c" "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ffunicode.c" "$RISCRTE_READER_ROOT/Drivers/usb_device_msc_esp32s3/StackDefaults.c" "$build/stack/tusb.c" "$build/stack/common/tusb_fifo.c" "$build/stack/device/usbd.c" "$build/stack/device/usbd_control.c" "$build/stack/class/msc/msc_device.c";do
 cc -std=c11 "${flags[@]}" -c "$source" -o "$build/$(basename "$source").o"
done
g++ "${flags[@]}" "$build"/*.o -o "$build/test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$build/test" "${X4_SD_READ_MS:-8}" "${X4_SD_WRITE_MS:-20}" "${X4_SD_USB_SCENARIO:-full}"
