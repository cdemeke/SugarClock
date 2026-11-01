import Foundation
import CoreFoundation
#if canImport(Darwin)
import Darwin
#endif

/// Requests the clock understands, independent of the link carrying them.
/// Bluetooth frames them over GATT; the local network maps them onto the
/// clock's existing web API. ClockModel never needs to know which is in use.
@MainActor public protocol ClockRequesting: AnyObject {
    var requestTimeout: TimeInterval { get set }
    func request(_ operation: String, fields: [String: Any]) async throws -> [String: Any]
    func schema(progress: SchemaProgress?) async throws -> [[String: Any]]
    func save(_ patch: [String: Any]) async throws
}
public extension ClockRequesting {
    func request(_ operation: String) async throws -> [String: Any] { try await request(operation, fields: [:]) }
}
extension ClockClient: ClockRequesting {}

public protocol LocalHTTP: Sendable {
    func send(_ request: URLRequest) async throws -> (Data, Int)
}

public struct URLSessionLocalHTTP: LocalHTTP {
    private let session: URLSession
    public init(requestTimeout: TimeInterval = 6) {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = requestTimeout
        configuration.timeoutIntervalForResource = requestTimeout * 2
        configuration.waitsForConnectivity = false
        configuration.httpMaximumConnectionsPerHost = 2
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.urlCache = nil
        session = URLSession(configuration: configuration)
    }
    public func send(_ request: URLRequest) async throws -> (Data, Int) {
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw ClockError.malformed }
        return (data, http.statusCode)
    }
}

/// The clock's eFuse identity, derived the same way the firmware's Bluetooth
/// hello derives it, so one saved clock can be reached over either link.
public enum LocalClockIdentity {
    private static let localOnlyPrefix = "5C0C1000-0000-4000-8000-"

    /// `ESP.getEfuseMac()` holds the MAC little-endian; `%012llX` therefore
    /// prints its bytes in reverse order of the colon-separated station MAC.
    public static func identity(fromMAC mac: String) -> String? {
        let parts = mac.split(separator: ":")
        guard parts.count == 6, parts.allSatisfy({ $0.count == 2 && UInt8($0, radix: 16) != nil }) else { return nil }
        return parts.reversed().joined().uppercased()
    }
    public static func name(for identity: String) -> String { "SugarClock-" + identity.prefix(6) }
    /// Clocks added over Wi-Fi have no Core Bluetooth identifier. A stable
    /// synthetic one keeps navigation and saved-clock storage unchanged.
    public static func peripheralID(for identity: String) -> UUID? {
        guard identity.count == 12, identity.allSatisfy(\.isHexDigit) else { return nil }
        return UUID(uuidString: localOnlyPrefix + identity.uppercased())
    }
    public static func isLocalOnly(_ id: UUID) -> Bool { id.uuidString.hasPrefix(localOnlyPrefix) }
    /// Accepts what people type: "192.168.1.81", "http://clock.local/", "10.0.0.5:8080".
    public static func normalizedHost(_ text: String) -> String? {
        var host = text.trimmingCharacters(in: .whitespacesAndNewlines)
        for scheme in ["http://", "https://"] where host.lowercased().hasPrefix(scheme) { host.removeFirst(scheme.count) }
        while host.hasSuffix("/") { host.removeLast() }
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:")
        guard !host.isEmpty, host.count <= 253, host.unicodeScalars.allSatisfy(allowed.contains) else { return nil }
        return host
    }
}

public struct LocalClockCandidate: Identifiable, Equatable, Sendable {
    public let host: String
    public let identity: String
    public let firmware: String
    public var id: String { identity }
    public var name: String { LocalClockIdentity.name(for: identity) }
    public var peripheralID: UUID? { LocalClockIdentity.peripheralID(for: identity) }
}

