import XCTest
import Combine
@testable import SugarClockCore

/// Runs only when SUGARCLOCK_LIVE_HOST names a real clock on this network.
/// The save step writes back the clock's current brightness unchanged.
@MainActor final class LiveClockTests: XCTestCase {
    func testRealClockOverLocalNetwork() async throws {
        guard let host = ProcessInfo.processInfo.environment["SUGARCLOCK_LIVE_HOST"] else { throw XCTSkip("Set SUGARCLOCK_LIVE_HOST to run") }
        let network = LocalClockNetwork()
        let started = Date()
        let found = await network.discover()
        let sweep = Date().timeIntervalSince(started)
        print("[live] sweep \(String(format: "%.1f", sweep))s found:", found.map { "\($0.host) \($0.identity) \($0.firmware)" })
        let candidate = try XCTUnwrap(found.first { $0.host == host }, "Discovery should find \(host)")

        let connectStart = Date()
        let connected = await network.connect(host: host, expecting: candidate.identity)
        let (client, hello) = try XCTUnwrap(connected)
        let settingsResponse = try await client.request("settings.get")
        let settings = try XCTUnwrap(settingsResponse["settings"] as? [String: Any])
        let fields = try await client.schema(progress: nil)
        let statusResponse = try await client.request("status.get")
        let ready = Date().timeIntervalSince(connectStart)
        print("[live] hello", hello["device_id"] ?? "", hello["firmware"] ?? "", "ready in \(String(format: "%.2f", ready))s,",
              settings.count, "settings,", fields.count, "fields, status keys", (statusResponse["status"] as? [String: Any])?.keys.sorted() ?? [])
        XCTAssertFalse(fields.isEmpty)
        XCTAssertNil(settings["dexcom_password"], "Secrets are never read back")

        let brightness = try XCTUnwrap(settings["brightness"] as? Int)
        try await client.save(["brightness": brightness])
        let rereadResponse = try await client.request("settings.get")
        let reread = rereadResponse["settings"] as? [String: Any]
        XCTAssertEqual(reread?["brightness"] as? Int, brightness)
        print("[live] rewrote unchanged brightness \(brightness) and read it back")
    }
}

/// Bluetooth switched off, so the coordinator can only use Wi-Fi.
@MainActor private final class RadioOff: ClockConnectionTransport {
    var operationTimeout: TimeInterval = 45
    var connected = false
    var isPoweredOn = false
    var availabilityMessage = "Bluetooth is off"
    var powerPublisher: AnyPublisher<Bool, Never> { Just(false).eraseToAnyPublisher() }
    var connectionPublisher: AnyPublisher<Bool, Never> { Just(false).eraseToAnyPublisher() }
    var packetLimit = 20
    func connect(id: UUID) async throws { throw ClockError.unavailable(availabilityMessage) }
    func write(_ packet: Data) async throws { throw ClockError.disconnected }
    func read() async throws -> Data { throw ClockError.disconnected }
    func close() {}
}

extension LiveClockTests {
    /// The production coordinator finds a Bluetooth-saved clock on Wi-Fi by
    /// identity alone, loads it, and confirms an (unchanged) save by readback.
    func testCoordinatorReachesRealClockOverWiFi() async throws {
        guard let host = ProcessInfo.processInfo.environment["SUGARCLOCK_LIVE_HOST"] else { throw XCTSkip("Set SUGARCLOCK_LIVE_HOST to run") }
        let probe = await LocalClockNetwork().probe(host: host)
        let identity = try XCTUnwrap(probe?.identity)
        let defaults = UserDefaults(suiteName: "SugarClockLive." + UUID().uuidString)!
        let model = ClockModel(enableBluetooth: false, loadSaved: false, transport: RadioOff(), preferences: defaults,
                               localSettingsStore: MemoryLocalSettingsStore(), localNetwork: LocalClockNetwork())
        let id = UUID()
        model.clocks = [SavedClock(id: identity, peripheral: id, nickname: "Office")]
        let started = Date()
        await model.connect(id)
        print("[live] coordinator ready in \(String(format: "%.1f", Date().timeIntervalSince(started)))s via \(String(describing: model.activeLink)) · \(model.connectionSummary) · \(model.fields.count) fields")
        XCTAssertTrue(model.sessionReady)
        XCTAssertEqual(model.activeLink, .localNetwork(host))
        XCTAssertEqual(model.clocks.first?.host, host)
        let brightness = try XCTUnwrap(model.settings["brightness"] as? Int)
        let saved = await model.save(["brightness": brightness])
        XCTAssertTrue(saved, "Readback should confirm the unchanged brightness")
        model.suspend()
        let restart = Date()
        model.resume()
        for _ in 0..<100 where !model.sessionReady { try await Task.sleep(nanoseconds: 50_000_000) }
        print("[live] warm reconnect in \(String(format: "%.2f", Date().timeIntervalSince(restart)))s")
        XCTAssertTrue(model.sessionReady)
        model.suspend()
    }
}

extension LiveClockTests {
    /// With one clock open, the list reports every other clock on this Wi-Fi
    /// using only the saved identities (no addresses) as a starting point.
    func testClockListReportsEveryRealClock() async throws {
        guard let host = ProcessInfo.processInfo.environment["SUGARCLOCK_LIVE_HOST"] else { throw XCTSkip("Set SUGARCLOCK_LIVE_HOST to run") }
        let found = await LocalClockNetwork().discover()
        let open = try XCTUnwrap(found.first { $0.host == host })
        let defaults = UserDefaults(suiteName: "SugarClockLive." + UUID().uuidString)!
        let model = ClockModel(enableBluetooth: false, loadSaved: false, transport: RadioOff(), preferences: defaults,
                               localSettingsStore: MemoryLocalSettingsStore(), localNetwork: LocalClockNetwork())
        let id = UUID()
        model.clocks = [SavedClock(id: open.identity, peripheral: id, nickname: "Office", host: host)] +
            found.filter { $0.host != host }.map { SavedClock(id: $0.identity, peripheral: UUID(), nickname: $0.host) }
        await model.connect(id)
        XCTAssertTrue(model.sessionReady)
        var started = Date()
        await model.refreshAvailability()
        print("[live] first list check \(String(format: "%.1f", Date().timeIntervalSince(started)))s")
        started = Date()
        await model.refreshAvailability()
        print("[live] repeat list check \(String(format: "%.2f", Date().timeIntervalSince(started)))s")
        for clock in model.clocks {
            let status = model.listStatus(for: clock)
            print("[live]", clock.nickname, clock.host ?? "-", status?.level ?? .idle, status?.text ?? "")
            XCTAssertEqual(status?.level, .connected)
        }
        model.suspend()
    }
}
