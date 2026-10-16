import Foundation
import CoreBluetooth
import Combine

struct SavedClock: Codable, Identifiable {
    var id: String
    var peripheral: UUID
    var nickname: String
}
enum SettingsUpdatePhase:Equatable {case idle,waiting,sending}
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
    @Published private(set) var lastStatusRefresh:Date?
    @Published private var refreshingSettings=false
    private var settingsDurable=false
    private var pendingSave:PendingSave?
    @Published private(set) var settingsDraft=SettingsDraft()
    @Published private(set) var settingsUpdatePhase:SettingsUpdatePhase = .idle
    @Published private(set) var settingsUpdateMessage=""
    @Published private(set) var draftStorageMessage=""
    private var draftClockID:String?
    private var localRestoreFailed=false
    private var localPersistenceFailed=false
    var canEditSettingsDraft:Bool {selected != nil && hasLoadedSettings && !localRestoreFailed}
    private var submittedSettingsDraft:SettingsDraft?
    private let localSettingsStore:LocalSettingsStore
    private var settingsUpdateTask:Task<Void,Never>?
    private var settingsUpdateID=UUID()
    var pendingChangeCount:Int {settingsDraft.changed.count}
    var canRequestSettingsUpdate:Bool {
        foreground && canEditSettingsDraft && pendingChangeCount>0 &&
        settingsUpdatePhase == .idle && updateMonitor==nil && !updatingClock && (!busy || reconnecting || checkingConnection)
    }

    var hasLoadedSettings:Bool {!settings.isEmpty && !fields.isEmpty}
    var quietReconnect:Bool {reconnecting && hasLoadedSettings}
    var syncingSettings:Bool {!updatingClock && (reconnecting || checkingConnection || refreshingSettings)}
    var connectionSummary:String {
        if syncingSettings,hasLoadedSettings {return "Syncing with clock…"}
        if sessionReady {return "Connected"}
        return connectionState
    }
    #if DEBUG
    func previewSettingsUpdate(_ phase:SettingsUpdatePhase) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil else {return}
        settingsUpdatePhase=phase
        if lastSettingsRefresh==nil {lastSettingsRefresh=Date().addingTimeInterval(-120)}
        settingsUpdateMessage=phase == .waiting ? "Waiting for your clock. You can keep editing.":""
    }
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
    private var schemaIdentity:SchemaIdentity?
    private var schemaProgressIdentity:SchemaIdentity?
    private var schemaProgress=SchemaProgress()
    private var client:ClockClient?
    private let transport:ClockConnectionTransport
    private let preferences:UserDefaults
    private let retryDelay:UInt64
    private var updateMonitor:Task<Void,Never>?
    private var reconnectTask:Task<Void,Never>?
    private var selectionRequest=UUID()
    private var healthTask:Task<Void,Never>?
    private var connectionSubscription:AnyCancellable?
    private var radioSubscription:AnyCancellable?
    @Published var updateMessage=""
    @Published private(set) var updatingClock=false
    init(enableBluetooth:Bool=true,loadSaved:Bool=true,transport:ClockConnectionTransport?=nil,
         preferences:UserDefaults = .standard,retryDelay:UInt64=2_000_000_000,localSettingsStore:LocalSettingsStore?=nil) {
        bluetooth=BluetoothTransport(enableRadio:enableBluetooth)
        self.transport=transport ?? bluetooth
        self.preferences=preferences;self.retryDelay=retryDelay
        if let localSettingsStore {self.localSettingsStore=localSettingsStore}
        else if enableBluetooth {self.localSettingsStore=KeychainLocalSettingsStore()}
        else {self.localSettingsStore=MemoryLocalSettingsStore()}

        if loadSaved,let data=preferences.data(forKey:"clocks.v1"),let saved=try? JSONDecoder().decode([SavedClock].self,from:data) {
            clocks=saved
            // Warm up the only saved clock without choosing for a multi-clock household.
            selected=saved.count==1 ? saved.first:nil
        }
        if let selected {restoreLocalSettings(for:selected)}
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
    func setDraft(_ draft:SettingsDraft) {
        guard let selected,canEditSettingsDraft else {return}
        settingsDraft=draft;draftClockID=selected.id
        persistLocalSettings()
    }
    func discardSettingsChanges() {
        guard settingsUpdatePhase != .sending else {return}
        cancelSettingsUpdate()
        let previous=settingsDraft,previousSubmission=submittedSettingsDraft,previousRestoreFailure=localRestoreFailed
        settingsDraft=SettingsDraft(settings:settings,fields:fields)
        submittedSettingsDraft=nil;localRestoreFailed=false
        if persistLocalSettings() {settingsUpdateMessage="Local changes discarded."}
        else {
            settingsDraft=previous;submittedSettingsDraft=previousSubmission;localRestoreFailed=previousRestoreFailure
            settingsUpdateMessage="Couldn’t discard the saved local changes securely. Unlock your iPhone and try again."
        }
    }
    private func restoreLocalSettings(for clock:SavedClock) {
        settingsUpdateMessage="";draftStorageMessage=""
        settingsDurable=false
        draftClockID=clock.id;settingsDraft=SettingsDraft();submittedSettingsDraft=nil;localRestoreFailed=false;localPersistenceFailed=false
        do {
            guard let snapshot=try localSettingsStore.load(clockID:clock.id) else {return}
            settings=try snapshot.settings();fields=try snapshot.fields()
            settingsDraft=snapshot.draft;lastSettingsRefresh=snapshot.lastSynced
            submittedSettingsDraft=snapshot.submittedDraft
            if let submitted=snapshot.submittedDraft {
                saveReceipts[clock.id]=SaveReceipt(id:UUID(),clockID:clock.id,keys:submitted.changed,phase:.unconfirmed)
                settingsUpdateMessage="An earlier update was not confirmed. Review the clock’s settings before updating again."
            }
            draftStorageMessage=""
        } catch {localRestoreFailed=true;draftStorageMessage="Couldn’t unlock this clock’s saved local changes. Unlock your iPhone and reopen the app."}
    }
    private func refreshLocalDraft() {
        guard let selected,hasLoadedSettings else {return}
        if localRestoreFailed,draftClockID==selected.id {
            do {
                if let snapshot=try localSettingsStore.load(clockID:selected.id) {
                    settingsDraft=snapshot.draft;submittedSettingsDraft=snapshot.submittedDraft
                }
                localRestoreFailed=false
            } catch {return} // Never overwrite a workspace we could not decrypt/read.
        }
        if draftClockID != selected.id {
            localRestoreFailed=false;draftClockID=selected.id;settingsDraft=SettingsDraft(settings:settings,fields:fields);submittedSettingsDraft=nil
        } else if settingsDraft.changed.isEmpty {
            settingsDraft=SettingsDraft(settings:settings,fields:fields)
        } else if settingsDurable {
            settingsDraft.reconcileConfirmedValues(settings:settings,fields:fields)
        }
        persistLocalSettings()
    }
    @discardableResult private func persistLocalSettings() -> Bool {
        guard let selected,draftClockID==selected.id,!localRestoreFailed,hasLoadedSettings,let synced=lastSettingsRefresh else {return false}
        do {
            let snapshot=try LocalSettingsSnapshot(settings:settings,fields:fields,draft:settingsDraft,lastSynced:synced,submittedDraft:submittedSettingsDraft)
            try localSettingsStore.save(snapshot,clockID:selected.id)
            draftStorageMessage="";localPersistenceFailed=false;return true
        } catch {
            localPersistenceFailed=true
            draftStorageMessage="Couldn’t save local changes securely. Keep this screen open and try again after unlocking your iPhone."
            return false
        }
    }
    func cancelSettingsUpdate() {
        guard settingsUpdatePhase == .waiting else {return}
        settingsUpdateTask?.cancel()
        settingsUpdateMessage="Update cancelled. Your local changes are kept."
    }
    func updateSettings(timeout:TimeInterval=90,pollDelay:UInt64=100_000_000) async {
        guard canRequestSettingsUpdate,let clock=selected else {return}
        let submitted=settingsDraft,requestID=UUID()
        settingsUpdateID=requestID;settingsUpdatePhase = .waiting
        settingsUpdateMessage="Waiting for your clock. You can keep editing."
        automaticReconnect=true;startReconnect()
        let task=Task { [self] in
            var ownsBusy=false
            let deadline=Task { @MainActor [weak self] in
                do {try await Task.sleep(nanoseconds:UInt64(max(0,min(timeout,90))*1_000_000_000))} catch {return}
                guard let self,self.settingsUpdateID==requestID,self.settingsUpdatePhase == .waiting else {return}
                self.settingsUpdateMessage="The clock didn’t reconnect in time. Your local changes are kept; tap Update clock to try again."
                self.settingsUpdateTask?.cancel()
            }
            defer {
                deadline.cancel()
                if ownsBusy {busy=false}
                settingsUpdatePhase = .idle;settingsUpdateTask=nil
                if !sessionReady {startReconnect()}
            }
            do {
                var preparedPatch:[String:Any]?
                while preparedPatch==nil {
                    while !readyForOperation {
                        try Task.checkCancellation()
                        guard foreground,selected?.id==clock.id,selected?.peripheral==clock.peripheral,updateMonitor==nil else {throw CancellationError()}
                        if !automaticReconnect,!reconnecting {throw ClockError.unavailable("Reconnect to the clock, then tap Update clock again.")}
                        try await Task.sleep(nanoseconds:pollDelay)
                    }
                    try Task.checkCancellation()
                    guard foreground,selected?.id==clock.id,selected?.peripheral==clock.peripheral else {throw CancellationError()}
                    busy=true;ownsBusy=true
                    settingsUpdateMessage="Checking the clock’s current settings…"
                    do {
                        try await refreshSettings()
                        try Task.checkCancellation()
                        guard foreground,selected?.id==clock.id,selected?.peripheral==clock.peripheral,
                              (hello["capabilities"] as? [String] ?? []).contains("settings.patch") else {throw ClockError.unavailable("Reconnect to compatible firmware before updating.")}
                        var remaining=submitted
                        if settingsDurable {remaining.reconcileConfirmedValues(settings:settings,fields:fields)}
                        let patch:[String:Any]=remaining.changed.isEmpty ? [:]:try remaining.validatedPatch(settings:settings,fields:fields)
                        let envelope=try JSONSerialization.data(withJSONObject:["v":1,"id":65535,"op":"settings.patch","patch":patch])
                        guard envelope.count<=min(Frame.maximum,hello["max_message"] as? Int ?? Frame.maximum) else {throw ClockError.oversized}
                        preparedPatch=patch
                    } catch {
                        try Task.checkCancellation()
                        guard Self.canRetryConnection(error) else {throw error}
                        // Read-only preparation can reconnect within the same
                        // deadline. This loop ends permanently before any write.
                        busy=false;ownsBusy=false;sessionReady=false;client=nil;transport.close()
                        settingsUpdateMessage="Waiting for your clock. Your update has not been sent."
                        startReconnect()
                    }
                }
                let patch=preparedPatch!
                guard !patch.isEmpty else {settingsUpdateMessage="These settings already match your clock.";return}
                let previousSubmission=submittedSettingsDraft
                submittedSettingsDraft=submitted
                guard persistLocalSettings() else {
                    submittedSettingsDraft=previousSubmission
                    throw ClockError.unavailable("Update not sent because your local changes could not be saved securely.")
                }
                try Task.checkCancellation()
                settingsUpdatePhase = .sending;deadline.cancel()
                settingsUpdateMessage="Updating clock…"
                busy=false;ownsBusy=false
                let saved=await save(patch,fromSettingsUpdate:true)
                if saved {settingsUpdateMessage="Updated on clock."}
                else if case .failed(let detail)=saveReceipts[clock.id]?.phase {
                    submittedSettingsDraft=nil;persistLocalSettings();settingsUpdateMessage=detail
                } else {settingsUpdateMessage="Update not confirmed. Your local changes are kept; check the saved settings before trying again."}
            } catch {
                if !(error is CancellationError) {
                    settingsUpdateMessage=error.localizedDescription
                    if Self.canRetryConnection(error) {sessionReady=false;client=nil;transport.close()}
                }
            }
        }
        settingsUpdateTask=task
        await task.value
    }
    static func isUncertainPersistence(_ error:Error)->Bool {
        if case ClockError.rejected(let detail)=error {return detail=="persistence_failed" || detail.hasPrefix("persistence_failed:")}
        return false
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
        // Saved clocks recover for as long as the app is in the foreground. A new
        // clock must return control if its pairing window closes or it goes away.
        let addingNewClock = !clocks.contains(where:{$0.peripheral==id})
        reconnecting=true;busy=true;message="";sessionReady=false
        connectionState="Connecting…"
        reconnectTask=Task {
            defer {
                self.busy=false;self.reconnecting=false;self.reconnectTask=nil
                if Task.isCancelled {self.startReconnect()}
            }
            var failures=0
            while !Task.isCancelled {
                do {
                    try Task.checkCancellation()
                    guard self.foreground else {throw CancellationError()}
                    if failures>0 {
                        self.connectionState="Waiting for clock…"
                        try await Task.sleep(nanoseconds:SessionPolicy.retryDelay(afterFailures:failures,base:self.retryDelay))
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
                    if !Self.canRetryConnection(error) {
                        self.automaticReconnect=false
                        self.finishPendingAsUnconfirmed()
                        self.connectionState="Couldn't connect"
                        self.message=Self.connectionErrorMessage(error)
                        return
                    }
                    failures=min(failures+1,5)
                    if addingNewClock,failures>=SessionPolicy.newClockMaximumAttempts {
                        self.automaticReconnect=false
                        self.connectionState="Couldn’t finish adding clock"
                        self.message="Move closer, hold the clock’s middle button for 3 seconds, then release and tap it to try again. If it was already added, open it from My Clocks."
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
        let native=error as NSError
        guard native.domain == CBErrorDomain else {return false}
        return [CBError.connectionTimeout, .peripheralDisconnected, .connectionFailed, .connectionLimitReached, .unknown]
            .contains { $0.rawValue==native.code }
    }
    static func connectionErrorMessage(_ error:Error)->String {
        let native=error as NSError
        if native.domain==CBErrorDomain {
            switch native.code {
            case CBError.peerRemovedPairingInformation.rawValue:
                return "Pairing has changed. Forget SugarClock in iPhone Bluetooth Settings, then hold the clock’s middle button for 3 seconds and pair again."
            case CBError.encryptionTimedOut.rawValue:
                return "Pairing timed out. Open the clock’s pairing window and retry with its displayed code."
            case CBError.tooManyLEPairedDevices.rawValue:
                return "Bluetooth pairing storage is full. See Help for resetting clock bonds without erasing settings."
            default:break
            }
        }
        return error.localizedDescription
    }
    func connect(_ id:UUID) async {
        guard updateMonitor==nil,settingsUpdatePhase != .sending else {return}
        if selected?.peripheral != id,let pending=settingsUpdateTask {
            cancelSettingsUpdate();await pending.value
        }
        if selected?.peripheral==id,let task=reconnectTask,!task.isCancelled {await task.value;return}
        guard canChooseAnotherClock else {return}
        let request=UUID();selectionRequest=request
        if let task=reconnectTask {
            // Drain the old read-only session before giving the shared transport
            // to the next clock. Its cleanup must not close the new connection.
            automaticReconnect=false;task.cancel();transport.close()
            await task.value
            guard selectionRequest==request,foreground,updateMonitor==nil else {return}
        }
        guard !busy else {return}
        automaticReconnect=true
        if selected?.peripheral != id {
            finishPendingAsUnconfirmed();pendingSave=nil;lastSettingsRefresh=nil;lastStatusRefresh=nil
            sessionReady=false;unconfirmedChange=false;settingsDurable=false
            selected=clocks.first(where:{$0.peripheral==id})
            settings=[:];status=[:];hello=[:];fields=[];schemaIdentity=nil;networks=[];wifiScanMessage=""
            settingsDraft=SettingsDraft();draftClockID=nil;submittedSettingsDraft=nil;localRestoreFailed=false;localPersistenceFailed=false
            settingsUpdateMessage="";draftStorageMessage=""
            if let selected {restoreLocalSettings(for:selected)}
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
        let greetedIdentity=SchemaIdentity(greeting)
        let needsStatus=status.isEmpty || greetedIdentity==nil || SchemaIdentity(hello) != greetedIdentity
        if selected?.id != identity {settings=[:];status=[:];fields=[];schemaIdentity=nil;lastSettingsRefresh=nil;lastStatusRefresh=nil}
        let saved=clocks.first(where:{$0.id==identity}) ?? SavedClock(id:identity,peripheral:id,nickname:greeting["name"] as? String ?? "SugarClock")
        selected=SavedClock(id:identity,peripheral:id,nickname:saved.nickname)
        client=next;hello=greeting
        // Pairing may need 45 seconds; subsequent reads fail promptly on a stale link.
        next.requestTimeout=15;transport.operationTimeout=15
        connectionState="Loading settings…"
        try await refreshSettings()
        // Reconnection needs current configuration and bounds, not a second
        // status snapshot. Keep the last timestamped status until the health
        // check (or an explicit refresh), leaving more of each BLE window usable.
        if needsStatus {try await refreshStatus()}
        try await loadSchema(greeting,client:next)
        try Task.checkCancellation()
        // Hello proves identity, not a completed addition. Keep an incomplete
        // clock out of the durable library so retry or app relaunch cannot turn
        // its bounded setup into endless saved-clock recovery. Existing entries
        // (including nickname and old peripheral ID) survive failed reconnects.
        // A local rename can replace selected with the older library record
        // while reads await. Commit the identity/peripheral verified by this
        // attempt, retaining the latest user-chosen name without that old ID.
        let nickname=clocks.first(where:{$0.id==identity})?.nickname
            ?? (selected?.id==identity ? selected?.nickname:nil) ?? saved.nickname
        let confirmed=SavedClock(id:identity,peripheral:id,nickname:nickname)
        selected=confirmed
        if let index=clocks.firstIndex(where:{$0.id==identity}) {clocks[index]=confirmed}
        else {clocks.append(confirmed)}
        remember()
        refreshLocalDraft()
    }
    func checkConnection() async {
        guard foreground,!busy,settingsUpdateTask==nil,reconnectTask==nil,updateMonitor==nil,sessionReady,client != nil else {return}
        busy=true;checkingConnection=true
        defer {busy=false;checkingConnection=false;if !sessionReady {startReconnect()}}
        do {
            try await refreshStatus()
        } catch {
            sessionReady=false;self.client=nil;transport.close()
        }
    }
    private func loadSchema(_ hello:[String:Any],client:ClockClient) async throws {
        let identity=SchemaIdentity(hello)
        guard (hello["capabilities"] as? [String] ?? []).contains("schema") else {fields=[];schemaIdentity=nil;return}
        if let identity,!fields.isEmpty,schemaIdentity==identity {return}
        let cache=SchemaCache(preferences:preferences)
        if let identity,let cached=cache.read(identity) {
            fields=cached;schemaIdentity=identity;return
        }
        // Keep complete cached fields available for local editing while fresh
        // bounds load. The session cannot send until the new schema completes;
        // partially loaded pages are never exposed or treated as verified.
        schemaIdentity=nil
        if identity==nil || schemaProgressIdentity != identity {
            schemaProgress=SchemaProgress();schemaProgressIdentity=identity
        }
        let loaded=try await client.schema(progress:schemaProgress)
        try Task.checkCancellation()
        fields=loaded;schemaIdentity=identity
        if let identity {cache.store(loaded,identity:identity)}
    }
    private func refreshSettings() async throws {
        guard let client else {throw ClockError.disconnected}
        refreshingSettings=true;defer {refreshingSettings=false}
        let response=try await client.request("settings.get")
        guard let loaded=response["settings"] as? [String:Any] else {throw ClockError.malformed}
        try Task.checkCancellation()
        settings=loaded;lastSettingsRefresh=Date();settingsDurable=response["saved"] as? Bool == true
        verifyPendingSave(loaded,durable:settingsDurable)
        if !reconnecting {refreshLocalDraft()}
    }
    func refresh() async throws {
        try await refreshSettings()
        try await refreshStatus()
    }
    private func refreshStatus() async throws {
        guard let client else {throw ClockError.disconnected}
        let loadedStatus=try await client.request("status.get")["status"] as? [String:Any] ?? [:]
        try Task.checkCancellation()
        status=loadedStatus;lastStatusRefresh=Date()
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
            if draftClockID==pending.clockID,let submitted=submittedSettingsDraft {
                settingsDraft.confirm(submitted:submitted,settings:loaded,fields:fields)
                submittedSettingsDraft=nil
                persistLocalSettings()
            }
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
    private var readyForOperation:Bool {sessionReady && transport.connected && !busy && updateMonitor==nil}
    var canSend:Bool {readyForOperation && settingsUpdatePhase == .idle}
    var canChooseAnotherClock:Bool {updateMonitor==nil && !updatingClock && settingsUpdatePhase != .sending && !localPersistenceFailed && (!busy || reconnecting || settingsUpdatePhase == .waiting)}
    /// Add Clock owns discovery; an unrelated saved clock must not monopolize it.
    func prepareToAddClock() async -> Bool {
        guard canChooseAnotherClock else {return false}
        if let updating=settingsUpdateTask {cancelSettingsUpdate();await updating.value}
        let pending=reconnectTask
        stopReconnecting()
        let request=selectionRequest
        await pending?.value
        guard selectionRequest==request,foreground,!Task.isCancelled,!busy,updateMonitor==nil else {return false}
        disconnect()
        return true
    }
    func cancelConnection() {
        guard updateMonitor==nil,reconnectTask != nil else {return}
        stopReconnecting()
    }
    func stopReconnecting() {
        cancelSettingsUpdate()
        selectionRequest=UUID()
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
    @discardableResult func save(_ patch:[String:Any],fromSettingsUpdate:Bool=false) async -> Bool {
        guard let clock=selected,!patch.isEmpty,readyForOperation,(settingsUpdatePhase == .idle || fromSettingsUpdate),let client else {return false}
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
            if Self.canRetryConnection(error) || Self.isUncertainPersistence(error) || !foreground || error is CancellationError {
                pendingSave=PendingSave(receiptID:receipt.id,clockID:clock.id,expected:expected,acknowledged:acknowledged,containsSecrets:!secretKeys.isDisjoint(with:patch.keys))
                // Recovery may continue while the clock is out of range. The save's
                // outcome is already unknown; do not leave a checking spinner up.
                saveReceipts[clock.id]?.phase = .unconfirmed
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
        stopReconnecting();pendingSave=nil;lastSettingsRefresh=nil;lastStatusRefresh=nil;settingsDurable=false;selected=nil;settings=[:];status=[:];fields=[];hello=[:];schemaIdentity=nil;networks=[];wifiScanMessage=""
        settingsDraft=SettingsDraft();draftClockID=nil;submittedSettingsDraft=nil;localRestoreFailed=false;localPersistenceFailed=false
        settingsUpdateMessage="";draftStorageMessage=""
        remember()
    }
    func remove(_ clock:SavedClock) {
        guard canChooseAnotherClock else {return}
        // Keep the library entry available for retry if secure deletion fails.
        do {try localSettingsStore.remove(clockID:clock.id)} catch {
            draftStorageMessage="Couldn’t remove this clock’s encrypted local changes. Unlock your iPhone and try again."
            return
        }
        if selected?.id==clock.id {disconnect()}
        clocks.removeAll(where:{$0.id==clock.id});saveReceipts.removeValue(forKey:clock.id)
        SchemaCache(preferences:preferences).remove(clock.id);remember()
    }
}
