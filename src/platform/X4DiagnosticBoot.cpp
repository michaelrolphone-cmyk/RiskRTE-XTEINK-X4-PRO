#include "X4DiagnosticBoot.h"
#include "X4BootDiagnostics.h"

#if defined(BOARD_XTEINK_X4_PRO)

#include "RiscDisplayOutputV1.h"
#include "RiscFrontlightV1.h"
#include "RiscInputNavigationV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include "SdBootReader.h"
#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "fontIds.h"
#include "runtime/display/ProviderDisplaySurface.h"

#include <Board.h>
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <RiscI2cBusV1.h>
#include <RiscTouchV1.h>
#include <cstring>
#include <esp_heap_caps.h>
#include "runtime/network/PsramTlsAllocator.h"
#include <memory>
#include <initializer_list>
#include <new>

#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/StartupScreen.h"
#include "native/NativeNavigationInput.h"
#include "native/NativeTouchInput.h"
#include "native/NativeBatteryGauge.h"
#include "util/ButtonNavigator.h"
#include "runtime/drivers/ProviderModuleV2.h"

extern bool setupDisplayAndFonts();
extern void setupReaderState();
extern void prepareReaderApplication(bool deskClockUserWake);
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
extern ActivityManager activityManager;

namespace {
RuntimeInstalledProviders::Lease display_lease, light_lease, storage_lease;
const risc_display_output_api_v1 *display_api = nullptr;
const risc_frontlight_api_v1 *light_api = nullptr;
uint8_t surface[48000];
std::unique_ptr<ProviderDisplaySurface> provider_surface;
bool ready = false;
bool showing_home = false;
bool startup_prepared = false;

bool acquire(const char* capability, RuntimeInstalledProviders::Lease& lease) {
    if (RuntimeInstalledProviders::acquireCapability(capability,1,&lease)) return true;
    LOG_ERR("X4", "External capability %s unavailable: %s",capability,
            RuntimeInstalledProviders::lastError());
    return false;
}
bool bindDisplay() {
    if (provider_surface && provider_surface->isReady() && display_api) return true;
    const auto refuse = []() {
        display.detachProvider(); provider_surface.reset(); display_api = nullptr;
        display_lease.interface = nullptr; return false;
    };
    if (display_lease.grant.slot) {
        if (!RuntimeInstalledProviders::release(&display_lease)) {
            display_lease.interface = nullptr;
            return false;
        }
    }
    if (!acquire("display.output", display_lease)) return false;
    display_api = static_cast<const risc_display_output_api_v1*>(display_lease.interface);
    risc_display_info_v1 info{};
    if (!display_api || display_api->api_version != RISC_DISPLAY_OUTPUT_API_V1 ||
        display_api->struct_size < sizeof(*display_api) || !display_api->get_info ||
        !display_api->get_info(display_api->context, &info) ||
        info.width != 800 || info.height != 480 ||
        info.preferred_format != RISC_DISPLAY_FORMAT_MONO1) {
        LOG_ERR("X4", "display.output geometry rejected");
        return refuse();
    }
    provider_surface.reset(new (std::nothrow) ProviderDisplaySurface(display_api, surface,
                           sizeof(surface), 480, 800, 20000));
    if (!provider_surface || !provider_surface->isReady()) {
        LOG_ERR("X4", "display.output surface adapter rejected");
        return refuse();
    }
    if (!display.attachProvider(*provider_surface)) {
        LOG_ERR("X4", "Reader display facade rejected provider");
        return refuse();
    }
    return true;
}
}

bool x4BeginClockDisplay() {
    if (psramFound()) heap_caps_malloc_extmem_enable(1024);
    return loadPlatformSdPackages() && bindDisplay();
}
bool x4DrainProvidersForSleep() {
    RuntimeInstalledProviders::Lease retained[3];
    size_t count = 0;
    for (const auto* lease : {&display_lease, &light_lease, &storage_lease})
        if (lease->grant.slot) retained[count++] = *lease;
    return RuntimeInstalledProviders::drainExcept(retained, count);
}
bool x4ReleaseDisplayForSleep() {
    if (!provider_surface || !provider_surface->supportsSleep() || !x4DrainProvidersForSleep()) return false;
    display.detachProvider();
    provider_surface.reset();
    display_api = nullptr;
    const bool released = !display_lease.grant.slot || RuntimeInstalledProviders::release(&display_lease);
    display_lease.interface = nullptr;
    return released && x4DrainProvidersForSleep();
}
bool x4RestoreDisplayAfterSleep() {
    // No provider acquisition observes frozen Storage. Failed releases keep
    // their exact token; bindDisplay completes cleanup before reacquisition.
    if (!Storage.cancelSleep() || !bindDisplay()) return false;
    display.begin(false);
    return display.isReady();
}

