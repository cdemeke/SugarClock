import XCTest
@testable import SugarClockCore

/// Emulates the web API of mainline firmware (0.2.x): hex colors, `has_*`
/// secret flags, empty secrets meaning "unchanged" and unknown keys ignored.
final class FakeClockServer: LocalHTTP, @unchecked Sendable {
    struct Clock {
        var mac: String
        var firmware = "0.2.18"
        var config: [String: Any] = FakeClockServer.mainlineConfig
        var otaStatus: [String: Any] = [:]
        var failPersistence = false
        var queueOTARequests = false
    }
    static let officeMAC = "48:9D:31:F8:20:DC", officeIdentity = "DC20F8319D48"
    static let mainlineConfig: [String: Any] = [
        "wifi_ssid": "Home", "has_wifi_password": true, "data_source": 1, "server_url": "", "has_auth_token": false,
        "dexcom_username": "owner", "has_dexcom_password": true, "dexcom_us": true, "libre_email": "", "has_libre_password": false,
        "libre_people": [[String: Any]](), "poll_interval": 60, "brightness": 40, "auto_brightness": false, "use_mmol": false,
        "thresh_urgent_low": 55, "thresh_low": 70, "thresh_high": 180, "thresh_urgent_high": 250,
        "color_in_range": "#34a853", "ambient_character": 2, "weather_enabled": true, "has_weather_api_key": false, "timezone": "EST5EDT",
    ]
    private let lock = NSLock()
    private var clocks: [String: Clock]
    private var log: [String] = []
    private var bodies: [String: [String: Any]] = [:]
    private var down = false
    init(_ clocks: [String: Clock] = [:]) { self.clocks = clocks }

    var offline: Bool {
        get { lock.withLock { down } }
        set { lock.withLock { down = newValue } }
    }
    var requests: [String] { lock.withLock { log } }
    func config(_ host: String) -> [String: Any] { lock.withLock { clocks[host]?.config ?? [:] } }
    func lastBody(_ path: String) -> [String: Any]? { lock.withLock { bodies[path] } }
    func move(from old: String, to new: String) { lock.withLock { clocks[new] = clocks.removeValue(forKey: old) } }
    func place(_ clock: Clock, at host: String) { lock.withLock { clocks[host] = clock } }
    func mutateClock(at host: String, _ change: (inout Clock) -> Void) {
        lock.withLock { if var clock = clocks[host] { change(&clock); clocks[host] = clock } }
    }

    func send(_ request: URLRequest) async throws -> (Data, Int) {
        try lock.withLock {
            let url = request.url!, host = url.host! + (url.port.map { ":\($0)" } ?? ""), method = request.httpMethod ?? "GET"
            log.append("\(method) \(host)\(url.path)")
            if down { throw URLError(.cannotConnectToHost) }
            if host == "192.168.1.10" { return (Data("Not found".utf8), 404) }  // a printer
            guard var clock = clocks[host] else { throw URLError(.timedOut) }
            let body = request.httpBody.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] } ?? [:]
            bodies[url.path] = body
            func reply(_ object: [String: Any], _ code: Int = 200) throws -> (Data, Int) {
                (try JSONSerialization.data(withJSONObject: object), code)
            }
            switch (method, url.path) {
            case ("GET", "/api/status"):
                return try reply(["firmware_version": clock.firmware, "hardware": "ulanzi-tc001-esp32-4mb", "glucose": 112, "valid": true,
                                  "data_age_sec": 42, "failure_count": 0, "wifi_connected": true, "wifi_rssi": -50])
            case ("GET", "/api/debug"): return try reply(["mac": clock.mac, "free_heap": 160_000])
            case ("GET", "/api/config"): return try reply(clock.config)
            case ("POST", "/api/config"):
                for (key, value) in body {
                    if clock.config["has_" + key] != nil {
                        if let text = value as? String, !text.isEmpty { clock.config["has_" + key] = true }
                    } else if clock.config[key] != nil { clock.config[key] = value }
                }
                clocks[host] = clock
                if clock.failPersistence { return try reply(["error": "persistence_failed"], 500) }
                return try reply(["status": "ok"])
            case ("GET", "/api/wifi/status"):
                return try reply(["state": "connected", "trial": "idle", "detail": "", "ssid": "Home", "checks": ["dns": "ok", "data": "fail"]])
            case ("GET", "/api/ota/status"):
                var result: [String: Any] = ["current_version": clock.firmware, "state": "idle", "available_version": "", "progress": 0,
                                            "last_error": "", "safety_reason": "", "pending_verification": false]
                result.merge(clock.otaStatus) { $1 }
                return try reply(result)
            case ("POST", "/api/ota/check"), ("POST", "/api/ota/install"):
                if clock.queueOTARequests {
                    clock.otaStatus["request_pending"] = true
                    clock.otaStatus["request_failed"] = false
                    clocks[host] = clock
                    return try reply(["status": "queued"], 202)
                }
                return url.path.hasSuffix("/check") ? try reply(["error": "no_update_available"], 409)
                    : try reply(["error": "safety_requirements_not_met", "reason": "low_battery"], 412)
            case ("POST", "/api/wifi/scan/refresh"): return try reply(["started": true, "scanning": true])
            case ("GET", "/api/wifi/scan"):
                return try reply(["scanning": false, "networks": [["ssid": "Home", "rssi": -40, "enc": 3, "enterprise": false]]])
            case ("POST", "/api/wifi/connect"): return try reply(["status": "started"], 202)
            default: return (Data(), 404)
            }
        }
    }
}

