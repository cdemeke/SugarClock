import XCTest
import Combine
@testable import SugarClockCore

@MainActor private final class SessionTransport:ClockConnectionTransport {
    @Published var connected=false
    var connectionPublisher:AnyPublisher<Bool,Never> {$connected.eraseToAnyPublisher()}
    @Published var isPoweredOn=true
    var powerPublisher:AnyPublisher<Bool,Never> {$isPoweredOn.eraseToAnyPublisher()}
    var availabilityMessage="Bluetooth is off"
    var operationTimeout:TimeInterval=45
    var packetLimit=180
    var attempts=0
    var failures=0
    var failNextRead=false
    var failSave=false
    var failStatus=false
    var failSettingsAfterSave=false
    var loseSaveAck=false
    var ignorePatch=false
    var durable:Bool?=true
    var storedSettings:[String:Any]=["brightness":77,"dexcom_password_configured":true]

    var scanStartFails=false
    var scanResultsFail=false
    var scanNeverFinishes=false
    var scanResultsReads=0
    var holdConnection=false
    var waiting:CheckedContinuation<Void,Error>?
    var identity="clock-a"
    var operations:[String]=[]
    private var incoming=Data()
    private var response=Data()
    private var responseID:UInt16=0
    private var offset=0
    func connect(id:UUID) async throws {
        attempts+=1
        if holdConnection {try await withCheckedThrowingContinuation {waiting=$0}}
        if failures>0 {failures-=1;throw ClockError.timeout}
        connected=true
    }
    func close() {
        connected=false
        let pending=waiting;waiting=nil;pending?.resume(throwing:ClockError.disconnected)
    }
    func write(_ packet:Data) async throws {
        guard connected else {throw ClockError.disconnected}
        let frame=try Frame(data:packet)
        if frame.flags==2 {offset=frame.offset;return}
        if frame.offset==0 {incoming=Data()}
        incoming.append(frame.payload)
        guard incoming.count==frame.total else {return}
        let request=try JSONSerialization.jsonObject(with:incoming) as! [String:Any]
        let op=request["op"] as! String;operations.append(op)
        if op=="settings.patch",failSave {throw ClockError.timeout}
        var reply:[String:Any]=["v":1,"id":Int(frame.id),"state":"applied"]
        switch op {
        case "hello":reply["device_id"]=identity;reply["name"]="Bedside Clock";reply["firmware"]="1";reply["capabilities"]=["settings.patch","schema"]
        case "settings.get":
            if failSettingsAfterSave,operations.contains("settings.patch") {failSettingsAfterSave=false;throw ClockError.timeout}
            reply["settings"]=storedSettings
            if let durable {reply["saved"]=durable}
        case "status.get":if failStatus {throw ClockError.timeout};reply["status"]=["data_received":true,"ota":["state":"idle"]]
        case "schema.get":reply["fields"]=[["key":"brightness","type":"int"],["key":"dexcom_password","type":"secret"]];reply["more"]=false
        case "wifi.scan":if scanStartFails {reply["error"]="scan_failed"}
        case "wifi.results":
            if scanResultsFail {reply["error"]="scan_failed"}
            scanResultsReads+=1
            reply["scanning"]=scanNeverFinishes || scanResultsReads<2
            reply["networks"]=[["ssid":"Home","rssi":-40,"auth":3]]
        case "settings.patch":
            if !ignorePatch,let patch=request["patch"] as? [String:Any] {
                for (key,value) in patch {
                    if key=="dexcom_password" {storedSettings[key+"_configured"] = !(value is NSNull) && value as? String != ""}
                    else {storedSettings[key]=value}
                }
            }
            if loseSaveAck {throw ClockError.timeout}
            reply["saved"]=true
        default:break
        }
        response=try JSONSerialization.data(withJSONObject:reply);responseID=frame.id;offset=0
    }
    func read() async throws -> Data {
        if failNextRead {failNextRead=false;throw ClockError.timeout}
        guard connected else {throw ClockError.disconnected}
        let end=min(offset+172,response.count)
        return Frame(flags:1,id:responseID,offset:offset,total:response.count,payload:response.subdata(in:offset..<end)).data
    }
}

