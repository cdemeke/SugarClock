#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sensitive_memory.h"

// ArduinoJson copies parsed strings. Wipe its temporary credential copies too,
// including allocations replaced during document shrinking or growth.
class SensitiveJsonAllocator : public ArduinoJson::Allocator {
    struct alignas(std::max_align_t) Header { size_t size; };
public:
    void* allocate(size_t size) override {
        if (size > SIZE_MAX - sizeof(Header)) return nullptr;
        auto* header = static_cast<Header*>(malloc(sizeof(Header) + size));
        if (!header) return nullptr;
        header->size = size;
        return header + 1;
    }
    void deallocate(void* ptr) override {
        if (!ptr) return;
        auto* header = static_cast<Header*>(ptr) - 1;
        const size_t size = sizeof(Header) + header->size;
        sensitive_erase(header, size);
        free(header);
    }
    void* reallocate(void* ptr, size_t size) override {
        if (!size) { deallocate(ptr); return nullptr; }
        void* next = allocate(size);
        if (!next) return nullptr;
        if (ptr) {
            const size_t previous = (static_cast<Header*>(ptr) - 1)->size;
            memcpy(next, ptr, previous < size ? previous : size);
            deallocate(ptr);
        }
        return next;
    }
};
