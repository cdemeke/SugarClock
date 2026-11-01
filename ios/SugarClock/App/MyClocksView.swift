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
                        if let network=model.localNetwork {
                            LocalDiscoveryView(network:network) {id in path=[.settings(id)]}
                        }
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
            PageHeading(title:"Your SugarClocks",subtitle:"Choose a clock.",icon:"BrandLogo")
            if !model.clocks.isEmpty {
                SugarCard {
                    ForEach(model.clocks) {clock in
                        let status=model.listStatus(for:clock)
                        Button {openClock(clock)} label:{
                            DestinationRow(title:clock.nickname,subtitle:status?.text ?? "",symbol:"clock",
                                           loading:clock.id==model.selected?.id && (model.syncingSettings || (model.updatingClock && !model.sessionReady)),
                                           status:status?.level)
                        }
                        .buttonStyle(.plain)
                        // Read-only recovery can be interrupted by choosing another clock.
                        .disabled(!model.canChooseAnotherClock && model.selected?.peripheral != clock.peripheral)
                        .contextMenu {Button("Remove clock",role:.destructive) {model.remove(clock)}.disabled(!model.canChooseAnotherClock)}
                    }
                }
            }
            if model.reconnecting,!model.updatingClock {
                Button("Stop syncing") {model.cancelConnection()}
                    .buttonStyle(SugarButtonStyle(prominent:false))
            }
            NavigationLink(value:ClockRoute.add) {Label("Add clock",systemImage:"plus")}
                .buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canChooseAnotherClock)
            NavigationLink {TroubleshootingView()} label:{Label("Help",systemImage:"questionmark.circle")}
        }.navigationTitle("My Clocks")
        // Every clock's status stays current while the list is on screen.
        .task {
            while !Task.isCancelled {
                await model.refreshAvailability()
                try? await Task.sleep(for:.seconds(15))
            }
        }
    }
}

