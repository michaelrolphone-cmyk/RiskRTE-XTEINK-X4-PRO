#include "x4pro_mmio.h"
#include <stdio.h>

static int failures;
static void expect(int cond, const char *message) {
    if (!cond) { fprintf(stderr, "FAIL %s\n", message); ++failures; }
}
int main(void) {
    expect(!x4pro_pin_high_bank(0) && !x4pro_pin_high_bank(31), "low bank");
    expect(x4pro_pin_high_bank(32) && x4pro_pin_high_bank(38) && x4pro_pin_high_bank(42), "high bank");
    expect(x4pro_pin_bit(0) == 0 && x4pro_pin_bit(31) == 31 && x4pro_pin_bit(32) == 0, "bit index");
    expect(x4pro_pin_bit(38) == 6 && x4pro_pin_bit(39) == 7 && x4pro_pin_bit(40) == 8, "high bits");
    expect(x4pro_pin_mask(41) == (1u << 9) && x4pro_pin_mask(42) == (1u << 10), "sd masks");
    expect(x4pro_out_w1ts(12) == 0x60004008u && x4pro_out_w1ts(39) == 0x60004014u, "out set bank");
    expect(x4pro_in_reg(6) == 0x6000403cu && x4pro_in_reg(40) == 0x60004040u, "in bank");
    expect(x4pro_enable_w1tc(3) == 0x60004028u && x4pro_enable_w1tc(38) == 0x60004034u, "enable bank");
    expect(x4pro_pin_valid(0) && x4pro_pin_valid(48) && !x4pro_pin_valid(49), "range");
    expect(x4pro_iomux_reg(0) == 0x60009004u && x4pro_iomux_reg(6) == 0x6000901cu, "mux");
    if (failures) return 1;
    puts("x4pro gpio: PASS");
    return 0;
}
