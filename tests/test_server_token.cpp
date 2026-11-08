#include <cassert>
#include <cstring>
#include "config_patch.h"

static AppConfig configured() {
    AppConfig c={};
    c.thresh_urgent_low=55;c.thresh_low=70;c.thresh_high=180;c.thresh_urgent_high=250;
    c.alert_low=70;c.alert_high=250;
    strcpy(c.server_url,"https://ns.example.com/api/v1/entries.json");
    strcpy(c.auth_token,"stored-token");
    return c;
}

static AppConfig patched(const char* json,bool web=true) {
    AppConfig c=configured();
    JsonDocument patch;assert(!deserializeJson(patch,json));
    assert(!config_patch(c,patch.as<JsonObjectConst>(),web));
    return c;
}

int main() {
    // The token must never follow the URL to a different server.
    for(const char* json:{
            "{\"server_url\":\"https://attacker.example/collect\"}",
            "{\"server_url\":\"https://attacker.example/collect\",\"auth_token\":\"\"}",
            "{\"server_url\":\"http://ns.example.com/api/v1/entries.json\"}",
            "{\"server_url\":\"https://ns.example.com:8443/api/v1/entries.json\"}",
            "{\"server_url\":\"https://ns.example.com.attacker.example/\"}",
            "{\"server_url\":\"https://user@ns.example.com/\"}",
            "{\"server_url\":null}"}) {
        assert(!patched(json).auth_token[0]);
        assert(!patched(json,false).auth_token[0]);
    }
    // Same server: path, query and host-case edits keep the token.
    for(const char* json:{
            "{\"server_url\":\"https://ns.example.com/api/v2/entries.json?count=1\"}",
            "{\"server_url\":\"https://NS.example.com/other\"}",
            "{\"server_url\":\"\"}",
            "{\"brightness\":40}"}) {
        assert(!strcmp(patched(json).auth_token,"stored-token"));
    }
    // A token supplied together with the new server is that server's token.
    assert(!strcmp(patched("{\"server_url\":\"https://new.example/\",\"auth_token\":\"new-token\"}").auth_token,"new-token"));
    // First-time setup sends both fields together.
    AppConfig empty={};empty.thresh_urgent_low=55;empty.thresh_low=70;empty.thresh_high=180;empty.thresh_urgent_high=250;
    empty.alert_low=70;empty.alert_high=250;
    JsonDocument setup;deserializeJson(setup,"{\"server_url\":\"https://ns.example.com/\",\"auth_token\":\"first\"}");
    assert(!config_patch(empty,setup.as<JsonObjectConst>(),true) && !strcmp(empty.auth_token,"first"));
    return 0;
}
