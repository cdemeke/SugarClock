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

/// The firmware schedules updates at a whole hour in the clock's configured time zone.
enum ClockUpdateTime {
    static let choices:[Int:String]=Dictionary(uniqueKeysWithValues:(0..<24).map {hour in
        (hour,"\(hour % 12 == 0 ? 12:hour % 12):00 \(hour<12 ? "AM":"PM")")
    })
}

struct SettingsCategory:Identifiable {
    let id:String
    let title:String
    let subtitle:String
    let symbol:String
    let sections:[(String,[String])]
    var enableKey:String? {
        ["glucose":"glucose_enabled","time":"time_display_enabled","alerts":"alert_enabled","companions":"ambient_enabled","weather":"weather_enabled","pomodoro":"timer_enabled","stopwatch":"stopwatch_enabled","countdown":"countdown_enabled","notifications":"notify_enabled","system":"sysmon_enabled"][id]
    }
    func supported(by fields:[[String:Any]])->Bool {
        let keys=Set(fields.compactMap {$0["key"] as? String})
        if id=="glucose",keys.contains("data_source") {return true} // Legacy firmware has no master switch.
        return enableKey.map {keys.contains($0)} ?? false
    }
    func enabled(in settings:[String:Any])->Bool? {
        if id=="alerts",settings["glucose_enabled"] as? Bool==false {return false}
        if id=="glucose",settings["glucose_enabled"]==nil,settings["data_source"] != nil {return true}
        return enableKey.flatMap {settings[$0] as? Bool}
    }
    static let all:[SettingsCategory]=[
        .init(id:"glucose",title:"Blood Sugar",subtitle:"Source, credentials and glucose ranges",symbol:"drop",sections:[
            ("Blood sugar readings",["glucose_enabled","alert_enabled","data_source","dexcom_username","dexcom_password","dexcom_us","server_url","auth_token","poll_interval","stale_timeout_min"]),
            ("Reading display",["show_delta"]),
            ("Units and ranges",["use_mmol","thresh_urgent_low","thresh_low","thresh_high","thresh_urgent_high"])]),
        .init(id:"display",title:"Display",subtitle:"Choose how screens cycle",symbol:"sun.max",sections:[
            ("Screen rotation",["auto_cycle_enabled","auto_cycle_sec"])]),
        .init(id:"time",title:"Time",subtitle:"Time zone and clock format",symbol:"moon.stars",sections:[
            ("Time display",["time_display_enabled","timezone","use_24h","date_on_time_screen","date_format"])]),
        .init(id:"alerts",title:"Alerts",subtitle:"Thresholds and snooze preferences",symbol:"bell",sections:[
            ("Glucose alerts",["alert_enabled","alert_low","alert_high","alert_snooze_min"])]),
        .init(id:"companions",title:"Pixel Companions",subtitle:"A little company on your display",symbol:"sparkles",sections:[
            ("Pixel companions",["ambient_enabled","ambient_creature","ambient_seasonal"])]),
        .init(id:"weather",title:"Weather",subtitle:"Requires an OpenWeather API key and a location.",symbol:"cloud.sun",sections:[
            ("Weather",["weather_enabled","weather_city","weather_api_key","weather_use_f","weather_poll_min"])]),
        .init(id:"pomodoro",title:"Pomodoro",subtitle:"Set your focus and break periods. Use the clock’s buttons to control the timer.",symbol:"timer",sections:[
            ("Pomodoro",["timer_enabled","timer_work_min","timer_break_min","timer_long_break_min","timer_sessions","timer_buzzer"])]),
        .init(id:"stopwatch",title:"Stopwatch",subtitle:"Use the clock’s buttons to control the stopwatch.",symbol:"stopwatch",sections:[
            ("Stopwatch",["stopwatch_enabled"])]),
        .init(id:"countdown",title:"Countdown",subtitle:"Count down to an event.",symbol:"calendar.badge.clock",sections:[
            ("Countdown",["countdown_enabled","countdown_name","countdown_target"])]),
        .init(id:"notifications",title:"Notifications",subtitle:"Display messages sent by an integration connected to your clock.",symbol:"text.bubble",sections:[
            ("Notifications",["notify_enabled","notify_default_duration","notify_allow_buzzer"])]),
        .init(id:"system",title:"System Monitor",subtitle:"Requires an integration that sends system data to your clock.",symbol:"desktopcomputer",sections:[
            ("System monitor",["sysmon_enabled","sysmon_label","sysmon_display_mode","sysmon_warn_pct","sysmon_crit_pct"])])
    ]
}
