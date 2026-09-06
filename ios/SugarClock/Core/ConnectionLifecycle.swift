import Foundation

/// Platform-neutral session policy. A discovered service is transport-ready;
/// authorization is established by the first successful authenticated hello.
public struct ConnectionLifecycle: Equatable {
    public enum Phase: Equatable { case idle, connecting, discovering, ready }
    public private(set) var phase:Phase = .idle
    public private(set) var device:UUID?
    public private(set) var generation:UInt64=0
    public init() {}
    public mutating func begin(_ id:UUID) {generation &+= 1;device=id;phase = .connecting}
    public mutating func didConnect(_ id:UUID)->Bool {
        guard device==id,phase == .connecting else {return false}
        phase = .discovering;return true
    }
    public mutating func didDiscover(_ id:UUID)->Bool {
        guard device==id,phase == .discovering else {return false}
        phase = .ready;return true
    }
    public func acceptsDisconnect(_ id:UUID)->Bool {device==id && phase != .idle && phase != .connecting}
    public mutating func reset() {generation &+= 1;phase = .idle;device=nil}
}
