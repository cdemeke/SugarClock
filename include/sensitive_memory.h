#pragma once
#include <stddef.h>

// Volatile writes keep credential cleanup from being optimized away.
inline void sensitive_erase(void* data, size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
    while (size--) *p++ = 0;
}

template<class T> class SensitiveScope {
    T& value;
public:
    explicit SensitiveScope(T& v) : value(v) {}
    ~SensitiveScope() { sensitive_erase(&value, sizeof(value)); }
    SensitiveScope(const SensitiveScope&) = delete;
    SensitiveScope& operator=(const SensitiveScope&) = delete;
};
