import XCTest
@testable import SugarClockCore

final class SettingsDraftTests:XCTestCase {
    private let fields:[[String:Any]]=[
        ["key":"brightness","type":"int","min":1,"max":255],
        ["key":"use_mmol","type":"bool"],
        ["key":"thresh_low","type":"int","min":20,"max":600],
        ["key":"server_url","type":"secret","max_length":100]
    ]
    func testViewingAndUnitChangesPreserveOriginalThresholds() throws {
        var draft=SettingsDraft(settings:["brightness":40,"use_mmol":true,"thresh_low":77,"server_url_configured":true,"unknown_future_field":9],fields:fields)
        XCTAssertEqual(draft.text["thresh_low"],"4.28")
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
        draft.setBool(false,key:"use_mmol")
        XCTAssertEqual(draft.text["thresh_low"],"77")
        XCTAssertEqual(try draft.patch(fields:fields).count,1)
        draft.setBool(true,key:"use_mmol")
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
    }
    func testOnlyEditedFieldsArePatchedAndUndoBecomesClean() throws {
        var draft=SettingsDraft(settings:["brightness":40,"thresh_low":77],fields:fields)
        draft.setText("88",key:"brightness")
        let patch=try draft.patch(fields:fields)
        XCTAssertEqual(patch["brightness"] as? Int,88)
        XCTAssertEqual(patch.count,1)
        draft.setText("40",key:"brightness")
        XCTAssertTrue(draft.changed.isEmpty)
    }
    func testSecretsHaveExplicitUnchangedReplaceAndClear() throws {
        var draft=SettingsDraft(settings:["server_url_configured":true],fields:fields)
        XCTAssertNil(draft.text["server_url"])
        draft.setText("https://example.invalid/data",key:"server_url")
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
        draft.setSecretAction(1,key:"server_url")
        XCTAssertEqual(try draft.patch(fields:fields)["server_url"] as? String,"https://example.invalid/data")
        draft.setSecretAction(2,key:"server_url")
        XCTAssertTrue(try draft.patch(fields:fields)["server_url"] is NSNull)
        draft.setSecretAction(0,key:"server_url")
        XCTAssertTrue(try draft.patch(fields:fields).isEmpty)
    }
    func testExplicitMMOLEditConvertsAndBoundsAreEnforced() throws {
        var draft=SettingsDraft(settings:["use_mmol":true,"thresh_low":77],fields:fields)
        draft.setText("4,5",key:"thresh_low")
        XCTAssertEqual(try draft.patch(fields:fields)["thresh_low"] as? Int,81)
        draft.setText("256",key:"brightness")
        XCTAssertThrowsError(try draft.patch(fields:fields))
        draft.setText("nan",key:"brightness")
        XCTAssertThrowsError(try draft.patch(fields:fields))
    }
    func testSecretLengthUsesUTF8Bytes() throws {
        var draft=SettingsDraft(settings:[:],fields:fields)
        draft.setSecretAction(1,key:"server_url")
        draft.setText(String(repeating:"é",count:51),key:"server_url")
        XCTAssertThrowsError(try draft.patch(fields:fields))
    }
}
