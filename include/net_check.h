#ifndef NET_CHECK_H
#define NET_CHECK_H

// Post-connection reachability probes.
//
// A school network can authenticate the device perfectly and still drop its
// traffic on the floor, so "associated" and "working" are tracked as separate
// states. Each leg is probed and reported independently.

enum NetCheckResult {
    NC_UNKNOWN = 0,
    NC_OK,
    NC_FAIL
};

void netcheck_init();

// Non-blocking driver: runs at most one probe per call
void netcheck_loop();

// Queue a full run (also happens automatically after every new connection)
void netcheck_request();

bool netcheck_running();

NetCheckResult netcheck_dns();
NetCheckResult netcheck_data();
NetCheckResult netcheck_ntp();

// Hostname the data-source probe targets ("" when the source needs no network)
const char* netcheck_data_host();

// One-line human summary, e.g. "Connected, but share2.dexcom.com is blocked"
const char* netcheck_summary();

// True when the device is online but at least one required leg failed
bool netcheck_has_problem();

// millis() of the last completed run, 0 if never
unsigned long netcheck_last_run_ms();

#endif // NET_CHECK_H
