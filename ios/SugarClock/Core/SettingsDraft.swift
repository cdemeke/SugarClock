import Foundation

/// Editor state is independent of view rendering. Only explicit user changes
/// enter a patch; displaying converted thresholds never changes saved integers.
public struct SettingsDraft:Equatable,Codable {
    public private(set) var text:[String:String]=[:]
    public private(set) var booleans:[String:Bool]=[:]
    public private(set) var secrets:[String:Int]=[:]
    public private(set) var changed:Set<String>=[]
    private var initialText:[String:String]=[:]
    private var initialBool:[String:Bool]=[:]
    private var secretKeys:Set<String>=[]
    private var originalThresholds:[String:Int]=[:]
    private var fieldTypes:[String:String]=[:]
    private var initialSecretConfigured:[String:Bool]=[:]
    private var usesMMOL=false
    public init(settings:[String:Any]=[:],fields:[[String:Any]]=[]) {
        usesMMOL=settings["use_mmol"] as? Bool ?? false
        for field in fields {
            guard let key=field["key"] as? String else {continue}
            fieldTypes[key]=field["type"] as? String ?? "text"
            if field["type"] as? String=="secret" {
                secretKeys.insert(key)
                initialSecretConfigured[key]=settings[key+"_configured"] as? Bool ?? false
            }
            if Self.threshold(key),let n=settings[key] as? Int {originalThresholds[key]=n}
            if field["type"] as? String=="bool" {booleans[key]=settings[key] as? Bool ?? false}
            else if field["type"] as? String != "secret" {
                if usesMMOL,Self.threshold(key),let n=settings[key] as? Int {text[key]=String(format:"%.2f",Double(n)/18)}
                else {text[key]=settings[key].map{String(describing:$0)} ?? ""}
            }
        }
        initialText=text;initialBool=booleans
    }
    /// A readback can arrive after the user has continued editing during recovery.
    /// Rebase on confirmed settings, then retain only edits made after submission.
    public mutating func confirm(submitted:SettingsDraft,settings:[String:Any],fields:[[String:Any]]) {
        var confirmed=SettingsDraft(settings:settings,fields:fields)
        if booleans["use_mmol"] != submitted.booleans["use_mmol"],let units=booleans["use_mmol"] {
            confirmed.setBool(units,key:"use_mmol")
        }
        for field in fields {
            guard let key=field["key"] as? String else {continue}
            if secretKeys.contains(key) {
                if secrets[key] != submitted.secrets[key] || text[key] != submitted.text[key] {
                    confirmed.setSecretAction(secrets[key] ?? 0,key:key)
                    if let value=text[key] {confirmed.setText(value,key:key)}
                }
            } else if field["type"] as? String=="bool" {
                if key != "use_mmol",booleans[key] != submitted.booleans[key],let value=booleans[key] {
                    confirmed.setBool(value,key:key)
                }
            } else if let value=text[key] {
                if Self.threshold(key),let current=thresholdMGDL(key),let sent=submitted.thresholdMGDL(key) {
                    // Changing display units alone must not re-submit thresholds.
                    if current != sent {
                        let display=usesMMOL == confirmed.usesMMOL ? value
                            : confirmed.usesMMOL ? String(format:"%.2f",current/18):String(format:"%.0f",current)
                        confirmed.setText(display,key:key)
                    }
                } else if value != submitted.text[key] {confirmed.setText(value,key:key)}
            }
        }
        self=confirmed
    }
    private func thresholdMGDL(_ key:String)->Double? {
        if !changed.contains(key),let original=originalThresholds[key] {return Double(original)}
        guard let n=Double((text[key] ?? "").replacingOccurrences(of:",",with:".")),n.isFinite else {return nil}
        return usesMMOL ? (n*18).rounded():n
    }
    public static func threshold(_ key:String)->Bool {key.hasPrefix("thresh_") || ["alert_low","alert_high"].contains(key)}
    public func mmol(_ key:String)->Bool {usesMMOL && Self.threshold(key)}
    public mutating func setText(_ value:String,key:String) {
        text[key]=value;mark(key,secretKeys.contains(key) ? (secrets[key] ?? 0) != 0:value != initialText[key])
    }
    public mutating func setBool(_ value:Bool,key:String) {
        if key=="use_mmol",value != usesMMOL {
            for (threshold,original) in originalThresholds {
                // Preserve exact original mg/dL integers when only units change.
                let edited=Double((text[threshold] ?? "").replacingOccurrences(of:",",with:"."))
                let mgdl=changed.contains(threshold) ? edited.map {usesMMOL ? ($0*18).rounded():$0}:Double(original)
                initialText[threshold]=value ? String(format:"%.2f",Double(original)/18):String(original)
                if let mgdl,mgdl.isFinite {
                    text[threshold]=value ? String(format:"%.2f",mgdl/18):String(format:"%.0f",mgdl)
                    mark(threshold,text[threshold] != initialText[threshold])
                }
            }
            usesMMOL=value
        }
        booleans[key]=value;mark(key,value != initialBool[key])
    }
    public mutating func setSecretAction(_ value:Int,key:String) {
        secrets[key]=value;mark(key,value != 0)
        // An explicitly discarded/cleared replacement must not linger in storage.
        if value != 1 {text.removeValue(forKey:key)}
    }
    /// Explicitly use the clock's current value for one reviewed change. Keep
    /// every other edit's original conflict baseline, even if the clock changed
    /// those fields too. A removed field is forgotten without retaining secrets.
    public mutating func discardChange(_ key:String,settings:[String:Any],fields:[[String:Any]]) {
        if key=="use_mmol" {
            // This changes presentation, not the physical meaning of other edits.
            setBool(settings[key] as? Bool ?? usesMMOL,key:key)
        }
        text.removeValue(forKey:key);booleans.removeValue(forKey:key);secrets.removeValue(forKey:key)
        initialText.removeValue(forKey:key);initialBool.removeValue(forKey:key)
        originalThresholds.removeValue(forKey:key);initialSecretConfigured.removeValue(forKey:key)
        secretKeys.remove(key);fieldTypes.removeValue(forKey:key);changed.remove(key)
        guard let field=fields.first(where:{$0["key"] as? String==key}),
              let type=field["type"] as? String,["secret","bool","int","text","string"].contains(type) else {return}
        fieldTypes[key]=type
        if type=="secret" {
            secretKeys.insert(key)
            initialSecretConfigured[key]=settings[key+"_configured"] as? Bool ?? false
        } else if type=="bool" {
            booleans[key]=settings[key] as? Bool ?? false;initialBool[key]=booleans[key]
        } else {
            if Self.threshold(key),let number=settings[key] as? Int {
                originalThresholds[key]=number
                text[key]=usesMMOL ? String(format:"%.2f",Double(number)/18):String(number)
            } else {text[key]=settings[key].map {String(describing:$0)} ?? ""}
            initialText[key]=text[key]
        }
    }
    private mutating func mark(_ key:String,_ dirty:Bool) {if dirty {changed.insert(key)} else {changed.remove(key)}}
    public func patch(fields:[[String:Any]]) throws -> [String:Any] {
        try validateChangedSchema(fields)
        var result:[String:Any]=[:]
        for field in fields {
            guard let key=field["key"] as? String,changed.contains(key) else {continue}
            switch field["type"] as? String {
            case "secret":
                guard [1,2].contains(secrets[key] ?? 0) else {throw DraftError.invalid(key)}
                if secrets[key]==2 {result[key]=NSNull()}
                else if secrets[key]==1 {
                    let value=text[key] ?? ""
                    guard value.utf8.count<=field["max_length"] as? Int ?? Int.max else {throw DraftError.invalid(key)}
                    result[key]=value
                }
            case "bool":result[key]=booleans[key] ?? false
            case "int":
                guard let n=Double((text[key] ?? "").replacingOccurrences(of:",",with:".")),n.isFinite else {throw DraftError.invalid(key)}
                let value=mmol(key) ? (n*18).rounded():n
                guard value.rounded()==value,value>=Double(field["min"] as? Int ?? Int(Int32.min)),value<=Double(field["max"] as? Int ?? Int(Int32.max)) else {throw DraftError.invalid(key)}
                result[key]=Int(value)
            default:
                let value=text[key] ?? ""
                guard value.utf8.count<=field["max_length"] as? Int ?? Int.max else {throw DraftError.invalid(key)}
                result[key]=value
            }
        }
        // Cascade only into the submitted patch. Undoing an unsaved Off must not
        // create a hidden alert edit; confirmed Off readback becomes the new baseline.
        if result["glucose_enabled"] as? Bool==false,
           fields.contains(where:{$0["key"] as? String=="alert_enabled"}) {
            result["alert_enabled"]=false
        }
        return result
    }