/// Settings metadata for clocks reached over the local network. Mainline
/// firmware has no schema endpoint, so this mirrors the firmware's own field
/// table (config_patch.cpp) and is filtered to keys the clock actually returns.
public enum LocalClockSchema {
    struct Field: Sendable {
        let key: String, type: String
        var min: Int? = nil, max: Int? = nil, maxLength: Int? = nil
    }
    private static func int(_ key: String, _ min: Int, _ max: Int) -> Field { Field(key: key, type: "int", min: min, max: max) }
    private static func bool(_ key: String) -> Field { Field(key: key, type: "bool") }
    private static func text(_ key: String, _ length: Int) -> Field { Field(key: key, type: "text", maxLength: length) }
    private static func secret(_ key: String, _ length: Int) -> Field { Field(key: key, type: "secret", maxLength: length) }
    private static let colors = ["color_urgent_low", "color_low", "color_in_range", "color_high", "color_urgent_high", "color_stale", "color_clock", "color_weather"]

    // Wi-Fi uses its own trial flow and libre_patient_id requires the web
    // page's person picker, so neither is offered as an ordinary setting.
    static let all: [Field] = [
        bool("glucose_enabled"), int("data_source", 0, 3), secret("server_url", 255), secret("auth_token", 255),
        text("dexcom_username", 63), secret("dexcom_password", 63), bool("dexcom_us"),
        secret("libre_email", 63), secret("libre_password", 63),
        int("ambient_style", 0, 2), bool("ambient_use_glucose_colors"), bool("glucose_only_when_low"),
        int("poll_interval", 15, 3600), int("brightness", 1, 255), bool("auto_brightness"), bool("show_delta"), bool("use_mmol"),
        int("thresh_urgent_low", 20, 600), int("thresh_low", 20, 600), int("thresh_high", 20, 600), int("thresh_urgent_high", 20, 600),
        text("timezone", 63), bool("use_24h"), bool("time_display_enabled"), int("default_mode", 0, 3),
        bool("ambient_enabled"), int("ambient_character", 0, 6), bool("ambient_seasonal"),
        bool("alert_enabled"), int("alert_low", 20, 600), int("alert_high", 20, 600), int("alert_snooze_min", 1, 120),
    ] + colors.map { int($0, 0, 16_777_215) } + [
        bool("night_mode_enabled"), int("night_start_hour", 0, 23), int("night_end_hour", 0, 23), int("night_brightness", 1, 255),
        int("stale_timeout_min", 5, 60),
        bool("weather_enabled"), secret("weather_api_key", 47), text("weather_city", 63), bool("weather_use_f"), int("weather_poll_min", 5, 60),
        bool("date_on_time_screen"), int("date_format", 0, 2),
        bool("timer_enabled"), int("timer_work_min", 1, 120), int("timer_break_min", 1, 60), int("timer_long_break_min", 1, 60),
        int("timer_sessions", 1, 12), bool("timer_buzzer"), bool("stopwatch_enabled"),
        bool("notify_enabled"), int("notify_default_duration", 5, 600), bool("notify_allow_buzzer"),
        bool("sysmon_enabled"), text("sysmon_label", 7), int("sysmon_display_mode", 0, 1), int("sysmon_warn_pct", 0, 100), int("sysmon_crit_pct", 0, 100),
        bool("auto_cycle_enabled"), int("auto_cycle_sec", 3, 300),
        bool("auto_update_enabled"), int("auto_update_hour", 0, 23),
        bool("countdown_enabled"), text("countdown_name", 15), int("countdown_target", 0, 2_147_483_647),
    ]
    private static let byKey = Dictionary(uniqueKeysWithValues: all.map { ($0.key, $0) })
    // The dedicated Wi-Fi trial editor needs these readback values, but they
    // must never enter the ordinary settings schema or settings.patch route.
    private static let wifiMetadata: [Field] = [
        text("wifi_ssid", 32), secret("wifi_password", 63), int("wifi_security", 0, 1),
        int("wifi_eap_method", 0, 1), text("wifi_identity", 63), secret("wifi_eap_password", 63),
        text("wifi_anon_identity", 63), bool("wifi_validate_ca"),
    ]

