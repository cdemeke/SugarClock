import SwiftUI
import CoreBluetooth

struct SavedClock: Codable, Identifiable {
    var id: String
    var peripheral: UUID
    var nickname: String
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
    @Published var message=""
    @Published var busy=false
    private var client:ClockClient?
    private var updateMonitor:Task<Void,Never>?
    @Published var updateMessage=""
    init(enableBluetooth:Bool=true,loadSaved:Bool=true) {
        bluetooth=BluetoothTransport(enableRadio:enableBluetooth)
        if loadSaved,let data=UserDefaults.standard.data(forKey:"clocks.v1"),let saved=try? JSONDecoder().decode([SavedClock].self,from:data) {clocks=saved}
    }
    func remember() { if let data=try? JSONEncoder().encode(clocks) {UserDefaults.standard.set(data,forKey:"clocks.v1")} }
    func connect(_ id:UUID) async {
        await perform {
            try await self.bluetooth.connect(id:id)
            let client=ClockClient(transport:self.bluetooth);self.client=client
            let hello=try await client.request("hello")
            guard let identity=hello["device_id"] as? String, let caps=hello["capabilities"] as? [String],caps.contains("settings.patch") else {throw ClockError.unavailable("This firmware does not support companion settings.")}
            if let known=self.clocks.first(where:{$0.peripheral==id}),known.id != identity {
                self.bluetooth.close();throw ClockError.unavailable("Device identity changed. Add this clock again.")
            }
            self.hello=hello
            let saved=self.clocks.first(where:{$0.id==identity}) ?? SavedClock(id:identity,peripheral:id,nickname:hello["name"] as? String ?? "SugarClock")
            self.selected=SavedClock(id:identity,peripheral:id,nickname:saved.nickname)
            self.clocks.removeAll(where:{$0.id==identity});self.clocks.append(SavedClock(id:identity,peripheral:id,nickname:saved.nickname));self.remember()
            try await self.refresh()
            self.fields=[]
            if caps.contains("schema") {
                for page in 0..<11 {
                    let response=try await client.request("schema.get",fields:["page":page])
                    self.fields += response["fields"] as? [[String:Any]] ?? []
                    if response["more"] as? Bool != true {break}
                }
            }
            self.message="Connected. Existing settings loaded."
            if let expected=UserDefaults.standard.string(forKey:"update.expected."+identity) {
                let ota=self.status["ota"] as? [String:Any] ?? [:]
                self.updateMessage=(ota["current_version"] as? String==expected && ota["pending_verification"] as? Bool==false) ? "Firmware \(expected) confirmed after reconnecting." : "An update to \(expected) was requested. Check current version and rollback status."
            }
        }
    }
    func refresh() async throws {
        guard let client else {throw ClockError.disconnected}
        settings=try await client.request("settings.get")["settings"] as? [String:Any] ?? [:]
        status=try await client.request("status.get")["status"] as? [String:Any] ?? [:]
    }
    func perform(_ action: @escaping () async throws -> Void) async {
        guard !busy else {return};busy=true;defer{busy=false}
        do {try await action()} catch {message=error.localizedDescription}
    }
    func save(_ patch:[String:Any]) async {
        await perform {
            guard let client=self.client else {throw ClockError.disconnected}
            try await client.save(patch);try await self.refresh();self.message="Saved on the clock and read back."
        }
    }
    func command(_ op:String,fields:[String:Any]=[:]) async {
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
        if let expected {UserDefaults.standard.set(expected,forKey:"update.expected."+clock.id)}
        updateMessage="Update request accepted. Waiting for the clock; Bluetooth may temporarily disconnect."
        updateMonitor=Task {
            let deadline=Date().addingTimeInterval(180)
            while Date()<deadline {
                try? await Task.sleep(nanoseconds:2_000_000_000)
                if Task.isCancelled {return}
                if self.busy {continue}
                self.busy=true
                do {
                    if !self.bluetooth.connected {
                        try await self.bluetooth.connect(id:clock.peripheral)
                        self.client=ClockClient(transport:self.bluetooth)
                        let hello=try await self.client!.request("hello")
                        guard hello["device_id"] as? String==clock.id else {throw ClockError.unavailable("The reconnecting clock has a different identity.")}
                        self.hello=hello
                    }
                    try await self.refresh()
                    let ota=self.status["ota"] as? [String:Any] ?? [:]
                    let state=ota["state"] as? String ?? ""
                    self.updateMessage="Clock update: \(state) · \(ota["progress"] as? Int ?? 0)%"
                    if let expected,ota["current_version"] as? String==expected,ota["pending_verification"] as? Bool==false {
                        self.updateMessage="Reconnected. Firmware \(expected) passed startup validation."
                        UserDefaults.standard.removeObject(forKey:"update.expected."+clock.id)
                        self.busy=false;return
                    }
                    if ["error","deferred","update_available","idle"].contains(state) {
                        self.updateMessage="Reconnected · \(state). \(ota["error"] as? String ?? "") \(ota["deferral"] as? String ?? "")"
                        self.busy=false;return
                    }
                } catch {self.updateMessage="Waiting to reconnect. The clock may be updating or rebooting."}
                self.busy=false
            }
            self.updateMessage="Automatic reconnection timed out. Reconnect from My Clocks and check the current version and update status."
        }
    }
    func scanWiFi() async {
        await perform {
            guard let client=self.client else {throw ClockError.disconnected}
            _=try await client.request("wifi.scan")
            for _ in 0..<15 {
                try await Task.sleep(nanoseconds:1_000_000_000)
                let response=try await client.request("wifi.results")
                self.networks=response["networks"] as? [[String:Any]] ?? []
                if response["scanning"] as? Bool==false {break}
            }
        }
    }
    func disconnect() {updateMonitor?.cancel();bluetooth.close();client=nil;selected=nil;settings=[:];status=[:];fields=[]}
}
