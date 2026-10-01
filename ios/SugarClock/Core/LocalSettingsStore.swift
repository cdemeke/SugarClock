import Foundation
#if canImport(Security)
import Security
#endif

/// A local editor workspace, never a glucose or diagnostic history. The settings
/// dictionary is a redacted readback; only explicit pending edits contain secrets.
public struct LocalSettingsSnapshot:Codable,Equatable {
    public var settingsJSON:Data
    public var fieldsJSON:Data
    public var draft:SettingsDraft
    public var lastSynced:Date
    /// A reconciliation marker, not permission to repeat a write after relaunch.
    public var submittedDraft:SettingsDraft?

    public init(settings:[String:Any],fields:[[String:Any]],draft:SettingsDraft?=nil,lastSynced:Date=Date(),submittedDraft:SettingsDraft?=nil) throws {
        let safeFields=try Self.safeFields(fields)
        let safeSettings=Self.redactedSettings(settings,fields:safeFields)
        settingsJSON=try JSONSerialization.data(withJSONObject:safeSettings,options:.sortedKeys)
        fieldsJSON=try JSONSerialization.data(withJSONObject:safeFields,options:.sortedKeys)
        self.draft=(draft ?? SettingsDraft(settings:safeSettings,fields:safeFields)).withoutUnusedSecretText()
        self.lastSynced=lastSynced
        self.submittedDraft=submittedDraft?.withoutUnusedSecretText()
    }
    public func settings() throws ->[String:Any] {
        guard settingsJSON.count<=LocalSettingsCodec.maximumJSONBytes,
              let result=try JSONSerialization.jsonObject(with:settingsJSON) as? [String:Any] else {throw LocalSettingsStoreError.invalidData}
        return Self.redactedSettings(result,fields:try fields())
    }
    public func fields() throws ->[[String:Any]] {
        guard fieldsJSON.count<=LocalSettingsCodec.maximumJSONBytes,
              let result=try JSONSerialization.jsonObject(with:fieldsJSON) as? [[String:Any]] else {throw LocalSettingsStoreError.invalidData}
        return try Self.safeFields(result)
    }
    fileprivate func normalized() throws ->LocalSettingsSnapshot {
        let schema=try fields()
        try draft.validateForStorage()
        try submittedDraft?.validateForStorage()
        guard lastSynced.timeIntervalSince1970.isFinite else {throw LocalSettingsStoreError.invalidData}
        return try LocalSettingsSnapshot(settings:settings(),fields:schema,draft:draft,lastSynced:lastSynced,submittedDraft:submittedDraft)
    }
    private static func safeFields(_ fields:[[String:Any]]) throws ->[[String:Any]] {
        guard fields.count<=192 else {throw LocalSettingsStoreError.tooLarge}
        var seen:Set<String>=[]
        return try fields.map {field in
            guard let key=field["key"] as? String,!key.isEmpty,key.utf8.count<=96,
                  let type=field["type"] as? String,type.utf8.count<=32,seen.insert(key).inserted else {throw LocalSettingsStoreError.invalidData}
            var safe:[String:Any]=["key":key,"type":type]
            // Schema descriptors never need example values, credentials or status.
            for name in ["min","max","max_length"] {
                if let value=field[name] as? NSNumber {safe[name]=value}
            }
            return safe
        }
    }
    private static let knownSecrets:Set<String>=["wifi_password","wifi_eap_password","dexcom_password","auth_token","server_url","weather_api_key"]
    private static let wifiMetadata:Set<String>=["wifi_ssid","wifi_security","wifi_eap_method","wifi_identity","wifi_anon_identity","wifi_validate_ca"]
    private static func redactedSettings(_ settings:[String:Any],fields:[[String:Any]])->[String:Any] {
        let secrets=knownSecrets.union(fields.filter {$0["type"] as? String=="secret"}.compactMap {$0["key"] as? String})
        let allowed=Set(fields.compactMap {$0["key"] as? String}).union(wifiMetadata).subtracting(secrets)
        var safe:[String:Any]=[:]
        for key in allowed {
            if let value=settings[key] as? String {safe[key]=value}
            else if let value=settings[key] as? NSNumber {safe[key]=value}
        }
        for key in secrets {
            if let configured=settings[key+"_configured"] as? Bool {safe[key+"_configured"]=configured}
        }
        return safe
    }
}

public protocol LocalSettingsStore {
    func load(clockID:String) throws ->LocalSettingsSnapshot?
    func save(_ snapshot:LocalSettingsSnapshot,clockID:String) throws
    func remove(clockID:String) throws
}

public enum LocalSettingsStoreError:LocalizedError {
    case invalidData,unsupportedVersion,tooLarge,unavailable(Int32)
    public var errorDescription:String? {
        switch self {
        case .invalidData:return "The local settings draft could not be read. Connect to the clock to load its current settings."
        case .unsupportedVersion:return "This local settings draft requires a newer version of SugarClock."
        case .tooLarge:return "These settings are too large to keep locally. Shorten the pending entries."
        case .unavailable:return "The secure local settings store is unavailable. Unlock your iPhone and try again."
        }
    }
}

