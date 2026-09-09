import SwiftUI

struct DeviceView:View {
    @EnvironmentObject var model:ClockModel
    private var services:[SettingsCategory] {
        ["glucose","time","companions","weather","pomodoro","stopwatch","countdown","alerts","notifications"]
            .compactMap {id in SettingsCategory.all.first(where:{$0.id==id})}
            .filter {!model.hasLoadedSettings || $0.supported(by:model.fields)}
    }
    private func serviceList(_ title:String,_ items:[SettingsCategory])->some View {
        Group {
            if !items.isEmpty {
                SugarCard(title:title,spacing:8) {
                    ForEach(items) {category in
                        NavigationLink {ConfigurationView(category:category)} label:{DestinationRow(title:category.title,subtitle:"",symbol:category.symbol)}.buttonStyle(.plain)
                        if category.id != items.last?.id {Divider()}
                    }
                }
            }
        }
    }
    var body:some View {
        SugarScreen {
            HStack(spacing:14) {
                BrandIcon(name:"BrandLogo",size:56)
                Text(model.selected?.nickname ?? "SugarClock").font(.title2.bold())
            }
            OperationFeedback()
            if model.hasLoadedSettings {
                serviceList("Enabled services",services.filter {$0.enabled(in:model.settings)==true})
                serviceList("More services",services.filter {$0.enabled(in:model.settings)==false})
                serviceList("Services",services.filter {$0.enabled(in:model.settings)==nil})
            } else {serviceList("Services",services)}
            SugarCard(spacing:10) {
                NavigationLink {ConfigurationView(category:SettingsCategory.all.first(where:{$0.id=="display"})!)} label:{DestinationRow(title:"Display",subtitle:"",symbol:"sun.max")}.buttonStyle(.plain)
                Divider()
                NavigationLink {WiFiView()} label:{DestinationRow(title:"Wi-Fi",subtitle:model.settings["wifi_ssid"] as? String ?? "",symbol:"wifi")}.buttonStyle(.plain)
                Divider()
                NavigationLink {ClockDetailsView()} label:{DestinationRow(title:"Clock settings",subtitle:"",symbol:"gearshape")}.buttonStyle(.plain)
            }
        }.navigationTitle("SugarClock")
    }
}

struct ClockDetailsView:View {
    @EnvironmentObject var model:ClockModel
    @State private var nickname=""
    var body:some View {
        SugarScreen {
            SugarCard(title:"Name") {
                TextField("Clock name",text:$nickname).fieldSurface()
                Button("Save name") {
                    if let index=model.clocks.firstIndex(where:{$0.id==model.selected?.id}) {
                        let name=nickname.trimmingCharacters(in:.whitespacesAndNewlines)
                        if !name.isEmpty {model.clocks[index].nickname=name;model.selected=model.clocks[index];model.remember()}
                    }
                }.buttonStyle(SugarButtonStyle(prominent:false))
            }
            SugarCard {
                NavigationLink {FirmwareView()} label:{DestinationRow(title:"Firmware updates",subtitle:"",symbol:"arrow.down.circle")}.buttonStyle(.plain)
                Divider()
                NavigationLink {DiagnosticsView()} label:{DestinationRow(title:"Connection & data",subtitle:"",symbol:"heart.text.clipboard")}.buttonStyle(.plain)
                Divider()
                if let system=SettingsCategory.all.first(where:{$0.id=="system"}),!model.hasLoadedSettings || system.supported(by:model.fields) {
                    NavigationLink {ConfigurationView(category:system)} label:{DestinationRow(title:system.title,subtitle:"",symbol:system.symbol)}.buttonStyle(.plain)
                    Divider()
                }
                NavigationLink {AllSettingsView()} label:{DestinationRow(title:"Advanced",subtitle:"",symbol:"slider.horizontal.3")}.buttonStyle(.plain)
                Divider()
                NavigationLink {TroubleshootingView()} label:{DestinationRow(title:"Help",subtitle:"",symbol:"questionmark.circle")}.buttonStyle(.plain)
            }
            Text("Firmware \(model.hello["firmware"] as? String ?? "—")").font(.footnote).foregroundStyle(SugarTheme.secondary)
        }.navigationTitle("Clock settings").onAppear {nickname=model.selected?.nickname ?? ""}
    }
}

