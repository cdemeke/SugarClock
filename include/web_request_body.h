#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sensitive_memory.h"

// Owns a complete body until the handler returns. Partial bodies belong to the
// request and are wiped by its disconnect callback before the library frees it.
class WebRequestBody {
    struct Buffer {
        size_t total;
        size_t received;
        char* data() { return reinterpret_cast<char*>(this + 1); }
    };
    Buffer* complete = nullptr;

    static void release(Buffer* buffer) {
        if (!buffer) return;
        const size_t size = sizeof(Buffer) + buffer->total + 1;
        sensitive_erase(buffer, size);
        free(buffer);
    }
    template<class Request> static void discard(Request* request) {
        release(static_cast<Buffer*>(request->_tempObject));
        request->_tempObject = nullptr;
    }
public:
    WebRequestBody() = default;
    ~WebRequestBody() { release(complete); }
    WebRequestBody(const WebRequestBody&) = delete;
    WebRequestBody& operator=(const WebRequestBody&) = delete;
    const char* data() const { return complete->data(); }
    size_t size() const { return complete->total; }

    template<class Request>
    bool receive(Request* request, const uint8_t* data, size_t len,
                 size_t index, size_t total, size_t maximum) {
        // Ignore trailing callbacks after an error or a completed operation.
        if (request->getResponse()) { discard(request); return false; }
        if (total > maximum || total > SIZE_MAX - sizeof(Buffer) - 1 ||
            index > total || len > total - index) {
            discard(request);
            request->send(413, "application/json", "{\"error\":\"Body too large\"}");
            return false;
        }
        auto* buffer = static_cast<Buffer*>(request->_tempObject);
        if (!total || !len || !data || (buffer ? total != buffer->total || index != buffer->received : index != 0)) {
            discard(request);
            request->send(400, "application/json", "{\"error\":\"Invalid body chunks\"}");
            return false;
        }
        if (!buffer) {
            buffer = static_cast<Buffer*>(calloc(1, sizeof(Buffer) + total + 1));
            if (!buffer) {
                request->send(503, "application/json", "{\"error\":\"busy\"}");
                return false;
            }
            buffer->total = total;
            request->_tempObject = buffer;
            request->onDisconnect([request]() { discard(request); });
        }
        memcpy(buffer->data() + index, data, len);
        buffer->received += len;
        if (buffer->received != buffer->total) return false;
        complete = buffer;
        request->_tempObject = nullptr;
        return true;
    }
};
