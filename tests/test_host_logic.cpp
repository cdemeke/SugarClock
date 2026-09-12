#include "ota_policy.h"
#include "fleet_policy.h"
#include "double_click_detector.h"
#include "semver.h"
#include "libre_session.h"
#include "libre_patient.h"
#include "config_manager.h"

#include <assert.h>
#include <string.h>
#include <thread>

// Fake LibreLinkUp server for driving LibreSession through whole poll cycles
struct FakeLibre : public LibreTransport {
    int logins = 0;
    int reads = 0;
    LibreAuthResult login_result = LIBRE_AUTH_OK;
    LibreReadResult read_result = LIBRE_READ_OK;
    int unauthorized_reads_left = 0;  // refuse this many reads first
    int glucose = 131;
    int arrow = 3;
    const char* timestamp = "9/10/2026 6:40:00 PM";

    LibreAuthResult login() override { logins++; return login_result; }
    LibreReadResult read_latest(LibreRawReading& out) override {
        reads++;
        if (unauthorized_reads_left > 0) { unauthorized_reads_left--; return LIBRE_READ_UNAUTHORIZED; }
        if (read_result != LIBRE_READ_OK) return read_result;
        out.glucose = glucose;
        out.trend_arrow = arrow;
        strncpy(out.factory_timestamp, timestamp, sizeof(out.factory_timestamp) - 1);
        return LIBRE_READ_OK;
    }
};

static const uint32_t LIBRE_TS = 1789065600UL;  // 9/10/2026 6:40:00 PM UTC
static const uint32_t MIN_MS = 60UL * 1000UL;

static void test_libre_timestamps_and_trends() {
    assert(libre_parse_timestamp("9/10/2026 6:40:00 PM") == LIBRE_TS);
    assert(libre_parse_timestamp("1/1/2026 12:05:09 AM") == 1767225909UL);
    assert(libre_parse_timestamp("12/31/2025 12:00:00 PM") == 1767182400UL);
    assert(libre_parse_timestamp("2/29/2028 11:59:59 PM") == 1835481599UL);
    assert(libre_parse_timestamp("9/10/2026 18:40:00") == LIBRE_TS);    // 24-hour form
    assert(libre_parse_timestamp("9/10/2026 6:40:00 PM \t") == LIBRE_TS);
    assert(libre_parse_timestamp("1/1/2040 12:00:00 AM") == 2208988800UL);
    assert(libre_parse_timestamp("") == 0);
    assert(libre_parse_timestamp(nullptr) == 0);
    assert(libre_parse_timestamp("garbage") == 0);
    assert(libre_parse_timestamp("13/10/2026 6:40:00 PM") == 0);         // month
    assert(libre_parse_timestamp("2/30/2026 6:40:00 PM") == 0);          // day
    assert(libre_parse_timestamp("2/29/2026 6:40:00 PM") == 0);          // not a leap year
    assert(libre_parse_timestamp("9/10/2026 13:40:00 PM") == 0);         // 12-hour range
    assert(libre_parse_timestamp("9/10/2026 6:61:00 PM") == 0);
    assert(libre_parse_timestamp("9/10/2026 6:40:00 XM") == 0);
    assert(libre_parse_timestamp("9/10/2026 6:40:00 PMgarbage") == 0);
    assert(libre_parse_timestamp("9/10/2026 6:40:00 PM +0500") == 0);
    assert(libre_parse_timestamp("9/10/1999 6:40:00 PM") == 0);

    assert(libre_map_trend(1) == TREND_FALLING_FAST);
    assert(libre_map_trend(3) == TREND_FLAT);
    assert(libre_map_trend(5) == TREND_RISING_FAST);
    assert(libre_map_trend(0) == TREND_UNKNOWN);

    uint32_t age = 0;
    assert(libre_check_freshness(LIBRE_TS, false, LIBRE_TS + 60, &age) == LIBRE_CLOCK_UNSYNCED);
    assert(libre_check_freshness(0, true, LIBRE_TS, &age) == LIBRE_BAD_TIMESTAMP);
    assert(libre_check_freshness(LIBRE_TS + 6 * 60, true, LIBRE_TS, &age) == LIBRE_BAD_TIMESTAMP);
    assert(libre_check_freshness(LIBRE_TS + 60, true, LIBRE_TS, &age) == LIBRE_FRESH && age == 0);
    assert(libre_check_freshness(LIBRE_TS, true, LIBRE_TS + 600, &age) == LIBRE_FRESH && age == 600);
    assert(libre_check_freshness(LIBRE_TS, true, LIBRE_TS + 601, &age) == LIBRE_STALE && age == 601);
}

