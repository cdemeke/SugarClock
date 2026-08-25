#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <stdint.h>
#include <stddef.h>

struct WebAsset {
    const char* path;
    const uint8_t* data;
    size_t size;
    const char* mime_type;
    const char* etag;
};

const WebAsset* find_web_asset(const char* path);
size_t get_web_assets_count();
const WebAsset* get_web_asset_at(size_t index);

#endif // WEB_ASSETS_H
