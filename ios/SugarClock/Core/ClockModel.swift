import Foundation
import CoreBluetooth
import Combine

struct SavedClock: Codable, Identifiable {
    var id: String
    var peripheral: UUID
    var nickname: String
}
enum SavePhase:Equatable {
    case saving, checking, saved(Date), unconfirmed, failed(String)
}
struct SaveReceipt:Identifiable {
    let id:UUID
    let clockID:String
    let keys:Set<String>
    var phase:SavePhase
}
private struct PendingSave {
    let receiptID:UUID
    let clockID:String
    let expected:[String:Any]
    let acknowledged:Bool
    let containsSecrets:Bool
}

@MainActor final class ClockModel: ObservableObject {
    let bluetooth:BluetoothTransport
    @Published var clocks:[SavedClock]=[]
    @Published var selected:SavedClock?
    @Published var settings:[String:Any]=[:]
    @Published var status:[String:Any]=[:]
    @Published var hello:[String:Any]=[:]
    @Published var fields:[[String:Any]]=[]
    @Published var networks:[[String:Any]]=[]
    @Published private(set) var scanningWiFi=false
    @Published private(set) var wifiScanMessage=""
    @Published var message=""
    @Published private(set) var saveReceipts:[String:SaveReceipt]=[:]
    @Published private(set) var lastSettingsRefresh:Date?
    private var pendingSave:PendingSave?
    var hasLoadedSettings:Bool {!settings.isEmpty && !fields.isEmpty}
    var quietReconnect:Bool {reconnecting && hasLoadedSettings}
    var connectionSummary:String {
        if sessionReady {return "Connected"}
        if quietReconnect {return "Reconnecting quietly"}
        return connectionState
    }
    #if DEBUG
    func previewConnection(ready:Bool) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil else {return}
        sessionReady=ready
        connectionState=ready ? "Connected":"Couldn't connect"
    }
    func previewSave(_ phase:SavePhase) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil,let selected else {return}
        saveReceipts[selected.id]=SaveReceipt(id:UUID(),clockID:selected.id,keys:["brightness"],phase:phase)
    }
    #endif
    func saveReceipt(for keys:Set<String>)->SaveReceipt? {
        guard let id=selected?.id,let receipt=saveReceipts[id],!receipt.keys.isDisjoint(with:keys) else {return nil}
        return receipt
    }

    @Published var busy=false
    @Published var checkingConnection=false
    @Published var reconnecting=false
    @Published var connectionState = "Not connected"
    @Published private(set) var sessionReady=false
    @Published var operationTitle="Saving…"
    private var unconfirmedChange=false
    private var foreground=true
    private var automaticReconnect=true
    private var schemaFirmware=""
    private var client:ClockClient?
    private let transport:ClockConnectionTransport
    private let preferences:UserDefaults
    private let retryDelay:UInt64
    private var updateMonitor:Task<Void,Never>?
    private var reconnectTask:Task<Void,Never>?
    private var healthTask:Task<Void,Never>?
    private var connectionSubscription:AnyCancellable?
    private var radioSubscription:AnyCancellable?
    @Published var updateMessage=""
    @Published private(set) var updatingClock=false
    init(enableBluetooth:Bool=true,loadSaved:Bool=true,transport:ClockConnectionTransport?=nil,
         preferences:UserDefaults = .standard,retryDelay:UInt64=2_000_000_000) {
        bluetooth=BluetoothTransport(enableRadio:enableBluetooth)
        self.transport=transport ?? bluetooth
        self.preferences=preferences;self.retryDelay=retryDelay
        if loadSaved,let data=preferences.data(forKey:"clocks.v1"),let saved=try? JSONDecoder().decode([SavedClock].self,from:data) {
            clocks=saved
            // Warm up the only saved clock without choosing for a multi-clock household.
            selected=saved.count==1 ? saved.first:nil
        }
        connectionSubscription=self.transport.connectionPublisher.dropFirst().removeDuplicates().sink { [weak self] connected in
            Task { @MainActor [weak self] in
                guard let self,!connected,!self.transport.connected else {return}
                self.sessionReady=false;self.client=nil
                if self.foreground,self.automaticReconnect,self.reconnectTask==nil,!self.busy,self.updateMonitor==nil {
                    self.startReconnect()
                }
            }
        }
        radioSubscription=self.transport.powerPublisher.dropFirst().removeDuplicates().sink { [weak self] powered in
            Task { @MainActor [weak self] in
                guard powered,let self,self.foreground,self.automaticReconnect else {return}
                self.startReconnect()
            }
        }
    }
    func remember() {
        if let data=try? JSONEncoder().encode(clocks) {preferences.set(data,forKey:"clocks.v1")}
        preferences.removeObject(forKey:"clock.selected") // Retire the old auto-open preference.
    }
    func resume() {
        foreground=true;automaticReconnect=true
        startReconnect()
        healthTask?.cancel()
        healthTask=Task { [weak self] in
            while !Task.isCancelled {
                do {try await Task.sleep(nanoseconds:20_000_000_000)} catch {return}
                guard let self,self.foreground else {return}
                await self.checkConnection()
            }
        }
    }
    func suspend() {
        foreground=false;automaticReconnect=false
        healthTask?.cancel();healthTask=nil
        stopReconnecting()
        // Keep the selected device and in-memory drafts when leaving the app.
        connectionState="Not connected"
    }
    private func startReconnect() {
        guard foreground,automaticReconnect,updateMonitor==nil,!busy,reconnectTask==nil,!sessionReady,
              let selected,transport.isPoweredOn else {return}
        launchConnection(selected.peripheral)
    }
    private func launchConnection(_ id:UUID) {
        reconnecting=true;busy=true;message="";sessionReady=false
        connectionState="Connecting…"
        reconnectTask=Task {
            defer {
                self.busy=false;self.reconnecting=false;self.reconnectTask=nil
                if Task.isCancelled {self.startReconnect()}
            }
            for attempt in 0..<3 {
                do {
                    try Task.checkCancellation()
                    guard self.foreground else {throw CancellationError()}
                    if attempt>0 {
                        self.connectionState="Waiting for clock…"
                        try await Task.sleep(nanoseconds:self.retryDelay)
                    }
                    try await self.establish(id)
                    self.connectionState="Connected";self.sessionReady=true
                    self.message=self.unconfirmedChange ? "Reconnected. Review the saved settings before retrying your change." : ""
                    return
                } catch {
                    self.transport.close();self.client=nil;self.sessionReady=false
                    if Task.isCancelled || !self.foreground {return}
                    if !self.transport.isPoweredOn {
                        self.finishPendingAsUnconfirmed()
                        self.connectionState=self.transport.availabilityMessage
                        return
                    }
                    // Only connection/read operations are retried, never a save or command.
                    if !Self.canRetryConnection(error) || attempt==2 {
                        self.automaticReconnect=false
                        self.finishPendingAsUnconfirmed()
                        self.connectionState="Couldn't connect"
                        self.message=Self.canRetryConnection(error)
                            ? "Move closer and try again. Your clock is still saved."
                            : error.localizedDescription
                        return
                    }
                }
            }
        }
    }
    static func canRetryConnection(_ error:Error)->Bool {
        if let error=error as? ClockError {
            switch error {
            case .disconnected,.timeout: return true
            default:return false
            }
        }
        return (error as NSError).domain == CBErrorDomain
    }
    func connect(_ id:UUID) async {
        guard updateMonitor==nil else {return}
        if selected?.peripheral==id,let task=reconnectTask {await task.value;return}
        guard !busy else {return}
        automaticReconnect=true
        if selected?.peripheral != id {
            finishPendingAsUnconfirmed();pendingSave=nil;lastSettingsRefresh=nil
            sessionReady=false;unconfirmedChange=false
            selected=clocks.first(where:{$0.peripheral==id})
            settings=[:];status=[:];hello=[:];fields=[];schemaFirmware="";networks=[];wifiScanMessage=""
        }
        if sessionReady,transport.connected,selected?.peripheral==id {return}
        launchConnection(id)
        await reconnectTask?.value
    }
    private func establish(_ id:UUID) async throws {
        connectionState="Connecting…"
        transport.operationTimeout=45
        try await transport.connect(id:id)
        try Task.checkCancellation()
        connectionState="Verifying clock…"
        let next=ClockClient(transport:transport)
        let greeting=try await next.request("hello")
        try Task.checkCancellation()
        guard let identity=greeting["device_id"] as? String,
              let caps=greeting["capabilities"] as? [String],caps.contains("settings.patch") else {
            throw ClockError.unavailable("This clock needs companion-compatible firmware.")
        }
        if let known=clocks.first(where:{$0.peripheral==id}),known.id != identity {
            throw ClockError.unavailable("This clock's identity changed. Remove it from My Clocks and add it again.")
        }
        if selected?.id != identity {settings=[:];status=[:];fields=[];schemaFirmware=""}
        let saved=clocks.first(where:{$0.id==identity}) ?? SavedClock(id:identity,peripheral:id,nickname:greeting["name"] as? String ?? "SugarClock")
        selected=SavedClock(id:identity,peripheral:id,nickname:saved.nickname)
        if let index=clocks.firstIndex(where:{$0.id==identity}) {clocks[index]=selected!}
        else {clocks.append(selected!)}
        remember()
        client=next;hello=greeting
        // Pairing may need 45 seconds; subsequent reads fail promptly on a stale link.
        next.requestTimeout=15;transport.operationTimeout=15
        connectionState="Loading settings…"
        try await refresh()
        try await loadSchema(greeting,client:next)
        try Task.checkCancellation()
    }
    func checkConnection() async {
        guard foreground,!busy,reconnectTask==nil,updateMonitor==nil,sessionReady,let client else {return}
        busy=true;checkingConnection=true
        defer {busy=false;checkingConnection=false;if !sessionReady {startReconnect()}}
        do {
            let response=try await client.request("status.get")
            try Task.checkCancellation()
            status=response["status"] as? [String:Any] ?? [:]
        } catch {
            sessionReady=false;self.client=nil;transport.close()
        }
    }
    private func loadSchema(_ hello:[String:Any],client:ClockClient) async throws {
        let version=hello["firmware"] as? String ?? ""
        guard (hello["capabilities"] as? [String] ?? []).contains("schema") else {fields=[];schemaFirmware="";return}
        if !fields.isEmpty,schemaFirmware==version {return}
        let loaded=try await client.schema()
        try Task.checkCancellation()
        fields=loaded;schemaFirmware=version
    }
    private func refreshSettings() async throws {
        guard let client else {throw ClockError.disconnected}
        let response=try await client.request("settings.get")
        guard let loaded=response["settings"] as? [String:Any] else {throw ClockError.malformed}
        try Task.checkCancellation()
        settings=loaded;lastSettingsRefresh=Date()
        verifyPendingSave(loaded,durable:response["saved"] as? Bool == true)
    }
    func refresh() async throws {
        try await refreshSettings()
        guard let client else {throw ClockError.disconnected}
        let loadedStatus=try await client.request("status.get")["status"] as? [String:Any] ?? [:]
        try Task.checkCancellation()
        status=loadedStatus
    }
    private func verifyPendingSave(_ loaded:[String:Any],durable:Bool) {
        guard let pending=pendingSave,pending.clockID==selected?.id,
              saveReceipts[pending.clockID]?.id==pending.receiptID else {return}
        // Matching live values alone do not prove that the clock committed them.
        // Keep read-only verification possible if its storage recovery is pending.
        guard durable else {
            saveReceipts[pending.clockID]?.phase = .unconfirmed
            return
        }
        let matches=pending.expected.allSatisfy {key,value in
            guard let actual=loaded[key] else {return false}
            return (value as? NSObject)?.isEqual(actual) == true
        }
        // Secret values are never read back. Their configured flags only establish
        // success when the clock also acknowledged the original mutation.
        if matches, pending.acknowledged || !pending.containsSecrets {
            saveReceipts[pending.clockID]?.phase = .saved(Date())
            pendingSave=nil
        } else {
            saveReceipts[pending.clockID]?.phase = pending.acknowledged
                ? .failed("The clock's saved values differ. Review your changes.") : .unconfirmed
            pendingSave=nil
        }
    }
    private func finishPendingAsUnconfirmed() {
        guard let pending=pendingSave else {return}
        if saveReceipts[pending.clockID]?.id==pending.receiptID {
            saveReceipts[pending.clockID]?.phase = .unconfirmed
        }
    }
    var canSend:Bool {sessionReady && transport.connected && !busy && updateMonitor==nil}
    func stopReconnecting() {
        automaticReconnect=false;reconnectTask?.cancel();updateMonitor?.cancel()
        finishPendingAsUnconfirmed()
        transport.close();client=nil;sessionReady=false
        connectionState="Not connected";message=""
    }
    func retrySelected() async {
        guard updateMonitor==nil else {return}
        if let task=reconnectTask {task.cancel();transport.close();await task.value}
        if let selected {await connect(selected.peripheral)}
    }
    func perform(_ action: @escaping () async throws -> Void) async {
        guard canSend else {return};busy=true;message="";unconfirmedChange=false
        defer {busy=false;if !sessionReady {startReconnect()}}
        do {try await action()} catch {
            if !foreground || Task.isCancelled {return}
            message=error.localizedDescription
            if Self.canRetryConnection(error) {
                unconfirmedChange=true
                message="Connection interrupted. Check saved settings before trying your change again."
                sessionReady=false;client=nil;transport.close()
            }
        }
    }
    @discardableResult func save(_ patch:[String:Any]) async -> Bool {
        guard let clock=selected,!patch.isEmpty,canSend,let client else {return false}
        let receipt=SaveReceipt(id:UUID(),clockID:clock.id,keys:Set(patch.keys),phase:.saving)
        saveReceipts[clock.id]=receipt
        pendingSave=nil;busy=true;operationTitle="Saving…";message=""
        defer {busy=false;if !sessionReady {startReconnect()}}
        let secretKeys=Set(fields.filter {$0["type"] as? String=="secret"}.compactMap {$0["key"] as? String})
        var expected:[String:Any]=[:]
        for (key,value) in patch {
            if secretKeys.contains(key) {expected[key+"_configured"] = !(value is NSNull) && (value as? String != "")}
            else {expected[key]=value}
        }
        var acknowledged=false
        do {
            try await client.save(patch)
            acknowledged=true
            pendingSave=PendingSave(receiptID:receipt.id,clockID:clock.id,expected:expected,acknowledged:true,containsSecrets:!secretKeys.isDisjoint(with:patch.keys))
            saveReceipts[clock.id]?.phase = .checking
            // A status/glucose refresh is unrelated to whether settings were saved.
            try await refreshSettings()
            if case .saved = saveReceipts[clock.id]?.phase {return true}
            return false
        } catch {
            if Self.canRetryConnection(error) || !foreground || error is CancellationError {
                pendingSave=PendingSave(receiptID:receipt.id,clockID:clock.id,expected:expected,acknowledged:acknowledged,containsSecrets:!secretKeys.isDisjoint(with:patch.keys))
                saveReceipts[clock.id]?.phase = foreground ? .checking : .unconfirmed
                sessionReady=false;self.client=nil;transport.close()
            } else {
                saveReceipts[clock.id]?.phase = .failed(error.localizedDescription)
            }
            return false
        }
    }
    func command(_ op:String,fields:[String:Any]=[:]) async {
        operationTitle="Updating clock…"
        await perform {
            guard let client=self.client else {throw ClockError.disconnected}
            let result=try await client.request(op,fields:fields)
            self.message=result["state"] as? String=="queued" ? "Request queued. The clock is completing the operation." : "Request applied."
            if op.hasPrefix("ota."),let clock=self.selected {
                let ota=self.status["ota"] as? [String:Any] ?? [:]
                let expected=op=="ota.install" ? ota["available_version"] as? String:nil
                self.monitorUpdate(clock:clock,expected:expected)
            } else {try await self.refresh()}
        }
    }
    private func monitorUpdate(clock:SavedClock,expected:String?) {
        updateMonitor?.cancel()
        if let expected {preferences.set(expected,forKey:"update.expected."+clock.id)}
        updateMessage="Update request accepted. Waiting for the clock; Bluetooth may temporarily disconnect."
        updatingClock=true
        updateMonitor=Task {
            defer {self.updateMonitor=nil;self.updatingClock=false;self.startReconnect()}
            let deadline=Date().addingTimeInterval(180)
            while Date()<deadline {
                try? await Task.sleep(nanoseconds:2_000_000_000)
                if Task.isCancelled {return}
                if self.busy {continue}
                self.busy=true
                do {
                    if !self.transport.connected {try await self.establish(clock.peripheral)}
                    try await self.refresh()
                    try Task.checkCancellation()
                    let ota=self.status["ota"] as? [String:Any] ?? [:]
                    let state=ota["state"] as? String ?? ""
                    self.updateMessage="Clock update: \(state) · \(ota["progress"] as? Int ?? 0)%"
                    if let expected,ota["current_version"] as? String==expected,ota["pending_verification"] as? Bool==false {
                        self.updateMessage="Reconnected. Firmware \(expected) passed startup validation."
                        preferences.removeObject(forKey:"update.expected."+clock.id)
                        self.sessionReady=true;self.connectionState="Connected"
                        self.busy=false;return
                    }
                    if ["error","deferred","update_available","idle"].contains(state) {
                        self.updateMessage="Reconnected · \(state). \(ota["error"] as? String ?? "") \(ota["deferral"] as? String ?? "")"
                        self.sessionReady=true;self.connectionState="Connected"
                        self.busy=false;return
                    }
                } catch {self.sessionReady=false;self.transport.close();self.client=nil;self.updateMessage="Waiting to reconnect. The clock may be updating or rebooting."}
                self.busy=false
            }
            self.updateMessage="Automatic reconnection timed out. Reconnect from My Clocks and check the current version and update status."
        }
    }
    @discardableResult func scanWiFi(pollDelay:UInt64=1_000_000_000) async -> Bool {
        guard canSend,let client else {return false}
        busy=true;scanningWiFi=true;wifiScanMessage="";operationTitle="Finding networks…"
        defer {busy=false;scanningWiFi=false;if !sessionReady {startReconnect()}}
        do {
            _=try await client.request("wifi.scan")
            for _ in 0..<15 {
                try await Task.sleep(nanoseconds:pollDelay)
                let response=try await client.request("wifi.results")
                guard let scanning=response["scanning"] as? Bool,
                      let networks=response["networks"] as? [[String:Any]] else {throw ClockError.malformed}
                if !scanning {
                    self.networks=networks
                    wifiScanMessage=NearbyNetwork.sorted(networks).isEmpty ? "No networks found. Try again or enter a hidden network.":""
                    return true
                }
            }
            wifiScanMessage="The search took too long. Try again."
        } catch {
            wifiScanMessage="Couldn’t finish the search. Reconnect and try again."
            if Self.canRetryConnection(error) {
                sessionReady=false;self.client=nil;transport.close()
            }
        }
        return false
    }
    func disconnect() {
        stopReconnecting();pendingSave=nil;lastSettingsRefresh=nil;selected=nil;settings=[:];status=[:];fields=[];hello=[:];schemaFirmware="";networks=[];wifiScanMessage=""
        remember()
    }
    func remove(_ clock:SavedClock) {
        if selected?.id==clock.id {disconnect()}
        clocks.removeAll(where:{$0.id==clock.id});saveReceipts.removeValue(forKey:clock.id);remember()
    }
}
