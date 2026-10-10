# X4 0.1.55 fresh recovery build: pre-execution review

Status: source-union/command freeze pending. No final product build has started.

## Build boundary

The final invocation creates a new absent build root. It records UTC start, source commits/trees, hashes of every builder and SDK input, and compiler identity before compiling. It rejects tracked/untracked product outputs as source inputs and refuses any existing output directory. Product builders receive only source roots, generated SDK headers, pinned vendor/toolchain dependencies and output paths beneath that root. No baseline BIN, store ELF, earlier target directory, qualification ELF, or dist directory is a command input. Old artifacts may be inspected separately for behavior/ABI comparison but are not read by the build or assembler.

## Source-generated output groups (47 ELFs)

1. Native: combined Runtime0.1.106 (274bc66f), X4 early boot source plus optional17 requirements/18 policies, image cache, DIO, USB PHY,512-byte retained wake, failure evidence and stage logs. prepare_native_runtime.py creates a new source workspace; PlatformIO compiles with telemetry disabled and no old .pio cache. The official pinned vendor Arduino/ESP-IDF static libraries remain toolchain dependencies. app_data_image.py creates new empty AppData; stage validates all newly generated native artifacts. Fresh bootloader and partition outputs come from this same invocation.
2. X4 providers10: board,battery,i2c,frontlight,power,power-buttons,rtc,sd,touch,panel. build_drivers.py uses the coherent X4 source union, canonical Runtime106+Reader SDK and the pinned GCC8.4 compiler. Selected fast panel is exact public0.1.13; SD0.2.14 and exact recovered GT9110.1.9 are source overlays. Unselected fallback panel/buttons outputs, if produced by the legacy builder, are never installed.
3. Shared USB MSC1: Reader45cf61ac, pinned TinyUSB1eb6ce78, production stack patcher and build_usb_device_msc.py; new output only.
4. Radio providers7: BLE HID58ad371 with pinned NimBLE, IQ4088b689, Wi-Fi/HCI exact Watch5b9fd573 sources, BLE sensors/telemetry and battery telemetry Drivers4088b689. The battery provider uses the verified System flags-layout header. HCI retains its recovered no-SONAME/no-relax/text-literals flags.
5. Service providers5: telemetry-broadcast from the recovered Utilities source; Contexts RF-only source918c730a via its29-input hash guard; alarm0.4.8 publice8017235 native-UTC source closure; updater apps0.1.4 and source-routes firmware0.1.5 (last source-equivalence verification pending).
6. UI providers3: scene0.1.4, text0.1.2 and portrait monochrome profile0.1.1 from the single reconciled System source. Keep portrait270/touch0 selection.
7. System applications8: default Home, Springboard, Settings, file browser, Wi-Fi Settings, OTA update, App Store and USB transfer. One coherent System source, preserving app-specific flags, shared text, resident ownership and smooth-scroll selections. Corrected Home includes both external portable_idle_sleep.c and portable_sleep.c, crash-spool namespace62, SD custody status and native17/18. USB1.6 retains pre-release evidence and fails closed after Runtime revocation.
8. Utilities applications9: Alarms,Battery,Calculator,Stopwatch,Countdown,Scanner,Touchpad,Buttons,Waterfall. One coherent Utilities source; exact Scanner.19 prerequisite preserved before its reconstructed successor. App-specific historical flags and interface grants are carried forward.
9. Other applications4: Points,Timecard,Contexts,GameBoy. Source-only owner recipes. GameBoy a579dedd compiles recovered core/UI and reconstructed capability backend. Points successor keeps full preserved build flags; Timecard/Contexts app source commands are being finalized by their source owners.

## Assembly and checks

- Store starts empty. It accepts only the47 ELF+manifest pairs named in the build ledger, all resolved under the new build root with hashes recorded immediately after their compile steps. Compaction runs on those new ELFs and retains original debug sections separately.
- Board/boot declarations are committed source JSON derived from the existing deployment authority, then narrowly updated for the final Home requirements and provider versions. They are generated into the new store. No old store is copied or mounted.
- Native BIN export names/values must agree with its fresh ELF. All47 modules undergo production loader/import/relocation validation against that native. Complete graph/grants admission runs normally and with ASan/UBSan using exact17/18 limits.
- Generate cohort identity from new native bytes and final published product source commit. Pack bootfs twice and compare; read it with both independent SPIFFS decoders and require identical97-path contents.
- Generate OTA initial state and paired-bank SHA/CRC records from the new firmware/store. Assemble a new16MiB image initialized to0xff at fixed partition offsets. Every populated range must map to a ledger output generated in this build; check overlap/size and complete readback.
- Compare final manifests/graph and source flags with the reviewed intended changes. Report changed/unchanged component bytes as observations after the fresh build, never as permission to reuse a binary.
- Source bundles, final commands, build receipts and checksum manifest are preserved with the full0x0 BIN. No flashing or hardware test is performed.

## Pending before execution

Final System/Utilities remote union IDs, source-routes updater closure, Contexts/Timecard app commands, and the final executable orchestration manifest. Root reviews that manifest before packaging. Source recovery complete then build begins; no additional user approval is required.
