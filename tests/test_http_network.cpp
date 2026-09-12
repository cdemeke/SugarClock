#include <cassert>
#include <thread>
#include <future>
#include "../src/http_client.cpp"
#include "../src/net_task.cpp"

std::atomic<uint32_t> test_time_offset{1000};
static AppConfig fixture_config{};
static bool fixture_wifi = true;
static NightscoutResult fixture_result{};
static bool fixture_ok = true;
static std::atomic<bool> fixture_block{false};
static std::atomic<bool> fixture_entered{false};
AppConfig& config_get() { return fixture_config; }
bool config_has_server() { return true; }
bool wifi_is_connected() { return fixture_wifi; }
void weather_poll_tick() {}
bool nightscout_fetch(const NightscoutConfig&, NightscoutResult& result) {
    fixture_entered = true;
    while (fixture_block) std::this_thread::yield();
    result = fixture_result;
    return fixture_ok;
}
static void await_request() {
    for (int i=0; i<1000; ++i) {
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        bool pending = force_pending_id != 0;
        xSemaphoreGive(data_mutex);
        if (pending) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "forced request was not queued");
}
int main() {
    fixture_config.data_source = 4;
    fixture_config.poll_interval_sec = 60;
    http_init();
    assert(!http_has_ever_received() && !http_has_delta());
    fixture_result = {110, TREND_FLAT, 1700000300, 20, true, 100, 1700000000, 200, ""};
    assert(fetch_nightscout_reading());
    assert(http_get_reading().glucose == 110 && http_get_delta() == 10 && http_has_delta());
    GlucoseHistoryEntry history[48];
    assert(http_get_history(history, 48) == 2);
    assert(http_time_since_last_reading() >= 20000);
    test_time_offset += 30000;
    assert(fetch_nightscout_reading()); // repeated successful HTTP cannot reset age
    assert(http_time_since_last_reading() >= 50000);
    assert(http_get_history(history,48) == 2);
    fixture_result.timestamp -= 300;
    fixture_result.glucose = 90;
    assert(!fetch_nightscout_reading());
    assert(http_get_reading().glucose == 110 && http_get_history(history,48)==2);
    fixture_ok = false;
    assert(!fetch_nightscout_reading());
    assert(http_get_reading().glucose == 110);
    fixture_ok = true;
    fixture_result.timestamp = 1700001300; // gap exceeds ten minutes
    fixture_result.glucose = 120;
    fixture_result.has_previous = false;
    fixture_result.age_sec = UINT32_MAX;
    assert(fetch_nightscout_reading());
    assert(!http_has_delta() && http_time_since_last_reading() == UINT32_MAX);
    test_time_offset += UINT32_MAX - 1000; // cross a millis rollover while offline
    assert(http_time_since_last_reading() == UINT32_MAX);
    test_time_offset += 2000;
    assert(http_time_since_last_reading() == UINT32_MAX);
    http_reset_source();
    assert(!http_get_reading().valid && !http_has_ever_received());
    assert(http_get_history(history,48)==0 && http_get_failure_count()==0);

    fixture_config.data_source = 2;
    fixture_wifi = false;
    http_reset_source();
    auto demo = std::async(std::launch::async, []{ return http_force_fetch(1000); });
    await_request(); http_poll_tick();
    assert(demo.get() && http_get_reading().valid);

    // Request A times out while fetching. Its late completion must not satisfy B.
    fixture_config.data_source = 4;
    fixture_wifi = true;
    http_reset_source();
    fixture_result.age_sec = 5;
    fixture_block = true;
    fixture_entered = false;
    auto a = std::async(std::launch::async, []{ return http_force_fetch(40); });
    await_request();
    auto worker = std::async(std::launch::async, []{ http_poll_tick(); });
    while (!fixture_entered) std::this_thread::yield();
    assert(!a.get());
    char force_message[128];
    http_get_force_error(force_message, sizeof(force_message));
    assert(strstr(force_message, "timed out"));
    auto b = std::async(std::launch::async, []{ return http_force_fetch(1000); });
    await_request();
    fixture_block = false;
    worker.get();
    assert(b.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    fixture_ok = false;
    http_poll_tick();
    assert(!b.get());
    http_get_force_error(force_message, sizeof(force_message));
    assert(!force_message[0]); // a real fetch error uses the HTTP diagnostics
    http_set_paused(true);
    assert(!http_force_fetch(10));
    http_get_force_error(force_message, sizeof(force_message));
    assert(strstr(force_message, "busy or paused"));
    http_set_paused(false);

    // Run one iteration of the real worker: its gate must stay locked through
    // the transport call, so config/OTA cannot overtake an in-flight response.
    http_reset_source();
    fixture_ok = true;
    fixture_block = true;
    fixture_entered = false;
    net_task_start();
    auto polling = std::async(std::launch::async, [] {
        try { net_task_fn(nullptr); } catch (const StopNetworkLoop&) {}
    });
    while (!fixture_entered) std::this_thread::yield();
    assert(!net_task_quiesce(5));
    fixture_block = false;
    polling.get();
    assert(net_task_quiesce(100));
    http_reset_source();
    net_task_resume();
    assert(!http_get_reading().valid);
}