void x4DiagnosticSetup(bool deskClockUserWake) {
    LOG_INF("X4", "diagnostic entered");
    // Match the shared firmware allocation policy before UI/app allocations.
    if (psramFound()) heap_caps_malloc_extmem_enable(1024);
    RuntimeNetwork::enablePsramTlsAllocations();
    LOG_INF("X4", "diagnostic boot %s flash=16MB app0=0x10000", Board::firmwareMarker());
    using X4BootDiagnostics::Stage;
    const auto refused = [](const char* reason) { X4BootDiagnostics::fail(reason); };
    X4BootDiagnostics::mark(Stage::Packages);
    if (!loadPlatformSdPackages()) {
        refused("boot packages or SD handoff unavailable");
        LOG_ERR("X4", "External boot packages unavailable or invalid; no embedded fallback");
        return;
    }
    X4BootDiagnostics::mark(Stage::StorageMount);
    if (!acquire("storage.volume",storage_lease)) { refused(RuntimeInstalledProviders::lastError()); return; }
    auto* volume=static_cast<const risc_storage_volume_api_v1*>(storage_lease.interface);
    if (!volume || volume->api_version!=1 || volume->struct_size<sizeof(*volume)) {
        refused("storage.volume API rejected"); return;
    }
    const bool storage_mounted=Storage.bindVolume(volume);
    char reason[80]="none";
    if(volume->last_error) (void)volume->last_error(volume->context,reason,sizeof(reason));
    LOG_INF("X4","storage.volume mounted=%d reason=%s",storage_mounted?1:0,reason);
    X4BootDiagnostics::mark(Stage::Frontlight);
    if (!acquire("display.frontlight",light_lease)) { refused(RuntimeInstalledProviders::lastError()); return; }
    light_api=static_cast<const risc_frontlight_api_v1*>(light_lease.interface);
    if (!Board::attachFrontlight(light_api)) { refused("frontlight API rejected"); return; }
    // Shared input consumers acquire real graph leases, with the same normal
    // dependency/lifecycle path as T5. No bootstrap pointer attachment bypass.
    X4BootDiagnostics::mark(Stage::Navigation);
    nativeNavigationTick();
    X4BootDiagnostics::mark(Stage::Display);
    if (!bindDisplay()) { refused("display provider binding rejected; see provider diagnostics"); return; }
    // BoardX4Pro's power hooks do not touch T5S3 peripherals. Initialize
    // the shared power mutex before any native app takes its UI/power lock.
    X4BootDiagnostics::mark(Stage::PowerManager);
    powerManager.begin();
    X4BootDiagnostics::mark(Stage::Settings);
    SETTINGS.loadFromFile();
    // Optional rtc.clock/API2 recovers cold-boot time through the installed
    // provider. A missing package/chip or invalid time must never gate Home.
    // Deep sleep retains SDK time; no external read is needed on minute wakes.
    X4BootDiagnostics::mark(Stage::Rtc);
    halClock.begin();
    halClock.configure(SETTINGS.timeZoneId, SETTINGS.rtcStoresUtc != 0,
                       SETTINGS.rtcVariantHint, SETTINGS.rtcReferenceEpoch);
    if (!halClock.isSystemTimeValid() &&
        (!halClock.isAvailable() || !halClock.syncSystemTimeFromRtc())) {
        LOG_INF("CLK", "RTC time unavailable; clock remains unset until explicit synchronization");
    }
    // The bus deliberately rejects contending transactions rather than waiting.
    // Finish the one-shot RTC boot read before starting GT911's independent
    // capture task, so normal touch polling cannot make valid time look absent.
    X4BootDiagnostics::mark(Stage::Touch);
    nativeTouchTick();
    LOG_INF("X4","input.touch ready=%d",nativeTouchAvailable()?1:0);
    I18N.setLanguage(static_cast<Language>(SETTINGS.language));
    UITheme::getInstance().reload();
    X4BootDiagnostics::mark(Stage::Fonts);
    if (!setupDisplayAndFonts()) { refused("display or font setup rejected"); return; }
    display.setFlipOutput(SETTINGS.flipUi != 0);
    X4BootDiagnostics::mark(Stage::ReaderState);
    setupReaderState();
    // Reader's four button hints are laid out in portrait coordinates. The
    // physical X4 is portrait when held with its controls upright; landscape
    // left the Classic Home menu with a negative usable height.
    renderer.setOrientation(GfxRenderer::Portrait);
    if (renderer.getScreenWidth() != 480 || renderer.getScreenHeight() != 800) {
        refused("Reader portrait geometry rejected");
        LOG_ERR("X4", "Reader portrait geometry rejected");
        return;
    }
    Board::restoreBacklightLevel(SETTINGS.backlightLevel);
    // Reuse the same complete RiscRTE logo as T5's renderer fallback. X4
    // presents one static frame under its existing provider display owner.
    if (!deskClockUserWake) {
    X4BootDiagnostics::mark(Stage::Splash);
    StartupScreen::staticLogo(renderer);
    LOG_INF("X4", "boot splash present=%d", provider_surface->lastPresentSucceeded() ? 1 : 0);
    provider_surface->clearPresentStatus();
    }
    const auto &home = UITheme::getInstance().getMetrics();
    const int menu_height = renderer.getScreenHeight() -
        (home.homeTopPadding + home.homeCoverTileHeight + home.homeMenuTopOffset +
         home.buttonHintsHeight);
    const int required_menu_height = 3 * home.menuRowHeight + 2 * home.menuSpacing;
    if (menu_height < required_menu_height) {
        refused("Home menu geometry rejected");
        LOG_ERR("X4", "Home menu geometry rejected height=%d required=%d",
                menu_height, required_menu_height);
        return;
    }
    LOG_INF("X4", "home geometry width=%d height=%d menu_height=%d",
            renderer.getScreenWidth(), renderer.getScreenHeight(), menu_height);
    ButtonNavigator::setMappedInputManager(mappedInputManager);
    // setup and loop share the invocation-owner task. Prime the copied battery
    // snapshot after storage/input startup, before Home queues its first render.
    // The optional provider may be absent or fail; it never gates Home startup.
    X4BootDiagnostics::mark(Stage::Battery);
    nativeBatteryTick();
    X4BootDiagnostics::mark(Stage::HomePrepare);
    // The installed default entry (or once-only fallback) chooses the same
    // Home/book destination on its first pump. Do not create a duplicate UI.
    prepareReaderApplication(deskClockUserWake);
    startup_prepared = true;
    LOG_INF("X4", "Reader startup prepared=1");

}

