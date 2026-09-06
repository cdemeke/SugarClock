import Foundation

/// Editor state is independent of view rendering. Only explicit user changes
/// enter a patch; displaying converted thresholds never changes saved integers.
public struct SettingsDraft {
    public private(set) var text:[String:String]=[:]
    public private(set) var booleans:[String:Bool]=[:]
    public private(set) var secrets:[String:Int]=[:]
    public private(set) var changed:Set<String>=[]
    private var initialText:[String:String]=[:]
    private var initialBool:[String:Bool]=[:]
    private var secretKeys:Set<String>=[]
    private var originalThresholds:[String:Int]=[:]
    private var usesMMOL=false
    public init(settings:[String:Any]=[:],fields:[[String:Any]]=[]) {
        usesMMOL=settings["use_mmol"] as? Bool ?? false
        for field in fields {
            guard let key=field["key"] as? String else {continue}
            if field["type"] as? String=="secret" {secretKeys.insert(key)}
            if Self.threshold(key),let n=settings[key] as? Int {originalThresholds[key]=n}
            if field["type"] as? String=="bool" {booleans[key]=settings[key] as? Bool ?? false}
            else if field["type"] as? String != "secret" {
                if usesMMOL,Self.threshold(key),let n=settings[key] as? Int {text[key]=String(format:"%.2f",Double(n)/18)}
                else {text[key]=settings[key].map{String(describing:$0)} ?? ""}
            }
        }
        initialText=text;initialBool=booleans
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
    public mutating func setSecretAction(_ value:Int,key:String) {secrets[key]=value;mark(key,value != 0)}
    private mutating func mark(_ key:String,_ dirty:Bool) {if dirty {changed.insert(key)} else {changed.remove(key)}}
    public func patch(fields:[[String:Any]]) throws -> [String:Any] {
        var result:[String:Any]=[:]
        for field in fields {
            guard let key=field["key"] as? String,changed.contains(key) else {continue}
            switch field["type"] as? String {
            case "secret":
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
        return result
    }
}
public enum DraftError:LocalizedError {
    case invalid(String)
    public var errorDescription:String? {
        switch self {case .invalid(let key):return "Check \(key.replacingOccurrences(of:"_",with:" ")) and its allowed range or length."}
    }
}
