import SwiftUI

struct DeviceView: View {
    @EnvironmentObject var model:ClockModel
    @State private var nickname=""
    @State private var polling=true
    private let groups:[(String,[String])]=[
        ("Glucose source",["data_source","dexcom_username","dexcom_password","dexcom_us","server_url","auth_token","poll_interval","stale_timeout_min"]),
        ("Units and glucose ranges",["use_mmol","thresh_urgent_low","thresh_low","thresh_high","thresh_urgent_high"]),
        ("Display",["brightness","auto_brightness","show_delta","default_mode","time_display_enabled","auto_cycle_enabled","auto_cycle_sec"]),
        ("Pixel companions",["ambient_enabled","ambient_creature","ambient_seasonal"]),
        ("Alerts",["alert_enabled","alert_low","alert_high","alert_snooze_min"]),
        ("Time and night mode",["timezone","use_24h","date_on_time_screen","date_format","night_mode_enabled","night_start_hour","night_end_hour","night_brightness"])
    ]
    var body: some View {
        Form {
            Section("This clock") {
                TextField("Nickname",text:$nickname)
                Button("Save nickname on this phone") {
                    if let index=model.clocks.firstIndex(where:{$0.id==model.selected?.id}) {model.clocks[index].nickname=nickname;model.selected=model.clocks[index];model.remember()}
                }
                LabeledContent("Identity",value:model.selected?.id ?? "")
                LabeledContent("Firmware",value:model.hello["firmware"] as? String ?? "Unknown")
            }
            if model.settings["wifi_ssid"] as? String == "" || !(model.status["data_received"] as? Bool ?? false) {
                Section("First setup") {
                    Text("1. Connect the clock to Wi-Fi.\n2. Select and configure its glucose source.\n3. Set units, display, and alerts.\n4. Confirm a reading has arrived below.")
                    Text("Existing saved credentials stay on the clock. Replace them only when needed.").font(.footnote)
                }
            }
            Section {NavigationLink("Wi-Fi") {WiFiView()};NavigationLink("Firmware updates") {FirmwareView()}}
            StatusSection()
            ForEach(groups,id:\.0) {group in
                Section(group.0) {
                    ForEach(group.1,id:\.self) {key in
                        if let field=model.fields.first(where:{$0["key"] as? String==key}) {
                            NavigationLink {SettingEditor(field:field)} label:{LabeledContent(label(key),value:summary(key))}
                        }
                    }
                }
            }
            Section {NavigationLink("Additional supported settings") {
                List(model.fields.indices,id:\.self) {index in
                    let field=model.fields[index]
                    if let key=field["key"] as? String {NavigationLink(label(key)) {SettingEditor(field:field)}}
                }.navigationTitle("All settings")
            }}
            Section {Text(model.message).accessibilityLabel("Operation result: \(model.message)");if model.busy {ProgressView("Working")}}
            Section {Button("Read saved settings and status") {Task {await model.perform {try await model.refresh()}}};NavigationLink("Troubleshooting") {TroubleshootingView()}}
        }.navigationTitle(model.selected?.nickname ?? "Clock").disabled(model.busy)
        .onAppear {nickname=model.selected?.nickname ?? ""}
    }
    private func summary(_ key:String)->String {
        if model.settings[key+"_configured"] as? Bool==true {return "Configured"}
        if let value=model.settings[key] as? Bool, model.fields.first(where:{$0["key"] as? String==key})?["type"] as? String=="bool" {return value ? "On":"Off"}
        if let value=model.settings[key] {return String(describing:value)}
        return "Not configured"
    }
}
func label(_ key:String)->String {
    ["use_mmol":"Use mmol/L","data_source":"Glucose source","dexcom_us":"Dexcom US server","server_url":"URL / Nightscout endpoint","auth_token":"API token","ambient_creature":"Companion","default_mode":"Default screen","wifi_security":"Wi-Fi security","poll_interval":"Poll interval (seconds)"][key] ?? key.replacingOccurrences(of:"_",with:" ").capitalized
}
struct StatusSection: View {
    @EnvironmentObject var model:ClockModel
    var body: some View {
        Section("Connection and data confirmation") {
            LabeledContent("Wi-Fi / DHCP",value:model.status["wifi"] as? String ?? "Unknown")
            LabeledContent("Wi-Fi trial",value:model.status["trial"] as? String ?? "Unknown")
            Text(model.status["trial_detail"] as? String ?? "")
            LabeledContent("Configuration persisted",value:(model.status["configuration_saved"] as? Bool ?? false) ? "Yes":"Unconfirmed — retry or restart")
            LabeledContent("Network saved",value:(model.status["network_saved"] as? Bool ?? false) ? "Yes":"No")
            LabeledContent("Internet DNS",value:probe(model.status["internet_dns"]))
            LabeledContent("Provider reachable",value:probe(model.status["provider_reachable"]))
            LabeledContent("Reading received",value:(model.status["data_received"] as? Bool ?? false) ? "Yes":"Not yet")
            LabeledContent("Provider response",value:String(describing:model.status["provider_http"] ?? "Unknown"))
            if let age=model.status["data_age_ms"] as? Double,age<4_000_000_000 {LabeledContent("Reading age",value:"\(Int(age/1000)) seconds")}
        }
    }
    func probe(_ value:Any?)->String {switch value as? Int {case 1:return "Available";case 2:return "Failed";default:return "Not checked"}}
}
struct SettingEditor: View {
    @EnvironmentObject var model:ClockModel
    let field:[String:Any]
    @State private var text=""
    @State private var toggle=false
    @State private var secretAction=0
    @State private var changed=false
    @State private var validation=""
    var key:String {field["key"] as? String ?? ""}
    var type:String {field["type"] as? String ?? ""}
    var isThreshold:Bool {key.hasPrefix("thresh_") || ["alert_low","alert_high"].contains(key)}
    var mmol:Bool {isThreshold && model.settings["use_mmol"] as? Bool==true}
    var choices:[Int:String]? {
        switch key {
        case "data_source":return [0:"URL / Nightscout",1:"Dexcom Share",2:"Demo (synthetic data)"]
        case "ambient_creature":return [0:"Fish",1:"Ghost"]
        case "default_mode":return [0:"Glucose",1:"Time",2:"Weather",3:"Pixel companion"]
        case "date_format":return [0:"M/DD",1:"MMMDD",2:"DD/MM"]
        default:return nil
        }
    }
    var body: some View {
        Form {
            Section(label(key)) {
                if type=="secret" {
                    Text(model.settings[key+"_configured"] as? Bool==true ? "Configured on the clock":"Not configured")
                    Picker("Action",selection:$secretAction) {Text("Leave unchanged").tag(0);Text("Replace").tag(1);Text("Clear").tag(2)}
                    if secretAction==1 {SecureField("Replacement value",text:$text).textInputAutocapitalization(.never).autocorrectionDisabled()}
                } else if type=="bool" {Toggle(label(key),isOn:$toggle).onChange(of:toggle) {_,_ in changed=true}}
                else if let choices {
                    Picker(label(key),selection:$text) {ForEach(choices.keys.sorted(),id:\.self) {n in Text(choices[n] ?? "").tag(String(n))}}
                        .onChange(of:text) {_,_ in changed=true}
                } else {
                    TextField(mmol ? "mmol/L":label(key),text:$text).textInputAutocapitalization(.never).autocorrectionDisabled()
                        .keyboardType(type=="int" ? .decimalPad:.default).onChange(of:text) {_,_ in changed=true}
                    if let min=field["min"] as? Int,let max=field["max"] as? Int {
                        Text(mmol ? String(format:"%.1f–%.1f mmol/L",Double(min)/18,Double(max)/18) : "Allowed: \(min)–\(max)\(isThreshold ? " mg/dL":"")").font(.footnote)
                    }
                    if key=="timezone" {Text("Use a POSIX time zone, for example EST5EDT,M3.2.0,M11.1.0. This preserves the clock’s existing time-zone format.").font(.footnote)}
                }
                if key=="server_url" {Text("Enter the full JSON endpoint supported by the clock. Include any URL credentials only in this replacement field. The URL is kept on the clock.")}
                if key=="brightness" {Text("Automatic brightness may override this value. Turn it off in Display to use a fixed level.")}
                Button("Save on clock") {save()}.disabled(model.busy || (type=="secret" ? secretAction==0:!changed))
                Text(validation.isEmpty ? model.message:validation)
            }
        }.navigationTitle(label(key)).onAppear {load()}
        .onDisappear {text=""}
    }
    private func load() {
        if type=="bool" {toggle=model.settings[key] as? Bool ?? false}
        else if type != "secret" {
            if mmol,let value=model.settings[key] as? Int {text=String(format:"%.2f",Double(value)/18)}
            else {text=model.settings[key].map{String(describing:$0)} ?? ""}
        }
        // Loading or viewing never sends a patch, including rounded mmol values.
        changed=false
    }
    private func save() {
        var patch:[String:Any]=[:]
        if type=="secret" { (secretAction==2 ? SecretChange.clear:SecretChange.replace(text)).apply(to:&patch,key:key) }
        else if type=="bool" {patch[key]=toggle}
        else if type=="int" {
            guard let n=Double(text),n.isFinite else {validation="Enter a number.";return}
            let converted=mmol ? (n*18).rounded():n
            guard converted>=Double(field["min"] as? Int ?? 0),converted<=Double(field["max"] as? Int ?? Int(Int32.max)),converted.rounded()==converted else {validation="Enter a value within the displayed bounds.";return}
            patch[key]=Int(converted)
        } else {patch[key]=text}
        validation="";Task {await model.save(patch);if type=="secret" {text="";secretAction=0};changed=false}
    }
}
struct WiFiView: View {
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
    var body: some View {
        Form {
            Section("Clock’s radio") {
                Button("Scan 2.4 GHz networks") {Task {await model.scanWiFi()}}
                ForEach(model.networks.indices,id:\.self) {index in
                    let n=model.networks[index]
                    Button("\(n["ssid"] as? String ?? "Hidden") · \(n["rssi"] as? Int ?? 0) dBm") {ssid=n["ssid"] as? String ?? ""}
                }
            }
            Section("Join or replace network") {
                TextField("SSID (including hidden networks)",text:$ssid).textInputAutocapitalization(.never).autocorrectionDisabled()
                Picker("Security",selection:$security) {Text("Personal / open").tag(0);Text("WPA2 Enterprise").tag(1)}
                if security==1 {
                    Picker("EAP method",selection:$eap) {Text("PEAP").tag(0);Text("TTLS").tag(1)}
                    TextField("Identity",text:$identity).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("Anonymous identity (optional)",text:$anonymous).textInputAutocapitalization(.never).autocorrectionDisabled()
                    Toggle("Validate with stored CA certificate",isOn:$validateCA)
                    Text("Existing certificates are preserved. This release supports use of the stored certificate; upload or replace a CA through the existing web settings before joining a network that requires it. EAP-TLS is not supported.").font(.footnote)
                }
                Text(model.settings[(security==1 ? "wifi_eap_password":"wifi_password")+"_configured"] as? Bool==true ? "Password configured":"No saved password")
                Picker("Password action",selection:$secretAction) {Text("Leave unchanged").tag(0);Text("Replace").tag(1);Text("Clear / open network").tag(2)}
                if secretAction==1 {SecureField("New password",text:$password)}
                Button("Test connection, then save") {join()}
                Text("The clock saves this network only after it obtains an IP address. If the trial fails, it retries the previous saved network. Internet and glucose-source results are checked separately.").font(.footnote)
            }
            StatusSection()
            Section {Text(model.message);Button("Refresh connection result") {Task {await model.perform {try await model.refresh()}}}}
        }.navigationTitle("Wi-Fi").disabled(model.busy).onAppear {
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
struct FirmwareView: View {
    @EnvironmentObject var model:ClockModel
    private var ota:[String:Any] {model.status["ota"] as? [String:Any] ?? [:]}
    var body: some View {
        Form {
            Section("Signed Wi-Fi update") {
                Text(model.updateMessage)
                LabeledContent("Current version",value:ota["current_version"] as? String ?? "Unknown")
                LabeledContent("Available version",value:ota["available_version"] as? String ?? "Check for updates")
                LabeledContent("State",value:ota["state"] as? String ?? "Unknown")
                ProgressView(value:Double(ota["progress"] as? Int ?? 0),total:100).accessibilityLabel("Firmware update progress")
                Text(ota["deferral"] as? String ?? "");Text(ota["error"] as? String ?? "")
                Button("Check for update") {Task {await model.command("ota.check")}}
                Button("Install signed update") {Task {await model.command("ota.install")}}
                Button("Refresh update status") {Task {await model.perform {try await model.refresh()}}}
                Text("The clock downloads and verifies firmware over its saved Wi-Fi. It may defer for alerts, low battery, memory, or active timers. Bluetooth may disconnect to release memory. Return to My Clocks and reconnect afterward; verify the current version and validation state below.").font(.footnote)
                LabeledContent("Startup validation",value:ota["pending_verification"] as? Bool==true ? "Pending":"Complete")
                Text("If the clock reconnects with the prior version, the update may have failed or rolled back. Check its error and retry only after resolving the cause.").font(.footnote)
            }
            Section("Automatic updates") {
                ForEach(["auto_update_enabled","auto_update_hour"],id:\.self) {key in
                    if let field=model.fields.first(where:{$0["key"] as? String==key}) {NavigationLink(label(key)) {SettingEditor(field:field)}}
                }
            }
            Section {Text(model.message)}
        }.navigationTitle("Firmware").disabled(model.busy)
    }
}