    /// Reconcile a fresh, durable readback with current editor intent. This is
    /// not proof that a previous request ran: callers retain its receipt/marker.
    /// Each field is checked separately so unrelated conflicts keep their old
    /// baselines. Derived effects must already match too (for example disabling
    /// glucose also disables its alert). Secret flags cannot establish equality.
    @discardableResult public mutating func reconcileConfirmedValues(settings:[String:Any],fields:[[String:Any]]) ->Set<String> {
        let original=self
        var matched:Set<String>=[]
        for key in original.changed where !original.secretKeys.contains(key) {
            var isolated=original;isolated.changed=[key]
            guard let requested=try? isolated.validatedPatch(settings:settings,fields:fields),!requested.isEmpty else {continue}
            func sameValue(_ current:Any?,_ desired:Any,key:String)->Bool {
                guard !original.secretKeys.contains(key),let current else {return false}
                switch original.fieldTypes[key] {
                case "bool":return (current as? Bool)==(desired as? Bool)
                case "int":return (current as? NSNumber)?.doubleValue==(desired as? NSNumber)?.doubleValue
                case "text","string":return (current as? String)==(desired as? String)
                default:return false
                }
            }
            let matches=requested.allSatisfy {sameValue(settings[$0.key],$0.value,key:$0.key)}
            // Removing a controlling edit must not expose a contradictory dirty
            // dependent value that the original batch would have normalized.
            let preservesOtherIntent=requested.allSatisfy {dependent,value in
                guard dependent != key,original.changed.contains(dependent) else {return true}
                var other=original;other.changed=[dependent]
                guard let desired=try? other.patch(fields:fields)[dependent] else {return false}
                return sameValue(desired,value,key:dependent)
            }
            if matches,preservesOtherIntent {matched.insert(key)}
        }
        for key in matched {discardChange(key,settings:settings,fields:fields)}
        if changed.isEmpty {self=SettingsDraft(settings:settings,fields:fields)}
        return matched
    }