// Regression: a day-old reading was accepted for 120 polls while the clock was
// unsynchronized, and missing timestamps skipped the freshness check entirely.
static void test_libre_stale_readings_are_never_fresh() {
    const uint32_t day_later = LIBRE_TS + 24UL * 3600UL;

    {   // Clock never synchronized: nothing is accepted and no credentials are sent
        FakeLibre server; LibreSession session;
        for (int i = 0; i < 120; i++) {
            LibreResult r = session.fetch(server, 1000 + i * MIN_MS, false, 0);
            assert(r.status == LIBRE_FETCH_CLOCK_UNSYNCED);
        }
        assert(server.logins == 0 && server.reads == 0);
    }
    {   // Synchronized clock: the day-old reading is stale on every poll
        FakeLibre server; LibreSession session;
        for (int i = 0; i < 120; i++) {
            LibreResult r = session.fetch(server, 1000 + i * MIN_MS, true, day_later + i * 60);
            assert(r.status == LIBRE_FETCH_STALE);
            assert(r.age_sec >= 24UL * 3600UL);
        }
        assert(server.logins == 1);  // token reused, not re-authenticating each poll
    }
    {   // Missing, malformed and future timestamps are rejected, not trusted
        const char* bad[] = {"", "not a date", "2/30/2026 6:40:00 PM", "9/11/2026 6:40:00 PM"};
        for (const char* ts : bad) {
            FakeLibre server; LibreSession session;
            server.timestamp = ts;
            for (int i = 0; i < 120; i++) {
                LibreResult r = session.fetch(server, 1000 + i * MIN_MS, true, LIBRE_TS + 60);
                assert(r.status == LIBRE_FETCH_BAD_TIMESTAMP);
            }
        }
    }
    {   // A fresh reading is accepted with its real age
        FakeLibre server; LibreSession session;
        LibreResult r = session.fetch(server, 1000, true, LIBRE_TS + 90);
        assert(r.status == LIBRE_FETCH_OK && r.glucose == 131 && r.trend == TREND_FLAT);
        assert(r.timestamp == LIBRE_TS && r.age_sec == 90);
    }
}

