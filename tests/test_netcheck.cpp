#include "net_check.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>

constexpr unsigned long SETTLE_MS=4000, RERUN_INTERVAL_MS=900000;
unsigned long now=10000,generation=0;
unsigned long millis() {return now;}
unsigned long http_fetch_generation() {return generation;}
int responseCode=0;
int http_get_last_response_code() {return responseCode;}
struct AppConfig {bool glucose_enabled=true;int data_source=0;} cfg;
AppConfig& config_get() {return cfg;}
bool connected=true;
bool wifi_is_connected() {return connected;}
bool dnsOk=true,dataOk=false,ntpOk=true,leaseAvailable=true;
unsigned probes=0,leases=0,releases=0,hostsResolved=0;
void resolve_data_host() {++hostsResolved;}
bool probe_dns() {return dnsOk;}
bool probe_data() {++probes;return dataOk;}
bool probe_ntp() {return ntpOk;}
bool ble_acquire_network() {if(!leaseAvailable)return false;++leases;return true;}
void ble_release_network() {++releases;}
struct {template<class... T> void printf(const char*,T...) {}} Serial;
#include "netcheck.inc"

void finish_probes() {
 now+=SETTLE_MS;
 for(unsigned i=0;i<4;++i) netcheck_loop();
}

int main() {
 netcheck_init();netcheck_loop();finish_probes();
 assert(!netcheck_running() && netcheck_data()==NC_FAIL);
 assert(probes==1 && leases==releases);
 assert(strstr(netcheck_summary(),"cannot reach") && !strstr(netcheck_summary(),"blocked"));
 // Old unpublished/current-source-unknown success cannot clear a failed probe.
 responseCode=200;netcheck_loop();assert(netcheck_data()==NC_FAIL);
 // A completed real fetch recovers immediately, without another TLS probe or
 // waiting for the diagnostic interval. No glucose value is needed here.
 ++generation;netcheck_loop();
 assert(netcheck_data()==NC_OK && netcheck_dns()==NC_OK && !netcheck_has_problem());
 assert(probes==1);
 // Periodic diagnostics retain same-source evidence while rechecking time.
 netcheck_request();finish_probes();
 assert(netcheck_data()==NC_OK && netcheck_dns()==NC_OK && probes==1);

 // Source changes discard the previous HTTP success, and wait for evidence
 // from the newly configured source. A new authentication failure still proves
 // reachability without claiming that provider authentication succeeded.
 cfg.data_source=1;netcheck_configuration_changed();netcheck_loop();
 assert(netcheck_data()==NC_UNKNOWN && !netcheck_summary()[0]);
 finish_probes();assert(netcheck_data()==NC_FAIL && probes==2);
 responseCode=401;++generation;netcheck_loop();assert(netcheck_data()==NC_OK);

 // New successful traffic arriving during settle bypasses redundant DNS/TLS.
 netcheck_configuration_changed();netcheck_loop();
 responseCode=200;++generation;netcheck_loop();
 assert(netcheck_data()==NC_OK);dnsOk=false;
 finish_probes();assert(probes==2);
 assert(netcheck_data()==NC_OK && netcheck_dns()==NC_OK);dnsOk=true;
 assert(!netcheck_running());

 // A reconnect cannot reuse reachability from the old Wi-Fi connection.
 connected=false;netcheck_loop();connected=true;netcheck_loop();
 finish_probes();assert(netcheck_data()==NC_FAIL && probes==3);
 responseCode=-1;++generation;netcheck_loop();assert(netcheck_data()==NC_FAIL);
 responseCode=200;++generation;netcheck_loop();assert(netcheck_data()==NC_OK);

 // Synthetic readings cannot establish real source reachability.
 cfg.data_source=2;netcheck_configuration_changed();netcheck_loop();
 ++generation;netcheck_loop();assert(netcheck_data()==NC_UNKNOWN);
 // Source invalidation also works while disconnected.
 connected=false;cfg.data_source=0;netcheck_configuration_changed();netcheck_loop();
 assert(netcheck_data()==NC_UNKNOWN && !netcheck_running());
 connected=true;netcheck_loop();finish_probes();
 assert(netcheck_data()==NC_FAIL && probes==4);
 assert(leases==releases && hostsResolved>=5);
}
