#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

extern "C" uint32_t panel_cadence_clock(void);
extern "C" void panel_cadence_delay(uint32_t);
extern "C" void panel_cadence_poll(uint32_t);
extern "C" void panel_handoff_after_poll(void);
static RiscBoot::Runtime *active_runtime;
static unsigned wait_calls,wait_ms,max_wait;
extern "C" void panel_runtime_yield(uint32_t ms) { active_runtime->yield(ms); }
extern "C" void panel_runtime_poll(uint32_t ms) { panel_cadence_poll(ms);panel_handoff_after_poll(); }
extern "C" void panel_runtime_reset_metrics(void) { wait_calls=wait_ms=max_wait=0; }
extern "C" void panel_runtime_expect_idle(uint32_t ms) { assert(wait_calls==1&&wait_ms==ms&&max_wait==ms); }
extern "C" void panel_runtime_report(void) {
    printf("\"scheduler_waits\":%u,\"scheduler_wait_ms\":%u,\"max_requested_wait_ms\":%u",wait_calls,wait_ms,max_wait);
}
static void write(const std::string& path,const char *text) { std::ofstream(path)<<text; }
int main(int argc,char **argv) {
    assert(argc==2);const std::string root=argv[1];
    write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"cadence","revision":"unspecified","buses":[],"devices":[]})");
    write(root+"/provider.json",R"({"type":"driver","id":"cadence-proxy","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.cadence","api":1}]})");
    write(root+"/boot.json",R"({"board":"board.json","default_app":"app.elf","drivers":[{"manifest":"provider.json"}]})");
    RiscBoot::Runtime runtime({[](){return true;},[](risc_runtime_health_v1 *out){out->uptime_ms=panel_cadence_clock();return true;},
        [](uint32_t ms){assert(ms>0&&ms<=50);++wait_calls;wait_ms+=ms;if(ms>max_wait)max_wait=ms;panel_cadence_delay(ms);},
        [](const char*){return true;}});
    active_runtime=&runtime;assert(runtime.prepare(root.c_str()) && runtime.run() && !runtime.retained());
}
