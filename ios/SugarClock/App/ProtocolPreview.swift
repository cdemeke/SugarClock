#if DEBUG
import SwiftUI
struct ProtocolPreview:View {
    @State private var result="Explicit mock transport"
    private let client=ClockClient(transport:MockClockTransport())
    var body:some View {
        Form {
            Text("Preview · Mock clock").font(.headline)
            Text(result)
            Button("Save brightness 77 and read back") {Task {
                do {try await client.save(["brightness":77]);let response=try await client.request("settings.get");result=String(describing:response["settings"] ?? "")} catch {result=error.localizedDescription}
            }}
        }
    }
}
#Preview("Mock clock — no Bluetooth") {ProtocolPreview()}
#endif
