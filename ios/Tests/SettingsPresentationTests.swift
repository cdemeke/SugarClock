import XCTest
@testable import SugarClockCore

final class SettingsPresentationTests:XCTestCase {
    func testUpdateTimesKeepWholeHourWireValuesAndDistinguishNoonFromMidnight() throws {
        XCTAssertEqual(ClockUpdateTime.choices.count,24)
        XCTAssertEqual(ClockUpdateTime.choices[0],"12:00 AM")
        XCTAssertEqual(ClockUpdateTime.choices[12],"12:00 PM")
        XCTAssertEqual(ClockUpdateTime.choices[18],"6:00 PM")
        XCTAssertEqual(ClockUpdateTime.choices[23],"11:00 PM")
        let fields:[[String:Any]]=[["key":"auto_update_hour","type":"int","min":0,"max":23]]
        for hour in ClockUpdateTime.choices.keys {
            var draft=SettingsDraft(settings:["auto_update_hour":(hour+1)%24],fields:fields)
            draft.setText(String(hour),key:"auto_update_hour")
            XCTAssertEqual(try draft.patch(fields:fields)["auto_update_hour"] as? Int,hour)
        }
    }
    func testTimeZonePreservesUnrecognizedRulesUntilExplicitSelection() throws {
        let fields:[[String:Any]]=[["key":"timezone","type":"text","max_length":63]]
        let custom="CUSTOM-9:30"
        var draft=SettingsDraft(settings:["timezone":custom],fields:fields)
        XCTAssertNil(ClockTimeZone.matching(custom))
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
        let eastern=try XCTUnwrap(ClockTimeZone.choices.first)
        draft.setText(eastern.posix,key:"timezone")
        XCTAssertEqual(try draft.patch(fields:fields)["timezone"] as? String,eastern.posix)
        XCTAssertTrue(ClockTimeZone.choices.allSatisfy {$0.posix.utf8.count<=63})
    }
    func testExplicitDefaultTransitionTimesMatchWithoutChangingNondefaultTimes() {
        XCTAssertEqual(ClockTimeZone.matching("EST+5EDT,M3.2.0/02:00,M11.1.0/2")?.name,"Eastern · New York")
        XCTAssertNil(ClockTimeZone.matching("EST5EDT,M3.2.0/23,M11.1.0/2"))
        XCTAssertEqual(ClockTimeZone.matching("GMT0")?.observesDaylightSaving,false)
        XCTAssertEqual(ClockTimeZone.matching("GMT0BST,M3.5.0/1,M10.5.0")?.observesDaylightSaving,true)
    }
    func testNetworkListKeepsStrongestAccessPointAndExactSSID() {
        let networks=NearbyNetwork.sorted([
            ["ssid":"Home","rssi":-80,"auth":3],
            ["ssid":"Home","rssi":-45,"auth":3],
            ["ssid":"Office ","rssi":-65,"auth":5,"enterprise":true],
            ["ssid":"Guest","rssi":-60,"auth":0],
            ["ssid":"","rssi":-30],[:]
        ])
        XCTAssertEqual(networks.map(\.ssid),["Home","Guest","Office "])
        XCTAssertEqual(networks[0].signal,"Strong signal")
        XCTAssertTrue(networks[1].open)
        XCTAssertTrue(networks[2].enterprise)
    }
    func testDisablingBloodSugarAlsoDisablesAlertsWithoutClearingCredentials() throws {
        let fields:[[String:Any]]=[["key":"glucose_enabled","type":"bool"],["key":"alert_enabled","type":"bool"],["key":"dexcom_password","type":"secret"],["key":"data_source","type":"int"]]
        var draft=SettingsDraft(settings:["glucose_enabled":true,"alert_enabled":true,"data_source":1,"dexcom_password_configured":true],fields:fields)
        draft.setBool(false,key:"glucose_enabled")
        let patch=try draft.patch(fields:fields)
        XCTAssertEqual(Set(patch.keys),["glucose_enabled","alert_enabled"])
        XCTAssertEqual(patch["glucose_enabled"] as? Bool,false)
        XCTAssertEqual(patch["alert_enabled"] as? Bool,false)
        draft.setBool(true,key:"glucose_enabled")
        XCTAssertEqual(draft.booleans["alert_enabled"],true)
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
    }
    func testConfirmedOffDoesNotRestoreAlertsWhenReadingsAreEnabledAgain() throws {
        let fields:[[String:Any]]=[["key":"glucose_enabled","type":"bool"],["key":"alert_enabled","type":"bool"]]
        var draft=SettingsDraft(settings:["glucose_enabled":true,"alert_enabled":true],fields:fields)
        draft.setBool(false,key:"glucose_enabled")
        let submitted=draft
        draft.setBool(true,key:"glucose_enabled") // Edited while Off was awaiting confirmation.
        draft.confirm(submitted:submitted,settings:["glucose_enabled":false,"alert_enabled":false],fields:fields)
        XCTAssertEqual(draft.booleans["alert_enabled"],false)
        XCTAssertEqual(Set(try draft.patch(fields:fields).keys),["glucose_enabled"])
        XCTAssertEqual(try draft.patch(fields:fields)["glucose_enabled"] as? Bool,true)
    }
    func testUndoingBloodSugarOffKeepsExplicitAlertEdit() throws {
        let fields:[[String:Any]]=[["key":"glucose_enabled","type":"bool"],["key":"alert_enabled","type":"bool"]]
        var draft=SettingsDraft(settings:["glucose_enabled":true,"alert_enabled":true],fields:fields)
        draft.setBool(false,key:"alert_enabled")
        draft.setBool(false,key:"glucose_enabled");draft.setBool(true,key:"glucose_enabled")
        XCTAssertEqual(Set(try draft.patch(fields:fields).keys),["alert_enabled"])
        XCTAssertEqual(try draft.patch(fields:fields)["alert_enabled"] as? Bool,false)
    }
    func testDisablingSectionPreservesSavedOptions() throws {
        let fields:[[String:Any]]=[["key":"alert_enabled","type":"bool"],["key":"alert_low","type":"int","min":20,"max":600]]
        var draft=SettingsDraft(settings:["alert_enabled":true,"alert_low":70],fields:fields)
        draft.setBool(false,key:"alert_enabled")
        let patch=try draft.patch(fields:fields)
        XCTAssertEqual(Set(patch.keys),["alert_enabled"])
        XCTAssertEqual(patch["alert_enabled"] as? Bool,false)
        draft.setBool(true,key:"alert_enabled")
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
        XCTAssertEqual(draft.text["alert_low"],"70")
    }
    func testServiceAvailabilityDistinguishesUnsupportedUnknownAndDisabled() throws {
        let weather=try XCTUnwrap(SettingsCategory.all.first {$0.id=="weather"})
        XCTAssertFalse(weather.supported(by:[["key":"data_source","type":"int"]]))
        XCTAssertTrue(weather.supported(by:[["key":"weather_enabled","type":"bool"]]))
        XCTAssertNil(weather.enabled(in:[:]))
        XCTAssertEqual(weather.enabled(in:["weather_enabled":false]),false)
        XCTAssertEqual(weather.enabled(in:["weather_enabled":true]),true)
    }
    func testLegacyGlucoseAndDisabledGlucoseAlertPresentation() throws {
        let glucose=try XCTUnwrap(SettingsCategory.all.first {$0.id=="glucose"})
        let alerts=try XCTUnwrap(SettingsCategory.all.first {$0.id=="alerts"})
        XCTAssertTrue(glucose.supported(by:[["key":"data_source","type":"int"]]))
        XCTAssertEqual(glucose.enabled(in:["data_source":1]),true)
        XCTAssertNil(glucose.enabled(in:[:]))
        XCTAssertEqual(glucose.enabled(in:["glucose_enabled":false,"data_source":1]),false)
        XCTAssertEqual(alerts.enabled(in:["glucose_enabled":false,"alert_enabled":true]),false)
        XCTAssertEqual(alerts.enabled(in:["alert_enabled":true]),true)
    }
    func testEnablingServiceKeepsSavedOptionsAndDoesNotChangeConfirmedGrouping() throws {
        let category=try XCTUnwrap(SettingsCategory.all.first {$0.id=="pomodoro"})
        let fields:[[String:Any]]=[["key":"timer_enabled","type":"bool"],["key":"timer_work_min","type":"int","min":1,"max":120]]
        let saved:[String:Any]=["timer_enabled":false,"timer_work_min":35]
        var draft=SettingsDraft(settings:saved,fields:fields)
        draft.setBool(true,key:try XCTUnwrap(category.enableKey))
        XCTAssertEqual(category.enabled(in:saved),false)
        let patch=try draft.patch(fields:fields)
        XCTAssertEqual(Set(patch.keys),["timer_enabled"])
        XCTAssertEqual(patch["timer_enabled"] as? Bool,true)
        XCTAssertEqual(draft.text["timer_work_min"],"35")
        let confirmed=saved.merging(patch) {_,new in new}
        XCTAssertEqual(category.enabled(in:confirmed),true)
    }

}
