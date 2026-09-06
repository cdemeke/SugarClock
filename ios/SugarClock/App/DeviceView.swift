import SwiftUI

struct SettingsCategory:Identifiable {
    let id:String
    let title:String
    let subtitle:String
    let symbol:String
    let sections:[(String,[String])]
    static let all:[SettingsCategory]=[
        .init(id:"glucose",title:"Blood Sugar",subtitle:"Source, credentials and glucose ranges",symbol:"drop",sections:[
            ("Data source",["data_source","dexcom_username","dexcom_password","dexcom_us","server_url","auth_token","poll_interval","stale_timeout_min"]),
            ("Units and ranges",["use_mmol","thresh_urgent_low","thresh_low","thresh_high","thresh_urgent_high"])]),
        .init(id:"display",title:"Display",subtitle:"Brightness and what your clock shows",symbol:"sun.max",sections:[
            ("Brightness",["brightness","default_mode","auto_brightness"]),
            ("Reading display",["show_delta","time_display_enabled","auto_cycle_enabled","auto_cycle_sec"])]),
        .init(id:"time",title:"Time & Night Mode",subtitle:"Time zone, format and quiet evenings",symbol:"moon.stars",sections:[
            ("Time",["timezone","use_24h","date_on_time_screen","date_format"]),
            ("Night mode",["night_mode_enabled","night_start_hour","night_end_hour","night_brightness"])]),
        .init(id:"alerts",title:"Alerts",subtitle:"Thresholds and snooze preferences",symbol:"bell",sections:[
            ("Glucose alerts",["alert_enabled","alert_low","alert_high","alert_snooze_min"])]),
        .init(id:"companions",title:"Pixel Companions",subtitle:"A little company on your display",symbol:"sparkles",sections:[
            ("Your companion",["ambient_enabled","ambient_creature","ambient_seasonal"])])
    ]
}

