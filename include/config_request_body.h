#ifndef CONFIG_REQUEST_BODY_H
#define CONFIG_REQUEST_BODY_H
#include <stddef.h>
#include <string.h>

// One allocation per HTTP request; never share credential-bearing body chunks.
struct ConfigRequestBody {
    size_t received;
    size_t total;
    char data[4096];

    bool append(const void* chunk, size_t length, size_t index, size_t expected) {
        if (!expected || expected >= sizeof(data) || index != received ||
            index > expected || length > expected - index || (received && total != expected)) return false;
        total = expected;
        memcpy(data + index, chunk, length);
        received += length;
        data[received] = '\0';
        return true;
    }
};
#endif
