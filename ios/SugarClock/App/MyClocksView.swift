import SwiftUI

struct MyClocksView:View {
    @EnvironmentObject var model:ClockModel
    var body:some View {
        NavigationStack {
            SugarScreen {
                HStack(spacing:12) {
                    BrandIcon(name:"BrandLogo",size:44)
                    VStack(alignment:.leading,spacing:3) {
                        Text("SugarClock").font(.headline)
                        Text("Device control").font(.caption).foregroundStyle(SugarTheme.secondary)
                    }
                    Spacer()
                    Image(systemName:"antenna.radiowaves.left.and.right").foregroundStyle(SugarTheme.accent).accessibilityHidden(true)
                }
                Divider()
                PageHeading(title:"My Clocks",subtitle:"A little peace of mind, always nearby.",icon:"DashboardIcon")
                SugarCard(title:"Your devices") {
                    if model.clocks.isEmpty {
                        Text("Your clocks will appear here after pairing.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                    }
                    ForEach(model.clocks) {clock in
                        SavedClockRow(clock:clock,bluetooth:model.bluetooth)
                        if clock.id != model.clocks.last?.id {Divider()}
                    }
                    if model.selected != nil {
                        NavigationLink {DeviceView()} label:{Label("Manage selected clock",systemImage:"slider.horizontal.3")}
                            .buttonStyle(SugarButtonStyle())
                    }
                }
                DiscoveryView(bluetooth:model.bluetooth)
                SugarCard {
                    NavigationLink {TroubleshootingView()} label:{DestinationRow(title:"Need a hand?",subtitle:"Pairing, connection and setup help",symbol:"questionmark.circle")}
                        .buttonStyle(.plain)
                }
                OperationFeedback()
                Text("Your clock keeps working when this app is closed.")
                    .font(.footnote).foregroundStyle(SugarTheme.secondary).frame(maxWidth:.infinity).multilineTextAlignment(.center)
            }.disabled(model.busy)
        }.tint(SugarTheme.accent)
    }
}

struct DiscoveryView:View {
    @EnvironmentObject var model:ClockModel
    @ObservedObject var bluetooth:BluetoothTransport
    var body:some View {
        SugarCard(title:"Add a clock") {
            HStack(alignment:.top,spacing:12) {
                Image(systemName:"dot.radiowaves.left.and.right").font(.title2).foregroundStyle(SugarTheme.accent).accessibilityHidden(true)
                VStack(alignment:.leading,spacing:6) {
                    Text("Make your clock discoverable").font(.subheadline.weight(.semibold))
                    Text("Hold left + right for 3 seconds, then release. Choose your clock and enter its six-digit code in the iOS prompt.")
                        .font(.subheadline).foregroundStyle(SugarTheme.secondary)
                }
            }
            Text(bluetooth.state).font(.caption).foregroundStyle(SugarTheme.secondary)
            ForEach(bluetooth.devices,id:\.identifier) {device in
                Button {Task {await model.connect(device.identifier)}} label:{DestinationRow(title:device.name ?? "SugarClock",subtitle:"Nearby · Tap to pair",symbol:"plus.circle")}.buttonStyle(.plain)
            }
            Button {bluetooth.scan()} label:{Label("Search nearby",systemImage:"magnifyingglass")}.buttonStyle(SugarButtonStyle(prominent:false))
        }
    }
}

struct SavedClockRow:View {
    @EnvironmentObject var model:ClockModel
    let clock:SavedClock
    @ObservedObject var bluetooth:BluetoothTransport
    var connected:Bool {model.selected?.id==clock.id && bluetooth.connected}
    var body:some View {
        Button {Task {await model.connect(clock.peripheral)}} label: {
            HStack(spacing:14) {
                BrandIcon(name:"BrandLogo",size:48)
                VStack(alignment:.leading,spacing:5) {
                    Text(clock.nickname).font(.headline).foregroundStyle(SugarTheme.text)
                    Text(connected ? "Connected via Bluetooth":"Saved clock · Tap to connect").font(.caption).foregroundStyle(SugarTheme.secondary)
                }
                Spacer(minLength:4)
                Image(systemName:connected ? "checkmark.circle.fill":"chevron.right").foregroundStyle(SugarTheme.accent).accessibilityHidden(true)
            }.padding(.vertical,4).contentShape(Rectangle())
        }.buttonStyle(.plain).contextMenu {
            Button("Remove from My Clocks",role:.destructive) {model.clocks.removeAll(where:{$0.id==clock.id});model.remember()}
        }
    }
}

struct TroubleshootingView:View {
    private let topics:[(String,String)]=[
        ("Clock not found","Bluetooth-capable firmware must be installed first. Older firmware cannot be discovered here. Use the Mac USB installer or the clock’s existing signed Wi-Fi updater."),
        ("Pair a new phone","Hold left and right together for 3 seconds and release. Enter the fresh code on the clock. Urgent alerts take priority; retry when the clock can show its code."),
        ("Replace a phone or reset pairing","Hold left and right for 10 seconds to remove Bluetooth bonds. Wi-Fi, glucose, alerts, display settings and certificates remain. Also forget SugarClock in iOS Bluetooth Settings before pairing again."),
        ("Reconnect to your clock","Move within a few metres and allow Bluetooth access in iPhone Settings. Keep this app in the foreground. Tap a saved clock after leaving the app or rebooting."),
        ("Wi-Fi or glucose data isn’t working","Use a 2.4 GHz network. A failed trial keeps the previous saved network. Getting an IP address does not confirm internet or provider access—check each status separately."),
        ("Recover older firmware","If an older clock has broken Wi-Fi, use its setup portal or a USB upgrade. Firmware transfer over Bluetooth is not supported.")
    ]
    var body:some View {
        SugarScreen {
            PageHeading(title:"Here to help",subtitle:"Get your SugarClock connected again.",icon:"DiagnosticsIcon")
            ForEach(topics,id:\.0) {topic in
                SugarCard(title:topic.0) {Text(topic.1).font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            }
        }.navigationTitle("Troubleshooting")
    }
}
