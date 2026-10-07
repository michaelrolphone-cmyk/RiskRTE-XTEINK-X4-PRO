#include "x4pro_proto.h"
#include <stdio.h>

static int failures;

static void expect(int cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", message);
        ++failures;
    }
}

int main(void) {
    expect(x4pro_cw2017_millivolts(0x0F, 0xFF) == (uint16_t)(((0x0FFFu * 5u + 8u) >> 4)), "vcell");
    uint16_t x = 0, y = 0;
    uint8_t id = 0;
    uint8_t point[8] = {10, 0, 20, 0, 0, 0, 0, 3};
    expect(x4pro_gt911_map(point, &x, &y, &id) && x == 10 && y == 20 && id == 1, "portrait touch");
    point[0] = 224;
    point[1] = 1;
    expect(!x4pro_gt911_map(point, &x, &y, &id), "x range");
    uint8_t frame[6];
    x4pro_sd_command(0, 0, frame);
    expect(frame[0] == 0x40 && (frame[5] & 1u), "cmd0 host direction");
    static const uint8_t crc_sample[] = "123456789";
    expect(x4pro_sd_crc16(crc_sample, 9u) == 0x31c3u, "native SD data CRC16");
    if (failures) return 1;
    puts("x4pro protocol: PASS");
    return 0;
}
