// Real frontlight provider and Runtime CPU GPIO/sync custody; only physical
// GPIO operations are modeled. This is a host regression, not an electrical test.
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <RiscFrontlightV1.h>
#include <cassert>
#include <cstdio>

using RiscCpu::Hardware;
using RiscCpu::Port;

namespace {
struct Pad {
    bool held = true, high = false, pwm = false;
    uint64_t lastClaim = 0, retiredToken = 0;
};
struct Model {
    Pad pads[2];
    unsigned io = 0, opens = 0, reclaims = 0, retires = 0, refusals = 0;
    int refusePin = -1;
    uint64_t refusedToken = 0;
} model;
Port* active;
const risc_driver_v2* driver;
const risc_frontlight_api_v1* frontlight;

unsigned index(uint8_t pin) {
    assert(pin == 8 || pin == 9);
    return pin - 8;
}
void locked() {
    assert(active->syncs_[0].locks[0].held);
    assert(!active->providerStorageSafe() && !active->appExitSafe());
}
Hardware hardware() {
    Hardware h{};
    h.owner = []() { return true; };
    h.gpioOpen = [](uint8_t pin, bool output, bool initial, bool pullup) {
        locked();
        auto& pad = model.pads[index(pin)];
        const auto& claim = active->pins_[pin];
        assert(output && !initial && !pullup);
        assert(claim.owner == &active->gpios_[0] && !claim.token);
        // CpuPort allocates a fresh token before asking the hardware to open.
        assert(active->serial_ > pad.lastClaim);
        if (claim.retiredHeld) {
            assert(claim.held && pad.held && !pad.high && !pad.pwm);
            assert(pad.retiredToken == pad.lastClaim);
            ++model.reclaims;
        }
        pad.lastClaim = active->serial_;
        // Model the native open contract: stage the requested LOW first,
        // then release the boot/retired hold. A HIGH request fails above.
        pad.high = initial;
        pad.pwm = false;
        assert(!pad.high);
        pad.held = false;
        ++model.io; ++model.opens;
        return true;
    };
    h.gpioWrite = [](uint8_t pin, bool high) {
        locked();
        auto& pad = model.pads[index(pin)];
        assert(!pad.held && active->pins_[pin].token == pad.lastClaim);
        pad.high = high; pad.pwm = false; ++model.io;
        return true;
    };
    h.gpioPwm = [](uint8_t pin, uint32_t hz, uint16_t duty, uint16_t maximum) {
        locked();
        auto& pad = model.pads[index(pin)];
        assert(!pad.held && active->pins_[pin].token == pad.lastClaim);
        assert(hz == 25000 && maximum == 1024 && duty && duty < maximum);
        pad.pwm = true; ++model.io;
        return true;
    };
    h.deepHold = [](uint8_t pin, bool enable) {
        locked();
        auto& pad = model.pads[index(pin)];
        // Relighting must reclaim through gpioOpen, never unhold an old token.
        assert(enable && !pad.high && !pad.pwm && !pad.held);
        pad.held = true; ++model.io;
        return true;
    };
    h.gpioClose = [](uint8_t) {
        assert(!"Frontlight must retire held LOW instead of releasing a pad");
        return false;
    };
    return h;
}
bool retire(void* context, uint64_t token) {
    locked();
    assert(context == &active->gpios_[0]);
    for (uint8_t pin : {uint8_t(8), uint8_t(9)}) {
        auto& claim = active->pins_[pin];
        if (claim.token != token) continue;
        auto& pad = model.pads[index(pin)];
        assert(token && claim.owner == context && claim.held && !claim.retiredHeld);
        assert(pad.held && !pad.high && !pad.pwm && token == pad.lastClaim);
        if (model.refusePin == pin) {
            if (model.refusedToken) assert(model.refusedToken == token);
            model.refusedToken = token; ++model.refusals;
            return false;
        }
        assert(Port::gpioRetireHeldOutput(context, token));
        assert(!claim.token && claim.retiredHeld && claim.held);
        pad.retiredToken = token; ++model.retires;
        return true;
    }
    assert(!"Retirement must use a current exact scoped token");
    return false;
}
struct Fixture {
    Port port{hardware()};
    risc_hw_gpio_bank_v1 config{};
    risc_hardware_device_v1 device{};
    risc_provider_dependency_v1 deps[3]{};
    Fixture() {
        active = &port; model = Model{};
        // Bind only the real CPU scopes needed by this provider; JSON/profile
        // materialization is covered separately by the profile integration test.
        auto& g = port.gpios_[0];
        g.port = &port; g.instance = 1;
        g.input = g.output = (uint64_t(1) << 8) | (uint64_t(1) << 9);
        g.api.api_version = 1; g.api.struct_size = sizeof(g.api); g.api.context = &g;
        g.api.claim = Port::gpioClaim; g.api.write = Port::gpioWrite;
        g.api.read = Port::gpioRead; g.api.pwm = Port::gpioPwm;
        g.api.release = Port::gpioRelease; g.api.deep_sleep_hold = Port::gpioDeepSleepHold;
        g.api.retire_held_output = retire;
        port.gpioCount_ = 1;
        auto& s = port.syncs_[0];
        s.port = &port; s.instance = 1;
        s.api = {1, sizeof(s.api), &s, Port::syncOwner, Port::syncCreate,
                 Port::syncTryLock, Port::syncUnlock, Port::syncDestroy};
        port.syncCount_ = 1;
        config.struct_size = sizeof(config); config.count = 2; config.active_high = 1;
        config.pins[0] = 8; config.pins[1] = 9;
        device = {1, sizeof(device), 1, "xteink,x4-pro-frontlight", "unspecified",
                  "gpio.bank", 1, sizeof(config), &config};
        deps[0] = {"hardware.device", 1, &device};
        deps[1] = {"platform.gpio", 1, &g.api};
        deps[2] = {"platform.sync", 1, &s.api};
        assert(port.providerStorageSafe() && port.appExitSafe() && port.quiescent());
    }
    bool start() { return driver->start(deps, 3); }
    void dark() const {
        for (uint8_t pin : {uint8_t(8), uint8_t(9)}) {
            const auto& pad = model.pads[index(pin)];
            assert(pad.held && !pad.high && !pad.pwm && port.pins_[pin].held);
        }
    }
    void safeOff() const {
        dark();
        for (uint8_t pin : {uint8_t(8), uint8_t(9)}) {
            const auto& claim = port.pins_[pin];
            assert(claim.owner == &port.gpios_[0] && !claim.token && claim.retiredHeld);
        }
        assert(port.providerStorageSafe() && port.appExitSafe());
    }
    void clean() const {
        safeOff();
        assert(!port.syncs_[0].locks[0].token && port.quiescent());
    }
};
void level(uint16_t expected) {
    uint16_t value = 17, maximum = 19;
    assert(frontlight->get_level(nullptr, &value, &maximum));
    assert(value == expected && maximum == 1024);
}
void lifecycle() {
    Fixture f;
    assert(f.start()); f.safeOff(); level(0);
    assert(model.opens == 2 && model.retires == 2 && !model.reclaims);
    const unsigned initialIo = model.io;
    assert(frontlight->set_level(nullptr, 0, 1));
    assert(model.io == initialIo && model.retires == 2);
    // Retired generations cannot perform operations or release the held pad.
    auto& api = f.port.gpios_[0].api;
    for (const auto& pad : model.pads) {
        assert(!api.write(api.context, pad.retiredToken, true));
        assert(!api.release(api.context, pad.retiredToken));
        assert(!Port::gpioRetireHeldOutput(api.context, pad.retiredToken));
    }
    auto otherScope = f.port.gpios_[0];
    uint64_t foreign = 42;
    assert(!Port::gpioClaim(&otherScope, 8, true, false, false, &foreign) && !foreign);
    assert(model.io == initialIo);
    assert(frontlight->set_level(nullptr, 1, 2)); level(512);
    assert(model.opens == 4 && model.reclaims == 2);
    for (uint8_t pin : {uint8_t(8), uint8_t(9)}) {
        const auto& claim = f.port.pins_[pin];
        const auto& pad = model.pads[index(pin)];
        assert(claim.token == pad.lastClaim && claim.token != pad.retiredToken);
        assert(claim.owner == &f.port.gpios_[0] && !claim.held && !claim.retiredHeld && pad.pwm);
    }
    assert(frontlight->set_level(nullptr, 0, 1)); f.safeOff(); level(0);
    assert(model.retires == 4);
    assert(frontlight->set_level(nullptr, 1, 1)); level(1024);
    assert(model.opens == 6 && model.reclaims == 4);
    assert(driver->quiesce()); f.clean(); driver->stop();
    assert(f.start()); f.safeOff();
    assert(model.opens == 8 && model.reclaims == 6);
    assert(driver->quiesce()); f.clean();
    puts("Frontlight + CpuPort: start/off custody, fresh same-scope LOW relight PASS");
}
void refusedRetirement(bool atStart, uint8_t pin) {
    Fixture f;
    uint64_t expected = 0;
    if (!atStart) {
        assert(f.start()); f.safeOff();
        assert(frontlight->set_level(nullptr, 3, 4));
        expected = f.port.pins_[pin].token;
    }
    model.refusePin = pin;
    assert(!(atStart ? f.start() : frontlight->set_level(nullptr, 0, 1)));
    if (atStart) expected = model.pads[index(pin)].lastClaim;
    assert(expected && model.refusals && model.refusedToken == expected);
    f.dark();
    const auto& claim = f.port.pins_[pin];
    assert(claim.token == expected && claim.owner == &f.port.gpios_[0]);
    assert(claim.held && !claim.retiredHeld);
    const auto& peer = f.port.pins_[pin == 8 ? 9 : 8];
    assert(!peer.token && peer.held && peer.retiredHeld);
    assert(!f.port.providerStorageSafe() && !f.port.appExitSafe() && !f.port.quiescent());
    const unsigned io = model.io, opens = model.opens;
    uint16_t value = 17, maximum = 19;
    assert(!frontlight->get_level(nullptr, &value, &maximum) && value == 17 && maximum == 19);
    assert(!frontlight->set_level(nullptr, 1, 1));
    assert(!frontlight->set_level(nullptr, 0, 1));
    assert(!f.start()); driver->stop();
    assert(model.io == io && model.opens == opens && claim.token == expected);
    assert(!driver->quiesce() && claim.token == expected && claim.held);
    assert(!f.port.providerStorageSafe() && !f.port.appExitSafe());
    assert(model.io == io); // Retry never writes or unholds established LOWs.
    model.refusePin = -1;
    assert(driver->quiesce()); f.clean();
    assert(model.io == io && model.opens == opens);
    assert(model.pads[index(pin)].retiredToken == expected);
    assert(f.start()); f.safeOff();
    assert(driver->quiesce()); f.clean();
    std::printf("Frontlight + CpuPort: %s retirement refusal GPIO%u retains exact token, fences calls, retries PASS\n",
                atStart ? "start" : "off", unsigned(pin));
}
} // namespace

int main() {
    driver = t5_driver_get(2);
    assert(driver && driver->quiesce && driver->capability);
    frontlight = static_cast<const risc_frontlight_api_v1*>(driver->capability);
    lifecycle();
    for (uint8_t pin : {uint8_t(8), uint8_t(9)}) {
        refusedRetirement(true, pin);
        refusedRetirement(false, pin);
    }
}
