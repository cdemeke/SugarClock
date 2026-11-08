import XCTest
@testable import SugarClockCore

final class LocalSettingsStoreTests:XCTestCase {
    private let fields:[[String:Any]]=[
        ["key":"brightness","type":"int","min":1,"max":255],
        ["key":"use_mmol","type":"bool"],
        ["key":"thresh_low","type":"int","min":20,"max":600],
        ["key":"dexcom_password","type":"secret","max_length":100]]
    private var settings:[String:Any] {["brightness":40,"use_mmol":true,"thresh_low":77,"dexcom_password_configured":true]}

    func testRelaunchRestoresExactTextUnitsBaselineAndPendingSecret() throws {
        let store=MemoryLocalSettingsStore()
        var draft=SettingsDraft(settings:settings,fields:fields)
        draft.setText("4,5",key:"thresh_low")
        draft.setSecretAction(1,key:"dexcom_password");draft.setText("pending-replacement",key:"dexcom_password")
        let snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft,lastSynced:Date(timeIntervalSince1970:1234),submittedDraft:draft)
        try store.save(snapshot,clockID:"DC20F8319D48")
        let loaded=try XCTUnwrap(store.load(clockID:"DC20F8319D48"))
        XCTAssertEqual(loaded.draft,draft)
        XCTAssertEqual(loaded.submittedDraft,draft)
        XCTAssertEqual(loaded.lastSynced,snapshot.lastSynced)
        XCTAssertEqual(try loaded.draft.patch(fields:loaded.fields())["thresh_low"] as? Int,81)
        var reverted=loaded.draft
        reverted.setText("4.28",key:"thresh_low")
        XCTAssertFalse(reverted.changed.contains("thresh_low"))
        reverted.setBool(false,key:"use_mmol")
        XCTAssertEqual(reverted.text["thresh_low"],"77")
    }

    func testCachesOnlyRedactedSettingsNotReadingsOrRawCredentials() throws {
        var dirty=settings
        dirty["dexcom_password"]="must-not-cache";dirty["auth_token"]="must-not-cache"
        dirty["glucose"]=123;dirty["reading"]=["value":123];dirty["wifi_ssid"]="Home"
        let snapshot=try LocalSettingsSnapshot(settings:dirty,fields:fields)
        let stored=try snapshot.settings()
        XCTAssertNil(stored["dexcom_password"]);XCTAssertNil(stored["auth_token"])
        XCTAssertNil(stored["glucose"]);XCTAssertNil(stored["reading"])
        XCTAssertEqual(stored["dexcom_password_configured"] as? Bool,true)
        XCTAssertEqual(stored["wifi_ssid"] as? String,"Home")
        XCTAssertFalse(String(decoding:try LocalSettingsCodec.encode(snapshot),as:UTF8.self).contains("must-not-cache"))
    }

    func testSeparateClockRecordsAndExplicitRemoval() throws {
        let store=MemoryLocalSettingsStore()
        let first=try LocalSettingsSnapshot(settings:settings,fields:fields)
        var second=first;second.draft.setText("99",key:"brightness")
        try store.save(first,clockID:"clock-1");try store.save(second,clockID:"clock-2")
        try store.remove(clockID:"clock-1")
        XCTAssertNil(try store.load(clockID:"clock-1"))
        XCTAssertEqual(try store.load(clockID:"clock-2")?.draft.text["brightness"],"99")
    }

    func testDiscardAndConfirmedSecretRemoveReplacementBytes() throws {
        let store=MemoryLocalSettingsStore()
        var draft=SettingsDraft(settings:settings,fields:fields)
        draft.setSecretAction(1,key:"dexcom_password");draft.setText("unique-pending-secret",key:"dexcom_password")
        draft.setSecretAction(0,key:"dexcom_password")
        XCTAssertNil(draft.text["dexcom_password"])
        try store.save(LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft),clockID:"clock")
        XCTAssertFalse(String(decoding:try XCTUnwrap(store.records["clock"]),as:UTF8.self).contains("unique-pending-secret"))
        draft.setSecretAction(1,key:"dexcom_password");draft.setText("unique-pending-secret",key:"dexcom_password")
        let submitted=draft
        draft.confirm(submitted:submitted,settings:settings,fields:fields)
        try store.save(LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft),clockID:"clock")
        XCTAssertFalse(String(decoding:try XCTUnwrap(store.records["clock"]),as:UTF8.self).contains("unique-pending-secret"))
    }

    func testSecretClearAndUnusedTextDoNotRetainReplacement() throws {
        var draft=SettingsDraft(settings:settings,fields:fields)
        draft.setSecretAction(1,key:"dexcom_password");draft.setText("discard-me",key:"dexcom_password")
        draft.setSecretAction(2,key:"dexcom_password")
        XCTAssertNil(draft.text["dexcom_password"])
        XCTAssertTrue(try draft.patch(fields:fields)["dexcom_password"] is NSNull)
        draft.setSecretAction(0,key:"dexcom_password");draft.setText("unused",key:"dexcom_password")
        let snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields,draft:draft)
        XCTAssertNil(snapshot.draft.text["dexcom_password"])
    }

    func testCorruptUnsupportedAndOversizedRecordsFailClosed() throws {
        let store=MemoryLocalSettingsStore()
        store.records["clock"]=Data("broken".utf8)
        XCTAssertThrowsError(try store.load(clockID:"clock"))
        let snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields)
        store.records["clock"]=try JSONEncoder().encode(LocalSettingsCodec.Envelope(version:99,snapshot:snapshot))
        XCTAssertThrowsError(try store.load(clockID:"clock")) {error in
            guard case LocalSettingsStoreError.unsupportedVersion=error else {return XCTFail("Wrong error")}
        }
        store.records["clock"]=Data(repeating:0,count:LocalSettingsCodec.maximumBytes+1)
        XCTAssertThrowsError(try store.load(clockID:"clock"))
        var oversized=snapshot;oversized.draft.setText(String(repeating:"x",count:8193),key:"brightness")
        XCTAssertThrowsError(try store.save(oversized,clockID:"clock"))
        XCTAssertThrowsError(try store.save(snapshot,clockID:""))
    }

    func testMutatedRawSnapshotIsSanitizedAgainBeforeStorage() throws {
        let store=MemoryLocalSettingsStore()
        var snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields)
        snapshot.settingsJSON=try JSONSerialization.data(withJSONObject:["dexcom_password":"raw-secret","brightness":33,"glucose":100])
        try store.save(snapshot,clockID:"clock")
        let stored=try XCTUnwrap(store.load(clockID:"clock")).settings()
        XCTAssertNil(stored["dexcom_password"]);XCTAssertNil(stored["glucose"])
        XCTAssertEqual(stored["brightness"] as? Int,33)
    }
}
