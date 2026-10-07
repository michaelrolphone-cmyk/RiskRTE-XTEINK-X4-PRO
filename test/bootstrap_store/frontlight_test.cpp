#include <BoardX4Pro.h>
#include <cassert>
#include "../../src/native/NativeBatteryGauge.h"

// This isolated frontlight fixture deliberately has no optional battery. The
// production battery owner/cache is tested by run_native_battery_gauge_test.sh.
static unsigned batteryReads;
bool nativeBatteryReadSnapshot(NativeBatterySnapshot* out) {
 ++batteryReads;
 if(out)*out={};
 return false;
}
static unsigned writes;static uint16_t level;
static bool set(void*,uint16_t n,uint16_t maximum){assert(maximum==1000);level=n;++writes;return true;}
static bool get(void*,uint16_t* n,uint16_t* maximum){*n=level;*maximum=1000;return true;}
int main(){
 assert(!BoardX4Pro::capabilities().hasBacklight);
 BoardX4Pro::setBacklightLevel(2);assert(!writes);
 risc_frontlight_api_v1 api{1,sizeof(api),nullptr,set,get};
 auto bad=api;bad.struct_size=8;assert(!BoardX4Pro::attachFrontlight(&bad));
 assert(BoardX4Pro::attachFrontlight(&api));assert(BoardX4Pro::capabilities().hasBacklight);
 BoardX4Pro::restoreBacklightLevel(2);assert(level==2 && writes==1);
 BoardX4Pro::setBacklightLevel(0);assert(level==0 && writes==2);
 BoardX4Pro::setBacklightLevel(10);assert(level==1000 && writes==3);
 assert(!batteryReads); // Frontlight must not activate or read the gauge.
 BoardX4Pro::BatteryState state{};
 assert(!BoardX4Pro::readBatteryState(&state));
 assert(batteryReads==1 && !state.gaugeReadOk);
}

bool halStoragePrepareForSleep() { return true; }
void halStorageMediaUnavailable() {}
