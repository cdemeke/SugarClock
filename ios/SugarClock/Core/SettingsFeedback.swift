import Foundation

/// Presentation only: receipts and pending mutations keep their original lifecycle.
struct SettingsFeedback:Equatable {
    static let successDuration:TimeInterval=5
    let title:String
    let detail:String
    let attention:Bool

    static func resolve(phase:SavePhase?,now:Date,storage:String,update:String,message:String,
                        updating:Bool,loaded:Bool,ready:Bool,syncing:Bool,
                        includesConnection:Bool)->SettingsFeedback? {
        func notice(_ title:String,_ detail:String,attention:Bool=true)->SettingsFeedback {
            SettingsFeedback(title:title,detail:detail,attention:attention)
        }
        if !storage.isEmpty {return notice("Local changes need attention",storage)}
        // An explicit transfer already has progress in the bottom update control.
        if updating {return nil}
        switch phase {
        case .unconfirmed:
            return notice("Update not confirmed", "Your changes are kept. Reconnect and check the clock’s saved settings before trying again.")
        case .failed(let detail):return notice("Couldn't update clock",detail)
        case .saving,.checking:
            return notice("Confirming update…","Waiting for the clock to confirm its saved settings.",attention:false)
        default:break
        }
        // A finished update's duplicate message must never outlive its receipt.
        let routine=["Updated on clock.","Local changes discarded.","Update cancelled. Your local changes are kept.","These settings already match your clock."]
        if !update.isEmpty,!routine.contains(update) {return notice("Update needs attention",update)}
        if !message.isEmpty {return notice("Clock needs attention",message)}
        if case .saved(let date)=phase,now.timeIntervalSince(date)<successDuration {
            return notice("Updated on clock", "The clock confirmed that your settings were saved.",attention:false)
        }
        if includesConnection {
            if !loaded {return notice("Syncing settings…","Reading this clock’s settings for the first time.",attention:false)}
            if !ready,!syncing {return notice("Clock unavailable","Your settings are available to edit. Move closer and retry the connection, or tap Update clock when your changes are ready.")}
        }
        return nil
    }
}