bool x4ReaderStartupReady() { return startup_prepared; }
void x4ReaderActivityScheduled() {
    X4BootDiagnostics::mark(X4BootDiagnostics::Stage::HomePresent);
    showing_home = true;
    LOG_INF("X4", "home activity scheduled=1");
}

bool x4DiagnosticLoop() {
    static unsigned long last = 0;
    if (showing_home && !ready && provider_surface && provider_surface->lastPresentSucceeded()) {
        ready = true;
        X4BootDiagnostics::mark(X4BootDiagnostics::Stage::Ready);
        LOG_INF("X4", "home present=1");
    }
    const unsigned long now = millis();
    if (now - last >= 2000) {
        last = now;
        X4BootDiagnostics::poll(static_cast<bool>(logSerial));
        LOG_INF("X4", "heartbeat ready=%d", ready ? 1 : 0);
    }
    if (!ready) {
        delay(200);
        return false;
    }
    const risc_input_navigation_frame_v1 frame_in = nativeNavigationFrame();
    if (showing_home) {
        static uint32_t input_sequence = 0;
        constexpr struct { uint32_t bit; const char *name; } events[] = {
            {RISC_NAV_LEFT, "left"}, {RISC_NAV_RIGHT, "right"},
            {RISC_NAV_CONFIRM, "confirm"}, {RISC_NAV_BACK, "back"}
        };
        for (const auto &event : events) {
            if (frame_in.pressed & event.bit)
                LOG_INF("X4", "input.navigation event=%s sequence=%lu", event.name,
                        static_cast<unsigned long>(++input_sequence));
        }
    }
    return true;
}

#endif  // BOARD_XTEINK_X4_PRO
