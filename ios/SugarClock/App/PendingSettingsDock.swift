import SwiftUI

/// The inset reserves space for the final field and follows the keyboard.
/// Each settings screen owns its sheet; navigation never sends or drops edits.
struct ClockSettingsDock:ViewModifier {
    @EnvironmentObject var model:ClockModel
    let enabled:Bool
    @State private var reviewing=false
    private var needsAttention:Bool {
        if !model.draftStorageMessage.isEmpty {return true}
        guard let id=model.selected?.id,let receipt=model.saveReceipts[id] else {return false}
        switch receipt.phase {case .unconfirmed,.failed:return true;default:return false}
    }
    func body(content:Content)->some View {
        content.safeAreaInset(edge:.bottom,spacing:0) {
            if enabled,model.selected != nil,
               model.pendingChangeCount>0 || model.settingsUpdatePhase != .idle || needsAttention {
                PendingSettingsBar(review:{reviewing=true})
                    .frame(maxWidth:720).padding(.horizontal,16).padding(.vertical,10)
            }
        }
        #if DEBUG
        .onAppear {
            if enabled,ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"]=="pending-sheet" {reviewing=true}
        }
        #endif
        .sheet(isPresented:$reviewing) {
            NavigationStack {PendingSettingsReview()}
                .presentationDetents([.large]).presentationDragIndicator(.visible)
        }
    }
}

struct PendingSettingsBar:View {
    @EnvironmentObject var model:ClockModel
    @Environment(\.dynamicTypeSize) private var typeSize
    let review:()->Void
    private var title:String {
        switch model.settingsUpdatePhase {
        case .waiting:return "Syncing with clock…"
        case .sending:return "Updating clock…"
        case .idle:return model.pendingChangeCount>0 ? "Update clock":"Review update"
        }
    }
    private var count:String {
        let n=model.pendingChangeCount
        return n==1 ? "1 change pending":"\(n) changes pending"
    }
    private var primary:some View {
        Button {
            if model.pendingChangeCount==0 {review()}
            else {Task {await model.updateSettings()}}
        } label:{
            HStack(spacing:10) {
                if model.settingsUpdatePhase != .idle {ProgressView().tint(SugarTheme.buttonText)}
                VStack(spacing:2) {
                    Text(title).font(.subheadline.weight(.semibold))
                    if model.pendingChangeCount>0 {Text(count).font(.caption)}
                }
            }.frame(maxWidth:.infinity,minHeight:42).padding(.vertical,6).padding(.horizontal,12)
        }
        .buttonStyle(.plain).foregroundStyle(SugarTheme.buttonText)
        .background(SugarTheme.accent,in:RoundedRectangle(cornerRadius:16))
        .disabled(model.settingsUpdatePhase != .idle || (model.pendingChangeCount>0 && !model.canRequestSettingsUpdate))
        .accessibilityLabel(title).accessibilityValue(model.pendingChangeCount>0 ? count:"")
        .accessibilityIdentifier("pending-update-button")
    }
    private var secondary:some View {
        Button {
            if model.settingsUpdatePhase == .waiting {model.cancelSettingsUpdate()}
            else {review()}
        } label:{
            VStack(spacing:4) {
                Image(systemName:model.settingsUpdatePhase == .waiting ? "xmark":"list.bullet")
                Text(model.settingsUpdatePhase == .waiting ? "Cancel":"Review").font(.caption.weight(.semibold))
            }.frame(minWidth:70,minHeight:54).frame(maxWidth:typeSize.isAccessibilitySize ? .infinity:nil)
        }.buttonStyle(.plain).foregroundStyle(SugarTheme.accent)
            .accessibilityLabel(model.settingsUpdatePhase == .waiting ? "Cancel update, keep changes":"Review pending changes")
            .accessibilityIdentifier("pending-review-button")
    }
    var body:some View {
        Group {
            if typeSize.isAccessibilitySize {VStack(spacing:8) {primary;secondary}}
            else {HStack(spacing:6) {primary;secondary}}
        }.padding(7).background(.ultraThinMaterial,in:RoundedRectangle(cornerRadius:23))
            .overlay(RoundedRectangle(cornerRadius:23).strokeBorder(SugarTheme.border.opacity(0.7),lineWidth:1))
            .shadow(color:.black.opacity(0.12),radius:16,x:0,y:5)
    }
}

struct PendingSettingsFeedback:View {
    @EnvironmentObject var model:ClockModel
    @State private var showingDetails=false
    @State private var feedbackDate=Date()
    var includesConnection=false
    private var receipt:SavePhase? {model.selected.flatMap {model.saveReceipts[$0.id]?.phase}}
    var body:some View {
        // One expiry tick hides a successful receipt without deleting its audit state.
        Group {
            let feedback=SettingsFeedback.resolve(phase:receipt,now:feedbackDate,
                storage:model.draftStorageMessage,
                update:model.settingsUpdatePhase == .idle ? model.settingsUpdateMessage:"",
                message:includesConnection ? model.message:"",
                updating:model.settingsUpdatePhase != .idle,
                loaded:model.hasLoadedSettings,ready:model.sessionReady,
                syncing:model.syncingSettings,includesConnection:includesConnection)
            HStack(spacing:8) {
                if let feedback {
                    Button {showingDetails=true} label:{
                        Label(feedback.title,systemImage:feedback.attention ? "exclamationmark.circle":"checkmark.circle")
                            .lineLimit(1).truncationMode(.tail).frame(maxWidth:.infinity,alignment:.leading)
                    }.buttonStyle(.plain)
                        .foregroundStyle(feedback.attention ? .orange:SugarTheme.secondary)
                        .accessibilityLabel(feedback.title).accessibilityHint("Show details")
                        .alert(feedback.title,isPresented:$showingDetails) {
                            if includesConnection,!model.sessionReady,!model.reconnecting,!model.updatingClock {
                                Button("Retry connection") {Task {await model.retrySelected()}}
                            }
                            Button("OK",role:.cancel) {}
                        } message:{Text(feedback.detail)}
                }
            }.font(.footnote).frame(maxWidth:.infinity,minHeight:includesConnection || feedback != nil ? 44:0,alignment:.leading)
        }
        .task(id:receipt) {
            feedbackDate=Date()
            guard case .saved(let date)=receipt else {return}
            let remaining=date.addingTimeInterval(SettingsFeedback.successDuration).timeIntervalSinceNow
            guard remaining>0 else {return}
            do {try await Task.sleep(for:.seconds(remaining))} catch {return}
            feedbackDate=Date()
        }
    }

}