struct ConfigurationView:View {
    let category:SettingsCategory
    var body:some View {
        SettingsPage(title:category.title,subtitle:["weather","pomodoro","stopwatch","countdown","notifications","system"].contains(category.id) ? category.subtitle:"",sections:category.sections,headerToggleKey:category.enableKey)
            .navigationTitle(category.title)
    }
}
struct SettingEditor:View {
    let field:[String:Any]
    var body:some View {
        SettingsPage(title:label(field["key"] as? String ?? "Setting"),subtitle:"",sections:[("Preference",[field["key"] as? String ?? ""])],overrideFields:[field])
    }
}
struct AllSettingsView:View {
    @EnvironmentObject var model:ClockModel
    private var advancedFields:[[String:Any]] {model.fields.filter {!($0["key"] as? String ?? "").hasPrefix("night_")}}
    var body:some View {
        SugarScreen {
            PageHeading(title:"Additional Settings",subtitle:"Options available on your clock’s firmware.")
            SugarCard {
                ForEach(advancedFields.indices,id:\.self) {index in
                    let field=advancedFields[index]
                    if let key=field["key"] as? String {
                        NavigationLink {SettingEditor(field:field)} label:{DestinationRow(title:label(key),subtitle:"Edit on clock",symbol:"slider.horizontal.3")}.buttonStyle(.plain)
                        if index<advancedFields.count-1 {Divider()}
                    }
                }
            }
        }.navigationTitle("All Settings")
    }
}

func label(_ key:String)->String {
    ["weather_enabled":"Enabled","weather_city":"Location","weather_api_key":"OpenWeather API key","weather_use_f":"Use Fahrenheit","weather_poll_min":"Refresh interval (minutes)","timer_enabled":"Enabled","timer_work_min":"Focus (minutes)","timer_break_min":"Short break (minutes)","timer_long_break_min":"Long break (minutes)","timer_sessions":"Sessions before a long break","timer_buzzer":"Sound","stopwatch_enabled":"Enabled","countdown_enabled":"Enabled","countdown_name":"Event name","countdown_target":"Event date and time","notify_enabled":"Enabled","notify_default_duration":"Default duration (seconds)","notify_allow_buzzer":"Sound for urgent messages","sysmon_enabled":"Enabled","sysmon_label":"Label","sysmon_display_mode":"Display style","sysmon_warn_pct":"Warning (%)","sysmon_crit_pct":"Critical (%)","glucose_enabled":"Blood sugar readings","timezone":"Time zone","use_24h":"24-hour time","date_on_time_screen":"Show date","date_format":"Date format","ambient_enabled":"Enabled","ambient_seasonal":"Seasonal surprises","alert_enabled":"Enabled","time_display_enabled":"Enabled","auto_cycle_enabled":"Auto cycle","auto_cycle_sec":"Seconds per screen","alert_low":"Low glucose alert","alert_high":"High glucose alert","alert_snooze_min":"Snooze (minutes)","use_mmol":"Use mmol/L","data_source":"Source","auto_update_hour":"Update time","auto_update_enabled":"Automatic updates","dexcom_us":"Dexcom US server","server_url":"Server URL","auth_token":"Auth token","ambient_creature":"Pet","default_mode":"Default view","wifi_security":"Wi-Fi security","poll_interval":"Poll interval (seconds)","stale_timeout_min":"Stale timeout (minutes)","show_delta":"Show glucose change (delta)","auto_brightness":"Auto brightness","thresh_urgent_low":"Urgent low","thresh_low":"Low","thresh_high":"High","thresh_urgent_high":"Urgent high"][key] ?? key.replacingOccurrences(of:"_",with:" ").capitalized
}