// Regression: login succeeded but connections kept returning 403, so the
// device re-authenticated on every poll (20 logins in five minutes).
static void test_libre_authorization_failures_back_off() {
    const uint32_t poll = 15UL * 1000UL;  // minimum poll interval

    {   // Connections always refused: one login in the first five minutes
        FakeLibre server; LibreSession session;
        server.read_result = LIBRE_READ_UNAUTHORIZED;
        for (uint32_t t = 0; t < 5 * MIN_MS; t += poll) {
            LibreResult r = session.fetch(server, t, true, LIBRE_TS + 60);
            assert(r.status == (t == 0 ? LIBRE_FETCH_REJECTED : LIBRE_FETCH_BACKOFF));
        }
        assert(server.logins == 1);
        // ...and the backoff keeps growing: attempts at 0, 5, 15 and 35 minutes
        for (uint32_t t = 5 * MIN_MS; t < 60 * MIN_MS; t += poll) {
            session.fetch(server, t, true, LIBRE_TS + 60);
        }
        assert(server.logins == 4);
        assert(session.consecutive_rejections() == 4);
    }
    {   // Rejected logins (bad password) back off the same way
        FakeLibre server; LibreSession session;
        server.login_result = LIBRE_AUTH_REJECTED;
        for (uint32_t t = 0; t < 5 * MIN_MS; t += poll) session.fetch(server, t, true, LIBRE_TS + 60);
        assert(server.logins == 1 && server.reads == 0);
    }
    {   // An expired cached token gets one fresh login within the same poll
        FakeLibre server; LibreSession session;
        assert(session.fetch(server, 0, true, LIBRE_TS + 60).status == LIBRE_FETCH_OK);
        server.unauthorized_reads_left = 1;
        LibreResult r = session.fetch(server, MIN_MS, true, LIBRE_TS + 120);
        assert(r.status == LIBRE_FETCH_OK);
        assert(server.logins == 2 && session.consecutive_rejections() == 0);
    }
    {   // A successful read resets the backoff to its minimum
        FakeLibre server; LibreSession session;
        server.read_result = LIBRE_READ_UNAUTHORIZED;
        session.fetch(server, 0, true, LIBRE_TS + 60);                  // rejection 1
        session.fetch(server, 5 * MIN_MS, true, LIBRE_TS + 60);         // rejection 2
        assert(session.consecutive_rejections() == 2);
        server.read_result = LIBRE_READ_OK;
        assert(session.fetch(server, 15 * MIN_MS, true, LIBRE_TS + 60).status == LIBRE_FETCH_OK);
        assert(session.consecutive_rejections() == 0);
        server.unauthorized_reads_left = 2;                              // refused before and after re-login
        LibreResult r = session.fetch(server, 16 * MIN_MS, true, LIBRE_TS + 60);
        assert(r.status == LIBRE_FETCH_REJECTED && r.retry_in_ms == LIBRE_BACKOFF_MIN_MS);
    }
    {   // Rate limiting backs off but keeps the token
        FakeLibre server; LibreSession session;
        assert(session.fetch(server, 0, true, LIBRE_TS + 60).status == LIBRE_FETCH_OK);
        server.read_result = LIBRE_READ_REJECTED;
        assert(session.fetch(server, MIN_MS, true, LIBRE_TS + 60).status == LIBRE_FETCH_REJECTED);
        server.read_result = LIBRE_READ_OK;
        assert(session.fetch(server, 6 * MIN_MS, true, LIBRE_TS + 60).status == LIBRE_FETCH_OK);
        assert(server.logins == 1);
    }
    {   // Only an actual credential change lifts the backoff
        FakeLibre server; LibreSession session;
        server.login_result = LIBRE_AUTH_REJECTED;
        session.fetch(server, 0, true, LIBRE_TS + 60);
        session.reset();
        server.login_result = LIBRE_AUTH_OK;
        assert(session.fetch(server, poll, true, LIBRE_TS + 60).status == LIBRE_FETCH_OK);
    }
    {   // Backoff survives millis() wrapping
        FakeLibre server; LibreSession session;
        server.login_result = LIBRE_AUTH_REJECTED;
        session.fetch(server, 0xFFFFFFF0UL, true, LIBRE_TS + 60);
        assert(session.fetch(server, 0x00000100UL, true, LIBRE_TS + 60).status == LIBRE_FETCH_BACKOFF);
        session.fetch(server, (uint32_t)(0xFFFFFFF0UL + LIBRE_BACKOFF_MIN_MS), true, LIBRE_TS + 60);
        assert(server.logins == 2);
    }
    {   // Sessions are renewed daily
        FakeLibre server; LibreSession session;
        session.fetch(server, 0, true, LIBRE_TS + 60);
        session.fetch(server, LIBRE_SESSION_LIFETIME_MS - 1, true, LIBRE_TS + 60);
        assert(server.logins == 1);
        assert(session.fetch(server, LIBRE_SESSION_LIFETIME_MS, true, LIBRE_TS + 60).status == LIBRE_FETCH_BACKOFF);
        session.fetch(server, LIBRE_SESSION_LIFETIME_MS + LIBRE_MIN_REQUEST_INTERVAL_MS, true, LIBRE_TS + 60);
        assert(server.logins == 2);
    }
}

static void test_libre_person_binding_and_account_changes() {
    LibrePatient people[] = {{"person-a", "Alice"}, {"person-b", "Bob"}};
    assert(libre_patient_index(people, 2, "") == LIBRE_PATIENT_AMBIGUOUS);
    assert(libre_patient_index(people, 1, "") == 0);
    assert(libre_patient_index(people, 2, "person-a") == 0);
    LibrePatient reordered[] = {people[1], people[0]};
    assert(libre_patient_index(reordered, 2, "person-a") == 1);
    // Even when there is now only one sharer, never replace the saved person.
    assert(libre_patient_index(reordered, 1, "person-a") == LIBRE_PATIENT_MISSING);
    LibrePatient duplicate[] = {people[0], people[0]};
    assert(libre_patient_index(duplicate, 2, "person-a") == LIBRE_PATIENT_INVALID);
    LibrePatient missing[] = {{"", "Alice"}};
    assert(libre_patient_index(missing, 1, "") == LIBRE_PATIENT_INVALID);

    AppConfig cfg = {};
    strcpy(cfg.libre_email, "old@example.com");
    strcpy(cfg.libre_password, "old-password");
    strcpy(cfg.libre_region, "us");
    strcpy(cfg.libre_patient_id, "person-a");
    strcpy(cfg.libre_patient_name, "Alice");
    // Both web config and the installer overlay use this production helper.
    assert(!config_update_libre_credentials(cfg, "old@example.com", ""));
    assert(strcmp(cfg.libre_region, "us") == 0);
    assert(strcmp(cfg.libre_patient_id, "person-a") == 0);
    assert(config_update_libre_credentials(cfg, nullptr, "new-password"));
    assert(strcmp(cfg.libre_region, "us") == 0);
    assert(strcmp(cfg.libre_patient_id, "person-a") == 0);
    assert(config_update_libre_credentials(cfg, "new@example.com", "other-password"));
    assert(!cfg.libre_region[0] && !cfg.libre_patient_id[0] && !cfg.libre_patient_name[0]);
    assert(strcmp(cfg.libre_email, "new@example.com") == 0);
    assert(strcmp(cfg.libre_password, "other-password") == 0);
    assert(!config_update_libre_credentials(cfg, nullptr, nullptr));
}