struct PendingSettingsReview:View {
    @EnvironmentObject var model:ClockModel
    @Environment(\.dismiss) private var dismiss
    private var keys:[String] {model.settingsDraft.changed.sorted()}
    var body:some View {
        SugarScreen {
            VStack(alignment:.leading,spacing:4) {
                Text(model.selected?.nickname ?? "SugarClock").font(.headline)
                if let date=model.lastSettingsRefresh {
                    (Text("Clock settings last read ") + Text(date,style:.relative) + Text(" ago"))
                        .font(.caption).foregroundStyle(SugarTheme.secondary)
                }
            }
            PendingSettingsFeedback()
            if keys.isEmpty {
                Label("No pending changes",systemImage:"checkmark.circle")
                    .foregroundStyle(SugarTheme.secondary)
            }
            ForEach(keys,id:\.self) {key in
                SugarCard(spacing:10) {
                    HStack {
                        Text(label(key)).font(.headline)
                        Spacer()
                        Button(role:.destructive) {
                        var draft=model.settingsDraft
                        draft.discardChange(key,settings:model.settings,fields:model.fields)
                        model.setDraft(draft)
                        } label:{
                            Image(systemName:"xmark").font(.subheadline.weight(.semibold))
                                .frame(width:44,height:44).contentShape(Rectangle())
                        }.buttonStyle(.plain).foregroundStyle(SugarTheme.secondary)
                            .disabled(!model.canEditSettingsDraft || model.settingsUpdatePhase != .idle)
                            .accessibilityLabel("Discard \(label(key)) change")
                    }
                    Divider()
                    DetailRow(title:"On clock",value:clockValue(key))
                    DetailRow(title:"Your change",value:draftValue(key))
                    if key=="glucose_enabled",model.settingsDraft.booleans[key]==false {
                        Text("Glucose alerts will also be disabled.").font(.footnote).foregroundStyle(SugarTheme.secondary)
                    }
                }
            }
        }
        .navigationTitle("Pending changes")
        .toolbar {ToolbarItem(placement:.confirmationAction) {Button("Done") {dismiss()}}}
        .safeAreaInset(edge:.bottom,spacing:0) {
            VStack(spacing:10) {
                switch model.settingsUpdatePhase {
                case .waiting:
                    HStack {SugarSpinner();Text("Syncing with clock…")}.font(.subheadline)
                    Button("Cancel update") {model.cancelSettingsUpdate()}.buttonStyle(SugarButtonStyle(prominent:false))
                case .sending:
                    HStack {SugarSpinner();Text("Updating clock…")}.font(.subheadline)
                case .idle:
                    if model.pendingChangeCount>0 {
                        Button("Update clock") {Task {await model.updateSettings()}}
                            .buttonStyle(SugarButtonStyle()).disabled(!model.canRequestSettingsUpdate)
                    }
                }
            }.frame(maxWidth:720).padding(.horizontal,20).padding(.vertical,12)
                .frame(maxWidth:.infinity).background(.regularMaterial)
        }
    }
    private func secret(_ key:String)->Bool {
        model.settingsDraft.secrets[key] != nil || model.fields.first {$0["key"] as? String==key}?["type"] as? String=="secret"
    }
    private func display(_ value:Any,key:String)->String {
        if ["ambient_creature","ambient_character"].contains(key),let id=Int(String(describing:value)),let pet=PixelPetArtwork(rawValue:id) {return pet.name}
        if SettingsDraft.threshold(key),let n=Int(String(describing:value)) {
            return model.settingsDraft.mmol(key) ? String(format:"%.2f mmol/L",Double(n)/18):"\(n) mg/dL"
        }
        return String(describing:value)
    }
    private func clockValue(_ key:String)->String {
        if secret(key) {return model.settings[key+"_configured"] as? Bool==true ? "Configured":"Not configured"}
        guard let value=model.settings[key] else {return "Not available"}
        if model.fields.first(where:{$0["key"] as? String==key})?["type"] as? String=="bool" {return value as? Bool==true ? "On":"Off"}
        return display(value,key:key)
    }
    private func draftValue(_ key:String)->String {
        if secret(key) {return model.settingsDraft.secrets[key]==2 ? "Clear saved value":"Replace with a new value"}
        if let value=model.settingsDraft.booleans[key] {return value ? "On":"Off"}
        let value=model.settingsDraft.text[key] ?? ""
        if SettingsDraft.threshold(key) {return value+(model.settingsDraft.mmol(key) ? " mmol/L":" mg/dL")}
        return display(value,key:key)
    }
}
