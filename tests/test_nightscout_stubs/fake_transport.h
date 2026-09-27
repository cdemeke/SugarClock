#ifndef NIGHTSCOUT_FAKE_TRANSPORT_H
#define NIGHTSCOUT_FAKE_TRANSPORT_H
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <algorithm>
#include <cstdio>
#include <new>
struct FakeNightscout {
    uint32_t clock = 0;
    uint32_t byte_delay = 0;
    uint32_t next_byte = 0;
    bool stall = false;
    bool header_trickle = false;
    bool open = true;
    bool begin_ok = true;
    int code = 200;
    int size = -1;
    size_t offset = 0;
    std::string body;
    std::string encoding;
    std::string url;
    std::map<std::string, std::string> headers;
    bool ca_bundle = false;
    unsigned ca_count = 0;
    int follow_redirects = -1;
    int connect_timeout = 0;
    int read_timeout = 0;
    int handshake_timeout = 0;
    bool http10 = false;
    bool ended = false;
};
extern FakeNightscout fake;
inline unsigned long millis() { return fake.clock; }
inline void delay(unsigned long ms) { fake.clock += ms; }
class String : public std::string {
public:
    String() = default;
    String(const char* s) : std::string(s) {}
    String(const std::string& s) : std::string(s) {}
};
class Stream {
public:
    virtual ~Stream() = default;
    void setTimeout(unsigned long) {}
};
class WiFiClient : public Stream {
public:
    virtual int available() {
        if (!fake.open || fake.stall || fake.clock < fake.next_byte) return 0;
        return static_cast<int>(fake.body.size() - fake.offset);
    }
    virtual int read() { uint8_t ch; return read(&ch, 1) == 1 ? ch : -1; }
    virtual int read(uint8_t* buf, size_t size) {
        if (!available()) return -1;
        size = std::min(size, fake.body.size() - fake.offset);
        if (fake.byte_delay) size = std::min<size_t>(size, 1);
        memcpy(buf, fake.body.data() + fake.offset, size); fake.offset += size;
        fake.next_byte = fake.clock + fake.byte_delay;
        return static_cast<int>(size);
    }
    virtual uint8_t connected() { return fake.open && (fake.stall || fake.header_trickle || fake.offset < fake.body.size()); }
    virtual void stop() { fake.open = false; }
};
class WiFiClientSecure : public WiFiClient {
public:
    void setCACertBundle(const uint8_t* bundle) { fake.ca_bundle = bundle != nullptr; fake.ca_count = (bundle[0] << 8) | bundle[1]; }
    void setHandshakeTimeout(unsigned long timeout) { fake.handshake_timeout = static_cast<int>(timeout); }
    void setTimeout(unsigned long) {}
};
static const int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
static const int HTTP_CODE_OK = 200;
class HTTPClient {
public:
    void setConnectTimeout(int t) { fake.connect_timeout = t; }
    void setTimeout(int t) { fake.read_timeout = t; }
    void setFollowRedirects(int value) { fake.follow_redirects = value; }
    void useHTTP10(bool value) { fake.http10 = value; }
    void setReuse(bool) {}
    void collectHeaders(const char*[], size_t) {}
    bool begin(WiFiClient& client, const char* url) { client_ = &client; fake.url = url; return fake.begin_ok; }
    void addHeader(const char* key, const char* value) { fake.headers[key] = value; }
    int GET() {
        if (fake.header_trickle) {
            while (client_->connected()) { fake.clock += 100; client_->read(); }
            return -11;
        }
        return fake.code;
    }
    void end() { fake.ended = true; client_->stop(); }
    int getSize() { return fake.size; }
    String header(const char*) { return fake.encoding; }
    bool connected() { return client_->connected(); }
    WiFiClient* getStreamPtr() { return client_; }
private:
    WiFiClient* client_ = nullptr;
};
#endif
