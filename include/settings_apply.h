#pragma once
#include "config_manager.h"
// Apply a validated candidate and reconcile runtime effects with the configuration
// retained by the persistence transaction, including rejection/recovery outcomes.
bool settings_apply(const AppConfig& candidate);