    private func validateChangedSchema(_ fields:[[String:Any]]) throws {
        var seen:Set<String>=[]
        var supported:[String:String]=[:]
        for field in fields {
            guard let key=field["key"] as? String else {continue}
            guard seen.insert(key).inserted else {throw DraftError.unsupported(key)}
            supported[key]=field["type"] as? String ?? "text"
        }
        for key in changed {
            guard let current=supported[key],let original=fieldTypes[key],current==original,
                  ["secret","bool","int","text","string"].contains(current) else {throw DraftError.unsupported(key)}
        }
    }

    /// Validate the offline edit against freshly read clock values before sending.
    /// A clock edit to an unrelated field is safe; a different value on an edited
    /// field requires the user to review it. Secret values are never read back.
    public func validatedPatch(settings:[String:Any],fields:[[String:Any]]) throws ->[String:Any] {
        var result=try patch(fields:fields)
        var candidate=settings.merging(result,uniquingKeysWith:{$1})
        let mode=candidate["default_mode"] as? Int
        let unavailableMode=(mode==1 && candidate["time_display_enabled"] as? Bool==false)
            || (mode==3 && candidate["ambient_enabled"] as? Bool==false)
        if unavailableMode {
            if changed.contains("default_mode") {throw DraftError.invalid("default_mode")}
            // Mirror the firmware normalization in the expected readback.
            if fieldTypes["default_mode"] != nil {result["default_mode"]=0;candidate["default_mode"]=0}
        }
        let thresholdKeys=["thresh_urgent_low","thresh_low","thresh_high","thresh_urgent_high"]
        let thresholds=thresholdKeys.compactMap {candidate[$0] as? Int}
        if thresholds.count==4,!(thresholds[0]<=thresholds[1] && thresholds[1]<thresholds[2] && thresholds[2]<=thresholds[3]) {
            throw DraftError.invalid("glucose_threshold_order")
        }
        if let low=candidate["alert_low"] as? Int,let high=candidate["alert_high"] as? Int,low>=high {throw DraftError.invalid("alert_threshold_order")}
        for (key,desired) in result {
            let current=settings[key]
            let unchanged:Bool
            let alreadyDesired:Bool
            switch fieldTypes[key] {
            case "secret":
                let configured=settings[key+"_configured"] as? Bool ?? false
                unchanged=configured==(initialSecretConfigured[key] ?? false)
                alreadyDesired=false // Configured is not proof of a particular value.
            case "bool":
                unchanged=(current as? Bool)==initialBool[key]
                alreadyDesired=(current as? Bool)==(desired as? Bool)
            case "int":
                let initial=originalThresholds[key].map(Double.init) ?? initialText[key].flatMap(Double.init)
                unchanged=(current as? NSNumber)?.doubleValue==initial
                alreadyDesired=(current as? NSNumber)?.doubleValue==(desired as? NSNumber)?.doubleValue
            default:
                unchanged=(current as? String)==initialText[key]
                alreadyDesired=(current as? String)==(desired as? String)
            }
            guard unchanged || alreadyDesired else {throw DraftError.conflict(key)}
        }
        return result
    }