@MainActor final class LocalNetworkTests: XCTestCase {
    private func officeServer() -> FakeClockServer {
        FakeClockServer(["192.168.1.81": .init(mac: FakeClockServer.officeMAC)])
    }

    func testIdentityMatchesFirmwareBluetoothHello() throws {
        // ESP.getEfuseMac() is little-endian; the Bluetooth hello prints it with %012llX.
        XCTAssertEqual(LocalClockIdentity.identity(fromMAC: "48:9D:31:F8:20:DC"), "DC20F8319D48")
        XCTAssertEqual(LocalClockIdentity.identity(fromMAC: "48:9d:31:f8:20:dc"), "DC20F8319D48")
        XCTAssertEqual(LocalClockIdentity.name(for: "DC20F8319D48"), "SugarClock-DC20F8")
        XCTAssertNil(LocalClockIdentity.identity(fromMAC: "48:9D:31:F8:20"))
        XCTAssertNil(LocalClockIdentity.identity(fromMAC: "48:9D:31:F8:20:ZZ"))
        let id = try XCTUnwrap(LocalClockIdentity.peripheralID(for: "DC20F8319D48"))
        XCTAssertTrue(LocalClockIdentity.isLocalOnly(id))
        XCTAssertFalse(LocalClockIdentity.isLocalOnly(UUID()))
        XCTAssertEqual(LocalClockIdentity.peripheralID(for: "DC20F8319D48"), id)
        XCTAssertNil(LocalClockIdentity.peripheralID(for: "not-hex"))
    }
    func testTypedAddressesAreNormalized() {
        XCTAssertEqual(LocalClockIdentity.normalizedHost(" http://192.168.1.81/ "), "192.168.1.81")
        XCTAssertEqual(LocalClockIdentity.normalizedHost("clock.local:8080"), "clock.local:8080")
        XCTAssertNil(LocalClockIdentity.normalizedHost(""))
        XCTAssertNil(LocalClockIdentity.normalizedHost("192.168.1.81/api?x=1"))
        XCTAssertNil(LocalClockIdentity.normalizedHost("evil host"))
    }
    func testSchemaOnlyOffersFieldsTheClockReturns() {
        let fields = LocalClockSchema.fields(supportedBy: FakeClockServer.mainlineConfig)
        let keys = Set(fields.compactMap { $0["key"] as? String })
        XCTAssertTrue(keys.isSuperset(of: ["brightness", "dexcom_password", "server_url", "color_in_range", "weather_api_key"]))
        // 0.2.18 has neither the blood-sugar switch nor these settings in /api/config.
        XCTAssertFalse(keys.contains("glucose_enabled"))
        XCTAssertFalse(keys.contains("auto_update_enabled"))
        XCTAssertFalse(keys.contains(where: { $0.hasPrefix("wifi_") }))
        XCTAssertFalse(keys.contains("libre_patient_id"))
        let brightness = fields.first { $0["key"] as? String == "brightness" }
        XCTAssertEqual(brightness?["min"] as? Int, 1); XCTAssertEqual(brightness?["max"] as? Int, 255)
        XCTAssertEqual(fields.first { $0["key"] as? String == "dexcom_password" }?["type"] as? String, "secret")
    }
    func testSettingsUseBluetoothShapeWithoutRevealingSecrets() {
        var config = FakeClockServer.mainlineConfig
        config["server_url"] = "https://night.example"
        let settings = LocalClockSchema.settings(fromHTTP: config)
        XCTAssertEqual(settings["dexcom_password_configured"] as? Bool, true)
        XCTAssertEqual(settings["auth_token_configured"] as? Bool, false)
        XCTAssertEqual(settings["server_url_configured"] as? Bool, true)
        XCTAssertEqual(settings["libre_email_configured"] as? Bool, false)
        XCTAssertNil(settings["server_url"]); XCTAssertNil(settings["dexcom_password"])
        XCTAssertEqual(settings["color_in_range"] as? Int, 0x34a853)
        XCTAssertEqual(settings["brightness"] as? Int, 40)
        XCTAssertEqual(settings["dexcom_username"] as? String, "owner")
        XCTAssertNil(settings["libre_people"])
    }
    func testPatchTranslationForWebFirmware() throws {
        let body = try LocalClockSchema.httpBody(forPatch: ["color_in_range": 0xFF0000, "brightness": 90, "dexcom_password": "new"])
        XCTAssertEqual(body["color_in_range"] as? String, "#ff0000")
        XCTAssertEqual(body["brightness"] as? Int, 90)
        XCTAssertEqual(body["dexcom_password"] as? String, "new")
        // Mainline treats an empty or missing secret as "unchanged"; never pretend it cleared.
        XCTAssertThrowsError(try LocalClockSchema.httpBody(forPatch: ["dexcom_password": NSNull()])) {
            guard case ClockError.unavailable = $0 else { return XCTFail("\($0)") }
        }
        XCTAssertThrowsError(try LocalClockSchema.httpBody(forPatch: ["wifi_password": "x"]))
    }
    func testClientSpeaksTheBluetoothRequestContract() async throws {
        let server = officeServer()
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        let hello = try await client.request("hello")
        XCTAssertEqual(hello["device_id"] as? String, FakeClockServer.officeIdentity)
        XCTAssertEqual(hello["firmware"] as? String, "0.2.18")
        XCTAssertEqual(hello["state"] as? String, "applied")
        XCTAssertTrue((hello["capabilities"] as? [String] ?? []).contains("settings.patch"))
        XCTAssertNil(SchemaIdentity(hello), "No boot identity over Wi-Fi: schema is rebuilt locally, never cached as Bluetooth's")

        let settings = try await client.request("settings.get")
        XCTAssertEqual(settings["saved"] as? Bool, false, "Legacy /api/config is not durability evidence")
        XCTAssertEqual((settings["settings"] as? [String: Any])?["brightness"] as? Int, 40)
        let schema = try await client.schema(progress: nil)
        XCTAssertTrue(schema.contains { $0["key"] as? String == "brightness" })
        let page = try await client.request("schema.get", fields: ["page": 0])
        XCTAssertEqual(page["more"] as? Bool, false)

        let statusResponse = try await client.request("status.get")
        let status = try XCTUnwrap(statusResponse["status"] as? [String: Any])
        XCTAssertEqual(status["wifi"] as? String, "CONNECTED")
        XCTAssertEqual(status["internet_dns"] as? Int, 1)
        XCTAssertEqual(status["provider_reachable"] as? Int, 2)
        XCTAssertEqual(status["data_age_ms"] as? Double, 42_000)
        XCTAssertEqual((status["ota"] as? [String: Any])?["current_version"] as? String, "0.2.18")

        try await client.save(["brightness": 120, "color_in_range": 0xFF0000, "dexcom_password": "replacement"])
        XCTAssertEqual(server.config("192.168.1.81")["brightness"] as? Int, 120)
        XCTAssertEqual(server.config("192.168.1.81")["color_in_range"] as? String, "#ff0000")
        let reread = try await client.request("settings.get")["settings"] as? [String: Any]
        XCTAssertEqual(reread?["color_in_range"] as? Int, 0xFF0000)
    }
    func testOperationsMapWebErrorsToBluetoothErrors() async throws {
        let server = officeServer()
        server.mutateClock(at: "192.168.1.81") { $0.otaStatus = ["request_pending": false, "request_failed": false] }
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        do { _ = try await client.request("ota.check"); XCTFail() } catch { XCTAssertEqual(error as? ClockError, .rejected("no_update")) }
        do { _ = try await client.request("ota.install"); XCTFail() } catch { XCTAssertEqual(error as? ClockError, .rejected("deferred")) }
        do { _ = try await client.request("bonds.reset"); XCTFail() } catch { XCTAssertEqual(error as? ClockError, .rejected("unsupported_operation")) }
        let scan = try await client.request("wifi.scan")
        XCTAssertEqual(scan["state"] as? String, "queued")
        let results = try await client.request("wifi.results")
        XCTAssertEqual(results["scanning"] as? Bool, false)
        XCTAssertEqual((results["networks"] as? [[String: Any]])?.first?["auth"] as? Int, 3)
    }
    func testHTTPReadsRequireAnExplicitBooleanDurabilityMarker() async throws {
        let server = officeServer()
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        let cases: [(Any, Bool)] = [(false, false), ("true", false), (1, false), (NSNull(), false), (true, true)]
        for (evidence, expected) in cases {
            server.mutateClock(at: "192.168.1.81") { $0.config["configuration_saved"] = evidence }
            let read = try await client.request("settings.get")
            XCTAssertEqual(read["saved"] as? Bool, expected)
            XCTAssertNil((read["settings"] as? [String: Any])?["configuration_saved"])
        }
        server.mutateClock(at: "192.168.1.81") { $0.config.removeValue(forKey: "configuration_saved") }
        let legacy = try await client.request("settings.get")
        XCTAssertEqual(legacy["saved"] as? Bool, false)
    }
    func testQueuedHTTPUpdateCannotSettleAtAnOldTerminalState() async throws {
        for state in ["idle", "update_available"] {
            let server = officeServer()
            server.mutateClock(at: "192.168.1.81") {
                $0.queueOTARequests = true
                $0.otaStatus = ["state": state, "request_pending": false, "request_failed": false]
            }
            let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
            _ = try await client.request("ota.check")
            for _ in 0..<3 {
                let response = try await client.request("status.get")
                let ota = try XCTUnwrap((response["status"] as? [String: Any])?["ota"] as? [String: Any])
                XCTAssertEqual(ota["request_pending"] as? Bool, true)
                XCTAssertFalse(ClockModel.updateRequestSettled(ota))
            }
            server.mutateClock(at: "192.168.1.81") { $0.otaStatus["request_pending"] = false; $0.otaStatus["request_failed"] = true }
            let failed = try await client.request("status.get")
            let ota = try XCTUnwrap((failed["status"] as? [String: Any])?["ota"] as? [String: Any])
            XCTAssertEqual(ota["request_failed"] as? Bool, true)
            XCTAssertTrue(ClockModel.updateRequestSettled(ota))
        }
    }
    func testLegacyHTTPUpdatesAreNotSubmittedWithoutCompletionEvidence() async throws {
        let server = officeServer()
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        for op in ["ota.check", "ota.install"] {
            do { _ = try await client.request(op); XCTFail("Legacy queued updates cannot be monitored") }
            catch { guard case ClockError.unavailable = error else { return XCTFail("\(error)") } }
        }
        XCTAssertFalse(server.requests.contains { $0.hasPrefix("POST ") })
        // If evidence disappears during an already accepted request, keep waiting.
        let response = try await client.request("status.get")
        let ota = try XCTUnwrap((response["status"] as? [String: Any])?["ota"] as? [String: Any])
        XCTAssertFalse(ClockModel.updateRequestSettled(ota))
    }
    func testWiFiTrialNeedsAPasswordOverTheWebAPI() async throws {
        let server = officeServer()
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        do {
            _ = try await client.request("wifi.trial", fields: ["patch": ["wifi_ssid": "Office", "wifi_security": 0]])
            XCTFail("A web trial cannot reuse the stored password")
        } catch { guard case ClockError.unavailable = error else { return XCTFail("\(error)") } }
        let queued = try await client.request("wifi.trial", fields: ["patch": ["wifi_ssid": "Office", "wifi_security": 0, "wifi_password": "pw"]])
        XCTAssertEqual(queued["state"] as? String, "queued")
        XCTAssertEqual(server.lastBody("/api/wifi/connect")?["ssid"] as? String, "Office")
        XCTAssertEqual(server.lastBody("/api/wifi/connect")?["password"] as? String, "pw")
        _ = try await client.request("wifi.trial", fields: ["patch": ["wifi_ssid": "Cafe", "wifi_security": 0, "wifi_password": NSNull()]])
        XCTAssertEqual(server.lastBody("/api/wifi/connect")?["password"] as? String, "")
    }
    func testNetworkFailuresAreRetryableConnectionErrors() async {
        let server = officeServer(); server.offline = true
        let client = LocalNetworkClockClient(host: "192.168.1.81", http: server)
        do { _ = try await client.request("settings.get"); XCTFail() } catch {
            XCTAssertEqual(error as? ClockError, .disconnected)
            XCTAssertTrue(ClockModel.canRetryConnection(error))
        }
        XCTAssertEqual(LocalNetworkClockClient.linkError(URLError(.timedOut)) as? ClockError, .timeout)
        XCTAssertTrue(LocalNetworkClockClient.linkError(URLError(.cancelled)) is CancellationError)
        XCTAssertEqual(LocalNetworkClockClient.linkError(URLError(.notConnectedToInternet)) as? ClockError, .disconnected)
    }
    func testSubnetHostsStartNearThePhoneAndStayBounded() {
        let own: UInt32 = 0xC0A8_0158  // 192.168.1.88
        let home = LocalSubnet.hosts(own: own, mask: 0xFFFF_FF00, limit: 1022)
        XCTAssertEqual(home.count, 253)
        XCTAssertFalse(home.contains("192.168.1.88")); XCTAssertFalse(home.contains("192.168.1.0")); XCTAssertFalse(home.contains("192.168.1.255"))
        XCTAssertEqual(Set(home.prefix(2)), ["192.168.1.87", "192.168.1.89"])
        // A /16 would mean 65k probes; fall back to the phone's own /24.
        XCTAssertEqual(LocalSubnet.hosts(own: own, mask: 0xFFFF_0000, limit: 1022).count, 253)
        XCTAssertEqual(LocalSubnet.hosts(own: own, mask: 0xFFFF_FC00, limit: 1022).count, 1021)
        XCTAssertTrue(LocalSubnet.hosts(own: own, mask: 0xFFFF_FFFF, limit: 1022).isEmpty)
    }
    func testDiscoveryFindsClocksAmongOtherDevicesAndThrottlesBackgroundSweeps() async {
        let server = officeServer()
        server.place(.init(mac: "88:57:21:17:79:0C", firmware: "0.2.15"), at: "192.168.1.55")
        let network = LocalClockNetwork(http: server, probeHTTP: server,
                                        hosts: { ["192.168.1.10", "192.168.1.81", "192.168.1.99", "192.168.1.55"] }, minimumSweepInterval: 30)
        XCTAssertTrue(network.shouldSweep())
        let found = await network.discover()
        XCTAssertEqual(found.map(\.host), ["192.168.1.55", "192.168.1.81"])
        XCTAssertEqual(found.last?.identity, FakeClockServer.officeIdentity)
        XCTAssertEqual(found.first?.firmware, "0.2.15")
        XCTAssertEqual(network.candidates, found)
        XCTAssertFalse(network.searching); XCTAssertTrue(network.searched)
        XCTAssertFalse(network.shouldSweep())
        XCTAssertTrue(network.shouldSweep(now: Date().addingTimeInterval(31)))
        let typed = await network.probe(host: "192.168.1.81")
        XCTAssertEqual(typed?.identity, FakeClockServer.officeIdentity)
        let printer = await network.probe(host: "192.168.1.10")
        XCTAssertNil(printer)
    }
    func testConnectRejectsADifferentClockAtTheSavedAddress() async {
        let network = LocalClockNetwork(http: officeServer(), probeHTTP: officeServer(), hosts: { [] })
        let wrong = await network.connect(host: "192.168.1.81", expecting: "AAAAAAAAAAAA")
        XCTAssertNil(wrong)
        let right = await network.connect(host: "192.168.1.81", expecting: FakeClockServer.officeIdentity)
        XCTAssertEqual(right?.0.requestTimeout, 6)
        XCTAssertEqual(right?.1["device_id"] as? String, FakeClockServer.officeIdentity)
    }
}
