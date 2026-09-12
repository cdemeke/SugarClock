#pragma once
#include "config_manager.h"
#include "http_client.h"

// Complete stale frame, using the actual persisted settings and reading types.
void glucose_render_stale(const GlucoseReading& reading, const AppConfig& config,
                          uint8_t brightness);

// Complete transient delta frame; timing and freshness remain engine decisions.
void glucose_render_delta_flash(int delta, uint16_t color, bool use_mmol);