static void test_libre_manual_tests_and_transient_backoff() {
    {   // Test and scheduled polls share this gate: repeated clicks cannot log in.
        FakeLibre server; LibreSession session;
        server.login_result = LIBRE_AUTH_REJECTED;
        assert(session.fetch(server, 0, true, LIBRE_TS).status == LIBRE_FETCH_REJECTED);
        for (uint32_t t = 1; t < LIBRE_BACKOFF_MIN_MS; t += 100) {
            assert(session.fetch(server, t, true, LIBRE_TS).status == LIBRE_FETCH_BACKOFF);
        }
        assert(server.logins == 1);
    }
    {   // Even a healthy account cannot be hammered by forced requests.
        FakeLibre server; LibreSession session;
        assert(session.fetch(server, 0, true, LIBRE_TS).status == LIBRE_FETCH_OK);
        for (uint32_t t = 1; t < LIBRE_MIN_REQUEST_INTERVAL_MS; t += 100) {
            assert(session.fetch(server, t, true, LIBRE_TS).status == LIBRE_FETCH_BACKOFF);
        }
        assert(server.logins == 1 && server.reads == 1);
        assert(session.fetch(server, LIBRE_MIN_REQUEST_INTERVAL_MS, true, LIBRE_TS).status == LIBRE_FETCH_OK);
        assert(server.logins == 1 && server.reads == 2);
    }
    {   // Rejection -> allowed retry -> timeout must start another full cooldown.
        FakeLibre server; LibreSession session;
        server.login_result = LIBRE_AUTH_REJECTED;
        session.fetch(server, 0, true, LIBRE_TS);
        server.login_result = LIBRE_AUTH_TRANSIENT;
        assert(session.fetch(server, 5 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_TRANSIENT);
        for (uint32_t t = 5 * MIN_MS + 15000; t < 10 * MIN_MS; t += 15000) {
            assert(session.fetch(server, t, true, LIBRE_TS).status == LIBRE_FETCH_BACKOFF);
        }
        assert(server.logins == 2 && session.consecutive_rejections() == 1);
        server.login_result = LIBRE_AUTH_REJECTED;
        LibreResult r = session.fetch(server, 10 * MIN_MS, true, LIBRE_TS);
        assert(r.status == LIBRE_FETCH_REJECTED && r.retry_in_ms == 10 * MIN_MS);
    }
    {   // A read timeout after successful login also preserves the rejection gate.
        FakeLibre server; LibreSession session;
        server.read_result = LIBRE_READ_UNAUTHORIZED;
        session.fetch(server, 0, true, LIBRE_TS);
        server.read_result = LIBRE_READ_TRANSIENT;
        assert(session.fetch(server, 5 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_TRANSIENT);
        assert(session.fetch(server, 5 * MIN_MS + 15000, true, LIBRE_TS).status == LIBRE_FETCH_BACKOFF);
        assert(server.logins == 2 && server.reads == 2);
        server.read_result = LIBRE_READ_OK;
        assert(session.fetch(server, 10 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_OK);
        assert(session.consecutive_rejections() == 0 && server.logins == 2);
    }
}

static void test_libre_patient_snapshot_lifetime() {
    LibrePatientCache cache;
    auto single = std::make_shared<LibrePatientSnapshot>("first@example.com", 1);
    strcpy(single->people[0].id, "person-a");
    strcpy(single->people[0].name, "Alice");
    cache.publish(single);
    LibrePatients reader = cache.get("first@example.com");
    assert(reader && reader->people.size() == 1);
    assert(!cache.get("other@example.com"));

    auto replacement = std::make_shared<LibrePatientSnapshot>("second@example.com", 2);
    strcpy(replacement->people[0].id, "person-b");
    strcpy(replacement->people[1].id, "person-c");
    cache.publish(replacement);
    assert(!cache.get("first@example.com"));
    assert(cache.get("second@example.com")->people.size() == 2);
    // A web response can finish using its retained snapshot after replacement.
    assert(strcmp(reader->people[0].name, "Alice") == 0);
    cache.clear(); // rejected/oversized/empty response invalidates future reads
    assert(!cache.get("second@example.com"));
    assert(strcmp(reader->people[0].id, "person-a") == 0);

    std::thread publisher([&cache]() {
        for (int i = 0; i < 2000; ++i) {
            auto next = std::make_shared<LibrePatientSnapshot>("shared@example.com", 2);
            strcpy(next->people[0].id, i % 2 ? "odd" : "even");
            strcpy(next->people[1].id, next->people[0].id);
            cache.publish(next);
            if (i % 3 == 0) cache.clear();
        }
    });
    auto read = [&cache]() {
        for (int i = 0; i < 2000; ++i) {
            LibrePatients current = cache.get("shared@example.com");
            if (current) {
                assert(current->people.size() == 2);
                assert(strcmp(current->people[0].id, current->people[1].id) == 0);
            }
        }
    };
    std::thread reader1(read), reader2(read);
    publisher.join(); reader1.join(); reader2.join();
}

static void test_libre_account_action_cooldown() {
    FakeLibre server; LibreSession session;
    server.login_result = LIBRE_AUTH_NEEDS_ACTION;
    // An hour of unaccepted terms never escalates the five-minute cooldown.
    for (uint32_t t = 0; t < 60 * MIN_MS; t += 5 * MIN_MS) {
        LibreResult r = session.fetch(server, t, true, LIBRE_TS);
        assert(r.status == LIBRE_FETCH_NEEDS_ACTION && r.account_action_required);
        assert(r.retry_in_ms == 5 * MIN_MS);
        for (uint32_t elapsed = 100; elapsed < 5 * MIN_MS; elapsed += 1000) {
            r = session.fetch(server, t + elapsed, true, LIBRE_TS);
            assert(r.status == LIBRE_FETCH_BACKOFF && r.account_action_required);
        }
    }
    assert(server.logins == 12 && server.reads == 0);
    assert(session.consecutive_rejections() == 0);

    // Terms accepted in the app: recover without changing credentials/resetting.
    server.login_result = LIBRE_AUTH_OK;
    assert(session.fetch(server, 60 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_OK);
    assert(server.logins == 13 && server.reads == 1);

    // A new invalid-password failure still follows exponential backoff.
    server.unauthorized_reads_left = 1;
    server.login_result = LIBRE_AUTH_REJECTED;
    assert(session.fetch(server, 61 * MIN_MS, true, LIBRE_TS).retry_in_ms == 5 * MIN_MS);
    assert(session.fetch(server, 66 * MIN_MS, true, LIBRE_TS).retry_in_ms == 10 * MIN_MS);

    // A transient failure while waiting for action preserves the fixed gate.
    session.reset();
    server.login_result = LIBRE_AUTH_NEEDS_ACTION;
    session.fetch(server, 0, true, LIBRE_TS);
    server.login_result = LIBRE_AUTH_TRANSIENT;
    assert(session.fetch(server, 5 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_TRANSIENT);
    assert(session.fetch(server, 6 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_BACKOFF);
    server.login_result = LIBRE_AUTH_OK;
    assert(session.fetch(server, 10 * MIN_MS, true, LIBRE_TS).status == LIBRE_FETCH_OK);
}

int main() {
    DoubleClickDetector single_click(350);
    assert(single_click.on_press(100) == DOUBLE_CLICK_NONE);
    assert(single_click.on_short_release(150) == DOUBLE_CLICK_NONE);
    assert(single_click.poll(499, false) == DOUBLE_CLICK_NONE);
    assert(single_click.poll(500, false) == DOUBLE_CLICK_SINGLE);

    DoubleClickDetector double_click(350);
    assert(double_click.on_short_release(1000) == DOUBLE_CLICK_NONE);
    assert(double_click.on_press(1200) == DOUBLE_CLICK_NONE);
    assert(double_click.on_short_release(1450) == DOUBLE_CLICK_DOUBLE);
    assert(double_click.poll(2000, false) == DOUBLE_CLICK_NONE);

    DoubleClickDetector late_second_click(350);
    assert(late_second_click.on_short_release(2000) == DOUBLE_CLICK_NONE);
    assert(late_second_click.on_press(2350) == DOUBLE_CLICK_SINGLE);
    assert(late_second_click.on_short_release(2400) == DOUBLE_CLICK_NONE);
    assert(late_second_click.poll(2750, false) == DOUBLE_CLICK_SINGLE);

    DoubleClickDetector long_second_press(350);
    assert(long_second_press.on_short_release(3000) == DOUBLE_CLICK_NONE);
    assert(long_second_press.on_press(3200) == DOUBLE_CLICK_NONE);
    assert(long_second_press.poll(3600, true) == DOUBLE_CLICK_NONE);
    long_second_press.cancel();
    assert(long_second_press.poll(4000, false) == DOUBLE_CLICK_NONE);

    DoubleClickDetector wrapped_clock(350);
    assert(wrapped_clock.on_short_release(0xfffffff0U) == DOUBLE_CLICK_NONE);
    assert(wrapped_clock.on_press(0x00000040U) == DOUBLE_CLICK_NONE);
    assert(wrapped_clock.on_short_release(0x00000080U) == DOUBLE_CLICK_DOUBLE);

    assert(SemVer::parse("0.2.0").valid);
    assert(SemVer::parse("10.20.300") > SemVer::parse("2.99.999"));
    assert(SemVer::parse("1.10.0") > SemVer::parse("1.9.99"));
    assert(SemVer::parse("1.0.0") == SemVer::parse("1.0.0"));
    assert(!SemVer::parse("v1.0.0").valid);
    assert(!SemVer::parse("1.0").valid);
    assert(!SemVer::parse("1.02.3").valid);
    assert(!SemVer::parse("1.2.3-beta").valid);

    OtaSafetyInputs safe = {};
    safe.wifi_connected = true;
    safe.time_available = true;
    safe.battery_percent = 80;
    safe.free_heap = 100000;
    assert(ota_safety_failure(safe) == nullptr);

    OtaSafetyInputs test = safe;
    test.battery_percent = 44;
    assert(strcmp(ota_safety_failure(test), "battery_low") == 0);
    test = safe; test.urgent_glucose = true;
    assert(strcmp(ota_safety_failure(test), "urgent_glucose") == 0);
    test = safe; test.timer_running = true;
    assert(strcmp(ota_safety_failure(test), "timer_active") == 0);
    test = safe; test.stopwatch_running = true;
    assert(strcmp(ota_safety_failure(test), "stopwatch_active") == 0);
    test = safe; test.time_available = false;
    assert(strcmp(ota_safety_failure(test), "time_unavailable") == 0);
    test = safe; test.free_heap = 74999;
    assert(strcmp(ota_safety_failure(test), "heap_low") == 0);

    assert(ota_in_install_window(3, 3));
    assert(!ota_in_install_window(4, 3));
    assert(!ota_in_install_window(-1, 3));
    assert(ota_retry_delay_ms(1) == 15UL * 60UL * 1000UL);
    assert(ota_retry_delay_ms(2) == 30UL * 60UL * 1000UL);
    assert(ota_retry_delay_ms(99) == 6UL * 60UL * 60UL * 1000UL);

    assert(fleet_retry_delay_ms(0) == 0);
    assert(fleet_retry_delay_ms(1) == 60UL * 1000UL);
    assert(fleet_retry_delay_ms(2) == 5UL * 60UL * 1000UL);
    assert(fleet_retry_delay_ms(3) == 60UL * 60UL * 1000UL);
    assert(fleet_retry_delay_ms(99) == 60UL * 60UL * 1000UL);
    assert(!fleet_circuit_is_open(2));
    assert(fleet_circuit_is_open(3));

    test_libre_timestamps_and_trends();
    test_libre_stale_readings_are_never_fresh();
    test_libre_authorization_failures_back_off();
    test_libre_person_binding_and_account_changes();
    test_libre_manual_tests_and_transient_backoff();
    test_libre_patient_snapshot_lifetime();
    test_libre_account_action_cooldown();
    return 0;
}
