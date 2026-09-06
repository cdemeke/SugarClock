#pragma once
#include "config_manager.h"
#include <ArduinoJson.h>
// Patch only supplied fields. No I/O. Error is a stable field/reason identifier.
const char* config_patch(AppConfig& candidate, JsonObjectConst patch, bool web=false);
void config_public(JsonObject output, const AppConfig& config);
void config_schema(JsonArray output);

bool config_source_changed(const AppConfig& before, const AppConfig& after);