    private static func supported(_ field: Field, by config: [String: Any]) -> Bool {
        config[field.key] != nil || config["has_" + field.key] != nil
    }
    public static func fields(supportedBy config: [String: Any]) -> [[String: Any]] {
        all.filter { supported($0, by: config) }.map { field in
            var entry: [String: Any] = ["key": field.key, "type": field.type]
            if let min = field.min { entry["min"] = min }
            if let max = field.max { entry["max"] = max }
            if let length = field.maxLength { entry["max_length"] = length }
            return entry
        }
    }
    /// Web GET → Bluetooth `settings.get` shape: secrets become configured
    /// flags only, and hex colors become the integers the schema describes.
    public static func settings(fromHTTP config: [String: Any]) -> [String: Any] {
        var settings: [String: Any] = [:]
        for field in all + wifiMetadata where supported(field, by: config) {
            if field.type == "secret" {
                let configured = config["has_" + field.key] as? Bool ?? !((config[field.key] as? String) ?? "").isEmpty
                settings[field.key + "_configured"] = configured
            } else if colors.contains(field.key), let hex = config[field.key] as? String {
                if let value = color(hex) { settings[field.key] = value }
            } else if let value = config[field.key] {
                settings[field.key] = value
            }
        }
        return settings
    }
    /// Bluetooth patch → web POST body. Mainline web firmware treats an empty
    /// secret as "leave unchanged", so clearing one cannot be honoured here.
    public static func httpBody(forPatch patch: [String: Any]) throws -> [String: Any] {
        var body: [String: Any] = [:]
        for (key, value) in patch {
            guard let field = byKey[key] else { throw ClockError.rejected("unsupported_field: \(key)") }
            if field.type == "secret" {
                guard let text = value as? String, !text.isEmpty else {
                    throw ClockError.unavailable("Removing a saved password or key isn’t available over Wi-Fi. Enter a replacement, or use the clock’s web page.")
                }
                body[key] = text
            } else if colors.contains(key), let number = value as? Int {
                body[key] = String(format: "#%06x", number & 0xFFFFFF)
            } else {
                body[key] = value
            }
        }
        return body
    }
    static func color(_ hex: String) -> Int? {
        guard hex.count == 7, hex.hasPrefix("#") else { return nil }
        return Int(hex.dropFirst(), radix: 16)
    }
}

/// Speaks the clock's web API on the local network. The clock keeps this
/// server running alongside glucose updates, so there is no pairing, idle
/// timeout or radio pause to recover from between requests.
@MainActor public final class LocalNetworkClockClient: ClockRequesting {
    public let host: String
    public var requestTimeout: TimeInterval = 6
    private let http: LocalHTTP
    private var nextID = 1
    private var lastConfig: [String: Any]?
    private var expectedIdentity: String?
    static let capabilities = ["settings.patch", "schema", "wifi.trial", "ota.signed_wifi"]

    public init(host: String, http: LocalHTTP, expecting identity: String? = nil) {
        self.host = host; self.http = http; self.expectedIdentity = identity
    }

