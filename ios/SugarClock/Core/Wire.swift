import Foundation

public enum ClockError: Error, LocalizedError, Equatable {
    case unavailable(String), malformed, oversized, busy, timeout, disconnected, rejected(String)
    public var errorDescription: String? {
        switch self {
        case .unavailable(let detail), .rejected(let detail): return detail
        case .malformed: return "The clock sent an invalid response. Disconnect and reconnect."
        case .oversized: return "This request exceeds the clock’s supported message size."
        case .busy: return "Wait for the current operation to finish."
        case .timeout: return "The clock did not confirm the operation. Reconnect and read its settings before trying again."
        case .disconnected: return "Connection ended. Move closer and reconnect. If pairing failed, open the clock’s pairing window and enter the displayed code."
        }
    }
}
public struct Frame: Equatable {
    public static let maximum = 4096
    public let flags: UInt8
    public let id: UInt16
    public let offset: Int
    public let total: Int
    public let payload: Data
    public init(flags: UInt8, id: UInt16, offset: Int, total: Int, payload: Data = Data()) {
        self.flags=flags; self.id=id; self.offset=offset; self.total=total; self.payload=payload
    }
    public init(data: Data) throws {
        let b=Array(data)
        guard b.count>=8, b.count<=180, b[0]==1, b[1]<=2 else { throw ClockError.malformed }
        func word(_ i: Int)->Int { Int(b[i]) | Int(b[i+1])<<8 }
        flags=b[1]; id=UInt16(word(2)); offset=word(4); total=word(6); payload=Data(b.dropFirst(8))
        guard total<=Self.maximum, offset<=total, offset+payload.count<=total else { throw ClockError.malformed }
    }
    public var data: Data {
        var b:[UInt8]=[1,flags]
        for n in [Int(id),offset,total] { b += [UInt8(n & 255),UInt8((n>>8)&255)] }
        return Data(b)+payload
    }
    public static func split(_ data: Data, id: UInt16, packetLimit: Int) throws -> [Frame] {
        guard !data.isEmpty, data.count<=maximum else { throw ClockError.oversized }
        let limit=min(packetLimit,180)-8
        guard limit>0, id>0 else { throw ClockError.malformed }
        return stride(from: 0, to: data.count, by: limit).map { offset in
            Frame(flags:0,id:id,offset:offset,total:data.count,payload:data.subdata(in: offset..<min(offset+limit,data.count)))
        }
    }
}
public struct Reassembly {
    public private(set) var bytes=Data()
    private var total: Int?
    public init() {}
    public mutating func append(_ frame: Frame, expectedID: UInt16) throws -> Bool {
        guard frame.flags==1, frame.id==expectedID, frame.total>0, frame.total<=Frame.maximum,
              !frame.payload.isEmpty, frame.offset+frame.payload.count<=frame.total else { throw ClockError.malformed }
        if let total, total != frame.total { throw ClockError.malformed }
        total=frame.total
        if frame.offset<bytes.count {
            guard frame.offset+frame.payload.count<=bytes.count,
                  bytes.subdata(in:frame.offset..<frame.offset+frame.payload.count)==frame.payload else { throw ClockError.malformed }
        } else {
            guard frame.offset==bytes.count else { throw ClockError.malformed }
            bytes.append(frame.payload)
        }
        return bytes.count==frame.total
    }
}
@MainActor public protocol ClockTransport: AnyObject {
    var packetLimit: Int { get }
    func write(_ packet: Data) async throws
    func read() async throws -> Data
    func close()
}
@MainActor public final class ClockClient {
    public let transport: ClockTransport
    private var nextID: UInt16=1
    private var busy=false
    private let timeout:TimeInterval
    private let pollDelay:UInt64
    public init(transport: ClockTransport,timeout:TimeInterval=45,pollDelay:UInt64=200_000_000) { self.transport=transport;self.timeout=timeout;self.pollDelay=pollDelay }
    public func request(_ operation: String, fields: [String:Any]=[:]) async throws -> [String:Any] {
        guard !busy else { throw ClockError.busy }
        guard nextID<UInt16.max else { transport.close();throw ClockError.disconnected }
        busy=true;defer {busy=false}
        let id=nextID;nextID+=1
        var message=fields;message["v"]=1;message["id"]=Int(id);message["op"]=operation
        let bytes=try JSONSerialization.data(withJSONObject:message,options:[.sortedKeys])
        for packet in try Frame.split(bytes,id:id,packetLimit:transport.packetLimit) {
            try Task.checkCancellation();try await transport.write(packet.data)
        }
        var assembly=Reassembly();let deadline=Date().addingTimeInterval(timeout)
        while Date()<deadline {
            try Task.checkCancellation()
            let frame=try Frame(data:await transport.read())
            if frame.id != id || frame.total==0 {
                try await Task.sleep(nanoseconds:pollDelay);continue
            }
            let complete=try assembly.append(frame,expectedID:id)
            if complete {
                guard let response=try JSONSerialization.jsonObject(with:assembly.bytes) as? [String:Any],
                      response["v"] as? Int==1, response["id"] as? Int==Int(id),
                      let state=response["state"] as? String, ["queued","applied","failed"].contains(state) else { throw ClockError.malformed }
                if let error=response["error"] as? String { throw ClockError.rejected(error + (response["field"].map { ": \($0)" } ?? "")) }
                return response
            }
            try await transport.write(Frame(flags:2,id:id,offset:assembly.bytes.count,total:frame.total).data)
        }
        transport.close();throw ClockError.timeout
    }
    public func save(_ patch: [String:Any]) async throws {
        let response=try await request("settings.patch",fields:["patch":patch])
        guard response["state"] as? String=="applied", response["saved"] as? Bool==true else {throw ClockError.rejected("The clock has not confirmed a saved configuration.")}
    }
}
public enum SecretChange {
    case unchanged, replace(String), clear
    public func apply(to patch: inout [String:Any], key: String) {
        switch self { case .unchanged: break;case .replace(let value):patch[key]=value;case .clear:patch[key]=NSNull() }
    }
}
