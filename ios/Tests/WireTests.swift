import XCTest
@testable import SugarClockCore

@MainActor final class MockTransport: ClockTransport {
    var packetLimit=20
    var writes:[Data]=[]
    var replies:[Data]=[]
    var closed=false
    func write(_ packet:Data) async throws {writes.append(packet)}
    func read() async throws -> Data {guard !replies.isEmpty else {throw ClockError.disconnected};return replies.removeFirst()}
    func close() {closed=true}
    func response(_ object:[String:Any],id:UInt16=1) throws {
        let data=try JSONSerialization.data(withJSONObject:object,options:.sortedKeys)
        replies=try Frame.split(data,id:id,packetLimit:20).map {Frame(flags:1,id:id,offset:$0.offset,total:$0.total,payload:$0.payload).data}
    }
}
final class WireTests:XCTestCase {
    func testPairingSheetDoesNotEndForegroundSession() {
        for phase:ForegroundSessionPhase in [.active,.inactive,.active,.inactive,.active] {
            XCTAssertFalse(phase.mustDisconnect)
        }
        XCTAssertTrue(ForegroundSessionPhase.background.mustDisconnect)
    }
    func testDefaultMTUAndLargeMessage() throws {
        let message=Data(repeating:0x5a,count:4096)
        let frames=try Frame.split(message,id:65534,packetLimit:20)
        XCTAssertEqual(frames.count,342)
        var assembly=Reassembly()
        for f in frames {
            let read=try Frame(data:Frame(flags:1,id:f.id,offset:f.offset,total:f.total,payload:f.payload).data)
            _=try assembly.append(read,expectedID:65534)
        }
        XCTAssertEqual(assembly.bytes,message)
        XCTAssertThrowsError(try Frame.split(Data(repeating:0,count:4097),id:1,packetLimit:20))
        XCTAssertThrowsError(try Frame.split(message,id:1,packetLimit:8))
    }
    func testOutOfOrderAndChangedDuplicatesRejected() throws {
        var a=Reassembly()
        XCTAssertThrowsError(try a.append(Frame(flags:1,id:1,offset:1,total:3,payload:Data([2])),expectedID:1))
        a=Reassembly()
        XCTAssertFalse(try a.append(Frame(flags:1,id:1,offset:0,total:3,payload:Data([1,2])),expectedID:1))
        XCTAssertFalse(try a.append(Frame(flags:1,id:1,offset:0,total:3,payload:Data([1,2])),expectedID:1))
        XCTAssertThrowsError(try a.append(Frame(flags:1,id:1,offset:0,total:3,payload:Data([9,2])),expectedID:1))
        XCTAssertThrowsError(try a.append(Frame(flags:1,id:2,offset:2,total:3,payload:Data([3])),expectedID:1))
    }
    func testVersionBoundsAndFixtures() throws {
        XCTAssertThrowsError(try Frame(data:Data([2,0,1,0,0,0,1,0,65])))
        XCTAssertThrowsError(try Frame(data:Data([1,1,1,0,0,0,1,16])))
        let root=URL(fileURLWithPath:#filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let fixtures=try JSONSerialization.jsonObject(with:Data(contentsOf:root.appendingPathComponent("protocol/fixtures/frames.json"))) as! [[String:Any]]
        for fixture in fixtures {
            let hex=fixture["hex"] as! String
            let bytes=Data(stride(from:0,to:hex.count,by:2).map {i in UInt8(String(hex.dropFirst(i).prefix(2)),radix:16)!})
            if fixture["valid"] as! Bool {XCTAssertNoThrow(try Frame(data:bytes))} else {XCTAssertThrowsError(try Frame(data:bytes))}
        }
    }
    @MainActor func testSavedAckAndReadbackWithFragmentation() async throws {
        let transport=MockTransport()
        try transport.response(["v":1,"id":1,"state":"applied","saved":true])
        let client=ClockClient(transport:transport)
        try await client.save(["brightness":77])
        XCTAssertGreaterThan(transport.writes.count,1)
        try transport.response(["v":1,"id":2,"state":"applied","settings":["brightness":77]],id:2)
        let response=try await client.request("settings.get")
        XCTAssertEqual((response["settings"] as? [String:Int])?["brightness"],77)
    }
    @MainActor func testQueuedDoesNotMeanSaved() async throws {
        let transport=MockTransport();try transport.response(["v":1,"id":1,"state":"queued"])
        do {try await ClockClient(transport:transport).save(["brightness":88]);XCTFail("Must reject queued save")} catch {}
    }
    @MainActor func testFailureAndDisconnectNeverBecomeSuccess() async throws {
        let transport=MockTransport();try transport.response(["v":1,"id":1,"state":"failed","error":"persistence_failed"])
        let client=ClockClient(transport:transport)
        do {try await client.save(["brightness":88]);XCTFail()} catch {XCTAssertEqual(error as? ClockError,.rejected("persistence_failed"))}
        do {_=try await client.request("ota.install");XCTFail()} catch {XCTAssertEqual(error as? ClockError,.disconnected)}
        // No automatic replay of a non-idempotent command after a lost reply.
        let messages=try transport.writes.map {try Frame(data:$0)}.filter{$0.flags==0 && $0.offset==0}
        XCTAssertEqual(messages.count,2)
    }
    func testConnectionLifecycleRejectsLateAndWrongDeviceCallbacks() {
        let first=UUID(),second=UUID();var session=ConnectionLifecycle()
        session.begin(first);XCTAssertFalse(session.didDiscover(first));XCTAssertFalse(session.didConnect(second))
        XCTAssertTrue(session.didConnect(first));XCTAssertTrue(session.didDiscover(first));XCTAssertTrue(session.acceptsDisconnect(first))
        let oldGeneration=session.generation;session.reset();session.begin(second)
        XCTAssertGreaterThan(session.generation,oldGeneration);XCTAssertFalse(session.acceptsDisconnect(first));XCTAssertFalse(session.didDiscover(first))
        // A previous connection's delayed disconnect cannot cancel a new connect.
        XCTAssertFalse(session.acceptsDisconnect(second));XCTAssertTrue(session.didConnect(second))
    }
    @MainActor func testTimeoutClosesAndDoesNotReplay() async throws {
        let transport=MockTransport();let client=ClockClient(transport:transport,timeout:0)
        do {_=try await client.request("ota.install");XCTFail()} catch {XCTAssertEqual(error as? ClockError,.timeout)}
        XCTAssertTrue(transport.closed)
        let starts=try transport.writes.map {try Frame(data:$0)}.filter{$0.flags==0 && $0.offset==0}
        XCTAssertEqual(starts.count,1)
    }
    @MainActor func testMailboxPollingDoesNotResendCommand() async throws {
        let transport=MockTransport();try transport.response(["v":1,"id":1,"state":"queued"])
        transport.replies.insert(Frame(flags:1,id:0,offset:0,total:0).data,at:0)
        _=try await ClockClient(transport:transport,pollDelay:0).request("wifi.scan")
        let starts=try transport.writes.map {try Frame(data:$0)}.filter{$0.flags==0 && $0.offset==0}
        XCTAssertEqual(starts.count,1)
    }
    @MainActor func testInterruptedSchemaNeverReturnsPartialFieldsAndCanReload() async throws {
        let interrupted=MockTransport()
        try interrupted.response(["v":1,"id":1,"state":"applied","fields":[["key":"brightness"]],"more":true])
        do {_=try await ClockClient(transport:interrupted).schema();XCTFail("Partial schema must not appear complete")}
        catch {XCTAssertEqual(error as? ClockError,.disconnected)}
        let recovered=MockTransport()
        try recovered.response(["v":1,"id":1,"state":"applied","fields":[["key":"brightness"]],"more":true])
        let pageOne=recovered.replies
        try recovered.response(["v":1,"id":2,"state":"applied","fields":[["key":"use_mmol"]],"more":false],id:2)
        recovered.replies=pageOne+recovered.replies
        let fields=try await ClockClient(transport:recovered).schema()
        XCTAssertEqual(fields.compactMap{$0["key"] as? String},["brightness","use_mmol"])
    }
    @MainActor func testMalformedSchemaCompletionIsRejected() async throws {
        let transport=MockTransport()
        try transport.response(["v":1,"id":1,"state":"applied","fields":[["key":"brightness"]]])
        do {_=try await ClockClient(transport:transport).schema();XCTFail()}
        catch {XCTAssertEqual(error as? ClockError,.malformed)}
    }
    func testSecretsHaveThreeDistinctActions() {
        var patch:[String:Any]=["brightness":55]
        SecretChange.unchanged.apply(to:&patch,key:"dexcom_password");XCTAssertNil(patch["dexcom_password"])
        SecretChange.replace("test-only").apply(to:&patch,key:"dexcom_password");XCTAssertEqual(patch["dexcom_password"] as? String,"test-only")
        SecretChange.clear.apply(to:&patch,key:"dexcom_password");XCTAssertTrue(patch["dexcom_password"] is NSNull)
    }
}