/// Versioning and bounds are shared by the real store and the injected test store.
enum LocalSettingsCodec {
    static let maximumBytes=128*1024
    static let maximumJSONBytes=32*1024
    struct Envelope:Codable {let version:Int;let snapshot:LocalSettingsSnapshot}
    static func validateID(_ id:String) throws {
        guard !id.isEmpty,id.utf8.count<=128,id.unicodeScalars.allSatisfy({CharacterSet.alphanumerics.union(CharacterSet(charactersIn:"-_.:")).contains($0)}) else {throw LocalSettingsStoreError.invalidData}
    }
    static func encode(_ snapshot:LocalSettingsSnapshot) throws ->Data {
        let safe=try snapshot.normalized()
        guard safe.settingsJSON.count<=maximumJSONBytes,safe.fieldsJSON.count<=maximumJSONBytes else {throw LocalSettingsStoreError.tooLarge}
        let data=try JSONEncoder().encode(Envelope(version:1,snapshot:safe))
        guard data.count<=maximumBytes else {throw LocalSettingsStoreError.tooLarge}
        return data
    }
    static func decode(_ data:Data) throws ->LocalSettingsSnapshot {
        guard data.count<=maximumBytes else {throw LocalSettingsStoreError.tooLarge}
        do {
            let envelope=try JSONDecoder().decode(Envelope.self,from:data)
            guard envelope.version==1 else {throw LocalSettingsStoreError.unsupportedVersion}
            return try envelope.snapshot.normalized()
        } catch let error as LocalSettingsStoreError {throw error}
        catch {throw LocalSettingsStoreError.invalidData}
    }
}

/// The OS encrypts the complete workspace, including pending replacement secrets.
/// Items are scoped to this app and device, unavailable while locked, and do
/// not synchronize through iCloud. There is no plaintext UserDefaults fallback.
public final class KeychainLocalSettingsStore:LocalSettingsStore {
    private let service="com.sugarclock.companion.local-settings.v1"
    public init() {}
    #if canImport(Security)
    private func query(_ clockID:String)->[String:Any] {
        [kSecClass as String:kSecClassGenericPassword,kSecAttrService as String:service,
         kSecAttrAccount as String:clockID,kSecAttrSynchronizable as String:false,
         kSecUseDataProtectionKeychain as String:true]
    }
    #endif
    public func load(clockID:String) throws ->LocalSettingsSnapshot? {
        try LocalSettingsCodec.validateID(clockID)
        #if canImport(Security)
        var request=query(clockID)
        request[kSecReturnData as String]=true
        request[kSecMatchLimit as String]=kSecMatchLimitOne
        var item:CFTypeRef?
        let status=SecItemCopyMatching(request as CFDictionary,&item)
        if status==errSecItemNotFound {return nil}
        guard status==errSecSuccess else {throw LocalSettingsStoreError.unavailable(status)}
        guard let data=item as? Data else {throw LocalSettingsStoreError.invalidData}
        return try LocalSettingsCodec.decode(data)
        #else
        throw LocalSettingsStoreError.unavailable(-1)
        #endif
    }
    public func save(_ snapshot:LocalSettingsSnapshot,clockID:String) throws {
        try LocalSettingsCodec.validateID(clockID)
        let data=try LocalSettingsCodec.encode(snapshot)
        #if canImport(Security)
        let request=query(clockID)
        let attributes:[String:Any]=[kSecValueData as String:data,kSecAttrAccessible as String:kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        var status=SecItemUpdate(request as CFDictionary,attributes as CFDictionary)
        if status==errSecItemNotFound {
            status=SecItemAdd(request.merging(attributes,uniquingKeysWith:{$1}) as CFDictionary,nil)
            if status==errSecDuplicateItem {status=SecItemUpdate(request as CFDictionary,attributes as CFDictionary)}
        }
        guard status==errSecSuccess else {throw LocalSettingsStoreError.unavailable(status)}
        #else
        throw LocalSettingsStoreError.unavailable(-1)
        #endif
    }
    public func remove(clockID:String) throws {
        try LocalSettingsCodec.validateID(clockID)
        #if canImport(Security)
        let status=SecItemDelete(query(clockID) as CFDictionary)
        guard status==errSecSuccess || status==errSecItemNotFound else {throw LocalSettingsStoreError.unavailable(status)}
        #else
        throw LocalSettingsStoreError.unavailable(-1)
        #endif
    }
}

/// Explicitly injected in previews/tests; production never falls back to this.
public final class MemoryLocalSettingsStore:LocalSettingsStore {
    var records:[String:Data]=[:]
    public init() {}
    public func load(clockID:String) throws ->LocalSettingsSnapshot? {
        try LocalSettingsCodec.validateID(clockID)
        return try records[clockID].map(LocalSettingsCodec.decode)
    }
    public func save(_ snapshot:LocalSettingsSnapshot,clockID:String) throws {
        try LocalSettingsCodec.validateID(clockID)
        records[clockID]=try LocalSettingsCodec.encode(snapshot)
    }
    public func remove(clockID:String) throws {
        try LocalSettingsCodec.validateID(clockID)
        records.removeValue(forKey:clockID)
    }
}