    /// Refresh the displayed baseline only after conflicts have been checked.
    /// Dirty fields and pending replacement secrets retain their exact intent.
    public func rebasedKeepingEdits(settings:[String:Any],fields:[[String:Any]]) throws ->SettingsDraft {
        try validateChangedSchema(fields)
        var next=SettingsDraft(settings:settings,fields:fields)
        if changed.contains("use_mmol"),let value=booleans["use_mmol"] {next.setBool(value,key:"use_mmol")}
        for key in changed where key != "use_mmol" {
            if secretKeys.contains(key) {
                next.setSecretAction(secrets[key] ?? 0,key:key)
                if secrets[key]==1,let value=text[key] {next.setText(value,key:key)}
            } else if fieldTypes[key]=="bool",let value=booleans[key] {next.setBool(value,key:key)}
            else if let value=text[key] {
                if Self.threshold(key),usesMMOL != next.usesMMOL,let mgdl=thresholdMGDL(key) {
                    next.setText(next.usesMMOL ? String(format:"%.2f",mgdl/18):String(format:"%.0f",mgdl),key:key)
                } else {next.setText(value,key:key)}
            }
        }
        return next
    }

    func validateForStorage() throws {
        let dictionaries=[text,initialText]
        guard fieldTypes.count<=192,changed.count<=192,secretKeys.count<=192,
              dictionaries.allSatisfy({$0.count<=192 && $0.allSatisfy({$0.key.utf8.count<=96 && $0.value.utf8.count<=8192})}),
              booleans.count<=192,initialBool.count<=192,secrets.count<=192,originalThresholds.count<=192,
              initialSecretConfigured.count<=192,
              Set(text.keys).union(initialText.keys).union(booleans.keys).union(initialBool.keys).union(secrets.keys).union(changed).isSubset(of:Set(fieldTypes.keys)),
              secrets.values.allSatisfy({[0,1,2].contains($0)}),
              secretKeys.allSatisfy({fieldTypes[$0]=="secret"}) else {throw LocalSettingsStoreError.tooLarge}
    }

    /// Called before persistence so unused secret text cannot survive a discard.
    func withoutUnusedSecretText()->SettingsDraft {
        var next=self
        for key in secretKeys where secrets[key] != 1 {next.text.removeValue(forKey:key)}
        return next
    }

}
public enum DraftError:LocalizedError {
    case invalid(String)
    case unsupported(String)
    case conflict(String)
    public var errorDescription:String? {
        switch self {
        case .invalid(let key):return "Check \(key.replacingOccurrences(of:"_",with:" ")) and its allowed range or length."
        case .unsupported(let key):return "The clock no longer supports the edited \(key.replacingOccurrences(of:"_",with:" ")) setting. Review or discard the change before updating."
        case .conflict(let key):return "\(key.replacingOccurrences(of:"_",with:" ").capitalized) changed on the clock. Review its current value before updating."
        }
    }
}