@MainActor final class ClockSessionTests:XCTestCase {
    private func fixture()->(ClockModel,SessionTransport,UserDefaults,UUID) {
        let radio=SessionTransport(),id=UUID()
        let defaults=UserDefaults(suiteName:"SugarClockTests."+UUID().uuidString)!
        let model=ClockModel(enableBluetooth:false,loadSaved:false,transport:radio,preferences:defaults,retryDelay:0)
        model.clocks=[SavedClock(id:"clock-a",peripheral:id,nickname:"Bedside Clock")]
        model.selected=model.clocks[0]
        return (model,radio,defaults,id)
    }
    func testSingleSavedClockStartsConnectingOnLaunchAndOpeningReusesAttempt() async throws {
        let (_,radio,defaults,id)=fixture()
        defaults.set(try JSONEncoder().encode([SavedClock(id:"clock-a",peripheral:id,nickname:"Bedside Clock")]),forKey:"clocks.v1")
        radio.holdConnection=true
        let model=ClockModel(enableBluetooth:false,transport:radio,preferences:defaults,retryDelay:0)
        model.resume()
        await settle {radio.waiting != nil}
        XCTAssertEqual(model.selected?.peripheral,id)
        let opening=Task {await model.connect(id)}
        await Task.yield()
        radio.holdConnection=false
        let pending=radio.waiting;radio.waiting=nil;pending?.resume()
        await opening.value
        XCTAssertEqual(radio.attempts,1)
        XCTAssertTrue(model.sessionReady)
        await model.connect(id)
        XCTAssertEqual(radio.attempts,1)
        model.suspend()
    }
    func testMultipleSavedClocksWaitForSelectionDespiteRememberedClock() async throws {
        let (_,radio,defaults,id)=fixture()
        let clocks=[SavedClock(id:"clock-a",peripheral:id,nickname:"Bedside"),SavedClock(id:"clock-b",peripheral:UUID(),nickname:"Kitchen")]
        defaults.set(try JSONEncoder().encode(clocks),forKey:"clocks.v1")
        defaults.set("clock-b",forKey:"clock.selected")
        let model=ClockModel(enableBluetooth:false,transport:radio,preferences:defaults,retryDelay:0)
        model.resume()
        await Task.yield()
        XCTAssertNil(model.selected)
        XCTAssertFalse(model.reconnecting)
        XCTAssertEqual(radio.attempts,0)
        await model.connect(id)
        XCTAssertTrue(model.sessionReady)
        model.suspend();model.resume()
        await settle {model.sessionReady}
        XCTAssertEqual(radio.attempts,2)
        model.suspend()
    }
    func testNoSavedClocksDoesNotStartConnectionOnLaunch() async {
        let (_,radio,defaults,_)=fixture()
        let model=ClockModel(enableBluetooth:false,transport:radio,preferences:defaults,retryDelay:0)
        model.resume()
        await Task.yield()
        XCTAssertNil(model.selected)
        XCTAssertEqual(radio.attempts,0)
        model.suspend()
    }
    func testReconnectingPreservesClockOrderAndRetiresOldSelectionPreference() async {
        let (model,radio,defaults,id)=fixture()
        model.clocks.append(SavedClock(id:"clock-b",peripheral:UUID(),nickname:"Kitchen"))
        defaults.set("clock-a",forKey:"clock.selected")
        await model.connect(id)
        XCTAssertEqual(model.clocks.map(\.id),["clock-a","clock-b"])
        radio.close();await settle {radio.attempts==2 && model.sessionReady}
        XCTAssertEqual(model.clocks.map(\.id),["clock-a","clock-b"])
        XCTAssertNil(defaults.string(forKey:"clock.selected"))
        let saved=try? JSONDecoder().decode([SavedClock].self,from:defaults.data(forKey:"clocks.v1")!)
        XCTAssertEqual(saved?.map(\.id),["clock-a","clock-b"])
        model.suspend()
    }
    func testUpdateMonitorOwnsReconnectAcrossRadioResumeAndExplicitSelection() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        await model.command("ota.check")
        XCTAssertTrue(model.updatingClock)
        radio.isPoweredOn=false;radio.close()
        await settle {!model.sessionReady}
        radio.isPoweredOn=true
        model.resume()
        await model.retrySelected()
        await model.connect(UUID())
        try? await Task.sleep(nanoseconds:30_000_000)
        XCTAssertEqual(radio.attempts,1)
        XCTAssertEqual(model.selected?.peripheral,id)
        XCTAssertFalse(model.reconnecting)
        try? await Task.sleep(nanoseconds:2_100_000_000)
        await settle {!model.updatingClock}
        XCTAssertEqual(radio.attempts,2)
        XCTAssertTrue(model.sessionReady)
        XCTAssertTrue(model.canSend)
        model.suspend()
    }
    func testWiFiStartAndCompletionFailuresKeepPreviousResultsWithoutReconnecting() async {
        for failAtStart in [true,false] {
            let (model,radio,_,id)=fixture()
            await model.connect(id)
            model.networks=[["ssid":"Previous"]]
            radio.scanStartFails=failAtStart;radio.scanResultsFail = !failAtStart
            let succeeded=await model.scanWiFi(pollDelay:0)
            XCTAssertFalse(succeeded)
            XCTAssertEqual(model.networks.first?["ssid"] as? String,"Previous")
            XCTAssertFalse(model.wifiScanMessage.isEmpty)
            XCTAssertEqual(radio.scanResultsReads,failAtStart ? 0:1)
            XCTAssertEqual(radio.attempts,1)
            XCTAssertTrue(model.canSend)
            model.suspend()
        }
    }
    func testWiFiScanWaitsForCompletedResultsWithoutSavingSettings() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        let result=await model.scanWiFi(pollDelay:0)
        XCTAssertTrue(result)
        XCTAssertEqual(model.networks.first?["ssid"] as? String,"Home")
        XCTAssertEqual(radio.scanResultsReads,2)
        XCTAssertFalse(model.scanningWiFi)
        XCTAssertTrue(model.wifiScanMessage.isEmpty)
        XCTAssertFalse(radio.operations.contains("settings.patch"))
        model.suspend()
    }
    func testWiFiScanStopsPollingAndRetainsPreviousResultsOnTimeout() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        model.networks=[["ssid":"Previous"]]
        radio.scanNeverFinishes=true
        let result=await model.scanWiFi(pollDelay:0)
        XCTAssertFalse(result)
        XCTAssertEqual(radio.scanResultsReads,15)
        XCTAssertEqual(model.networks.first?["ssid"] as? String,"Previous")
        XCTAssertFalse(model.scanningWiFi)
        XCTAssertFalse(model.busy)
        XCTAssertTrue(model.wifiScanMessage.contains("too long"))
        XCTAssertTrue(model.canSend)
        model.suspend()
    }
    private func settle(_ condition:()->Bool) async {
        for _ in 0..<200 {
            if condition() {return}
            try? await Task.sleep(nanoseconds:1_000_000)
        }
        XCTFail("Session did not settle")
    }
    func testBackgroundReturnPreservesClockAndReconnectsWithoutPairing() async {
        let (model,radio,defaults,id)=fixture()
        await model.connect(id)
        XCTAssertTrue(model.sessionReady)
        model.suspend()
        XCTAssertEqual(model.selected?.id,"clock-a")
        XCTAssertEqual(model.settings["brightness"] as? Int,77)
        XCTAssertFalse(model.canSend)
        model.resume()
        await settle {model.sessionReady}
        XCTAssertEqual(radio.attempts,2)
        let restored=ClockModel(enableBluetooth:false,transport:SessionTransport(),preferences:defaults)
        XCTAssertEqual(restored.selected?.peripheral,id)
        model.suspend()
    }
    func testTransientTimeoutRetriesAndLoadsSettingsBeforeReady() async {
        let (model,radio,_,id)=fixture();radio.failures=1
        await model.connect(id)
        XCTAssertEqual(radio.attempts,2)
        XCTAssertTrue(model.canSend)
        XCTAssertEqual(model.fields.count,2)
        model.suspend()
    }
    func testFifthConnectionAttemptCanRecoverWithoutReplayingSaves() async {
        let (model,radio,_,id)=fixture();radio.failures=4
        await model.connect(id)
        XCTAssertEqual(radio.attempts,5)
        XCTAssertTrue(model.canSend)
        XCTAssertEqual(model.settings["brightness"] as? Int,77)
        XCTAssertFalse(radio.operations.contains("settings.patch"))
        model.suspend()
    }
    func testTimeoutExhaustionStopsAndExplicitRetryRecovers() async {
        let (model,radio,_,id)=fixture();radio.failures=10
        await model.connect(id)
        try? await Task.sleep(nanoseconds:20_000_000)
        XCTAssertEqual(radio.attempts,5)
        XCTAssertFalse(model.busy)
        XCTAssertEqual(model.connectionState,"Couldn't connect")
        XCTAssertEqual(model.clocks.count,1)
        radio.failures=0
        await model.retrySelected()
        XCTAssertTrue(model.canSend)
        model.suspend()
    }
    func testStaleConnectedLinkIsReplacedByHealthCheck() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        radio.failNextRead=true // Simulate a stale link still reported as connected by iOS.
        await model.checkConnection()
        await settle {model.sessionReady}
        XCTAssertEqual(radio.attempts,2)
        model.suspend()
    }
    func testForegroundRadioDropAutomaticallyReconnectsSavedClock() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        radio.close()
        await settle {radio.attempts==2 && model.sessionReady}
        XCTAssertEqual(model.selected?.peripheral,id)
        XCTAssertEqual(model.clocks.count,1)
        model.suspend()
    }
    func testCancelStopsRetriesAndKeepsSavedClock() async {
        let (model,radio,_,_)=fixture();radio.holdConnection=true
        model.resume()
        await settle {radio.waiting != nil}
        XCTAssertFalse(model.canSend)
        model.stopReconnecting()
        await settle {!model.busy}
        XCTAssertEqual(radio.attempts,1)
        XCTAssertFalse(model.sessionReady)
        XCTAssertEqual(model.clocks.count,1)
        model.suspend()
    }
    func testLostSaveIsNeverReplayedDuringRecovery() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.failSave=true
        let saved=await model.save(["brightness":99])
        XCTAssertFalse(saved)
        await settle {model.sessionReady}
        XCTAssertEqual(radio.operations.filter{$0=="settings.patch"}.count,1)
        XCTAssertEqual(model.saveReceipt(for:["brightness"])?.phase,.unconfirmed)
        model.suspend()
    }
    func testSaveConfirmationDoesNotDependOnStatusAndSurvivesReconnect() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.failStatus=true
        let before=radio.operations.filter{$0=="status.get"}.count
        let saved=await model.save(["brightness":99])
        XCTAssertTrue(saved)
        XCTAssertEqual(model.settings["brightness"] as? Int,99)
        XCTAssertEqual(radio.operations.filter{$0=="status.get"}.count,before)
        let receipt=model.saveReceipt(for:["brightness"])!
        guard case .saved = receipt.phase else {return XCTFail("Save not confirmed")}
        radio.failStatus=false;radio.close()
        await settle {radio.attempts==2 && model.sessionReady}
        XCTAssertEqual(model.saveReceipt(for:["brightness"])?.phase,receipt.phase)
        model.suspend()
    }
    func testReadbackAfterDisconnectConfirmsWithoutResendingSave() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.failSettingsAfterSave=true
        let saved=await model.save(["brightness":99])
        XCTAssertFalse(saved)
        await settle {model.sessionReady}
        guard case .saved = model.saveReceipt(for:["brightness"])?.phase else {return XCTFail("Readback not confirmed")}
        XCTAssertEqual(radio.operations.filter{$0=="settings.patch"}.count,1)
        model.suspend()
    }
    func testReadbackMismatchNeverClaimsSuccess() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.ignorePatch=true
        let saved=await model.save(["brightness":99])
        XCTAssertFalse(saved)
        guard case .failed = model.saveReceipt(for:["brightness"])?.phase else {return XCTFail("Mismatch not surfaced")}
        model.suspend()
    }
    func testMatchingReadbackRequiresDurableStorageEvenWithAcknowledgment() async throws {
        for acknowledgmentLost in [false,true] {
            for durability:Bool? in [false,nil] {
                let (model,radio,_,id)=fixture()
                await model.connect(id)
                radio.durable=durability;radio.loseSaveAck=acknowledgmentLost
                let saved=await model.save(["brightness":99])
                XCTAssertFalse(saved)
                await settle {model.canSend}
                XCTAssertEqual(model.saveReceipt(for:["brightness"])?.phase,.unconfirmed)
                XCTAssertEqual(model.settings["brightness"] as? Int,99)
                // A later durable readback may confirm the original write, but
                // recovery must never issue another settings.patch.
                radio.durable=true
                try await model.refresh()
                guard case .saved = model.saveReceipt(for:["brightness"])?.phase else {
                    model.suspend();return XCTFail("Durable readback did not confirm save")
                }
                XCTAssertEqual(radio.operations.filter{$0=="settings.patch"}.count,1)
                model.suspend()
            }
        }
    }
    func testMissingSecretAcknowledgmentCannotBeConfirmedByConfiguredFlag() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.loseSaveAck=true
        let saved=await model.save(["dexcom_password":"test-replacement"])
        XCTAssertFalse(saved)
        await settle {model.sessionReady}
        XCTAssertEqual(model.saveReceipt(for:["dexcom_password"])?.phase,.unconfirmed)
        XCTAssertEqual(radio.operations.filter{$0=="settings.patch"}.count,1)
        model.suspend()
    }
    func testReceiptsDoNotAppearOnUnrelatedPageOrClock() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        _=await model.save(["brightness":99])
        XCTAssertNil(model.saveReceipt(for:["alert_enabled"]))
        let second=UUID();radio.identity="clock-b"
        model.clocks.append(SavedClock(id:"clock-b",peripheral:second,nickname:"Kitchen"))
        await model.connect(second)
        XCTAssertNil(model.saveReceipt(for:["brightness"]))
        model.suspend()
    }
    func testLoadedSettingsStayAvailableDuringQuietReconnect() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id);radio.holdConnection=true;radio.close()
        await settle {radio.waiting != nil}
        XCTAssertTrue(model.quietReconnect)
        XCTAssertEqual(model.settings["brightness"] as? Int,77)
        XCTAssertEqual(model.fields.count,2)
        XCTAssertFalse(model.canSend)
        model.suspend()
    }
    func testNewEditsSurviveDelayedSaveVerification() {
        let fields:[[String:Any]]=[["key":"brightness","type":"int"]]
        var draft=SettingsDraft(settings:["brightness":77],fields:fields)
        draft.setText("99",key:"brightness")
        let submitted=draft
        draft.setText("120",key:"brightness")
        draft.confirm(submitted:submitted,settings:["brightness":99],fields:fields)
        XCTAssertEqual(draft.text["brightness"],"120")
        XCTAssertEqual(draft.changed,["brightness"])
        var unchanged=submitted
        unchanged.confirm(submitted:submitted,settings:["brightness":99],fields:fields)
        XCTAssertTrue(unchanged.changed.isEmpty)
        XCTAssertEqual(unchanged.text["brightness"],"99")
    }
    func testWrongIdentityNeverLoadsSettingsOrRetries() async {
        let (model,radio,_,id)=fixture();radio.identity="different-clock"
        await model.connect(id)
        XCTAssertFalse(model.sessionReady)
        XCTAssertEqual(radio.attempts,1)
        XCTAssertFalse(radio.operations.contains("settings.get"))
        model.suspend()
    }
    func testCancelPendingConnectAndResumeDoesNotStrandBusyState() async {
        let (model,radio,_,_)=fixture();radio.holdConnection=true
        model.resume()
        await settle {radio.waiting != nil}
        model.suspend();radio.holdConnection=false;model.resume()
        await settle {model.sessionReady}
        XCTAssertFalse(model.busy)
        XCTAssertEqual(radio.attempts,2)
        model.suspend()
    }
    func testSwitchingClockCannotReusePreviousSettings() async {
        let (model,radio,_,id)=fixture()
        await model.connect(id)
        let second=UUID();radio.identity="clock-b"
        model.clocks.append(SavedClock(id:"clock-b",peripheral:second,nickname:"Kitchen"))
        await model.connect(second)
        XCTAssertEqual(radio.attempts,2)
        XCTAssertEqual(model.selected?.id,"clock-b")
        XCTAssertEqual(radio.operations.filter{$0=="schema.get"}.count,2)
        model.suspend()
    }
}