struct DeviceView:View {
    @EnvironmentObject var model:ClockModel
    @State private var nickname=""
    var body:some View {
        SugarScreen {
            PageHeading(title:"Configuration",subtitle:"Tune what your SugarClock shows, sounds and connects to.")
            OperationFeedback()
            SugarCard {
                HStack(spacing:14) {
                    BrandIcon(name:"BrandLogo",size:48)
                    VStack(alignment:.leading,spacing:5) {
                        Text(model.selected?.nickname ?? "SugarClock").font(.headline)
                        Text("Firmware \(model.hello["firmware"] as? String ?? "Unknown")").font(.caption).foregroundStyle(SugarTheme.secondary)
                    }
                    Spacer()
                }
                DisclosureGroup("Clock details & nickname") {
                    VStack(alignment:.leading,spacing:16) {
                        TextField("Nickname",text:$nickname).fieldSurface()
                        Button("Save nickname on this phone") {
                            if let index=model.clocks.firstIndex(where:{$0.id==model.selected?.id}) {
                                model.clocks[index].nickname=nickname;model.selected=model.clocks[index];model.remember()
                            }
                        }.buttonStyle(SugarButtonStyle(prominent:false))
                        DetailRow(title:"Identity",value:model.selected?.id ?? "Unknown")
                    }.padding(.top,12)
                }.font(.subheadline)
            }
            if model.settings["wifi_ssid"] as? String=="" || model.status["data_received"] as? Bool != true {
                SugarCard(title:"Finish setting up") {
                    Text("Connect Wi-Fi, choose your glucose source, then set units and alerts. Confirm a reading has arrived in Connection & Data below.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                    Text("Existing saved credentials stay on your clock.").font(.footnote).foregroundStyle(SugarTheme.accent)
                }
            }
            SugarCard(title:"Make it yours") {
                ForEach(SettingsCategory.all) {category in
                    NavigationLink {ConfigurationView(category:category)} label:{DestinationRow(title:category.title,subtitle:category.subtitle,symbol:category.symbol)}.buttonStyle(.plain)
                    if category.id != SettingsCategory.all.last?.id {Divider()}
                }
            }
            SugarCard(title:"Connections & device") {
                NavigationLink {WiFiView()} label:{DestinationRow(title:"Wi-Fi",subtitle:model.settings["wifi_ssid"] as? String ?? "Connect to a network",symbol:"wifi")}.buttonStyle(.plain)
                Divider()
                NavigationLink {FirmwareView()} label:{DestinationRow(title:"Firmware Updates",subtitle:"Signed updates over your clock’s Wi-Fi",symbol:"arrow.down.circle")}.buttonStyle(.plain)
                Divider()
                NavigationLink {DiagnosticsView()} label:{DestinationRow(title:"Connection & Data",subtitle:"Network, saved settings and readings",symbol:"heart.text.clipboard")}.buttonStyle(.plain)
                Divider()
                NavigationLink {AllSettingsView()} label:{DestinationRow(title:"Additional Settings",subtitle:"Every option supported by this firmware",symbol:"slider.horizontal.3")}.buttonStyle(.plain)
            }
        }.navigationTitle(model.selected?.nickname ?? "Clock")
            .onAppear {nickname=model.selected?.nickname ?? ""}
    }
}

struct ConfigurationView:View {
    let category:SettingsCategory
    var body:some View {
        SettingsPage(title:category.title+" Configuration",subtitle:category.subtitle,sections:category.sections)
            .navigationTitle(category.title)
    }
}
struct SettingEditor:View {
    let field:[String:Any]
    var body:some View {
        SettingsPage(title:label(field["key"] as? String ?? "Setting"),subtitle:"A small adjustment, saved on your clock.",sections:[("Preference",[field["key"] as? String ?? ""])],overrideFields:[field])
    }
}
struct AllSettingsView:View {
    @EnvironmentObject var model:ClockModel
    var body:some View {
        SugarScreen {
            PageHeading(title:"Additional Settings",subtitle:"Options available on your clock’s firmware.")
            SugarCard {
                ForEach(model.fields.indices,id:\.self) {index in
                    let field=model.fields[index]
                    if let key=field["key"] as? String {
                        NavigationLink {SettingEditor(field:field)} label:{DestinationRow(title:label(key),subtitle:"Edit on clock",symbol:"slider.horizontal.3")}.buttonStyle(.plain)
                        if index<model.fields.count-1 {Divider()}
                    }
                }
            }
        }.navigationTitle("All Settings")
    }
}

func label(_ key:String)->String {
    ["use_mmol":"Use mmol/L","data_source":"Source type","dexcom_us":"Dexcom US server","server_url":"Server URL","auth_token":"Auth token","ambient_creature":"Companion","default_mode":"Default view","wifi_security":"Wi-Fi security","poll_interval":"Poll interval (seconds)","stale_timeout_min":"Stale timeout (minutes)","show_delta":"Show delta on display","auto_brightness":"Auto brightness","thresh_urgent_low":"Urgent low","thresh_low":"Low","thresh_high":"High","thresh_urgent_high":"Urgent high"][key] ?? key.replacingOccurrences(of:"_",with:" ").capitalized
}

struct SettingsPage:View {
    @EnvironmentObject var model:ClockModel
    let title:String
    let subtitle:String
    let sections:[(String,[String])]
    var overrideFields:[[String:Any]]?=nil
    @State private var draft=SettingsDraft()
    @State private var loaded=false
    @State private var validation=""
    var fields:[[String:Any]] {
        let keys=Set(sections.flatMap{$0.1})
        return (overrideFields ?? model.fields).filter {keys.contains($0["key"] as? String ?? "")}
    }
    var body:some View {
        SugarScreen {
            PageHeading(title:title,subtitle:subtitle)
            OperationFeedback()
            ForEach(sections,id:\.0) {section in
                let available=section.1.compactMap {key in fields.first(where:{$0["key"] as? String==key})}.filter {field in
                    guard sections.flatMap({$0.1}).contains("data_source") else {return true}
                    let key=field["key"] as? String ?? ""
                    let source=Int(draft.text["data_source"] ?? "") ?? 0
                    if key.hasPrefix("dexcom_") {return source==1}
                    if ["server_url","auth_token"].contains(key) {return source==0}
                    return true
                }
                if !available.isEmpty {
                    SugarCard(title:section.0) {
                        ForEach(available.indices,id:\.self) {index in
                            DraftField(field:available[index],draft:$draft,settings:model.settings)
                            if index<available.count-1 {Divider()}
                        }
                    }
                }
            }
            if fields.isEmpty {Text(model.fields.isEmpty ? "Settings have not finished loading. Reconnect to load them before editing." : "These settings are not supported by the connected firmware.").foregroundStyle(SugarTheme.secondary)}
            else {
                Button {save()} label:{Label("Save on clock",systemImage:"checkmark")}
                    .buttonStyle(SugarButtonStyle()).disabled(!model.canSend || draft.changed.isEmpty)
                Text("Only your changes are saved. Existing credentials stay on the clock.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            if !validation.isEmpty {Text(validation).font(.subheadline).foregroundStyle(.red).accessibilityLabel("Save error: \(validation)")}
        }.onAppear {
            if !loaded,!fields.isEmpty {draft=SettingsDraft(settings:model.settings,fields:fields);loaded=true}
        }.onChange(of:model.fields.count) { _,_ in
            if !loaded,!fields.isEmpty {draft=SettingsDraft(settings:model.settings,fields:fields);loaded=true}
        }.onDisappear {draft=SettingsDraft();loaded=false}
    }
    private func save() {
        do {
            let patch=try draft.patch(fields:fields)
            guard !patch.isEmpty else {return}
            validation=""
            Task {
                if await model.save(patch) {draft=SettingsDraft(settings:model.settings,fields:fields)}
                else {validation=model.message}
            }
        } catch {validation=error.localizedDescription}
    }
}

struct DraftField:View {
    let field:[String:Any]
    @Binding var draft:SettingsDraft
    let settings:[String:Any]
    var key:String {field["key"] as? String ?? ""}
    var type:String {field["type"] as? String ?? ""}
    var text:Binding<String> {Binding(get:{draft.text[key] ?? ""},set:{draft.setText($0,key:key)})}
    var choices:[Int:String]? {
        switch key {
        case "data_source":return [0:"Custom URL / Nightscout",1:"Dexcom Share",2:"Demo (synthetic data)"]
        case "ambient_creature":return [0:"Fish",1:"Ghost"]
        case "default_mode":return [0:"Glucose",1:"Time",2:"Weather",3:"Pixel companion"]
        case "date_format":return [0:"M/DD",1:"MMMDD",2:"DD/MM"]
        default:return nil
        }
    }
    var body:some View {
        VStack(alignment:.leading,spacing:10) {
            if type=="bool" {
                Toggle(label(key),isOn:Binding(get:{draft.booleans[key] ?? false},set:{draft.setBool($0,key:key)})).font(.subheadline).tint(SugarTheme.accent)
            } else {
                Text(label(key)).font(.subheadline.weight(.medium)).foregroundStyle(SugarTheme.secondary)
                if type=="secret" {
                    StatusPill(text:settings[key+"_configured"] as? Bool==true ? "Configured on clock":"Not configured",active:settings[key+"_configured"] as? Bool==true)
                    Picker("Action",selection:Binding(get:{draft.secrets[key] ?? 0},set:{draft.setSecretAction($0,key:key)})) {
                        Text("Leave unchanged").tag(0);Text("Replace").tag(1);Text("Clear").tag(2)
                    }.pickerStyle(.menu).fieldSurface()
                    if draft.secrets[key]==1 {SecureField("Replacement value",text:text).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()}
                    if draft.secrets[key]==2 {Text("This saved value will be cleared when you save.").font(.footnote).foregroundStyle(.red)}
                } else if let choices {
                    Picker(label(key),selection:text) {ForEach(choices.keys.sorted(),id:\.self) {value in Text(choices[value] ?? "").tag(String(value))}}
                        .labelsHidden().pickerStyle(.menu).frame(maxWidth:.infinity,alignment:.leading).fieldSurface().accessibilityLabel(label(key))
                } else {
                    TextField(draft.mmol(key) ? "mmol/L":label(key),text:text)
                        .keyboardType(type=="int" ? .decimalPad:.default).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
                    if key=="brightness" {
                        Slider(value:Binding(get:{Double(draft.text[key] ?? "") ?? 40},set:{draft.setText(String(Int($0)),key:key)}),in:Double(field["min"] as? Int ?? 1)...Double(field["max"] as? Int ?? 255),step:1)
                            .accessibilityLabel("Brightness")
                    }
                    if let min=field["min"] as? Int,let max=field["max"] as? Int {
                        Text(draft.mmol(key) ? String(format:"%.1f–%.1f mmol/L",Double(min)/18,Double(max)/18):"Allowed: \(min)–\(max)\(SettingsDraft.threshold(key) ? " mg/dL":"")")
                            .font(.caption).foregroundStyle(SugarTheme.secondary)
                    }
                }
            }
            if key=="timezone" {Text("POSIX format, for example EST5EDT,M3.2.0,M11.1.0.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
            if key=="brightness" {Text("Turn off auto brightness to use a fixed level.").font(.caption).foregroundStyle(SugarTheme.secondary)}
            if key=="server_url" {Text("Use the full JSON endpoint. The URL and any credentials stay on your clock.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
        }
    }
}

struct StatusSection:View {
    @EnvironmentObject var model:ClockModel
    var body:some View {
        SugarCard(title:"Connection & data confirmation") {
            DetailRow(title:"Wi-Fi / DHCP",value:model.status["wifi"] as? String ?? "Unknown")
            Divider()
            DetailRow(title:"Wi-Fi trial",value:model.status["trial"] as? String ?? "Unknown")
            if let detail=model.status["trial_detail"] as? String,!detail.isEmpty {Text(detail).font(.footnote).foregroundStyle(SugarTheme.secondary)}
            DetailRow(title:"Configuration persisted",value:model.status["configuration_saved"] as? Bool==true ? "Yes":"Unconfirmed")
            DetailRow(title:"Network saved",value:model.status["network_saved"] as? Bool==true ? "Yes":"No")
            Divider()
            DetailRow(title:"Internet DNS",value:probe(model.status["internet_dns"]))
            DetailRow(title:"Provider reachable",value:probe(model.status["provider_reachable"]))
            DetailRow(title:"Reading received",value:model.status["data_received"] as? Bool==true ? "Yes":"Not yet")
            DetailRow(title:"Provider response",value:String(describing:model.status["provider_http"] ?? "Unknown"))
            if let age=model.status["data_age_ms"] as? Double,age<4_000_000_000 {DetailRow(title:"Reading age",value:"\(Int(age/1000)) seconds")}
        }
    }
    func probe(_ value:Any?)->String {switch value as? Int {case 1:return "Available";case 2:return "Failed";default:return "Not checked"}}
}
struct DiagnosticsView:View {
    @EnvironmentObject var model:ClockModel
    var body:some View {
        SugarScreen {
            PageHeading(title:"Connection & Data",subtitle:"Know what’s connected, saved and up to date.",icon:"DiagnosticsIcon")
            StatusSection()
            Button("Refresh status") {Task {await model.perform {try await model.refresh()}}}.buttonStyle(SugarButtonStyle()).disabled(!model.canSend)
            NavigationLink("Troubleshooting") {TroubleshootingView()}.buttonStyle(SugarButtonStyle(prominent:false))
            OperationFeedback()
        }.navigationTitle("Diagnostics")
    }
}

struct WiFiView:View {
    @EnvironmentObject var model:ClockModel
    @State private var ssid=""
    @State private var password=""
    @State private var identity=""
    @State private var anonymous=""
    @State private var security=0
    @State private var eap=0
    @State private var secretAction=0
    @State private var validateCA=false
    @State private var original:[String:Any]=[:]
    var body:some View {
        SugarScreen {
            PageHeading(title:"Wi-Fi Configuration",subtitle:"Connect your clock to a 2.4 GHz network.")
            SugarCard(title:"Nearby networks") {
                ForEach(model.networks.indices,id:\.self) {index in
                    let n=model.networks[index]
                    Button {ssid=n["ssid"] as? String ?? ""} label:{
                        HStack {
                            Image(systemName:"wifi").foregroundStyle(SugarTheme.accent)
                            Text(n["ssid"] as? String ?? "Hidden network").foregroundStyle(SugarTheme.text)
                            Spacer()
                            Text("\(n["rssi"] as? Int ?? 0) dBm").font(.caption).foregroundStyle(SugarTheme.secondary)
                        }.frame(minHeight:44)
                    }.buttonStyle(.plain)
                    Divider()
                }
                Button {Task {await model.scanWiFi()}} label:{Label("Scan using clock",systemImage:"arrow.clockwise")}.buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
            }
            SugarCard(title:"Join or replace network") {
                VStack(alignment:.leading,spacing:8) {
                    Text("Network name").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                    TextField("SSID, including hidden networks",text:$ssid).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
                }
                Picker("Security",selection:$security) {Text("Personal / open").tag(0);Text("WPA2 Enterprise").tag(1)}.fieldSurface()
                if security==1 {
                    Picker("EAP method",selection:$eap) {Text("PEAP").tag(0);Text("TTLS").tag(1)}.fieldSurface()
                    TextField("Identity",text:$identity).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
                    TextField("Anonymous identity (optional)",text:$anonymous).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
                    Toggle("Validate with stored CA certificate",isOn:$validateCA)
                    Text("Existing certificates are preserved. Upload or replace a CA through web settings before joining a network that requires it. EAP-TLS is not supported.").font(.footnote).foregroundStyle(SugarTheme.secondary)
                }
                StatusPill(text:model.settings[(security==1 ? "wifi_eap_password":"wifi_password")+"_configured"] as? Bool==true ? "Password configured":"No saved password")
                Picker("Password",selection:$secretAction) {Text("Leave unchanged").tag(0);Text("Replace").tag(1);Text("Clear / open network").tag(2)}.fieldSurface()
                if secretAction==1 {SecureField("New password",text:$password).fieldSurface()}
                Button("Test connection, then save") {join()}.buttonStyle(SugarButtonStyle()).disabled(!model.canSend)
                Text("Saved only after the clock gets an IP address. If the trial fails, it retries your previous network. Internet and glucose access are checked separately.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            StatusSection()
            Button("Refresh connection result") {Task {await model.perform {try await model.refresh()}}}.buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
            OperationFeedback()
        }.navigationTitle("Wi-Fi").onAppear {
            original=model.settings;ssid=original["wifi_ssid"] as? String ?? "";security=original["wifi_security"] as? Int ?? 0;eap=original["wifi_eap_method"] as? Int ?? 0
            identity=original["wifi_identity"] as? String ?? "";anonymous=original["wifi_anon_identity"] as? String ?? "";validateCA=original["wifi_validate_ca"] as? Bool ?? false
        }.onDisappear {password=""}
    }
    func join() {
        var patch:[String:Any]=["wifi_ssid":ssid]
        if security != original["wifi_security"] as? Int {patch["wifi_security"]=security}
        if security==1 {
            if eap != original["wifi_eap_method"] as? Int {patch["wifi_eap_method"]=eap}
            if identity != original["wifi_identity"] as? String {patch["wifi_identity"]=identity}
            if anonymous != original["wifi_anon_identity"] as? String {patch["wifi_anon_identity"]=anonymous}
            if validateCA != original["wifi_validate_ca"] as? Bool {patch["wifi_validate_ca"]=validateCA}
        }
        let action:SecretChange=secretAction==0 ? .unchanged:secretAction==1 ? .replace(password):.clear
        action.apply(to:&patch,key:security==1 ? "wifi_eap_password":"wifi_password")
        Task {await model.command("wifi.trial",fields:["patch":patch]);password="";secretAction=0}
    }
}

struct FirmwareView:View {
    @EnvironmentObject var model:ClockModel
    private var ota:[String:Any] {model.status["ota"] as? [String:Any] ?? [:]}
    var body:some View {
        SugarScreen {
            PageHeading(title:"Firmware Updates",subtitle:"Keep your clock ready for what’s next.",icon:"DashboardIcon")
            SugarCard(title:"Installed firmware") {
                HStack(alignment:.firstTextBaseline) {
                    Text(ota["current_version"] as? String ?? "Unknown").font(.largeTitle.weight(.semibold))
                    Spacer()
                    StatusPill(text:ota["state"] as? String ?? "Unknown",active:ota["state"] as? String=="idle")
                }
                DetailRow(title:"Available version",value:ota["available_version"] as? String ?? "Check for updates")
                DetailRow(title:"Startup validation",value:ota["pending_verification"] as? Bool==true ? "Pending":"Complete")
                if !model.updateMessage.isEmpty {Text(model.updateMessage).font(.subheadline).foregroundStyle(SugarTheme.secondary)}
                if let reason=ota["deferral"] as? String,!reason.isEmpty {Text(reason).font(.subheadline).foregroundStyle(.orange)}
                if let error=ota["error"] as? String,!error.isEmpty {Text(error).font(.subheadline).foregroundStyle(.red)}
                if let progress=ota["progress"] as? Int,progress>0 {ProgressView(value:Double(progress),total:100).accessibilityLabel("Firmware update progress")}
                Button {Task {await model.command("ota.check")}} label:{Label("Check for update",systemImage:"arrow.clockwise")}.buttonStyle(SugarButtonStyle()).disabled(!model.canSend)
                Button("Install signed update") {Task {await model.command("ota.install")}}.buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
            }
            SugarCard(title:"Automatic updates") {
                ForEach(["auto_update_enabled","auto_update_hour"],id:\.self) {key in
                    if let field=model.fields.first(where:{$0["key"] as? String==key}) {
                        NavigationLink {SettingEditor(field:field)} label:{DestinationRow(title:label(key),subtitle:"Edit preference",symbol:"clock.arrow.circlepath")}.buttonStyle(.plain)
                    }
                }
            }
            SugarCard {
                Label("Verified on your clock",systemImage:"checkmark.shield").font(.headline).foregroundStyle(SugarTheme.accent)
                Text("Firmware is downloaded and verified over saved Wi-Fi. Updates may wait for alerts, low battery, memory or active timers. Bluetooth can disconnect temporarily; reconnect to confirm the version and startup validation.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                Text("If the previous version returns, check the error: the update may have failed or rolled back.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            Button("Refresh update status") {Task {await model.perform {try await model.refresh()}}}.buttonStyle(SugarButtonStyle(prominent:false))
            OperationFeedback()
        }.navigationTitle("Firmware")
    }
}