    public func request(_ operation: String, fields: [String: Any]) async throws -> [String: Any] {
        let id = nextID
        nextID = nextID >= Int(UInt16.max) ? 1 : nextID + 1
        var response: [String: Any] = ["v": 1, "id": id, "state": "applied"]
        switch operation {
        case "hello":
            response.merge(try await hello()) { $1 }
        case "settings.get":
            let read = try await settings()
            response["settings"] = read.values; response["saved"] = read.saved
        case "schema.get":
            let page = fields["page"] as? Int ?? 0
            response["fields"] = page == 0 ? try await schema(progress: nil) : []
            response["more"] = false
        case "status.get":
            response["status"] = try await status()
        case "settings.patch":
            try await save(fields["patch"] as? [String: Any] ?? [:])
            response["saved"] = try await settings().saved
        case "wifi.scan":
            let (code, body) = try await send("POST", "/api/wifi/scan/refresh")
            guard (200..<300).contains(code), body["started"] as? Bool == true || body["scanning"] as? Bool == true else {
                throw ClockError.rejected("scan_failed")
            }
            response["state"] = "queued"
        case "wifi.results":
            let body = try await get("/api/wifi/scan")
            if let error = body["error"] as? String, !error.isEmpty { throw ClockError.rejected("scan_failed") }
            response["scanning"] = body["scanning"] as? Bool ?? false
            response["networks"] = (body["networks"] as? [[String: Any]] ?? []).map { network in
                ["ssid": network["ssid"] ?? "", "rssi": network["rssi"] ?? -100,
                 "enterprise": network["enterprise"] ?? false, "auth": network["enc"] ?? 0]
            }
        case "wifi.trial":
            try await startWiFiTrial(fields["patch"] as? [String: Any] ?? [:]); response["state"] = "queued"
        case "ota.check", "ota.install":
            // Legacy web firmware cannot distinguish a queued fleet visit from
            // an old idle result. Do not submit a request we cannot monitor.
            let ota = try await get("/api/ota/status")
            guard Self.reportedBool(ota["request_pending"]) != nil,
                  Self.reportedBool(ota["request_failed"]) != nil else {
                throw ClockError.unavailable("This clock’s firmware can’t confirm update requests over Wi-Fi. Use its web page to check or install firmware updates.")
            }
            let (code, body) = try await send("POST", operation == "ota.check" ? "/api/ota/check" : "/api/ota/install")
            guard (200..<300).contains(code) else {
                let error = body["error"] as? String ?? "internal"
                throw ClockError.rejected(code == 412 ? "deferred" : error == "no_update_available" ? "no_update" : error == "ota_busy" ? "busy" : error)
            }
            response["state"] = "queued"
        default:
            throw ClockError.rejected("unsupported_operation")
        }
        return response
    }
    public func schema(progress: SchemaProgress? = nil) async throws -> [[String: Any]] {
        let config: [String: Any]
        if let lastConfig {
            _ = try await verifyIdentity()
            config = lastConfig
        } else { config = try await get("/api/config"); lastConfig = config }
        return LocalClockSchema.fields(supportedBy: config)
    }
    public func save(_ patch: [String: Any]) async throws {
        let body = try LocalClockSchema.httpBody(forPatch: patch)
        let (code, response) = try await send("POST", "/api/config", body: body)
        guard (200..<300).contains(code) else {
            throw ClockError.rejected(response["error"] as? String ?? "http_\(code)")
        }
    }
    func hello() async throws -> [String: Any] {
        let identity = try await verifyIdentity()
        let status = try await get("/api/status")
        guard let firmware = status["firmware_version"] as? String, !firmware.isEmpty else { throw ClockError.malformed }
        return ["device_id": identity, "name": LocalClockIdentity.name(for: identity), "firmware": firmware,
                "hardware": status["hardware"] as? String ?? "ulanzi-tc001-esp32-4mb",
                "capabilities": Self.capabilities, "max_message": Frame.maximum, "transport": "wifi", "host": host]
    }
    private func settings() async throws -> (values: [String: Any], saved: Bool) {
        let config = try await get("/api/config")
        lastConfig = config
        return (LocalClockSchema.settings(fromHTTP: config), Self.reportedBool(config["configuration_saved"]) == true)
    }
    /// Only an actual JSON Boolean is evidence; numeric/string lookalikes and
    /// absent legacy metadata must not establish durability or request completion.
    private static func reportedBool(_ value: Any?) -> Bool? {
        guard let number = value as? NSNumber, CFGetTypeID(number) == CFBooleanGetTypeID() else { return nil }
        return number.boolValue
    }
    /// The Bluetooth status shape the diagnostics and firmware screens read.
    private func status() async throws -> [String: Any] {
        let clock = try await get("/api/status")
        let wifi = (try? await get("/api/wifi/status")) ?? [:]
        // The web API reports durability in /api/config. Read it afresh:
        // cached evidence could outlive a later failed write or Wi-Fi trial.
        // Missing/unreadable evidence stays unconfirmed without hiding OTA status.
        let config = (try? await get("/api/config")) ?? [:]
        let ota = (try? await get("/api/ota/status")) ?? [:]
        let pending = Self.reportedBool(ota["request_pending"])
        let failed = Self.reportedBool(ota["request_failed"])
        func probe(_ value: Any?) -> Int { value as? String == "ok" ? 1 : value as? String == "fail" ? 2 : 0 }
        let checks = wifi["checks"] as? [String: Any] ?? [:]
        var status: [String: Any] = [
            "wifi": clock["wifi_connected"] as? Bool == true ? "CONNECTED" : (wifi["state"] as? String ?? "unknown").uppercased(),
            "trial": wifi["trial"] as? String ?? "idle", "trial_detail": wifi["detail"] as? String ?? "",
            "network_saved": !(wifi["ssid"] as? String ?? "").isEmpty,
            "configuration_saved": Self.reportedBool(config["configuration_saved"]) == true,
            "internet_dns": probe(checks["dns"]), "provider_reachable": probe(checks["data"]),
            "data_received": clock["valid"] as? Bool ?? false,
            "provider_failures": clock["failure_count"] as? Int ?? 0,
            "ota": ["state": ota["state"] as? String ?? "unknown", "progress": ota["progress"] as? Int ?? 0,
                    "current_version": ota["current_version"] as? String ?? clock["firmware_version"] as? String ?? "",
                    "available_version": ota["available_version"] as? String ?? "", "error": ota["last_error"] as? String ?? "",
                    "deferral": ota["safety_reason"] as? String ?? "", "pending_verification": ota["pending_verification"] as? Bool ?? false,
                    "request_pending": pending == nil || failed == nil ? true : pending!,
                    "request_failed": pending == false ? failed ?? false : false] as [String: Any],
        ]
        if let age = clock["data_age_sec"] as? NSNumber, age.doubleValue >= 0 { status["data_age_ms"] = age.doubleValue * 1000 }
        if let rssi = clock["wifi_rssi"] as? Int { status["wifi_rssi"] = rssi }
        // Optional diagnostic reads may fail, but must not hide an address
        // reassignment and keep another clock's session marked ready.
        _ = try await verifyIdentity()
        return status
    }
    private func startWiFiTrial(_ patch: [String: Any]) async throws {
        let security = patch["wifi_security"] as? Int ?? 0
        let passwordKey = security == 1 ? "wifi_eap_password" : "wifi_password"
        let password: String
        switch patch[passwordKey] {
        case let text as String: password = text
        case is NSNull: password = ""
        default:
            // The web trial cannot reuse the stored password the way Bluetooth can.
            throw ClockError.unavailable("Enter the network password to change Wi-Fi from this iPhone.")
        }
        let body: [String: Any] = [
            "ssid": patch["wifi_ssid"] as? String ?? "", "security": security, "eap_method": patch["wifi_eap_method"] as? Int ?? 0,
            "identity": patch["wifi_identity"] as? String ?? "", "anon_identity": patch["wifi_anon_identity"] as? String ?? "",
            "validate_ca": patch["wifi_validate_ca"] as? Bool ?? false, "password": password,
        ]
        let (code, response) = try await send("POST", "/api/wifi/connect", body: body)
        guard (200..<300).contains(code) else { throw ClockError.rejected(response["error"] as? String ?? "http_\(code)") }
    }
    private func get(_ path: String) async throws -> [String: Any] {
        let (code, body) = try await send("GET", path)
        // A clock mid-restart can briefly answer with errors; retrying is safe for reads.
        guard (200..<300).contains(code) else { throw ClockError.disconnected }
        // Validate after readback so a network/address change during the read
        // cannot confirm another clock's settings under the saved identity.
        if path != "/api/debug" { _ = try await verifyIdentity() }
        return body
    }
    /// An address is only a route, never a clock identity. Pin the first valid
    /// greeting (or the saved identity) and never rebind this client on DHCP or
    /// network changes. A retryable disconnect makes the coordinator rediscover
    /// the saved clock without replaying an uncertain mutation.
    @discardableResult private func verifyIdentity() async throws -> String {
        let debug = try await get("/api/debug")
        guard let mac = debug["mac"] as? String, let identity = LocalClockIdentity.identity(fromMAC: mac),
              expectedIdentity == nil || expectedIdentity == identity else {
            lastConfig = nil
            throw ClockError.disconnected
        }
        expectedIdentity = identity
        return identity
    }
    private func send(_ method: String, _ path: String, body: [String: Any]? = nil) async throws -> (Int, [String: Any]) {
        // Every mutating route, including direct save(), checks the pinned
        // identity immediately before transmitting settings, secrets or commands.
        if method != "GET" { _ = try await verifyIdentity() }
        guard let url = URL(string: "http://\(host)\(path)") else { throw ClockError.unavailable("This clock address isn’t valid.") }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: requestTimeout)
        request.httpMethod = method
        if let body {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        let data: Data, code: Int
        do { (data, code) = try await http.send(request) } catch { throw Self.linkError(error) }
        try Task.checkCancellation()
        if data.isEmpty { return (code, [:]) }
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            if (200..<300).contains(code) { throw ClockError.malformed }
            return (code, [:])
        }
        return (code, object)
    }
    /// Network failures are connection problems the coordinator may retry.
    static func linkError(_ error: Error) -> Error {
        if error is CancellationError || error is ClockError { return error }
        guard let url = error as? URLError else { return ClockError.disconnected }
        switch url.code {
        case .cancelled: return CancellationError()
        case .timedOut: return ClockError.timeout
        default: return ClockError.disconnected
        }
    }
}