struct SettingsPage:View {
    @EnvironmentObject var model:ClockModel
    let title:String
    let subtitle:String
    let sections:[(String,[String])]
    var overrideFields:[[String:Any]]?=nil
    var headerToggleKey:String?=nil
    @State private var draft=SettingsDraft()
    @State private var loaded=false
    @State private var validation=""
    @State private var submittedDraft:SettingsDraft?
    private var receipt:SaveReceipt? {model.saveReceipt(for:Set(sections.flatMap{$0.1}))}
    private var saveTitle:String {
        switch receipt?.phase {
        case .saving:return "Saving…"
        case .checking:return "Checking save…"
        case .saved where draft.changed.isEmpty:return "Saved on clock"
        default:return "Save changes"
        }
    }
    private var blockedByBloodSugar:Bool {headerToggleKey=="alert_enabled" && model.settings["glucose_enabled"] as? Bool==false}
    private var confirming:Bool {
        receipt?.phase == .saving || receipt?.phase == .checking
    }
    var fields:[[String:Any]] {
        let keys=Set(sections.flatMap{$0.1})
        return (overrideFields ?? model.fields).filter {keys.contains($0["key"] as? String ?? "")}
    }
    var body:some View {
        SugarScreen {
            if !subtitle.isEmpty {Text(subtitle).font(.subheadline).foregroundStyle(SugarTheme.secondary)}
            OperationFeedback()
            if let key=headerToggleKey,fields.contains(where:{$0["key"] as? String==key}) {
                SugarCard {
                    Toggle(isOn:Binding(get:{!blockedByBloodSugar && (draft.booleans[key] ?? false)},set:{draft.setBool($0,key:key)})) {
                        Text(sections.first?.0 ?? title).font(.headline)
                    }.tint(SugarTheme.accent).disabled(blockedByBloodSugar).accessibilityValue(!blockedByBloodSugar && draft.booleans[key] == true ? "Enabled":"Disabled")
                    if blockedByBloodSugar {Text("Turn on Blood Sugar readings to enable alerts.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
                    if key=="glucose_enabled" {Text("Turning this off stops readings and disables glucose alerts. Your source settings are kept.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
                }
            }
            if headerToggleKey=="glucose_enabled",!fields.contains(where:{$0["key"] as? String=="glucose_enabled"}),!fields.isEmpty {
                Text("Update your clock’s firmware to turn blood sugar readings on or off.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            ForEach(sections,id:\.0) {section in
                let available=section.1.compactMap {key in fields.first(where:{$0["key"] as? String==key})}.filter {field in
                    let key=field["key"] as? String ?? ""
                    if blockedByBloodSugar || (headerToggleKey=="glucose_enabled" && key=="alert_enabled") {return false}
                    if let headerToggleKey,fields.contains(where:{$0["key"] as? String==headerToggleKey}) {
                        if key==headerToggleKey || draft.booleans[headerToggleKey] != true {return false}
                    }
                    if key=="auto_cycle_sec",draft.booleans["auto_cycle_enabled"] == false {return false}
                    if key=="date_format",draft.booleans["date_on_time_screen"] == false {return false}
                    guard sections.flatMap({$0.1}).contains("data_source") else {return true}
                    let source=Int(draft.text["data_source"] ?? "") ?? 0
                    if key.hasPrefix("dexcom_") {return source==1}
                    if ["server_url","auth_token"].contains(key) {return source==0}
                    return true
                }
                if !available.isEmpty {
                    SugarCard(title:headerToggleKey == nil ? section.0:nil) {
                        ForEach(available.indices,id:\.self) {index in
                            DraftField(field:available[index],draft:$draft,settings:model.settings)
                            if index<available.count-1 {Divider()}
                        }
                    }
                }
            }
            if fields.isEmpty {
                if !model.reconnecting {
                    Text(model.fields.isEmpty ? "Connect to load these settings." : "These settings are not supported by the connected firmware.").foregroundStyle(SugarTheme.secondary)
                }
            }
            else {
                VStack(alignment:.leading,spacing:10) {
                    Button {save()} label:{
                        HStack {
                            if case .saved = receipt?.phase,draft.changed.isEmpty {Image(systemName:"checkmark.circle.fill")}
                            Text(saveTitle)
                        }
                    }
                    .buttonStyle(SugarButtonStyle()).disabled(!model.canSend || draft.changed.isEmpty || confirming || blockedByBloodSugar)
                    if let receipt {SaveConfirmation(receipt:receipt)}
                    if !draft.changed.isEmpty {Text("Unsaved changes").font(.caption).foregroundStyle(SugarTheme.secondary)}
                }
            }
            if !validation.isEmpty {Text(validation).font(.subheadline).foregroundStyle(.red).accessibilityLabel("Save error: \(validation)")}
        }.onAppear {
            if !loaded,!fields.isEmpty {draft=SettingsDraft(settings:model.settings,fields:fields);loaded=true}
        }.onChange(of:model.fields.count) { _,_ in
            if !loaded,!fields.isEmpty {draft=SettingsDraft(settings:model.settings,fields:fields);loaded=true}
        }.onChange(of:receipt?.phase) {_,phase in
            if case .saved = phase,let submittedDraft {
                draft.confirm(submitted:submittedDraft,settings:model.settings,fields:fields)
                self.submittedDraft=nil
            }
        }.onDisappear {draft=SettingsDraft();submittedDraft=nil;loaded=false}
    }
    private func save() {
        do {
            let patch=try draft.patch(fields:fields)
            guard !patch.isEmpty else {return}
            validation=""
            let submitted=draft
            submittedDraft=submitted
            Task {
                if await model.save(patch),submittedDraft==submitted {
                    draft.confirm(submitted:submitted,settings:model.settings,fields:fields)
                    submittedDraft=nil
                }
            }
        } catch {validation=error.localizedDescription}
    }
}

struct SaveConfirmation:View {
    let receipt:SaveReceipt
    var body:some View {
        switch receipt.phase {
        case .saving:HStack(spacing:8) {SugarSpinner();Text("Saving…").font(.footnote).foregroundStyle(SugarTheme.secondary)}
        case .checking:
            HStack(spacing:8) {SugarSpinner();Text("Checking save…").font(.footnote).foregroundStyle(SugarTheme.secondary)}
        case .saved(let date):
            Label {Text("Last save confirmed at ") + Text(date,style:.time)} icon:{Image(systemName:"checkmark.circle.fill")}
                .font(.footnote).foregroundStyle(SugarTheme.accent).accessibilityAddTraits(.updatesFrequently)
        case .unconfirmed:
            Text("Couldn't confirm the save. Refresh the connection before retrying.").font(.footnote).foregroundStyle(.orange)
        case .failed(let detail):
            Text(detail).font(.footnote).foregroundStyle(.red)
        }
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
        case "default_mode":return [0:"Glucose",1:"Time",2:"Weather",3:"Pixel Pet"]
        case "sysmon_display_mode":return [0:"Text",1:"Bar"]
        case "auto_update_hour":return ClockUpdateTime.choices
        case "date_format":return [0:"M/DD",1:"MMMDD",2:"DD/MM"]
        default:return nil
        }
    }
    var body:some View {
        VStack(alignment:.leading,spacing:10) {
            if type=="bool" {
                Toggle(label(key),isOn:Binding(get:{draft.booleans[key] ?? false},set:{draft.setBool($0,key:key)})).font(.subheadline).tint(SugarTheme.accent)
                    .disabled(key=="alert_enabled" && settings["glucose_enabled"] as? Bool==false)
                if key=="alert_enabled",settings["glucose_enabled"] as? Bool==false {Text("Turn on Blood Sugar readings to enable alerts.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
            } else {
                Text(label(key)).font(.subheadline.weight(.medium)).foregroundStyle(SugarTheme.secondary)
                if type=="secret" {
                    StatusPill(text:settings[key+"_configured"] as? Bool==true ? "Configured on clock":"Not configured",active:settings[key+"_configured"] as? Bool==true)
                    Picker("Action",selection:Binding(get:{draft.secrets[key] ?? 0},set:{draft.setSecretAction($0,key:key)})) {
                        Text("Leave unchanged").tag(0);Text("Replace").tag(1);Text("Clear").tag(2)
                    }.pickerStyle(.menu).fieldSurface()
                    if draft.secrets[key]==1 {SecureField("Replacement value",text:text).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()}
                    if draft.secrets[key]==2 {Text("This saved value will be cleared when you save.").font(.footnote).foregroundStyle(.red)}
                } else if key=="ambient_creature" {
                    CompanionPicker(value:text,minimum:field["min"] as? Int ?? 0,maximum:field["max"] as? Int ?? 1)
                } else if key=="countdown_target" {
                    CountdownDateField(value:text)
                } else if key=="timezone" {
                    TimeZoneField(value:text)
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
            if key=="weather_city" {Text("City and country, such as London,GB, or a ZIP code.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
            if key=="auto_update_hour" {Text("Uses your clock’s time zone. Updates may wait until the clock is ready.").font(.footnote).foregroundStyle(SugarTheme.secondary)}
            if key=="auto_brightness" {Text("Adjusts brightness to room lighting. The middle button switches to manual brightness.").font(.caption).foregroundStyle(SugarTheme.secondary)}
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
    @State private var enteredManually=false
    @State private var showingNetworks=false
    private var currentSSID:String {model.settings["wifi_ssid"] as? String ?? ""}
    private var wifiConnected:Bool {(model.status["wifi"] as? String)?.uppercased()=="CONNECTED"}
    var body:some View {
        SugarScreen {
            PageHeading(title:"Wi-Fi Configuration",subtitle:"Connect your clock to a 2.4 GHz network.")
            SugarCard(title:"Wi-Fi network") {
                if !currentSSID.isEmpty {
                    DetailRow(title:wifiConnected ? "Connected to":"Saved network",value:currentSSID)
                }
                Button {showingNetworks=true} label:{Label("Choose another network",systemImage:"wifi")}
                    .buttonStyle(SugarButtonStyle(prominent:false))
                Button("Other network…") {enteredManually=true;ssid="";password="";secretAction=1}.font(.subheadline)
            }
            SugarCard(title:"Connect to network") {
                if enteredManually {
                    TextField("Network name",text:$ssid).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
                } else if !ssid.isEmpty {
                    DetailRow(title:"Network",value:ssid)
                } else {
                    Text("Choose a network or enter its name.").foregroundStyle(SugarTheme.secondary)
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
                Button("Test connection, then save") {join()}.buttonStyle(SugarButtonStyle()).disabled(!model.canSend || ssid.isEmpty)
                Text("Saved only after the clock gets an IP address. If the trial fails, it retries your previous network. Internet and glucose access are checked separately.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            StatusSection()
            Button("Refresh connection result") {Task {await model.perform {try await model.refresh()}}}.buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
            OperationFeedback()
        }.navigationTitle("Wi-Fi").onAppear {
            original=model.settings;ssid=original["wifi_ssid"] as? String ?? "";security=original["wifi_security"] as? Int ?? 0;eap=original["wifi_eap_method"] as? Int ?? 0
            identity=original["wifi_identity"] as? String ?? "";anonymous=original["wifi_anon_identity"] as? String ?? "";validateCA=original["wifi_validate_ca"] as? Bool ?? false
        }.sheet(isPresented:$showingNetworks) {
            NearbyNetworksSheet(currentSSID:wifiConnected ? currentSSID:"") {network in
                selectNetwork(network)
                showingNetworks=false
            }
        }.onDisappear {password=""}
    }

    private func selectNetwork(_ network:NearbyNetwork) {
        let changed=ssid != network.ssid || security != (network.enterprise ? 1:0)
        ssid=network.ssid;security=network.enterprise ? 1:0;enteredManually=false
        if changed || network.open {
            password=""
            let originalSecurity=original["wifi_security"] as? Int ?? 0
            secretAction=network.open ? 2:(ssid==original["wifi_ssid"] as? String && security==originalSecurity ? 0:1)
        }
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

struct NearbyNetworksSheet:View {
    @EnvironmentObject var model:ClockModel
    @Environment(\.dismiss) private var dismiss
    let currentSSID:String
    let select:(NearbyNetwork)->Void
    @State private var results:[NearbyNetwork]=[]
    @State private var searched=false
    @State private var message=""
    @State private var searchID=0
    private var available:[NearbyNetwork] {results.filter {$0.ssid != currentSSID}}
    var body:some View {
        NavigationStack {
            SugarScreen {
                Text("Choose a 2.4 GHz network for your clock.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                SugarCard {
                    if model.scanningWiFi {
                        HStack(spacing:8) {SugarSpinner();Text("Finding nearby networks…").font(.subheadline)}
                    } else {
                        ForEach(available) {network in
                            Button {select(network)} label:{
                                HStack(spacing:12) {
                                    Image(systemName:"wifi").foregroundStyle(SugarTheme.accent)
                                    VStack(alignment:.leading,spacing:4) {
                                        Text(network.ssid).foregroundStyle(SugarTheme.text)
                                        Text(network.signal).font(.caption).foregroundStyle(SugarTheme.secondary)
                                    }
                                    Spacer()
                                    if !network.open {Image(systemName:"lock.fill").foregroundStyle(SugarTheme.secondary).accessibilityLabel(network.enterprise ? "Enterprise network":"Secured network")}
                                }.frame(minHeight:44).contentShape(Rectangle())
                            }.buttonStyle(.plain)
                            Divider()
                        }
                        if !message.isEmpty {
                            Text(message).font(.subheadline).foregroundStyle(SugarTheme.secondary)
                        } else if searched,available.isEmpty {
                            Text("No other networks found. Try again or enter a network name on the Wi-Fi page.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                        } else if !model.canSend {
                            Text("Connect to your clock, then tap Search again.").font(.subheadline).foregroundStyle(SugarTheme.secondary)
                        }
                        Button {searchID += 1} label:{Label("Search again",systemImage:"arrow.clockwise")}
                            .buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
                    }
                }
            }.navigationTitle("Nearby networks")
                .toolbar {ToolbarItem(placement:.confirmationAction) {Button("Done") {dismiss()}}}
        }.tint(SugarTheme.accent)
            .presentationDetents([.medium,.large]).presentationDragIndicator(.visible)
            .task(id:searchID) {await search()}
    }
    private func search() async {
        results=[];searched=false;message=""
        guard model.canSend else {return}
        if await model.scanWiFi() {
            results=NearbyNetwork.sorted(model.networks);searched=true
        } else if !Task.isCancelled {
            message=model.wifiScanMessage
        }
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
                if let progress=ota["progress"] as? Int,progress>0 {ProgressView(value:Double(progress),total:100).tint(SugarTheme.accent).accessibilityLabel("Firmware update progress")}
                Button {Task {await model.command("ota.check")}} label:{Label("Check for update",systemImage:"arrow.clockwise")}.buttonStyle(SugarButtonStyle()).disabled(!model.canSend)
                Button("Install signed update") {Task {await model.command("ota.install")}}.buttonStyle(SugarButtonStyle(prominent:false)).disabled(!model.canSend)
            }
            SugarCard(title:"Automatic updates") {
                ForEach(["auto_update_enabled","auto_update_hour"],id:\.self) {key in
                    if let field=model.fields.first(where:{$0["key"] as? String==key}) {
                        NavigationLink {SettingEditor(field:field)} label:{DestinationRow(title:label(key),subtitle:key=="auto_update_hour" ? ClockUpdateTime.choices[model.settings[key] as? Int ?? -1] ?? "Choose a time" : (model.settings[key] as? Bool==true ? "On":"Off"),symbol:"clock.arrow.circlepath")}.buttonStyle(.plain)
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

struct TimeZoneField:View {
    @Binding var value:String
    private var selected:ClockTimeZone? {ClockTimeZone.matching(value)}
    var body:some View {
        Picker("Time zone",selection:$value) {
            if !ClockTimeZone.choices.contains(where:{$0.posix==value}) {
                Text(selected?.name ?? "Custom time zone").tag(value)
            }
            ForEach(ClockTimeZone.choices) {zone in Text(zone.name).tag(zone.posix)}
        }.labelsHidden().pickerStyle(.menu).frame(maxWidth:.infinity,alignment:.leading).fieldSurface().accessibilityLabel("Time zone")
        if let selected {
            Text(selected.observesDaylightSaving ? "Daylight saving time adjusts automatically.":"Uses the same time offset all year.")
                .font(.footnote).foregroundStyle(SugarTheme.secondary)
        }
        DisclosureGroup("Custom time zone") {
            TextField("POSIX rule",text:$value).textInputAutocapitalization(.never).autocorrectionDisabled().fieldSurface()
            Text("For locations not listed. Your existing clock setting is kept until you choose a zone and save.")
                .font(.footnote).foregroundStyle(SugarTheme.secondary)
        }.font(.footnote)
    }
}

struct CountdownDateField:View {
    @Binding var value:String
    private var timestamp:Double {Double(value) ?? 0}
    var body:some View {
        VStack(alignment:.leading,spacing:10) {
            if timestamp>0 {
                DatePicker("Event date and time",selection:Binding(get:{Date(timeIntervalSince1970:timestamp)},set:{value=String(Int($0.timeIntervalSince1970))}),in:Date(timeIntervalSince1970:1)...Date(timeIntervalSince1970:2147483647),displayedComponents:[.date,.hourAndMinute])
                    .labelsHidden().datePickerStyle(.compact).tint(SugarTheme.accent)
                Text("Shown in your phone’s time zone.").font(.caption).foregroundStyle(SugarTheme.secondary)
                Button("Clear date") {value="0"}.font(.subheadline)
            } else {
                Button("Choose date and time") {value=String(Int(Date().addingTimeInterval(86400).timeIntervalSince1970))}
                    .buttonStyle(SugarButtonStyle(prominent:false))
            }
        }
    }
}
