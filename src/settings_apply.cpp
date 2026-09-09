#include "settings_apply.h"
#include "config_patch.h"
#include "http_client.h"
#include "glucose_engine.h"
#include "display.h"
#include <cstdlib>
#include <ctime>

bool settings_apply(const AppConfig& candidate) {
    ConfigGuard guard;
    const bool sourceChanged=config_source_changed(config_get(),candidate);
    config_get()=candidate;
    const bool saved=config_save();
    // Rejection can restore the committed config; a pending journal retains the
    // candidate. Use the actual outcome, never the pre-save candidate, for effects.
    const AppConfig& active=config_get();
    if(sourceChanged || !saved) http_configuration_changed();
    engine_rebuild_toggle_order();
    if(!active.auto_brightness) display_set_brightness(active.brightness);
    setenv("TZ",active.timezone,1);tzset();
    return saved;
}
