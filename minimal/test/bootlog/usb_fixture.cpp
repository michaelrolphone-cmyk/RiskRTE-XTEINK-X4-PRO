#define main original_logger_test_main
#include "logger_test.cpp"
#undef main
extern "C" void usb_native_fixture_boot(void) {
 resetRam(true);noFiles();boot();mounted();productionBoot(20);
 // Preserve the prior boot transcript and NVS history across a cold boot.
 resetRam();boot();mounted();
}
extern "C" void usb_native_fixture_continue(void) {productionBoot(20);}
extern "C" unsigned long long usb_native_fixture_size(void) {return X4BootLog::recoveryBytes+X4BootLog::traceBytes;}
extern "C" void usb_native_fixture_progress(void) {emit("RTE_STAGE us=1 usb preparation progress result=pending");}
