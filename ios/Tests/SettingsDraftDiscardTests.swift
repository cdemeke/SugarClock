import XCTest
@testable import SugarClockCore

final class SettingsDraftDiscardTests:XCTestCase {
    func testDiscardOneConflictDoesNotBlessAnotherAndReeditUsesReviewedBaseline() throws {
        let fields:[[String:Any]]=[["key":"brightness","type":"int"],["key":"auto_cycle_sec","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":40,"auto_cycle_sec":10],fields:fields)
        draft.setText("99",key:"brightness");draft.setText("30",key:"auto_cycle_sec")
        let current:[String:Any]=["brightness":50,"auto_cycle_sec":20]
        draft.discardChange("brightness",settings:current,fields:fields)
        XCTAssertEqual(draft.text["brightness"],"50")
        XCTAssertEqual(draft.changed,["auto_cycle_sec"])
        XCTAssertThrowsError(try draft.validatedPatch(settings:current,fields:fields)) {error in
            guard case DraftError.conflict("auto_cycle_sec")=error else {return XCTFail("Unrelated baseline was replaced")}
        }
        draft.setText("60",key:"brightness")
        draft.discardChange("auto_cycle_sec",settings:current,fields:fields)
        XCTAssertEqual(try draft.validatedPatch(settings:current,fields:fields)["brightness"] as? Int,60)
        draft.setText("50",key:"brightness")
        XCTAssertTrue(draft.changed.isEmpty)
    }

    func testDiscardUnitsPreservesOtherThresholdIntentAndOriginalConflictBaseline() throws {
        let fields:[[String:Any]]=[["key":"use_mmol","type":"bool"],["key":"thresh_low","type":"int"],["key":"thresh_high","type":"int"]]
        var draft=SettingsDraft(settings:["use_mmol":false,"thresh_low":77,"thresh_high":199],fields:fields)
        draft.setBool(true,key:"use_mmol");draft.setText("4.50",key:"thresh_low")
        draft.discardChange("use_mmol",settings:["use_mmol":false,"thresh_low":80,"thresh_high":200],fields:fields)
        XCTAssertEqual(draft.text["thresh_low"],"81")
        XCTAssertEqual(draft.text["thresh_high"],"199")
        XCTAssertEqual(draft.changed,["thresh_low"])
        XCTAssertEqual(try draft.patch(fields:fields)["thresh_low"] as? Int,81)
        XCTAssertThrowsError(try draft.validatedPatch(settings:["use_mmol":false,"thresh_low":80,"thresh_high":200],fields:fields))
        draft.setBool(true,key:"use_mmol");draft.setBool(false,key:"use_mmol")
        XCTAssertEqual(draft.text["thresh_low"],"81")
        XCTAssertEqual(draft.text["thresh_high"],"199")
        XCTAssertEqual(draft.changed,["thresh_low"])
    }

    func testDiscardThresholdUsesCurrentDraftUnitsAndExactClockInteger() throws {
        let fields:[[String:Any]]=[["key":"use_mmol","type":"bool"],["key":"thresh_low","type":"int"]]
        var draft=SettingsDraft(settings:["use_mmol":false,"thresh_low":77],fields:fields)
        draft.setBool(true,key:"use_mmol");draft.setText("4.50",key:"thresh_low")
        draft.discardChange("thresh_low",settings:["use_mmol":false,"thresh_low":79],fields:fields)
        XCTAssertEqual(draft.text["thresh_low"],"4.39")
        XCTAssertEqual(draft.changed,["use_mmol"])
        draft.setBool(false,key:"use_mmol")
        XCTAssertEqual(draft.text["thresh_low"],"79")
        XCTAssertTrue(draft.changed.isEmpty)
    }

    func testDiscardRemovedSecretForgetsReplacementAndRetainsOtherEdits() throws {
        let fields:[[String:Any]]=[["key":"password","type":"secret"],["key":"brightness","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":40,"password_configured":true],fields:fields)
        draft.setSecretAction(1,key:"password");draft.setText("never-retain-this",key:"password")
        draft.setText("99",key:"brightness")
        let currentFields=Array(fields.dropFirst())
        draft.discardChange("password",settings:["brightness":40],fields:currentFields)
        XCTAssertNil(draft.text["password"]);XCTAssertNil(draft.secrets["password"])
        XCTAssertEqual(draft.changed,["brightness"])
        XCTAssertEqual(try draft.patch(fields:currentFields)["brightness"] as? Int,99)
        XCTAssertFalse(String(decoding:try JSONEncoder().encode(draft),as:UTF8.self).contains("never-retain-this"))
        try draft.validateForStorage()
    }

    func testDiscardChangedSchemaAllowsExplicitNewEditOfReviewedType() throws {
        var draft=SettingsDraft(settings:["future":40],fields:[["key":"future","type":"int"]])
        draft.setText("99",key:"future")
        let fields:[[String:Any]]=[["key":"future","type":"bool"]]
        draft.discardChange("future",settings:["future":false],fields:fields)
        XCTAssertNil(draft.text["future"]);XCTAssertEqual(draft.booleans["future"],false)
        XCTAssertTrue(draft.changed.isEmpty)
        draft.setBool(true,key:"future")
        XCTAssertEqual(try draft.validatedPatch(settings:["future":false],fields:fields)["future"] as? Bool,true)
    }
}
