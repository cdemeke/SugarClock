import XCTest
@testable import SugarClockCore

final class SettingsDraftConfirmationTests:XCTestCase {
    func testRevertingAfterDelayedConfirmationIsStillAnEdit() throws {
        let fields:[[String:Any]]=[["key":"brightness","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":77],fields:fields)
        draft.setText("99",key:"brightness")
        let submitted=draft
        draft.setText("120",key:"brightness")
        draft.confirm(submitted:submitted,settings:["brightness":99],fields:fields)
        XCTAssertEqual(draft.text["brightness"],"120")
        draft.setText("77",key:"brightness")
        XCTAssertEqual(try draft.patch(fields:fields)["brightness"] as? Int,77)
        draft.setText("99",key:"brightness")
        XCTAssertTrue(draft.changed.isEmpty)
    }

    func testSavedFieldsBecomeCleanWhileNewEditsRemain() throws {
        let fields:[[String:Any]]=[
            ["key":"brightness","type":"int"],["key":"ambient_creature","type":"string"],
            ["key":"ambient_enabled","type":"bool"]]
        var draft=SettingsDraft(settings:["brightness":77,"ambient_creature":"cat","ambient_enabled":false],fields:fields)
        draft.setText("99",key:"brightness");draft.setBool(true,key:"ambient_enabled")
        let submitted=draft
        draft.setText("dog",key:"ambient_creature");draft.setBool(false,key:"ambient_enabled")
        draft.confirm(submitted:submitted,settings:["brightness":99,"ambient_creature":"cat","ambient_enabled":true],fields:fields)
        XCTAssertEqual(draft.changed,["ambient_creature","ambient_enabled"])
        let patch=try draft.patch(fields:fields)
        XCTAssertNil(patch["brightness"])
        XCTAssertEqual(patch["ambient_creature"] as? String,"dog")
        XCTAssertEqual(patch["ambient_enabled"] as? Bool,false)
    }

    func testUnitChangeAfterSubmissionDoesNotResendThresholds() throws {
        let fields:[[String:Any]]=[["key":"use_mmol","type":"bool"],["key":"thresh_high","type":"int"]]
        var draft=SettingsDraft(settings:["use_mmol":false,"thresh_high":180],fields:fields)
        draft.setText("199",key:"thresh_high")
        let submitted=draft
        draft.setBool(true,key:"use_mmol")
        draft.confirm(submitted:submitted,settings:["use_mmol":false,"thresh_high":199],fields:fields)
        XCTAssertEqual(draft.changed,["use_mmol"])
        XCTAssertEqual(draft.text["thresh_high"],"11.06")
        XCTAssertNil(try draft.patch(fields:fields)["thresh_high"])
        draft.setText("12.00",key:"thresh_high")
        XCTAssertEqual(try draft.patch(fields:fields)["thresh_high"] as? Int,216)
    }

    func testThresholdEditUsesRefreshedUnitsWithoutChangingItsValue() throws {
        let fields:[[String:Any]]=[["key":"use_mmol","type":"bool"],["key":"thresh_high","type":"int"]]
        var draft=SettingsDraft(settings:["use_mmol":false,"thresh_high":180],fields:fields)
        let submitted=draft
        draft.setText("216",key:"thresh_high")
        draft.confirm(submitted:submitted,settings:["use_mmol":true,"thresh_high":180],fields:fields)
        XCTAssertEqual(draft.text["thresh_high"],"12.00")
        XCTAssertEqual(draft.changed,["thresh_high"])
        XCTAssertEqual(try draft.patch(fields:fields)["thresh_high"] as? Int,216)
    }

    func testConfirmedSecretIsClearedWhileAnotherEditRemains() throws {
        let fields:[[String:Any]]=[["key":"password","type":"secret"],["key":"brightness","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":77],fields:fields)
        draft.setSecretAction(1,key:"password");draft.setText("first-test-secret",key:"password")
        let submitted=draft
        draft.setText("99",key:"brightness")
        draft.confirm(submitted:submitted,settings:["brightness":77,"password_configured":true],fields:fields)
        XCTAssertNil(draft.text["password"])
        XCTAssertEqual(draft.changed,["brightness"])
        XCTAssertNil(try draft.patch(fields:fields)["password"])
    }

    func testNewSecretReplacementSurvivesConfirmation() throws {
        let fields:[[String:Any]]=[["key":"password","type":"secret"]]
        var draft=SettingsDraft(settings:[:],fields:fields)
        draft.setSecretAction(1,key:"password");draft.setText("first-test-secret",key:"password")
        let submitted=draft
        draft.setText("newer-test-secret",key:"password")
        draft.confirm(submitted:submitted,settings:["password_configured":true],fields:fields)
        XCTAssertEqual(try draft.patch(fields:fields)["password"] as? String,"newer-test-secret")
    }
}