/// Clocks already on this iPhone's Wi-Fi can be added without pairing, using
/// the same local web access as the clock's admin page.
struct LocalDiscoveryView:View {
    @EnvironmentObject var model:ClockModel
    @ObservedObject var network:LocalClockNetwork
    var onConnected:(UUID)->Void={_ in}
    @State private var address=""
    @State private var addressMessage=""
    @State private var checkingAddress=false
    private var newClocks:[LocalClockCandidate] {
        network.candidates.filter {candidate in !model.clocks.contains(where:{$0.id==candidate.identity})}
    }
    private func add(_ candidate:LocalClockCandidate) async {
        await model.addLocalClock(candidate)
        if model.sessionReady,model.selected?.id==candidate.identity,let id=model.selected?.peripheral {onConnected(id)}
    }
    private func search() async {
        let found=await network.discover()
        model.rememberLocalHosts(found)
    }
    var body:some View {
        SugarCard(title:"On your Wi-Fi") {
            Text("Clocks on the same Wi-Fi as this iPhone connect instantly, without pairing.")
                .font(.subheadline).foregroundStyle(SugarTheme.secondary)
            ForEach(newClocks) {candidate in
                Button {Task {await add(candidate)}} label:{
                    DestinationRow(title:candidate.name,subtitle:"\(candidate.host) · Firmware \(candidate.firmware)",symbol:"wifi")
                }.buttonStyle(.plain).disabled(model.busy || model.updatingClock)
            }
            if network.searching {
                HStack(spacing:8) {SugarSpinner();Text("Searching…").font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            } else if network.searched,newClocks.isEmpty {
                Text(network.candidates.isEmpty ? "No clocks found on this Wi-Fi.":"Every clock on this Wi-Fi is already in My Clocks.")
                    .font(.subheadline).foregroundStyle(SugarTheme.secondary)
            }
            Button {Task {await search()}} label:{Label("Search Wi-Fi",systemImage:"magnifyingglass")}
                .buttonStyle(SugarButtonStyle(prominent:false)).disabled(network.searching || model.busy || model.updatingClock)
            DisclosureGroup("Enter the clock’s address") {
                VStack(alignment:.leading,spacing:10) {
                    TextField("192.168.1.50",text:$address).fieldSurface()
                        .keyboardType(.numbersAndPunctuation).textInputAutocapitalization(.never).autocorrectionDisabled()
                        .accessibilityLabel("Clock IP address")
                    Text("Shown on the clock’s web page and in your router. Leave this to search automatically.")
                        .font(.footnote).foregroundStyle(SugarTheme.secondary)
                    Button {Task {
                        guard let host=LocalClockIdentity.normalizedHost(address) else {addressMessage="Enter an address like 192.168.1.50.";return}
                        checkingAddress=true;addressMessage=""
                        defer {checkingAddress=false}
                        guard let candidate=await network.probe(host:host) else {
                            addressMessage="No SugarClock answered at \(host). Check the address and that your iPhone is on the same Wi-Fi."
                            return
                        }
                        await add(candidate)
                    }} label:{
                        HStack(spacing:8) {if checkingAddress {SugarSpinner()};Text("Connect")}
                    }.buttonStyle(SugarButtonStyle()).disabled(checkingAddress || address.isEmpty || model.busy || model.updatingClock)
                    if !addressMessage.isEmpty {Text(addressMessage).font(.footnote).foregroundStyle(SugarTheme.secondary)}
                }.padding(.top,8)
            }.font(.subheadline).tint(SugarTheme.accent)
        }.task {
            // DiscoveryView first stops any unrelated saved-clock recovery.
            if !Task.isCancelled {await search()}
        }
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
        SugarCard(title:"Nearby with Bluetooth") {
            Text("Hold the middle button for 3 seconds, then release. Enter the code on your clock when asked.")
                .font(.subheadline).foregroundStyle(SugarTheme.secondary)
            ForEach(newDevices,id:\.identifier) {device in
                Button {Task {
                    await model.connect(device.identifier)
                    if model.sessionReady,model.selected?.peripheral==device.identifier {onConnected(device.identifier)}
                }} label:{DestinationRow(title:bluetooth.displayName(for:device),subtitle:"Tap to pair",symbol:"plus.circle")}
                    .buttonStyle(.plain).disabled(model.busy || model.updatingClock)
            }
            if newDevices.isEmpty {Text(bluetooth.poweredOn ? "No new clocks nearby":"Turn on Bluetooth to find your clock.").font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            if model.reconnecting {
                Button("Stop syncing") {model.cancelConnection()}
                    .buttonStyle(SugarButtonStyle(prominent:false))
            }
            Button {bluetooth.scan()} label:{Label("Search nearby",systemImage:"magnifyingglass")}
                .buttonStyle(SugarButtonStyle(prominent:false)).disabled(model.busy || model.updatingClock)
        }.task {
            if await model.prepareToAddClock(),!Task.isCancelled {bluetooth.scan()}
        }.onDisappear {
            // Successful pairing navigates to settings with its connection intact.
            if !model.sessionReady {model.cancelConnection()}
        }
    }
}

struct TroubleshootingView:View {
    private let topics:[(String,String)]=[
        ("Edit while the clock is offline","Connect once to load a clock’s settings. You can then edit across screens, review pending changes and tap Update clock when ready. Keep the app open near the clock while it connects. Cancelling or closing the app before sending keeps your edits for later; they are never sent automatically when you reopen."),
        ("Clock not found","On the same Wi-Fi as your clock, open Add clock: clocks on your network appear under On your Wi-Fi, or enter the clock’s IP address. Any SugarClock firmware works over Wi-Fi. Bluetooth needs Bluetooth-capable firmware and is used when your iPhone isn’t on the clock’s network."),
        ("Pair a new phone","Hold the middle button for 3 seconds, then release. Enter the fresh code on the clock. Urgent alerts take priority; retry when the clock can show its code."),
        ("Replace a phone or fix pairing","Forget SugarClock in iOS Bluetooth Settings, then hold the middle button for 3 seconds, release and pair again. A clock remembers up to four phones; pairing another replaces the one that has gone longest without connecting. Wi-Fi, glucose, alerts and display settings remain."),
        ("Reconnect to your clock","The app connects over Wi-Fi when your iPhone is on the clock’s network, and otherwise over Bluetooth within a few metres. Allow Local Network and Bluetooth access in iPhone Settings → SugarClock. If the clock’s address changes, the app finds it again automatically. If needed, tap Retry; you do not need to add it again."),
        ("Wi-Fi or glucose data isn’t working","Use a 2.4 GHz network. A failed trial keeps the previous saved network. Getting an IP address does not confirm internet or provider access—check each status separately."),
        ("Recover older firmware","If an older clock has broken Wi-Fi, use its setup portal or a USB upgrade. Firmware transfer over Bluetooth is not supported.")
    ]
    var body:some View {
        SugarScreen {
            PageHeading(title:"Here to help",subtitle:"Get your SugarClock connected again.",icon:"DiagnosticsIcon")
            SugarCard(title:"What is SugarClock?") {
                Text("SugarClock is a smart pixel clock that keeps your blood sugar readings visible at a glance. Connect it to Dexcom or Nightscout, and personalize it with time, weather and Pixel Pets.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                Link("Learn more at SugarClock.com",destination:URL(string:"https://sugarclock.com")!)
            }
            ForEach(topics,id:\.0) {topic in
                SugarCard(title:topic.0) {Text(topic.1).font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            }
        }.navigationTitle("Help")
    }
}
