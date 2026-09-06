import SwiftUI

struct MyClocksView: View {
    @EnvironmentObject var model:ClockModel
    var body: some View {
        NavigationStack {
            List {
                Image("AppLogo").resizable().scaledToFit().frame(height:64).frame(maxWidth:.infinity).accessibilityHidden(true)
                Section("My Clocks") {
                    ForEach(model.clocks) {clock in
                        SavedClockRow(clock:clock,bluetooth:model.bluetooth)
                    }.onDelete {indices in model.clocks.remove(atOffsets:indices);model.remember()}
                }
                DiscoveryView(bluetooth:model.bluetooth)
                if model.selected != nil { NavigationLink("Clock Settings") {DeviceView()} }
                Section {Text(model.message).foregroundStyle(.secondary);if model.busy {ProgressView("Working with your clock")}}
                Section {NavigationLink("Clock not found or pairing failed?") {TroubleshootingView()}}
            }.navigationTitle("SugarClock").disabled(model.busy)
        }
    }
}
struct DiscoveryView: View {
    @EnvironmentObject var model:ClockModel
    @ObservedObject var bluetooth:BluetoothTransport
    var body: some View {
        Section("Add Clock") {
            Text(bluetooth.state).font(.callout)
            Text("Hold left and right together for 3 seconds, then release. Choose the name matching your clock and enter its six-digit code in the iOS pairing prompt.").font(.footnote)
            ForEach(bluetooth.devices,id:\.identifier) {device in
                Button(device.name ?? "SugarClock") {Task {await model.connect(device.identifier)}}
            }
            Button("Search nearby") {bluetooth.scan()}
        }
    }
}
struct TroubleshootingView: View {
    var body: some View { List {
        Text("Bluetooth-capable firmware must be installed first. The app cannot discover older firmware. Use the Mac USB installer, or the clock’s existing signed Wi-Fi updater if compatible.")
        Text("A configured clock admits a new phone only during a physical pairing window. Hold left and right together for 3 seconds and release. Enter the fresh code shown on its display. Urgent alerts take priority; retry when the clock can display the code.")
        Text("For a replaced phone, stale bonds, or full bond storage: hold left and right together for 10 seconds to remove all Bluetooth bonds. Wi-Fi, glucose, alerts, display settings, and certificates remain. Also forget SugarClock in iOS Bluetooth Settings before pairing again.")
        Text("Move within a few metres. Turn Bluetooth on and allow SugarClock access in iPhone Settings. Configuration sessions run in the foreground; tap your saved clock to reconnect after leaving the app or rebooting.")
        Text("Wi-Fi requires a 2.4 GHz network. A failed trial preserves the previous saved network and resumes retries. DHCP success does not establish internet access or provider authentication. Check those status fields separately.")
        Text("If old firmware has broken Wi-Fi, recover through its setup portal or USB upgrade. There is no Bluetooth firmware transfer in this release.")
    }.navigationTitle("Troubleshooting") }
}

struct SavedClockRow:View {
    @EnvironmentObject var model:ClockModel
    let clock:SavedClock
    @ObservedObject var bluetooth:BluetoothTransport
    var body:some View {
        Button {Task {await model.connect(clock.peripheral)}} label: {
            VStack(alignment:.leading) {
                Text(clock.nickname)
                Text(model.selected?.id==clock.id && bluetooth.connected ? "Connected":"Tap to connect").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}
