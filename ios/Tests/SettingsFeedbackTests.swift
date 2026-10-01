import XCTest
@testable import SugarClockCore

final class SettingsFeedbackTests:XCTestCase {
    let now=Date(timeIntervalSince1970:1000)
    func feedback(_ phase:SavePhase?=nil,age:TimeInterval=0,storage:String="",update:String="",message:String="",updating:Bool=false,loaded:Bool=true,ready:Bool=true,syncing:Bool=false)->SettingsFeedback? {
        SettingsFeedback.resolve(phase:phase,now:now.addingTimeInterval(age),storage:storage,update:update,message:message,updating:updating,loaded:loaded,ready:ready,syncing:syncing,includesConnection:true)
    }
    func testHealthyConnectionAndRoutineReconnectAreQuiet() {
        XCTAssertNil(feedback())
        XCTAssertNil(feedback(ready:false,syncing:true))
    }
    func testConfirmedSaveExpiresWithoutDuplicateMessage() {
        XCTAssertEqual(feedback(.saved(now),update:"Updated on clock.")?.title,"Updated on clock")
        XCTAssertNil(feedback(.saved(now),age:5,update:"Updated on clock."))
        XCTAssertNil(feedback(.saved(now),age:60,update:"Updated on clock."))
    }
    func testUncertainSavePersistsEvenWhenConnected() {
        XCTAssertEqual(feedback(.unconfirmed,age:600,update:"Update not confirmed")?.title,"Update not confirmed")
        XCTAssertEqual(feedback(.failed("Invalid threshold"),age:600)?.detail,"Invalid threshold")
    }
    func testStorageAndConnectionFailuresAreNotHiddenBySuccess() {
        XCTAssertEqual(feedback(.saved(now),storage:"Storage locked")?.detail,"Storage locked")
        XCTAssertEqual(feedback(.saved(now),message:"Bluetooth denied")?.detail,"Bluetooth denied")
        XCTAssertEqual(feedback(.saved(now),update:"Clock timed out")?.detail,"Clock timed out")
    }
    func testExplicitTransferUsesDockWithoutDuplicateProgress() {
        XCTAssertNil(feedback(.checking,updating:true,ready:false,syncing:true))
        XCTAssertNotNil(feedback(.checking,storage:"Storage locked",updating:true))
    }
    func testInitialReadAndExhaustedReconnectStayVisible() {
        XCTAssertEqual(feedback(loaded:false,ready:false,syncing:true)?.title,"Syncing settings…")
        XCTAssertEqual(feedback(ready:false)?.title,"Clock unavailable")
        XCTAssertEqual(feedback(message:"Pairing rejected",loaded:false,ready:false)?.detail,"Pairing rejected")
    }
}
