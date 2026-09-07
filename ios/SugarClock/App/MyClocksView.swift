import SwiftUI
import CoreBluetooth

struct MyClocksView:View {
    @EnvironmentObject var model:ClockModel
    @State private var showClocks=false
    var body:some View {
        NavigationStack {
            Group {
                if model.selected != nil {DeviceView()}
                else {
                    SugarScreen {
                        PageHeading(title:"Your SugarClock",subtitle:"Make it yours.",icon:"BrandLogo")
                        OperationFeedback()
                        if !model.clocks.isEmpty {
                            SugarCard {
                                ForEach(model.clocks) {clock in SavedClockRow(clock:clock)}
                            }
                        }
                        DiscoveryView(bluetooth:model.bluetooth)
                    }.navigationTitle("SugarClock")
                }
            }
            .toolbar {
                ToolbarItem(placement:.topBarTrailing) {
                    Button {showClocks=true} label:{Image(systemName:"clock.badge.checkmark")}
                        .accessibilityLabel("My Clocks")
                }
            }
        }.id(model.selected?.id).tint(SugarTheme.accent)
        .sheet(isPresented:$showClocks) {
            NavigationStack {
                ClockLibraryView().toolbar {
                    ToolbarItem(placement:.confirmationAction) {Button("Done") {showClocks=false}}
                }
            }.tint(SugarTheme.accent)
        }
    }
}

struct ClockLibraryView:View {
    @EnvironmentObject var model:ClockModel
    @Environment(\.dismiss) private var dismiss
    var body:some View {
        SugarScreen {
            SugarCard {
                ForEach(model.clocks) {clock in
                    Button {
                        dismiss()
                        Task {await model.connect(clock.peripheral)}
                    } label:{DestinationRow(title:clock.nickname,subtitle:clock.id==model.selected?.id ? "Selected":"",symbol:"clock")}
                        .buttonStyle(.plain).disabled(model.busy)
                        .contextMenu {Button("Remove clock",role:.destructive) {model.remove(clock)}}
                }
            }
            NavigationLink {SugarScreen {DiscoveryView(bluetooth:model.bluetooth);OperationFeedback()}.navigationTitle("Add clock")}
                label:{Label("Add clock",systemImage:"plus")}.buttonStyle(SugarButtonStyle(prominent:false))
            NavigationLink {TroubleshootingView()} label:{Label("Help",systemImage:"questionmark.circle")}
        }.navigationTitle("My Clocks")
            .onChange(of:model.sessionReady) {_,ready in if ready {dismiss()}}
    }
}

struct DiscoveryView:View {
    @EnvironmentObject var model:ClockModel
    @ObservedObject var bluetooth:BluetoothTransport
    private var newDevices:[CBPeripheral] {
        bluetooth.devices.filter {device in !model.clocks.contains(where:{$0.peripheral==device.identifier})}
    }
    var body:some View {
        SugarCard(title:"Add a clock") {
            Text("Hold the middle button for 3 seconds, then release. Enter the code on your clock when asked.")
                .font(.subheadline).foregroundStyle(SugarTheme.secondary)
            ForEach(newDevices,id:\.identifier) {device in
                Button {Task {await model.connect(device.identifier)}} label:{DestinationRow(title:device.name ?? "SugarClock",subtitle:"Tap to pair",symbol:"plus.circle")}
                    .buttonStyle(.plain).disabled(model.busy)
            }
            if newDevices.isEmpty {Text(bluetooth.poweredOn ? "No new clocks nearby":"Turn on Bluetooth to find your clock.").font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            Button {bluetooth.scan()} label:{Label("Search nearby",systemImage:"magnifyingglass")}
                .buttonStyle(SugarButtonStyle(prominent:false)).disabled(model.busy)
        }.onAppear {if !model.busy {bluetooth.scan()}}
    }
}

struct SavedClockRow:View {
    @EnvironmentObject var model:ClockModel
    let clock:SavedClock
    var body:some View {
        Button {Task {await model.connect(clock.peripheral)}} label:{DestinationRow(title:clock.nickname,subtitle:"",symbol:"clock")}
            .buttonStyle(.plain).disabled(model.busy)
            .contextMenu {Button("Remove clock",role:.destructive) {model.remove(clock)}}
    }
}

struct TroubleshootingView:View {
    private let topics:[(String,String)]=[
        ("Clock not found","Bluetooth-capable firmware must be installed first. Older firmware cannot be discovered here. Use the Mac USB installer or the clock’s existing signed Wi-Fi updater."),
        ("Pair a new phone","Hold the middle button for 3 seconds, then release. Enter the fresh code on the clock. Urgent alerts take priority; retry when the clock can show its code."),
        ("Replace a phone or reset pairing","Hold the middle button for 10 seconds, then release to remove Bluetooth bonds. Wi-Fi, glucose, alerts, display settings and certificates remain. Also forget SugarClock in iOS Bluetooth Settings before pairing again."),
        ("Reconnect to your clock","Move within a few metres and allow Bluetooth access in iPhone Settings. Open the app near your clock. It reconnects automatically. If needed, tap Retry; you do not need to add it again."),
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
