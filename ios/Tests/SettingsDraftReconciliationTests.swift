import XCTest
@testable import SugarClockCore

final class SettingsDraftReconciliationTests:XCTestCase {
    func testMatchingValuesClearOnlyTheirOwnConflictBaselines() throws {
        let fields:[[String:Any]]=[["key":"brightness","type":"int"],["key":"ambient_enabled","type":"bool"],["key":"ambient_creature","type":"text"]]
        var draft=SettingsDraft(settings:["brightness":77,"ambient_enabled":false,"ambient_creature":"cat"],fields:fields)
        draft.setText("099",key:"brightness");draft.setBool(true,key:"ambient_enabled");draft.setText("dog",key:"ambient_creature")
        let current:[String:Any]=["brightness":99,"ambient_enabled":true,"ambient_creature":"fox"]
        XCTAssertEqual(draft.reconcileConfirmedValues(settings:current,fields:fields),["brightness","ambient_enabled"])
        XCTAssertEqual(draft.changed,["ambient_creature"])
        XCTAssertEqual(draft.text["brightness"],"99")
        XCTAssertThrowsError(try draft.validatedPatch(settings:current,fields:fields)) {error in guard case DraftError.conflict("ambient_creature")=error else {return XCTFail("Expected original creature conflict")}}
        draft.setText("77",key:"brightness")
        XCTAssertEqual(try draft.patch(fields:fields)["brightness"] as? Int,77)
    }
    func testMatchingTextAndBooleanClearWithoutRemovingInvalidOrUnsupportedEdits() {
        let fields:[[String:Any]]=[["key":"name","type":"text"],["key":"on","type":"bool"],["key":"count","type":"int"],["key":"removed","type":"bool"]]
        var draft=SettingsDraft(settings:["name":"old","on":false,"count":1,"removed":false],fields:fields)
        draft.setText("new",key:"name");draft.setBool(true,key:"on");draft.setText("not a number",key:"count");draft.setBool(true,key:"removed")
        let current:[String:Any]=["name":"new","on":true,"count":2,"removed":true]
        XCTAssertEqual(draft.reconcileConfirmedValues(settings:current,fields:Array(fields.dropLast())),["name","on"])
        XCTAssertEqual(draft.changed,["count","removed"])
    }
    func testTypeChangedOrMissingReadbackNeverClearsPendingValue() {
        let fields:[[String:Any]]=[["key":"brightness","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":77],fields:fields)
        draft.setText("99",key:"brightness")
        XCTAssertTrue(draft.reconcileConfirmedValues(settings:["brightness":99],fields:[["key":"brightness","type":"text"]]).isEmpty)
        XCTAssertTrue(draft.reconcileConfirmedValues(settings:[:],fields:fields).isEmpty)
        XCTAssertEqual(draft.changed,["brightness"])
    }
    func testConfiguredFlagsNeverReconcileSecretReplacementOrClear() {
        let fields:[[String:Any]]=[["key":"password","type":"secret"]]
        for action in [1,2] {
            var draft=SettingsDraft(settings:["password_configured":true],fields:fields)
            draft.setSecretAction(action,key:"password")
            if action==1 {draft.setText("pending replacement",key:"password")}
            XCTAssertTrue(draft.reconcileConfirmedValues(settings:["password_configured":action==1],fields:fields).isEmpty)
            XCTAssertEqual(draft.changed,["password"])
            if action==1 {XCTAssertEqual(draft.text["password"],"pending replacement")}
        }
    }
    func testThresholdReconciliationUsesExactWireValuesAndKeepsOtherBaseline() throws {
        let fields:[[String:Any]]=[["key":"use_mmol","type":"bool"],["key":"thresh_high","type":"int"],["key":"alert_high","type":"int"]]
        var draft=SettingsDraft(settings:["use_mmol":false,"thresh_high":180,"alert_high":250],fields:fields)
        draft.setBool(true,key:"use_mmol");draft.setText("11.11",key:"thresh_high");draft.setText("16.67",key:"alert_high")
        let near:[String:Any]=["use_mmol":true,"thresh_high":199,"alert_high":260]
        XCTAssertEqual(draft.reconcileConfirmedValues(settings:near,fields:fields),["use_mmol"])
        XCTAssertEqual(try draft.patch(fields:fields)["thresh_high"] as? Int,200)
        let exact:[String:Any]=["use_mmol":true,"thresh_high":200,"alert_high":260]
        XCTAssertEqual(draft.reconcileConfirmedValues(settings:exact,fields:fields),["thresh_high"])
        XCTAssertEqual(draft.changed,["alert_high"])
        XCTAssertThrowsError(try draft.validatedPatch(settings:exact,fields:fields)) {error in guard case DraftError.conflict("alert_high")=error else {return XCTFail("Expected original alert conflict")}}
    }
    func testDerivedEffectsMustMatchBeforeControllingEditClears() {
        let glucoseFields:[[String:Any]]=[["key":"glucose_enabled","type":"bool"],["key":"alert_enabled","type":"bool"]]
        var glucose=SettingsDraft(settings:["glucose_enabled":true,"alert_enabled":true],fields:glucoseFields)
        glucose.setBool(false,key:"glucose_enabled")
        XCTAssertTrue(glucose.reconcileConfirmedValues(settings:["glucose_enabled":false,"alert_enabled":true],fields:glucoseFields).isEmpty)
        XCTAssertEqual(glucose.reconcileConfirmedValues(settings:["glucose_enabled":false,"alert_enabled":false],fields:glucoseFields),["glucose_enabled"])
        let displayFields:[[String:Any]]=[["key":"time_display_enabled","type":"bool"],["key":"default_mode","type":"int"]]
        var display=SettingsDraft(settings:["time_display_enabled":true,"default_mode":1],fields:displayFields)
        display.setBool(false,key:"time_display_enabled")
        XCTAssertTrue(display.reconcileConfirmedValues(settings:["time_display_enabled":false,"default_mode":1],fields:displayFields).isEmpty)
        XCTAssertEqual(display.reconcileConfirmedValues(settings:["time_display_enabled":false,"default_mode":0],fields:displayFields),["time_display_enabled"])
    }
    func testReconciliationCannotExposeContradictoryDependentDirtyIntent() {
        let fields:[[String:Any]]=[["key":"glucose_enabled","type":"bool"],["key":"alert_enabled","type":"bool"]]
        var draft=SettingsDraft(settings:["glucose_enabled":true,"alert_enabled":false],fields:fields)
        draft.setBool(false,key:"glucose_enabled");draft.setBool(true,key:"alert_enabled")
        XCTAssertTrue(draft.reconcileConfirmedValues(settings:["glucose_enabled":false,"alert_enabled":false],fields:fields).isEmpty)
        XCTAssertEqual(draft.changed,["glucose_enabled","alert_enabled"])
    }
}
