import Foundation
import CoreBluetooth
import Combine

@MainActor protocol ClockConnectionTransport:ClockTransport {
    var operationTimeout:TimeInterval {get set}
    var connected:Bool {get}
    var isPoweredOn:Bool {get}
    var availabilityMessage:String {get}
    var connectionPublisher:AnyPublisher<Bool,Never> {get}
    func connect(id:UUID) async throws
}

@MainActor public final class BluetoothTransport: NSObject, ObservableObject, ClockConnectionTransport, @preconcurrency CBCentralManagerDelegate, @preconcurrency CBPeripheralDelegate {
    public static let service=CBUUID(string:"ca7c0001-63a2-4b7c-9a5b-763e4e0c1000")
    private let requestUUID=CBUUID(string:"ca7c0002-63a2-4b7c-9a5b-763e4e0c1000")
    private let responseUUID=CBUUID(string:"ca7c0003-63a2-4b7c-9a5b-763e4e0c1000")
    @Published public private(set) var devices:[CBPeripheral]=[]
    @Published public private(set) var state="Starting Bluetooth"
    @Published public private(set) var connected=false
    @Published public private(set) var poweredOn=false
    var isPoweredOn:Bool {poweredOn}
    var availabilityMessage:String {state}
    var connectionPublisher:AnyPublisher<Bool,Never> {$connected.eraseToAnyPublisher()}
    var operationTimeout:TimeInterval=45
    private var preparing=false
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
        devices=[]
        state="Looking for nearby clocks"
        central.scanForPeripherals(withServices:[Self.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false])
    }
    public func centralManagerDidUpdateState(_ central:CBCentralManager) {
        poweredOn=central.state == .poweredOn
        switch central.state {
        case .poweredOn:scan()
        case .poweredOff:state="Bluetooth is off. Turn it on in Settings.";fail(ClockError.disconnected)
        case .unauthorized:state="Bluetooth access denied. Allow SugarClock in Settings → Privacy & Security → Bluetooth.";fail(ClockError.disconnected)
        case .unsupported:state="Bluetooth is unavailable on this device. Use a physical iPhone.";fail(ClockError.disconnected)
        default:state="Bluetooth is temporarily unavailable.";fail(ClockError.disconnected)
        }
    }
    public func centralManager(_ central:CBCentralManager,didDiscover peripheral:CBPeripheral,advertisementData:[String:Any],rssi RSSI:NSNumber) {
        if let index=devices.firstIndex(where:{$0.identifier==peripheral.identifier}) {devices[index]=peripheral} else {devices.append(peripheral)}
    }
    public func connect(id:UUID) async throws {
        guard central?.state == .poweredOn else {throw ClockError.unavailable(state)}
        guard !preparing,connecting==nil else {throw ClockError.busy}
        preparing=true;defer {preparing=false}
        let previous=peripheral
        close()
        let p=central.retrievePeripherals(withIdentifiers:[id]).first ?? devices.first(where:{$0.identifier==id})
        guard let p else {scan();throw ClockError.disconnected}
        // Core Bluetooth cancels asynchronously. Do not connect the same peripheral
        // until cancellation finishes, or a late callback can strand the new attempt.
        for old in [previous,p].compactMap({$0}) {
            let deadline=Date().addingTimeInterval(5)
            if old.state != .disconnected {central.cancelPeripheralConnection(old)}
            while old.state != .disconnected,Date()<deadline {
                try await Task.sleep(nanoseconds:100_000_000)
            }
            guard old.state == .disconnected else {throw ClockError.timeout}
        }
        try Task.checkCancellation()
        peripheral=p;p.delegate=self;lifecycle.begin(id);state="Connecting…"
        central.scanForPeripherals(withServices:[Self.service],options:nil)
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                connecting=continuation;central.connect(p)
                connectionTimer=Task {
                    do {try await Task.sleep(nanoseconds:20_000_000_000)} catch {return}
                    self.fail(ClockError.timeout);self.close()
                }
            }
        } onCancel: {
            Task { @MainActor [weak self] in self?.close() }
        }
        try Task.checkCancellation()
    }
    public func centralManager(_ central:CBCentralManager,didConnect peripheral:CBPeripheral) { guard self.peripheral === peripheral,lifecycle.didConnect(peripheral.identifier) else {return};central.stopScan();peripheral.discoverServices([Self.service]) }
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
        let duration=UInt64(operationTimeout * 1_000_000_000)
        operationTimer=Task {try? await Task.sleep(nanoseconds:duration);if !Task.isCancelled {self.fail(ClockError.timeout);self.close()} }
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
        guard self.peripheral === peripheral,peripheral.state == .disconnected, lifecycle.acceptsDisconnect(peripheral.identifier) else {return}
        state="Disconnected · Tap your clock to reconnect";fail(error ?? ClockError.disconnected)
    }
    private func fail(_ error:Error) {
        connected=false;operationTimer?.cancel();connectionTimer?.cancel()
        let a=connecting;connecting=nil;let b=writing;writing=nil;let c=reading;reading=nil
        a?.resume(throwing:error);b?.resume(throwing:error);c?.resume(throwing:error)
    }
    public func close() {
        central?.stopScan()
        lifecycle.reset()
        fail(ClockError.disconnected)
        if let peripheral {peripheral.delegate=nil;central.cancelPeripheralConnection(peripheral)}
        peripheral=nil;rx=nil;tx=nil
    }
}