/// Finds clocks on the phone's Wi-Fi network. The firmware does not advertise
/// a Bonjour service, so this probes the local subnet's web API and identifies
/// clocks by the same eFuse identity Bluetooth reports.
@MainActor public final class LocalClockNetwork: ObservableObject {
    @Published public private(set) var candidates: [LocalClockCandidate] = []
    @Published public private(set) var searching = false
    @Published public private(set) var searched = false
    private let http: LocalHTTP
    private let probeHTTP: LocalHTTP
    private let hosts: () -> [String]
    private let minimumSweepInterval: TimeInterval
    private var lastSweep: Date?
    private var activeSearches = 0

    public init(http: LocalHTTP = URLSessionLocalHTTP(), probeHTTP: LocalHTTP = URLSessionLocalHTTP(requestTimeout: 1.5),
                hosts: @escaping () -> [String] = { LocalSubnet.candidateHosts() }, minimumSweepInterval: TimeInterval = 30) {
        self.http = http; self.probeHTTP = probeHTTP; self.hosts = hosts; self.minimumSweepInterval = minimumSweepInterval
    }
    public func client(host: String, expecting identity: String? = nil) -> LocalNetworkClockClient {
        LocalNetworkClockClient(host: host, http: http, expecting: identity)
    }

    /// Greets a clock at a known address. Nil when it is unreachable or is a
    /// different clock (for example after a DHCP address change).
    public func connect(host: String, expecting identity: String?) async -> (LocalNetworkClockClient, [String: Any])? {
        let client = client(host: host, expecting: identity)
        client.requestTimeout = 2.5
        guard let greeting = try? await client.request("hello"),
              let found = greeting["device_id"] as? String, identity == nil || found == identity else { return nil }
        client.requestTimeout = 6
        return (client, greeting)
    }
    /// Background recovery sweeps at most once per interval; a person tapping
    /// Search always gets a fresh one.
    public func shouldSweep(now: Date = Date()) -> Bool {
        guard let lastSweep else { return true }
        return now.timeIntervalSince(lastSweep) >= minimumSweepInterval
    }
    @discardableResult public func discover() async -> [LocalClockCandidate] {
        lastSweep = Date()
        activeSearches += 1; searching = true
        defer { activeSearches -= 1; searching = activeSearches > 0; searched = true }
        let addresses = hosts()
        let http = probeHTTP
        var found: [LocalClockCandidate] = []
        await withTaskGroup(of: LocalClockCandidate?.self) { group in
            var next = 0
            // Absent addresses cost a full timeout, so breadth decides sweep time.
            let width = min(64, addresses.count)
            while next < width { let host = addresses[next]; group.addTask { await Self.probe(host: host, http: http) }; next += 1 }
            while let result = await group.next() {
                if let result { found.append(result) }
                if Task.isCancelled { group.cancelAll(); continue }
                if next < addresses.count { let host = addresses[next]; group.addTask { await Self.probe(host: host, http: http) }; next += 1 }
            }
        }
        found.sort { $0.host.compare($1.host, options: .numeric) == .orderedAscending }
        if !Task.isCancelled { candidates = found }
        return found
    }
    /// Checks a typed address. Uses the normal timeout: a person is waiting on it.
    public func probe(host: String) async -> LocalClockCandidate? { await Self.probe(host: host, http: http) }
    /// Background availability check for a saved clock: short timeouts, and
    /// only an answer with the expected identity counts.
    public func isAvailable(host: String, identity: String) async -> Bool {
        await Self.probe(host: host, http: probeHTTP)?.identity == identity
    }

