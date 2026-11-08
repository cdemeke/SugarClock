import XCTest
@testable import SugarClockCore

final class OfflineDraftValidationTests:XCTestCase {
    private let fields:[[String:Any]]=[
        ["key":"brightness","type":"int","min":1,"max":255],
        ["key":"use_mmol","type":"bool"],
        ["key":"thresh_low","type":"int","min":20,"max":600],
        ["key":"dexcom_password","type":"secret","max_length":100]]

    func testUnknownRemovedOrRetypedChangedFieldFailsClosed() throws {
        var draft=SettingsDraft(settings:["brightness":40],fields:fields)
        draft.setText("99",key:"brightness")
        XCTAssertThrowsError(try draft.patch(fields:fields.filter {$0["key"] as? String != "brightness"}))
        var retyped=fields;retyped[0]["type"]="secret"
        XCTAssertThrowsError(try draft.patch(fields:retyped))
        draft.setText("new value",key:"future_unknown")
        XCTAssertThrowsError(try draft.patch(fields:fields))
    }

    func testBoundsFromNewSchemaAreUsed() throws {
        var draft=SettingsDraft(settings:["brightness":40],fields:fields)
        draft.setText("200",key:"brightness")
        var newer=fields;newer[0]["max"]=100
        XCTAssertThrowsError(try draft.patch(fields:newer))
    }

    func testOnlyEditedFieldsConflictAndAlreadyDesiredIsAllowed() throws {
        var draft=SettingsDraft(settings:["brightness":40,"thresh_low":77],fields:fields)
        draft.setText("99",key:"brightness")
        XCTAssertEqual(try draft.validatedPatch(settings:["brightness":40,"thresh_low":81],fields:fields)["brightness"] as? Int,99)
        XCTAssertThrowsError(try draft.validatedPatch(settings:["brightness":60,"thresh_low":77],fields:fields))
        XCTAssertEqual(try draft.validatedPatch(settings:["brightness":99],fields:fields)["brightness"] as? Int,99)
    }

    func testRebaseKeepsUserIntentAndExactThresholdAcrossExternalUnitChange() throws {
        var draft=SettingsDraft(settings:["brightness":40,"use_mmol":false,"thresh_low":77],fields:fields)
        draft.setText("99",key:"brightness");draft.setText("81",key:"thresh_low")
        draft.setSecretAction(1,key:"dexcom_password");draft.setText("pending",key:"dexcom_password")
        let rebased=try draft.rebasedKeepingEdits(settings:["brightness":45,"use_mmol":true,"thresh_low":77],fields:fields)
        XCTAssertEqual(rebased.text["thresh_low"],"4.50")
        let patch=try rebased.patch(fields:fields)
        XCTAssertEqual(patch["thresh_low"] as? Int,81)
        XCTAssertEqual(patch["brightness"] as? Int,99)
        XCTAssertEqual(patch["dexcom_password"] as? String,"pending")
        XCTAssertNil(patch["use_mmol"])
    }

    func testCrossFieldThresholdValidationUsesFreshUneditedClockValues() throws {
        let schema:[[String:Any]]=["thresh_urgent_low","thresh_low","thresh_high","thresh_urgent_high","alert_low","alert_high"].map {["key":$0,"type":"int","min":20,"max":600]}
        let baseline:[String:Any]=["thresh_urgent_low":55,"thresh_low":70,"thresh_high":180,"thresh_urgent_high":250,"alert_low":70,"alert_high":180]
        var draft=SettingsDraft(settings:baseline,fields:schema)
        draft.setText("90",key:"thresh_low")
        var fresh=baseline;fresh["thresh_high"]=80
        XCTAssertThrowsError(try draft.validatedPatch(settings:fresh,fields:schema))
        draft.setText("70",key:"thresh_low");draft.setText("190",key:"alert_low")
        XCTAssertThrowsError(try draft.validatedPatch(settings:baseline,fields:schema))
    }

    func testDisableDisplayNormalizesDefaultModeAndContradictionFails() throws {
        let schema:[[String:Any]]=[["key":"time_display_enabled","type":"bool"],["key":"ambient_enabled","type":"bool"],["key":"default_mode","type":"int","min":0,"max":3]]
        let initial:[String:Any]=["time_display_enabled":true,"ambient_enabled":true,"default_mode":1]
        var draft=SettingsDraft(settings:initial,fields:schema)
        draft.setBool(false,key:"time_display_enabled")
        let patch=try draft.validatedPatch(settings:initial,fields:schema)
        XCTAssertEqual(patch["default_mode"] as? Int,0)
        draft.setBool(false,key:"ambient_enabled");draft.setText("3",key:"default_mode")
        XCTAssertThrowsError(try draft.validatedPatch(settings:initial,fields:schema))
    }
}
