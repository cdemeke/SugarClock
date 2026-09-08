import Foundation

/// Device-compatible rules; named locations avoid ambiguous regional abbreviations.
/// Rules checked against IANA tzdb 2026c (https://data.iana.org/time-zones/tzdb/).
struct ClockTimeZone:Identifiable {
    let name:String
    let posix:String
    let observesDaylightSaving:Bool
    var id:String {posix}
    static let choices:[ClockTimeZone]=[
        .init(name:"Eastern · New York",posix:"EST5EDT,M3.2.0,M11.1.0",observesDaylightSaving:true),
        .init(name:"Central · Chicago",posix:"CST6CDT,M3.2.0,M11.1.0",observesDaylightSaving:true),
        .init(name:"Mountain · Denver",posix:"MST7MDT,M3.2.0,M11.1.0",observesDaylightSaving:true),
        .init(name:"Pacific · Los Angeles",posix:"PST8PDT,M3.2.0,M11.1.0",observesDaylightSaving:true),
        .init(name:"Alaska · Anchorage",posix:"AKST9AKDT,M3.2.0,M11.1.0",observesDaylightSaving:true),
        .init(name:"Hawaii · Honolulu",posix:"HST10",observesDaylightSaving:false),
        .init(name:"Arizona · Phoenix",posix:"MST7",observesDaylightSaving:false),
        .init(name:"Greenwich Mean Time · GMT",posix:"GMT0",observesDaylightSaving:false),
        .init(name:"Coordinated Universal Time · UTC",posix:"UTC0",observesDaylightSaving:false),
        .init(name:"United Kingdom · London",posix:"GMT0BST,M3.5.0/1,M10.5.0",observesDaylightSaving:true),
        .init(name:"Central Europe · Paris",posix:"CET-1CEST,M3.5.0,M10.5.0/3",observesDaylightSaving:true),
        .init(name:"East Africa · Addis Ababa",posix:"EAT-3",observesDaylightSaving:false),
        .init(name:"India · Kolkata",posix:"IST-5:30",observesDaylightSaving:false),
        .init(name:"China · Shanghai",posix:"CST-8",observesDaylightSaving:false),
        .init(name:"Japan · Tokyo",posix:"JST-9",observesDaylightSaving:false)
    ]
    static func matching(_ value:String)->ClockTimeZone? {
        // Explicit 02:00 transition times and a positive offset sign are equivalent.
        let normalized=value.replacingOccurrences(of:"/0?2(?::00)?(?=,|$)",with:"",options:.regularExpression).replacingOccurrences(of:"+",with:"")
        return choices.first {$0.posix==normalized}
    }
}

struct NearbyNetwork:Identifiable,Equatable {
    let ssid:String
    let rssi:Int
    let enterprise:Bool
    let open:Bool
    var id:String {ssid}
    var signal:String {rssi >= -60 ? "Strong signal":rssi >= -75 ? "Good signal":"Weak signal"}
    static func sorted(_ results:[[String:Any]])->[NearbyNetwork] {
        var unique:[String:NearbyNetwork]=[:]
        for result in results {
            guard let ssid=result["ssid"] as? String,!ssid.isEmpty else {continue}
            let network=NearbyNetwork(ssid:ssid,rssi:result["rssi"] as? Int ?? -100,enterprise:result["enterprise"] as? Bool ?? false,open:result["auth"] as? Int == 0)
            if unique[ssid]==nil || network.rssi>unique[ssid]!.rssi {unique[ssid]=network}
        }
        return unique.values.sorted {$0.rssi == $1.rssi ? $0.ssid<$1.ssid:$0.rssi>$1.rssi}
    }
}
