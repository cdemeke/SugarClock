import XCTest
@testable import SugarClockCore

@MainActor final class BluetoothTransportTests:XCTestCase {
    private func settle(_ condition:()->Bool) async {
        for _ in 0..<200 {
            if condition() {return}
            try? await Task.sleep(nanoseconds:1_000_000)
        }
        XCTFail("Transport did not settle")
    }
    func testCancelledReadDrainsRealTransportAndCannotCloseReplacement() async throws {
        let transport=BluetoothTransport(enableRadio:false)
        transport.beginConnection(UUID())
        let old=transport.sessionGeneration
        var started=false
        let reading=Task {try await transport.awaitRead {started=true}}
        await settle {started}
        reading.cancel()
        do {_=try await reading.value;XCTFail("Cancelled read succeeded")} catch {}
        transport.beginConnection(UUID())
        let replacement=transport.sessionGeneration
        started=false
        var completed=false
        let next=Task {let value=try await transport.awaitRead {started=true};completed=true;return value}
        await settle {started}
        // Exercise the actual transport cancellation handler/completion boundary,
        // including a delayed callback after a different session has begun.
        transport.cancelOperation(generation:old)
        transport.completeRead(data:Data([1]),error:nil,generation:old)
        await Task.yield();await Task.yield()
        XCTAssertFalse(completed)
        XCTAssertEqual(transport.sessionGeneration,replacement)
        transport.completeRead(data:Data([2]),error:nil,generation:replacement)
        let data=try await next.value
        XCTAssertEqual(data,Data([2]))
        transport.close()
    }
    func testCancelledWriteReleasesContinuationAndWriteBackpressure() async throws {
        let transport=BluetoothTransport(enableRadio:false)
        transport.beginConnection(UUID())
        let old=transport.sessionGeneration
        var started=false
        let writing=Task {try await transport.awaitWrite {started=true}}
        await settle {started}
        do {_=try await transport.awaitRead {};XCTFail("Overlapping read accepted")}
        catch {XCTAssertEqual(error as? ClockError,.busy)}
        writing.cancel()
        do {try await writing.value;XCTFail("Cancelled write succeeded")} catch {}
        transport.beginConnection(UUID())
        let generation=transport.sessionGeneration
        started=false
        var completed=false
        let next=Task {try await transport.awaitWrite {started=true};completed=true}
        await settle {started}
        transport.completeWrite(error:ClockError.disconnected,generation:old)
        transport.cancelOperation(generation:old)
        await Task.yield();await Task.yield()
        XCTAssertFalse(completed)
        transport.completeWrite(error:nil,generation:generation)
        try await next.value
        XCTAssertTrue(completed)
        transport.close()
    }
    func testMissingSavedPeripheralScansUntilItActuallyAppears() async throws {
        var scanning=false;var discovered:UUID?
        let wanted=UUID()
        let search=Task {try await PeripheralDiscovery.resolve(lookup:{discovered},start:{scanning=true},stop:{scanning=false},available:{true},poll:1_000_000)}
        await settle {scanning}
        XCTAssertTrue(scanning)
        discovered=wanted
        let result=try await search.value
        XCTAssertEqual(result,wanted)
        XCTAssertFalse(scanning)
        // A known device bypasses discovery altogether.
        _=try await PeripheralDiscovery.resolve(lookup:{wanted},start:{XCTFail("Unnecessary scan")},stop:{},available:{true})
    }
    func testMissingPeripheralScanStopsOnCancellationAndPowerLoss() async {
        for powerLoss in [false,true] {
            var scanning=false;var powered=true
            let search=Task {try await PeripheralDiscovery.resolve(lookup:{Optional<UUID>.none},start:{scanning=true},stop:{scanning=false},available:{powered},poll:1_000_000)}
            await settle {scanning}
            if powerLoss {powered=false} else {search.cancel()}
            do {_=try await search.value;XCTFail("Unavailable clock found")} catch {}
            XCTAssertFalse(scanning)
        }
    }
}