    nonisolated static func probe(host: String, http: LocalHTTP) async -> LocalClockCandidate? {
        func get(_ path: String, timeout: TimeInterval) async -> [String: Any]? {
            guard let url = URL(string: "http://\(host)\(path)") else { return nil }
            var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: timeout)
            request.httpMethod = "GET"
            guard let (data, code) = try? await http.send(request), (200..<300).contains(code) else { return nil }
            return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        }
        // A clock answers on the LAN in tens of milliseconds; once it has, give
        // the identity read more time in case it is busy with a glucose fetch.
        guard let status = await get("/api/status", timeout: 1.5), let firmware = status["firmware_version"] as? String,
              status["glucose"] != nil || (status["hardware"] as? String)?.hasPrefix("ulanzi-tc001") == true,
              let debug = await get("/api/debug", timeout: 4), let mac = debug["mac"] as? String,
              let identity = LocalClockIdentity.identity(fromMAC: mac) else { return nil }
        return LocalClockCandidate(host: host, identity: identity, firmware: firmware)
    }
}

public enum LocalSubnet {
    /// IPv4 hosts on the phone's Wi-Fi (or wired) subnets, never cellular or VPN.
    public static func candidateHosts(limit: Int = 1022) -> [String] {
        var result: [String] = []
        var pointer: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&pointer) == 0, let first = pointer else { return [] }
        defer { freeifaddrs(pointer) }
        for entry in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let flags = Int32(entry.pointee.ifa_flags)
            guard flags & IFF_UP != 0, flags & IFF_RUNNING != 0, flags & IFF_LOOPBACK == 0,
                  String(cString: entry.pointee.ifa_name).hasPrefix("en"),
                  let address = entry.pointee.ifa_addr, address.pointee.sa_family == UInt8(AF_INET),
                  let netmask = entry.pointee.ifa_netmask else { continue }
            let own = address.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            let mask = netmask.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            result += hosts(own: own, mask: mask, limit: limit).filter { !result.contains($0) }
        }
        return result
    }
    /// Nearest addresses first, so a small home network is covered quickly.
    /// Subnets larger than the limit fall back to the phone's own /24.
    static func hosts(own: UInt32, mask: UInt32, limit: Int) -> [String] {
        let effective = (~mask) > UInt32(limit + 1) ? UInt32(0xFFFF_FF00) : mask
        let network = own & effective, broadcast = network | ~effective
        guard broadcast > network + 1 else { return [] }
        let addresses = (network + 1..<broadcast).filter { $0 != own }
        return addresses.sorted { (Int64($0) - Int64(own)).magnitude < (Int64($1) - Int64(own)).magnitude }.map { value in
            "\(value >> 24 & 255).\(value >> 16 & 255).\(value >> 8 & 255).\(value & 255)"
        }
    }
}
