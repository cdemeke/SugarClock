import Foundation

/// Keep one native pending connection through typical network pauses. Each
/// attempt remains bounded; foreground recovery is cancellable and backs off.
enum SessionPolicy {
    static let connectionTimeout: UInt64 = 60_000_000_000
    static let discoveryTimeout: UInt64 = 15_000_000_000
    static func retryDelay(afterFailures count: Int, base: UInt64 = 2_000_000_000) -> UInt64 {
        let multiplier = UInt64(1 << min(max(count - 1, 0), 4))
        return min(base, 30_000_000_000 / multiplier) * multiplier
    }
    static func mailboxDelay(emptyReads: Int, maximum: UInt64) -> UInt64 {
        min(maximum, UInt64(20_000_000) << min(max(emptyReads - 1, 0), 4))
    }
}

/// Only schema metadata is cached, never settings, status or credentials. A
/// verified hello must match the same device, boot and firmware before reuse.
/// Older firmware without a boot identity simply reloads its schema.
struct SchemaIdentity: Codable, Equatable {
    let device: String
    let boot: UInt32
    let firmware: String
    let hardware: String
    let capabilities: [String]
    let protocolVersion: Int

    init?(_ hello: [String: Any]) {
        guard let device = hello["device_id"] as? String, !device.isEmpty,
              let boot = hello["boot_id"] as? UInt32,
              let firmware = hello["firmware"] as? String, !firmware.isEmpty,
              let hardware = hello["hardware"] as? String,
              let capabilities = hello["capabilities"] as? [String],
              hello["v"] as? Int == 1 else { return nil }
        self.device = device; self.boot = boot; self.firmware = firmware
        self.hardware = hardware; self.capabilities = capabilities.sorted(); protocolVersion = 1
    }
}

struct SchemaCache {
    private static let metadata: Set<String> = ["key", "type", "min", "max", "max_length"]
    private let preferences: UserDefaults
    init(preferences: UserDefaults) { self.preferences = preferences }
    private func key(_ device: String) -> String { "schema.v1." + device }

    func read(_ identity: SchemaIdentity) -> [[String: Any]]? {
        guard let data = preferences.data(forKey: key(identity.device)), data.count <= 65_536,
              let entry = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let header = entry["identity"] as? [String: Any],
              let encoded = try? JSONSerialization.data(withJSONObject: header),
              let saved = try? JSONDecoder().decode(SchemaIdentity.self, from: encoded), saved == identity,
              let fields = entry["fields"] as? [[String: Any]], Self.valid(fields) else { return nil }
        return fields
    }
    func store(_ fields: [[String: Any]], identity: SchemaIdentity) {
        // Unknown schema extensions stay in memory but are not persisted: a
        // future firmware may give them semantics this client cannot validate.
        guard Self.valid(fields),
              let header = try? JSONEncoder().encode(identity),
              let object = try? JSONSerialization.jsonObject(with: header),
              let data = try? JSONSerialization.data(withJSONObject: ["identity": object, "fields": fields]),
              data.count <= 65_536 else { return }
        preferences.set(data, forKey: key(identity.device))
    }
    func remove(_ device: String) { preferences.removeObject(forKey: key(device)) }
    private static func valid(_ fields: [[String: Any]]) -> Bool {
        guard !fields.isEmpty, fields.count <= 176 else { return false }
        var keys = Set<String>()
        return fields.allSatisfy { field in
            guard Set(field.keys).isSubset(of: metadata),
                  let key = field["key"] as? String, !key.isEmpty, key.utf8.count <= 128,
                  keys.insert(key).inserted,
                  let type = field["type"] as? String, ["bool", "int", "text", "secret"].contains(type) else { return false }
            return ["min", "max", "max_length"].allSatisfy { field[$0] == nil || field[$0] is NSNumber }
        }
    }
}
