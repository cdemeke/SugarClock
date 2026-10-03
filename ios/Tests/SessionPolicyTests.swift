import XCTest
import CoreBluetooth
@testable import SugarClockCore

final class SessionPolicyTests: XCTestCase {
    func testBackoffStartsFastAndCapsInsteadOfBusyLooping() {
        let delays=(1...9).map {SessionPolicy.retryDelay(afterFailures:$0)}
        XCTAssertEqual(delays,[2,4,8,16,30,30,30,30,30].map {UInt64($0)*1_000_000_000})
        XCTAssertEqual(SessionPolicy.retryDelay(afterFailures:Int.max,base:UInt64.max),30_000_000_000)
    }
    func testMailboxStartsAt20MillisecondsAndRelaxesWhenClockIsBusy() {
        let delays=(1...7).map {SessionPolicy.mailboxDelay(emptyReads:$0,maximum:200_000_000)}
        XCTAssertEqual(delays,[20,40,80,160,200,200,200].map {UInt64($0)*1_000_000})
    }
    @MainActor func testPairingAndAuthorizationErrorsAreNotTransient() {
        for code:CBError.Code in [.peerRemovedPairingInformation,.encryptionTimedOut,.tooManyLEPairedDevices,.operationNotSupported] {
            XCTAssertFalse(ClockModel.canRetryConnection(NSError(domain:CBErrorDomain,code:code.rawValue)))
        }
        XCTAssertFalse(ClockModel.canRetryConnection(NSError(domain:CBATTErrorDomain,code:CBATTError.insufficientAuthentication.rawValue)))
        XCTAssertTrue(ClockModel.canRetryConnection(NSError(domain:CBErrorDomain,code:CBError.connectionTimeout.rawValue)))
    }
    func testSchemaCacheContainsMetadataOnlyAndRejectsUnknownExtensions() throws {
        let preferences=UserDefaults(suiteName:UUID().uuidString)!
        let cache=SchemaCache(preferences:preferences)
        let hello:[String:Any]=["v":1,"device_id":"clock","boot_id":UInt32(44),"firmware":"1","hardware":"tc001","capabilities":["schema","settings.patch"]]
        let identity=try XCTUnwrap(SchemaIdentity(hello))
        let fields:[[String:Any]]=[["key":"password","type":"secret","max_length":99],["key":"brightness","type":"int","min":1,"max":255]]
        cache.store(fields,identity:identity)
        XCTAssertEqual(cache.read(identity)?.count,2)
        let data=try XCTUnwrap(preferences.data(forKey:"schema.v1.clock"))
        let entry=try XCTUnwrap(try JSONSerialization.jsonObject(with:data) as? [String:Any])
        XCTAssertEqual(Set(entry.keys),["identity","fields"])
        cache.remove("clock")
        cache.store([["key":"password","type":"secret","value":"must-not-persist"]],identity:identity)
        XCTAssertNil(preferences.data(forKey:"schema.v1.clock"))
        var changed=hello;changed["boot_id"]=UInt32(45)
        cache.store(fields,identity:identity)
        XCTAssertNil(cache.read(try XCTUnwrap(SchemaIdentity(changed))))
        changed=hello;changed.removeValue(forKey:"boot_id")
        XCTAssertNil(SchemaIdentity(changed))
        changed=hello;changed["capabilities"]=["schema"]
        XCTAssertNil(cache.read(try XCTUnwrap(SchemaIdentity(changed))))
        cache.remove("clock")
    }
}
