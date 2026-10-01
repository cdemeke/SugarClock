#if DEBUG
import Foundation
import Security

/// Explicit simulator check of the real Keychain backend. Never selected by a
/// normal launch, never uses a real clock identity, and removes its test record.
enum KeychainSmokeCheck {
    static func run()->String {
        let id="smoke-"+UUID().uuidString
        let store=KeychainLocalSettingsStore()
        defer {try? store.remove(clockID:id)}
        do {
            let fields:[[String:Any]]=[
                ["key":"brightness","type":"int","min":1,"max":255],
                ["key":"dexcom_password","type":"secret","max_length":63]]
            let settings:[String:Any]=["brightness":40,"dexcom_password_configured":true]
            var draft=SettingsDraft(settings:settings,fields:fields)
            draft.setText("77",key:"brightness")
            draft.setSecretAction(1,key:"dexcom_password")
            draft.setText("local-test-sentinel",key:"dexcom_password")
            let snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft)
            try store.save(snapshot,clockID:id)
            guard try KeychainLocalSettingsStore().load(clockID:id)==snapshot else {return "Keychain check failed: readback"}
            let query:[String:Any]=[
                kSecClass as String:kSecClassGenericPassword,
                kSecAttrService as String:"com.sugarclock.companion.local-settings.v1",
                kSecAttrAccount as String:id,kSecAttrSynchronizable as String:false,
                kSecUseDataProtectionKeychain as String:true,kSecReturnAttributes as String:true]
            var attributes:CFTypeRef?
            guard SecItemCopyMatching(query as CFDictionary,&attributes)==errSecSuccess,
                  let values=attributes as? [String:Any],
                  values[kSecAttrAccessible as String] as? String==kSecAttrAccessibleWhenUnlockedThisDeviceOnly as String,
                  values[kSecAttrSynchronizable as String] as? Bool != true else {return "Keychain check failed: protection"}
            draft.setSecretAction(0,key:"dexcom_password")
            try store.save(LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft),clockID:id)
            guard try store.load(clockID:id)?.draft.text["dexcom_password"]==nil else {return "Keychain check failed: secret cleanup"}
            try store.remove(clockID:id)
            guard try store.load(clockID:id)==nil else {return "Keychain check failed: removal"}
            return "Keychain check passed: save, reopen, device-only protection, secret cleanup and removal."
        } catch LocalSettingsStoreError.unavailable(let status) {
            return "Keychain check failed (\(status)): \(SecCopyErrorMessageString(status,nil) as String? ?? "Unavailable")"
        } catch {return "Keychain check failed: \(error.localizedDescription)"}
    }
}
#endif
