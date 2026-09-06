import Foundation
import CoreBluetooth
import Combine

@MainActor public final class BluetoothTransport: NSObject, ObservableObject, ClockTransport, @preconcurrency CBCentralManagerDelegate, @preconcurrency CBPeripheralDelegate {
    public static let service=CBUUID(string:"ca7c0001-63a2-4b7c-9a5b-763e4e0c1000")
    private let requestUUID=CBUUID(string:"ca7c0002-63a2-4b7c-9a5b-763e4e0c1000")
    private let responseUUID=CBUUID(string:"ca7c0003-63a2-4b7c-9a5b-763e4e0c1000")
    @Published public private(set) var devices:[CBPeripheral]=[]
    @Published public private(set) var state="Starting Bluetooth"
    @Published public private(set) var connected=false
    private var lifecycle=ConnectionLifecycle()
    private var central:CBCentralManager!
    private var peripheral:CBPeripheral?
    private var rx:CBCharacteristic?,tx:CBCharacteristic?
    private var connecting:CheckedContinuation<Void,Error>?
    private var writing:CheckedContinuation<Void,Error>?
    private var reading:CheckedContinuation<Data,Error>?
    private var operationTimer:Task<Void,Never>?
    private var connectionTimer:Task<Void,Never>?
    public override convenience init() {self.init(enableRadio:true)}
    public init(enableRadio:Bool) {super.init();if enableRadio {central=CBCentralManager(delegate:self,queue:.main)} else {state="Screenshot preview · Bluetooth disabled"}}
    public var packetLimit:Int {min(180,peripheral?.maximumWriteValueLength(for:.withResponse) ?? 20)}
    public func scan() {
        guard central?.state == .poweredOn else { return }
        state="Looking for nearby clocks"
        central.scanForPeripherals(withServices:[Self.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false])
    }
    public func centralManagerDidUpdateState(_ central:CBCentralManager) {
        switch central.state {
        case .poweredOn:scan()
        case .poweredOff:state="Bluetooth is off. Turn it on in Settings.";fail(ClockError.disconnected)
        case .unauthorized:state="Bluetooth access denied. Allow SugarClock in Settings → Privacy & Security → Bluetooth.";fail(ClockError.disconnected)
        case .unsupported:state="Bluetooth is unavailable on this device. Use a physical iPhone.";fail(ClockError.disconnected)
        default:state="Bluetooth is temporarily unavailable.";fail(ClockError.disconnected)
        }
    }
    public func centralManager(_ central:CBCentralManager,didDiscover peripheral:CBPeripheral,advertisementData:[String:Any],rssi RSSI:NSNumber) {
        if !devices.contains(where:{$0.identifier==peripheral.identifier}) {devices.append(peripheral)}
    }
    public func connect(id:UUID) async throws {
        guard central?.state == .poweredOn else {throw ClockError.unavailable(state)}
        guard connecting==nil else {throw ClockError.busy}
        close()
        guard let p=devices.first(where:{$0.identifier==id}) ?? central.retrievePeripherals(withIdentifiers:[id]).first else {throw ClockError.unavailable("Clock not found. Move closer and search again.")}
        peripheral=p;p.delegate=self;lifecycle.begin(id);state="Connecting… Enter the code shown on the clock if iOS asks."
        central.stopScan()
        try await withCheckedThrowingContinuation { continuation in
            connecting=continuation;central.connect(p)
            connectionTimer=Task {try? await Task.sleep(nanoseconds:45_000_000_000);if !Task.isCancelled {self.fail(ClockError.timeout);self.close()} }
        }
    }
    public func centralManager(_ central:CBCentralManager,didConnect peripheral:CBPeripheral) { guard lifecycle.didConnect(peripheral.identifier) else {return};peripheral.discoverServices([Self.service]) }
    public func peripheral(_ peripheral:CBPeripheral,didDiscoverServices error:Error?) {
        guard self.peripheral === peripheral, lifecycle.phase == .discovering else {return}
        if let error {fail(error);return}
        guard let service=peripheral.services?.first(where:{$0.uuid==Self.service}) else {fail(ClockError.unavailable("Unsupported firmware. Install Bluetooth-capable SugarClock firmware by USB or the existing Wi-Fi updater."));return}
        peripheral.discoverCharacteristics([requestUUID,responseUUID],for:service)
    }
    public func peripheral(_ peripheral:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?) {
        guard self.peripheral === peripheral, lifecycle.phase == .discovering else {return}
        if let error {fail(error);return}
        rx=service.characteristics?.first(where:{$0.uuid==requestUUID});tx=service.characteristics?.first(where:{$0.uuid==responseUUID})
        guard rx != nil, tx != nil else {fail(ClockError.malformed);return}
        guard lifecycle.didDiscover(peripheral.identifier) else {return}
        connected=true;state="Connected · Pairing may be requested on first access"
        connectionTimer?.cancel();let c=connecting;connecting=nil;c?.resume()
    }
    private func startOperationTimer() {
        operationTimer?.cancel()
        operationTimer=Task {try? await Task.sleep(nanoseconds:45_000_000_000);if !Task.isCancelled {self.fail(ClockError.timeout);self.close()} }
    }
    public func write(_ packet:Data) async throws {
        guard let peripheral,let rx,connected else {throw ClockError.disconnected}
        guard writing==nil,reading==nil else {throw ClockError.busy}
        try await withCheckedThrowingContinuation { writing=$0;startOperationTimer();peripheral.writeValue(packet,for:rx,type:.withResponse) }
    }
    public func read() async throws -> Data {
        guard let peripheral,let tx,connected else {throw ClockError.disconnected}
        guard writing==nil,reading==nil else {throw ClockError.busy}
        return try await withCheckedThrowingContinuation {reading=$0;startOperationTimer();peripheral.readValue(for:tx)}
    }
    public func peripheral(_ peripheral:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?) {
        guard self.peripheral === peripheral, lifecycle.phase == .ready, characteristic === rx else {return}
        operationTimer?.cancel();let c=writing;writing=nil
        if let error {c?.resume(throwing:error)} else {c?.resume()}
    }
    public func peripheral(_ peripheral:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?) {
        guard self.peripheral === peripheral, lifecycle.phase == .ready, characteristic === tx else {return}
        operationTimer?.cancel();let c=reading;reading=nil
        if let error {c?.resume(throwing:error)} else if let data=characteristic.value {c?.resume(returning:data)} else {c?.resume(throwing:ClockError.malformed)}
    }
    public func centralManager(_ central:CBCentralManager,didFailToConnect peripheral:CBPeripheral,error:Error?) {
        guard self.peripheral === peripheral, lifecycle.phase == .connecting else {return}
        fail(error ?? ClockError.disconnected)
    }
    public func centralManager(_ central:CBCentralManager,didDisconnectPeripheral peripheral:CBPeripheral,error:Error?) {
        guard lifecycle.acceptsDisconnect(peripheral.identifier) else {return}
        state="Disconnected · Tap your clock to reconnect";fail(error ?? ClockError.disconnected)
    }
    private func fail(_ error:Error) {
        connected=false;operationTimer?.cancel();connectionTimer?.cancel()
        let a=connecting;connecting=nil;let b=writing;writing=nil;let c=reading;reading=nil
        a?.resume(throwing:error);b?.resume(throwing:error);c?.resume(throwing:error)
    }
    public func close() {
        lifecycle.reset()
        fail(ClockError.disconnected)
        if let peripheral {peripheral.delegate=nil;central.cancelPeripheralConnection(peripheral)}
        peripheral=nil;rx=nil;tx=nil
    }
}
