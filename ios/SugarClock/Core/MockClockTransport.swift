#if DEBUG
import Foundation
// Explicit opt-in for previews only. Never selected by BluetoothTransport or app startup.
@MainActor public final class MockClockTransport:ClockTransport {
    public var packetLimit=20
    private var incoming=Data(),outgoing=Data()
    private var offset=0
    private var requestID:UInt16=0
    private var brightness=40
    public init() {}
    public func write(_ packet:Data) async throws {
        let frame=try Frame(data:packet)
        if frame.flags==2 {offset=frame.offset;return}
        if frame.offset==0 {incoming=Data();requestID=frame.id}
        guard frame.offset==incoming.count else {throw ClockError.malformed}
        incoming.append(frame.payload)
        if incoming.count==frame.total {
            guard let message=try JSONSerialization.jsonObject(with:incoming) as? [String:Any] else {throw ClockError.malformed}
            var response:[String:Any]=["v":1,"id":Int(requestID),"state":"applied"]
            switch message["op"] as? String {
            case "settings.patch":brightness=(message["patch"] as? [String:Int])?["brightness"] ?? brightness;response["saved"]=true
            case "settings.get":response["settings"]=["brightness":brightness]
            default:response["state"]="failed";response["error"]="mock_operation_not_implemented"
            }
            outgoing=try JSONSerialization.data(withJSONObject:response);offset=0;incoming=Data()
        }
    }
    public func read() async throws -> Data {
        let count=min(packetLimit-8,outgoing.count-offset)
        return Frame(flags:1,id:requestID,offset:offset,total:outgoing.count,payload:outgoing.subdata(in:offset..<offset+count)).data
    }
    public func close() {incoming=Data();outgoing=Data();offset=0}
}
#endif
