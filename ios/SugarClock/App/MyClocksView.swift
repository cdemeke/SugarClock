import SwiftUI
import CoreBluetooth

private enum ClockRoute:Hashable {
    case add
    case settings(UUID)
}

struct MyClocksView:View {
    @EnvironmentObject var model:ClockModel
    @State private var path:[ClockRoute]=[]
    var body:some View {
        NavigationStack(path:$path) {
            ClockLibraryView {clock in
                path=[.settings(clock.peripheral)]
                Task {await model.connect(clock.peripheral)}
            }
            .navigationDestination(for:ClockRoute.self) {route in
                switch route {
                case .add:
                    SugarScreen {
                        DiscoveryView(bluetooth:model.bluetooth) {id in path=[.settings(id)]}
                        OperationFeedback()
                    }.navigationTitle("Add clock")
                case .settings(let id):
                    // A clock switch starts asynchronously; never show the previous clock's settings.
                    if model.selected?.peripheral==id {DeviceView()}
                    else {SugarScreen {HStack(spacing:8) {SugarSpinner();Text("Connecting…")}}.navigationTitle("SugarClock")}
                }
            }
        }.tint(SugarTheme.accent)
    }
}

struct ClockLibraryView:View {
    @EnvironmentObject var model:ClockModel
    let openClock:(SavedClock)->Void
    var body:some View {
        SugarScreen {
            PageHeading(title:"Your SugarClocks",subtitle:"Choose a clock to make it yours.",icon:"BrandLogo")
            if !model.clocks.isEmpty {
                SugarCard {
                    ForEach(model.clocks) {clock in
                        Button {openClock(clock)} label:{
                            DestinationRow(title:clock.nickname,subtitle:clock.id==model.selected?.id ? model.connectionSummary:"",symbol:"clock",loading:clock.id==model.selected?.id && (model.reconnecting || (model.updatingClock && !model.sessionReady)))
                        }
                        .buttonStyle(.plain)
                        // Opening the clock already connecting must remain available.
                        .disabled((model.busy || model.updatingClock) && model.selected?.peripheral != clock.peripheral)
                        .contextMenu {Button("Remove clock",role:.destructive) {model.remove(clock)}}
                    }
                }
            }
            NavigationLink(value:ClockRoute.add) {Label("Add clock",systemImage:"plus")}
                .buttonStyle(SugarButtonStyle(prominent:false))
            NavigationLink {TroubleshootingView()} label:{Label("Help",systemImage:"questionmark.circle")}
        }.navigationTitle("My Clocks")
    }
}

struct DiscoveryView:View {
    @EnvironmentObject var model:ClockModel
    @ObservedObject var bluetooth:BluetoothTransport
    var onConnected:(UUID)->Void={_ in}
    private var newDevices:[CBPeripheral] {
        bluetooth.devices.filter {device in !model.clocks.contains(where:{$0.peripheral==device.identifier})}
    }
    var body:some View {
        SugarCard(title:"Add a clock") {
            Text("Hold the middle button for 3 seconds, then release. Enter the code on your clock when asked.")
                .font(.subheadline).foregroundStyle(SugarTheme.secondary)
            ForEach(newDevices,id:\.identifier) {device in
                Button {Task {
                    await model.connect(device.identifier)
                    if model.sessionReady,model.selected?.peripheral==device.identifier {onConnected(device.identifier)}
                }} label:{DestinationRow(title:device.name ?? "SugarClock",subtitle:"Tap to pair",symbol:"plus.circle")}
                    .buttonStyle(.plain).disabled(model.busy || model.updatingClock)
            }
            if newDevices.isEmpty {Text(bluetooth.poweredOn ? "No new clocks nearby":"Turn on Bluetooth to find your clock.").font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            Button {bluetooth.scan()} label:{Label("Search nearby",systemImage:"magnifyingglass")}
                .buttonStyle(SugarButtonStyle(prominent:false)).disabled(model.busy || model.updatingClock)
        }.onAppear {if !model.busy {bluetooth.scan()}}
    }
}

struct TroubleshootingView:View {
    private let topics:[(String,String)]=[
        ("Clock not found","Bluetooth-capable firmware must be installed first. Older firmware cannot be discovered here. Use the Mac USB installer or the clock’s existing signed Wi-Fi updater."),
        ("Pair a new phone","Hold the middle button for 3 seconds, then release. Enter the fresh code on the clock. Urgent alerts take priority; retry when the clock can show its code."),
        ("Replace a phone or reset pairing","Hold the middle button for 10 seconds, then release to remove Bluetooth bonds. Wi-Fi, glucose, alerts, display settings and certificates remain. Also forget SugarClock in iOS Bluetooth Settings before pairing again."),
        ("Reconnect to your clock","Move within a few metres and allow Bluetooth access in iPhone Settings. Open the app near your clock and select it from My Clocks. If you have one saved clock, it starts connecting automatically. If needed, tap Retry; you do not need to add it again."),
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
