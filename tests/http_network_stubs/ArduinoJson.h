#pragma once
#include "Arduino.h"
// Compile-only surface for unrelated generic/Dexcom parsing. The Nightscout
// parser has its own tests; these exercise the actual publishing/polling code.
struct JsonValue {
    JsonValue operator[](const char*) const { return {}; }
    JsonValue operator[](int) const { return {}; }
    template<class T> JsonValue& operator=(const T&) { return *this; }
    template<class T> T operator|(T fallback) const { return fallback; }
    template<class T> bool is() const { return false; }
    template<class T> T as() const { return T(); }
    size_t size() const { return 0; }
};
using JsonDocument = JsonValue;
using JsonArray = JsonValue;
using JsonObject = JsonValue;
struct DeserializationError {
    operator bool() const { return true; }
    const char* c_str() const { return "stub"; }
};
inline DeserializationError deserializeJson(JsonDocument&, const String&) { return {}; }
inline void serializeJson(const JsonDocument&, String&) {}
