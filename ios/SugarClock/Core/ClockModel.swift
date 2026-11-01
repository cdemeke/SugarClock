import Foundation
import CoreBluetooth
import Combine

struct SavedClock: Codable, Identifiable {
    var id: String
    var peripheral: UUID
    var nickname: String
    /// Last local-network address that answered with this clock's identity.
    var host: String?=nil
}
/// How the current session reaches the clock. Wi-Fi is preferred: the clock
/// serves it continuously, while Bluetooth pauses for every secure fetch.
enum ClockLink:Equatable {
    case bluetooth, localNetwork(String)
    var host:String? {if case .localNetwork(let host)=self {return host};return nil}
}
/// What the clock screen's status badge shows: one glanceable level plus the
/// link in use and the current step or failure reason.
struct ConnectionStatus:Equatable {
    enum Level:Equatable {case connected,connecting,failed,idle}
    let level:Level
    let title:String
    /// "Wi-Fi · 192.168.1.81" or "Bluetooth" once a link is up.
    let link:String?
    let detail:String
}
/// Whether a clock other than the open one answers on this Wi-Fi. Web requests
/// are stateless, so an available clock opens in well under a second.
enum ClockAvailability:Equatable {case checking,onWiFi(String),notFound}
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

/// A greeted clock and the link that reached it, handed from a connection race.
private struct EstablishedRoute:@unchecked Sendable {
    let client:ClockRequesting
    let greeting:[String:Any]
    let link:ClockLink
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
    var connectionStatus:ConnectionStatus {
        let link=activeLink.map {$0.host.map {"Wi-Fi · "+$0} ?? "Bluetooth"}
        if sessionReady {return ConnectionStatus(level:.connected,title:"Connected",link:link,detail:"")}
        if reconnecting || updatingClock {
            let step=updatingClock ? updateMessage:connectionState
            return ConnectionStatus(level:.connecting,title:"Connecting",link:nil,detail:message.isEmpty ? step:message)
        }
        // Recovery stopped on an error, Bluetooth being off with no other route,
        // or a new clock that never answered. "Not connected" is a deliberate stop.
        if selected != nil,connectionState.hasPrefix("Couldn") {
            return ConnectionStatus(level:.failed,title:"Not connected",link:nil,detail:message.isEmpty ? connectionState:message)
        }
        if selected != nil,!canAttemptConnection {
            return ConnectionStatus(level:.failed,title:"Not connected",link:nil,detail:transport.availabilityMessage)
        }
        return ConnectionStatus(level:.idle,title:"Not connected",link:nil,detail:"Tap Retry to connect.")
    }
    /// The clock list's status line: the open clock's session, or whether any
    /// other clock answers on Wi-Fi. Nil before the first check.
    func listStatus(for clock:SavedClock)->(level:ConnectionStatus.Level,text:String)? {
        if clock.id==selected?.id {return (connectionStatus.level,connectionSummary)}
        switch availability[clock.id] {
        case .checking?:return (.connecting,"Searching…")
        case .onWiFi?:return (.connected,"Available on Wi-Fi")
        case .notFound?:return (.idle,"Not connected")
        case nil:return nil
        }
    }
    var connectionSummary:String {
        if updatingClock {return "Updating clock…"}
        if reconnecting {
            if connectionState=="Loading settings…" {return "Updating settings…"}
            if connectionState.hasPrefix("Looking") || connectionState.hasPrefix("Waiting") {return "Searching…"}
            return "Connecting…"
        }
        if syncingSettings {return "Updating settings…"}
        if sessionReady {return activeLink?.host != nil ? "Connected via Wi-Fi":"Connected via Bluetooth"}
        return "Not connected"
    }
    #if DEBUG
    func previewSettingsUpdate(_ phase:SettingsUpdatePhase) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil || ProcessInfo.processInfo.arguments.contains(where:{$0.hasPrefix("--sugarclock-screenshot=")}) else {return}
        settingsUpdatePhase=phase
        if lastSettingsRefresh==nil {lastSettingsRefresh=Date().addingTimeInterval(-120)}
        settingsUpdateMessage=phase == .waiting ? "Waiting for your clock. You can keep editing.":""
    }
    func previewConnection(ready:Bool) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil || ProcessInfo.processInfo.arguments.contains(where:{$0.hasPrefix("--sugarclock-screenshot=")}) else {return}
        sessionReady=ready
        connectionState=ready ? "Connected":"Couldn't connect"
    }
    func previewSave(_ phase:SavePhase) {
        guard ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil || ProcessInfo.processInfo.arguments.contains(where:{$0.hasPrefix("--sugarclock-screenshot=")}),let selected else {return}
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
    @Published private(set) var activeLink:ClockLink?
    let localNetwork:LocalClockNetwork?
    @Published private(set) var availability:[String:ClockAvailability]=[:]
    private var lastAvailabilitySweep:Date?
    var availabilitySweepInterval:TimeInterval=120
    /// Clocks found on Wi-Fi that are being added, keyed by synthetic peripheral ID.
    private var pendingLocalClocks:[UUID:LocalClockCandidate]=[:]
    /// How often a saved Wi-Fi address is rechecked while Bluetooth is pending.
    var localWatchInterval:UInt64=4_000_000_000
    var updatePollInterval:UInt64=2_000_000_000
    @Published var operationTitle="Saving…"
    private var unconfirmedChange=false
    private var foreground=true
    private var automaticReconnect=true
    private var schemaIdentity:SchemaIdentity?
    private var schemaProgressIdentity:SchemaIdentity?
    private var schemaProgress=SchemaProgress()
    private var client:ClockRequesting?
    private let transport:ClockConnectionTransport
    private let preferences:UserDefaults
    private let retryDelay:UInt64
    private var updateMonitor:Task<Void,Never>?
    private var reconnectTask:Task<Void,Never>?
    private var hostSearch:Task<Void,Never>?
    private var upgradeCheck:Task<Void,Never>?
    private var lastUpgrade:Date?
    private var selectionRequest=UUID()
    private var healthTask:Task<Void,Never>?
    private var connectionSubscription:AnyCancellable?
    private var radioSubscription:AnyCancellable?
    @Published var updateMessage=""
    @Published private(set) var updatingClock=false
    init(enableBluetooth:Bool=true,loadSaved:Bool=true,transport:ClockConnectionTransport?=nil,
         preferences:UserDefaults = .standard,retryDelay:UInt64=2_000_000_000,localSettingsStore:LocalSettingsStore?=nil,
         localNetwork:LocalClockNetwork?=nil) {
        bluetooth=BluetoothTransport(enableRadio:enableBluetooth)
        self.localNetwork=localNetwork ?? (enableBluetooth ? LocalClockNetwork():nil)
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
                // A Bluetooth event says nothing about a session carried over Wi-Fi.
                guard let self,!connected,!self.transport.connected,self.activeLink?.host==nil else {return}
                self.sessionReady=false;self.client=nil;self.activeLink=nil
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
    func discardSettingChange(_ key:String) {
        guard canEditSettingsDraft,settingsUpdatePhase == .idle else {return}
        let previous=settingsDraft,previousSubmission=submittedSettingsDraft
        settingsDraft.discardChange(key,settings:settings,fields:fields)
        // An uncertain submission stays as evidence, but an explicitly discarded
        // replacement must leave every persisted copy of that secret.
        if submittedSettingsDraft?.secrets[key] != nil {
            submittedSettingsDraft?.redactSubmittedSecret(key)
        }
        if persistLocalSettings() {settingsUpdateMessage="Local changes discarded."}
        else {
            settingsDraft=previous;submittedSettingsDraft=previousSubmission
            settingsUpdateMessage="Couldn’t discard the saved local change securely. Unlock your iPhone and try again."
        }
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
                        busy=false;ownsBusy=false;sessionReady=false;client=nil;closeLink()
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
                    if Self.canRetryConnection(error) {sessionReady=false;client=nil;closeLink()}
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
              let selected,canAttemptConnection else {return}
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
                        self.connectionState=self.transport.isPoweredOn || self.localNetwork==nil ? "Waiting for clock…":"Waiting for clock on Wi-Fi…"
                        try await Task.sleep(nanoseconds:SessionPolicy.retryDelay(afterFailures:failures,base:self.retryDelay))
                    }
                    try await self.establish(id)
                    self.connectionState="Connected";self.sessionReady=true
                    self.message=self.unconfirmedChange ? "Reconnected. Review the saved settings before retrying your change." : ""
                    self.preferLocalNetwork()
                    return
                } catch {
                    self.closeLink();self.client=nil;self.sessionReady=false
                    if Task.isCancelled || !self.foreground {return}
                    if !self.canAttemptConnection {
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
                        self.pendingLocalClocks[id]=nil
                        self.connectionState="Couldn’t finish adding clock"
                        self.message=LocalClockIdentity.isLocalOnly(id)
                            ? "Couldn’t reach this clock on Wi-Fi. Check that your iPhone is on the same network as the clock, then try again."
                            : "Move closer, hold the clock’s middle button for 3 seconds, then release and tap it to try again. If it was already added, open it from My Clocks."
                        return
                    }
                    if failures==3,self.localNetwork != nil {
                        self.message="Still looking for your clock. Check that it’s powered on and that your iPhone is on the same Wi-Fi, or nearby with Bluetooth on."
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
                return "Bluetooth pairing storage is full. See Help for replacing a phone."
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
            automaticReconnect=false;task.cancel();closeLink()
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
        if sessionReady,linkConnected,selected?.peripheral==id {return}
        launchConnection(id)
        await reconnectTask?.value
    }
    private func establish(_ id:UUID) async throws {
        if let (local,greeting)=try await connectToKnownLocalHost(id) {
            try await adopt(local,greeting:greeting,peripheral:id,link:.localNetwork(local.host))
            return
        }
        // A clock added over Wi-Fi has no Bluetooth identity to connect to.
        let bluetoothPossible = !LocalClockIdentity.isLocalOnly(id) && transport.isPoweredOn
        guard let localNetwork else {
            guard bluetoothPossible else {throw ClockError.timeout}
            let route=try await connectBluetooth(id)
            try await adopt(route.client,greeting:route.greeting,peripheral:id,link:route.link)
            return
        }
        guard bluetoothPossible else {
            connectionState="Looking for your clock on Wi-Fi…"
            guard let (local,greeting)=try await searchLocalNetwork(id,network:localNetwork) else {throw ClockError.timeout}
            try await adopt(local,greeting:greeting,peripheral:id,link:.localNetwork(local.host))
            return
        }
        // A pending Bluetooth connection can wait up to a minute for a clock that
        // is not advertising, so search Wi-Fi alongside it rather than before it.
        // The search outlives this attempt: if Bluetooth answers first, a clock
        // found on Wi-Fi afterwards still moves to the better link.
        searchForLocalHost(id,network:localNetwork)
        let interval=localWatchInterval
        let route=try await withThrowingTaskGroup(of:EstablishedRoute.self) {group in
            group.addTask {try await self.connectBluetooth(id)}
            group.addTask {
                while true {
                    try await Task.sleep(nanoseconds:interval)
                    if let (client,greeting)=try await self.connectToKnownLocalHost(id,quiet:true) {
                        return EstablishedRoute(client:client,greeting:greeting,link:.localNetwork(client.host))
                    }
                    // Still no address: search again (rate-limited) in case the
                    // clock joined Wi-Fi after this attempt began.
                    await self.searchForLocalHost(id,network:localNetwork)
                }
            }
            defer {group.cancelAll()}
            guard let first=try await group.next() else {throw ClockError.timeout}
            return first
        }
        try Task.checkCancellation()
        if route.link.host != nil {transport.close()}
        try await adopt(route.client,greeting:route.greeting,peripheral:id,link:route.link)
    }
    private func connectBluetooth(_ id:UUID) async throws -> EstablishedRoute {
        connectionState="Connecting over Bluetooth…"
        transport.operationTimeout=45
        try await transport.connect(id:id)
        try Task.checkCancellation()
        connectionState="Verifying clock…"
        let next=ClockClient(transport:transport)
        let greeting=try await next.request("hello")
        try Task.checkCancellation()
        // Pairing may need 45 seconds; subsequent reads fail promptly on a stale link.
        next.requestTimeout=15;transport.operationTimeout=15
        return EstablishedRoute(client:next,greeting:greeting,link:.bluetooth)
    }
    /// The saved (or just-discovered) address answers within a second when the
    /// iPhone shares the clock's network, and fails fast otherwise.
    private func connectToKnownLocalHost(_ id:UUID,quiet:Bool=false) async throws -> (LocalNetworkClockClient,[String:Any])? {
        guard let localNetwork else {return nil}
        let saved=clocks.first(where:{$0.peripheral==id}),pending=pendingLocalClocks[id]
        var tried:Set<String>=[]
        for host in [saved?.host,pending?.host].compactMap({$0}) where tried.insert(host).inserted {
            try Task.checkCancellation()
            if !quiet {connectionState="Connecting over Wi-Fi…"}
            if let found=await localNetwork.connect(host:host,expecting:saved?.id ?? pending?.identity) {return found}
        }
        return nil
    }
    private func searchForLocalHost(_ id:UUID,network:LocalClockNetwork) {
        guard hostSearch==nil,network.shouldSweep(),
              let expected=clocks.first(where:{$0.peripheral==id})?.id ?? pendingLocalClocks[id]?.identity else {return}
        hostSearch=Task { [weak self] in
            let found=await network.discover()
            guard let self,!Task.isCancelled else {return}
            self.hostSearch=nil
            self.rememberLocalHosts(found)
            if found.contains(where:{$0.identity==expected}) {self.preferLocalNetwork()}
        }
    }
    /// Move an idle Bluetooth session to Wi-Fi once the clock's address is known
    /// and answering: a quiet reconnect, never during a save, command or update.
    private func preferLocalNetwork() {
        guard let localNetwork,upgradeCheck==nil,activeLink == .bluetooth,let clock=selected,let host=clock.host,
              lastUpgrade.map({Date().timeIntervalSince($0)>=60}) ?? true else {return}
        upgradeCheck=Task { [weak self] in
            let reachable=await localNetwork.connect(host:host,expecting:clock.id) != nil
            guard let self,!Task.isCancelled else {return}
            self.upgradeCheck=nil
            guard reachable,self.selected?.id==clock.id,self.activeLink == .bluetooth,self.sessionReady,!self.busy,
                  self.pendingSave==nil,self.settingsUpdateTask==nil,self.updateMonitor==nil else {return}
            // At most once a minute, so a flapping Wi-Fi cannot bounce the session.
            self.lastUpgrade=Date()
            self.sessionReady=false;self.client=nil;self.closeLink()
            self.startReconnect()
        }
    }
    /// Rate-limited subnet search for this clock's identity, covering a first
    /// Wi-Fi connection and DHCP address changes.
    private func searchLocalNetwork(_ id:UUID,network:LocalClockNetwork) async throws -> (LocalNetworkClockClient,[String:Any])? {
        guard let expected=clocks.first(where:{$0.peripheral==id})?.id ?? pendingLocalClocks[id]?.identity,
              network.shouldSweep() else {return nil}
        let found=await network.discover()
        try Task.checkCancellation()
        rememberLocalHosts(found)
        guard let match=found.first(where:{$0.identity==expected}) else {return nil}
        return await network.connect(host:match.host,expecting:expected)
    }
    private func adopt(_ next:ClockRequesting,greeting:[String:Any],peripheral id:UUID,link:ClockLink) async throws {
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
        let host=link.host ?? saved.host
        selected=SavedClock(id:identity,peripheral:id,nickname:saved.nickname,host:host)
        // A saved clock found under a new peripheral ID keeps its workspace.
        if draftClockID != identity,let selected {restoreLocalSettings(for:selected)}
        client=next;hello=greeting;activeLink=link
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
        let confirmed=SavedClock(id:identity,peripheral:id,nickname:nickname,host:host)
        selected=confirmed
        if let index=clocks.firstIndex(where:{$0.id==identity}) {clocks[index]=confirmed}
        else {clocks.append(confirmed)}
        pendingLocalClocks[id]=nil
        remember()
        refreshLocalDraft()
    }
    /// Keep saved clocks' Wi-Fi addresses current whenever a search sees them.
    func rememberLocalHosts(_ found:[LocalClockCandidate]) {
        var changed=false
        for candidate in found {
            guard let index=clocks.firstIndex(where:{$0.id==candidate.identity}),clocks[index].host != candidate.host else {continue}
            clocks[index].host=candidate.host;changed=true
            if selected?.id==candidate.identity {selected?.host=candidate.host}
        }
        if changed {remember()}
    }
    /// Checks every clock except the open one at its saved Wi-Fi address. A
    /// clock missing from its address (new, or moved by DHCP) gets a subnet
    /// search at most every two minutes. The open clock's session is untouched.
    func refreshAvailability() async {
        guard foreground,let localNetwork else {return}
        let others=clocks.filter {$0.id != selected?.id}
        for clock in others where availability[clock.id]==nil {availability[clock.id] = .checking}
        func check(_ clocks:[SavedClock]) async -> [String:String] {
            await withTaskGroup(of:(String,String?).self) {group in
                for clock in clocks {
                    group.addTask {
                        guard let host=clock.host,await localNetwork.isAvailable(host:host,identity:clock.id) else {return (clock.id,nil)}
                        return (clock.id,host)
                    }
                }
                var found:[String:String]=[:]
                for await (id,host) in group {if let host {found[id]=host}}
                return found
            }
        }
        var found=await check(others)
        let missing=others.filter {found[$0.id]==nil}
        if !missing.isEmpty,hostSearch==nil,localNetwork.shouldSweep(),
           lastAvailabilitySweep.map({Date().timeIntervalSince($0)>=availabilitySweepInterval}) ?? true {
            lastAvailabilitySweep=Date()
            let candidates=await localNetwork.discover()
            rememberLocalHosts(candidates)
            for candidate in candidates where missing.contains(where:{$0.id==candidate.identity}) {found[candidate.identity]=candidate.host}
        }
        guard !Task.isCancelled else {return}
        var next:[String:ClockAvailability]=[:]
        for clock in clocks where clock.id != selected?.id {
            // A clock added or closed during the check waits for the next one.
            next[clock.id]=others.contains(where:{$0.id==clock.id}) ? found[clock.id].map {.onWiFi($0)} ?? .notFound:availability[clock.id]
        }
        availability=next
    }
    /// Adds a clock found on Wi-Fi. No pairing: the clock's web API is already
    /// open to devices on its network, the same access its admin page uses.
    func addLocalClock(_ candidate:LocalClockCandidate) async {
        if let existing=clocks.first(where:{$0.id==candidate.identity}) {
            rememberLocalHosts([candidate]);await connect(existing.peripheral);return
        }
        guard let id=candidate.peripheralID else {return}
        pendingLocalClocks[id]=candidate
        await connect(id)
    }
    func checkConnection() async {
        guard foreground,!busy,settingsUpdateTask==nil,reconnectTask==nil,updateMonitor==nil,sessionReady,client != nil else {return}
        busy=true;checkingConnection=true
        defer {busy=false;checkingConnection=false;if !sessionReady {startReconnect()}}
        do {
            try await refreshStatus()
        } catch {
            sessionReady=false;self.client=nil;closeLink()
        }
    }
    private func loadSchema(_ hello:[String:Any],client:ClockRequesting) async throws {
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
    private var readyForOperation:Bool {sessionReady && linkConnected && !busy && updateMonitor==nil}
    /// Wi-Fi requests are independent; the session is up while they succeed.
    private var linkConnected:Bool {activeLink?.host != nil || transport.connected}
    /// Both radios are candidates whenever local-network discovery is available.
    private var canAttemptConnection:Bool {transport.isPoweredOn || localNetwork != nil}
    private func closeLink() {transport.close();activeLink=nil}
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
        hostSearch?.cancel();hostSearch=nil;upgradeCheck?.cancel();upgradeCheck=nil
        finishPendingAsUnconfirmed()
        closeLink();client=nil;sessionReady=false
        connectionState="Not connected";message=""
    }
    func retrySelected() async {
        guard updateMonitor==nil else {return}
        if let task=reconnectTask {task.cancel();closeLink();await task.value}
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
                sessionReady=false;client=nil;closeLink()
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
                sessionReady=false;self.client=nil;closeLink()
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
    // A queued request leaves OTA state unchanged until its fleet visit runs;
    // only a state reported after that visit belongs to the submitted request.
    static func updateRequestSettled(_ ota:[String:Any])->Bool {
        if ota["request_pending"] as? Bool==true {return false}
        if ota["request_failed"] as? Bool==true {return true}
        return ["error","deferred","update_available","idle"].contains(ota["state"] as? String ?? "")
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
                try? await Task.sleep(nanoseconds:self.updatePollInterval)
                if Task.isCancelled {return}
                if self.busy {continue}
                self.busy=true
                do {
                    if !self.linkConnected {try await self.establish(clock.peripheral)}
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
                    if Self.updateRequestSettled(ota) {
                        self.updateMessage=ota["request_failed"] as? Bool==true ?
                            "Reconnected. The clock couldn’t reach the update service; try again later." :
                            "Reconnected · \(state). \(ota["error"] as? String ?? "") \(ota["deferral"] as? String ?? "")"
                        self.sessionReady=true;self.connectionState="Connected"
                        self.busy=false;return
                    }
                } catch {self.sessionReady=false;self.closeLink();self.client=nil;self.updateMessage="Waiting to reconnect. The clock may be updating or rebooting."}
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
                sessionReady=false;self.client=nil;closeLink()
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
