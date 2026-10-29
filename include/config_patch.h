#pragma once
#include "config_manager.h"
#include "libre_patient.h"
#include <ArduinoJson.h>
// Patch only supplied fields. No I/O. Error is a stable field/reason identifier.
// A newly selected Libre person must appear in `people`, the tested list for
// the configured account; without it any new selection is rejected.
const char* config_patch(AppConfig& candidate, JsonObjectConst patch, bool web=false,
                         const LibrePatientSnapshot* people=nullptr);
void config_public(JsonObject output, const AppConfig& config);
void config_schema(JsonArray output);

bool config_source_changed(const AppConfig& before, const AppConfig& after);
