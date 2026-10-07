"""Execute the production boot diagnostic state, including damaged retention."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    (root/'Arduino.h').write_text("#pragma once\n#include <cstdint>\nextern uint32_t nowMs; inline unsigned long millis(){return nowMs;}\n")
    (root/'esp_attr.h').write_text("#define RTC_NOINIT_ATTR\n")
    (root/'esp_system.h').write_text("enum {ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,ESP_RST_SDIO};\n")
    (root/'Logging.h').write_text("#pragma once\nvoid capture(const char*,...);\n#define LOG_INF(tag, ...) capture(__VA_ARGS__)\n#define LOG_ERR(tag, ...) capture(__VA_ARGS__)\n")
    source=root/'test.cpp'
    source.write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
uint32_t nowMs=0;
std::vector<std::string> lines;
void capture(const char* fmt,...) { char text[256]; va_list ap;va_start(ap,fmt);
  const int n=vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);assert(n>=0 && n<256);lines.emplace_back(text); }
// Match firmware headers included before this interface by main.cpp.
#define Serial MySerialImpl::instance
#define Storage HalStorage::getInstance()
#include "platform/X4BootDiagnostics.cpp"
using namespace X4BootDiagnostics;
bool contains(const char* part){for(const auto& l:lines)if(l.find(part)!=std::string::npos)return true;return false;}
void failBoot(){begin(ESP_RST_POWERON);nowMs=501;mark(Stage::Packages);nowMs=20501;fail("SD handoff refused");}
int main(){
  // Unpowered/cold RTC garbage is never interpreted as boot evidence.
  memset(&retained,0xa5,sizeof(retained));begin(ESP_RST_POWERON);poll(true);
  assert(!previousValid && contains("previous record unavailable"));
  lines.clear();failBoot();
  char message[]="first copied failure";fail(message);message[0]='!';
  // Four minutes of offline heartbeats cannot replace the first failure.
  for(unsigned i=0;i<120;++i){nowMs+=2000;poll(false);}
  assert(lines.empty());poll(true);
  assert(contains("stage=packages") && contains("at_ms=501") && contains("failure_ms=20501"));
  assert(contains("SD handoff refused") && !contains("first copied failure"));
  const auto count=lines.size();for(unsigned i=0;i<120;++i)poll(true);assert(lines.size()==count);
  poll(false);poll(true);assert(lines.size()==2*count);
  for(auto reset:{ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP}){
    failBoot();lines.clear();begin(reset);poll(true);
    assert(previousValid && contains("previous-retained") && contains("SD handoff refused"));
    assert(retained.failureStage==static_cast<uint32_t>(Stage::Count));
  }
  for(auto reset:{ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_BROWNOUT,ESP_RST_SDIO}){
    failBoot();lines.clear();begin(reset);poll(true);assert(!previousValid && !contains("SD handoff refused"));
  }
  // Reset during a checkpoint update, corruption, wrong schema and invalid enums fail closed.
  for(unsigned fault=0;fault<5;++fault){
    failBoot();
    if(fault==0)retained.stageAtMs++;
    if(fault==1)retained.magic++;
    if(fault==2){retained.stage=99;retained.checksum=digest(retained);}
    if(fault==3){retained.failureStage=99;retained.checksum=digest(retained);}
    if(fault==4){memset(retained.reason,'x',sizeof(retained.reason));retained.checksum=digest(retained);}
    begin(ESP_RST_SW);assert(!previousValid);
  }
  for(uint32_t stage=0;stage<static_cast<uint32_t>(Stage::Count);++stage){
    begin(ESP_RST_POWERON);nowMs=UINT32_MAX-5;mark(static_cast<Stage>(stage));
    char copied[]="copied reason";fail(copied);copied[0]='!';
    nowMs=5;mark(Stage::Ready);fail("later failure");
    assert(retained.failureStage==stage && retained.failureAtMs==UINT32_MAX-5);
    assert(!strcmp(retained.reason,"copied reason") && valid(retained));
  }
  begin(ESP_RST_POWERON);char longReason[512];memset(longReason,'x',511);longReason[511]=0;
  mark(Stage::Frontlight);fail(longReason);lines.clear();poll(true);
  assert(strlen(retained.reason)==111 && valid(retained));
  mark(Stage::Count);assert(retained.stage==static_cast<uint32_t>(Stage::Frontlight));
  begin(ESP_RST_SW);assert(previousValid); // No log truncation even at maximum field lengths.
  lines.clear();poll(true);assert(contains("previous-retained reason="));
  puts("Production X4 boot diagnostics: offline4min, reconnect, first failure, supported resets, cold/brownout refusal, corruption, bounded text PASS");
}
''')
    binary=root/'test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-DBOARD_XTEINK_X4_PRO=1','-I'+str(root),'-I'+str(ROOT/'src'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10,env=os.environ)
