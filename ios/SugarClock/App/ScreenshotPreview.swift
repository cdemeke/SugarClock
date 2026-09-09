#if DEBUG
import SwiftUI

/// Explicit simulator screenshot fixture. Uses the production views, never a radio,
/// credentials, saved user preferences or an automatic fallback from a connection.
struct ScreenshotPreview:View {
    @EnvironmentObject var model:ClockModel
    let screen:String
    var body:some View {
        Group {
            if screen=="clocks" {MyClocksView()}
            else {
                NavigationStack {
                    switch screen {
                    case "display", "display-accessibility":ConfigurationView(category:SettingsCategory.all[1])
                    case "time", "time-off", "time-large":ConfigurationView(category:SettingsCategory.all[2])
                    case "alerts", "alerts-off", "alerts-blocked":ConfigurationView(category:SettingsCategory.all[3])
                    case "companions", "companions-off":ConfigurationView(category:SettingsCategory.all[4])
                    case "glucose", "glucose-off":ConfigurationView(category:SettingsCategory.all[0])
                    case "weather", "pomodoro", "stopwatch", "countdown", "notifications", "system":ConfigurationView(category:SettingsCategory.all.first {$0.id==screen}!)
                    case "wifi":WiFiView()
                    case "firmware":FirmwareView()
                    case "update-time":SettingEditor(field:["key":"auto_update_hour","type":"int","min":0,"max":23])
                    case "brightness", "saved", "saved-large", "checking":SettingEditor(field:["key":"brightness","type":"int","min":1,"max":255])
                    case "secret":SettingEditor(field:["key":"dexcom_password","type":"secret","max_length":63])
                    case "troubleshooting":TroubleshootingView()
                    default:DeviceView()
                    }
                }
            }
        }
        .tint(SugarTheme.accent)
        .dynamicTypeSize(["time-large","display-accessibility","saved-large","loading-large"].contains(screen) ? .accessibility3:.large)
        .safeAreaInset(edge:.bottom) {
            Label("SCREENSHOT PREVIEW · SAMPLE DATA",systemImage:"photo")
                .font(.system(size:10,weight:.semibold)).frame(maxWidth:.infinity).padding(10)
                .background(Color(uiColor:.secondarySystemBackground))
                .overlay(alignment:.top) {Divider()}
        }
        .allowsHitTesting(false)
    }
    @MainActor static func makeModel()->ClockModel {
        let model=ClockModel(enableBluetooth:false,loadSaved:false)
        let clock=SavedClock(id:"AABBCC123456",peripheral:UUID(uuidString:"11111111-1111-1111-1111-111111111111")!,nickname:"Bedside Clock")
        model.clocks=[clock,SavedClock(id:"AABBCC654321",peripheral:UUID(uuidString:"22222222-2222-2222-2222-222222222222")!,nickname:"Kitchen Clock")]
        model.selected=clock
        model.hello=["firmware":"0.3.0","device_id":clock.id]
        model.settings=["wifi_ssid":"Home Wi-Fi","wifi_security":0,"wifi_password_configured":true,"data_source":0,"server_url_configured":true,"auth_token_configured":true,"poll_interval":60,"stale_timeout_min":20,"default_mode":0,"show_delta":false,"time_display_enabled":true,"auto_cycle_enabled":false,"auto_cycle_sec":10,"thresh_urgent_low":55,"thresh_low":70,"thresh_high":180,"thresh_urgent_high":250,"dexcom_password_configured":true,"dexcom_us":true,"brightness":77,"auto_brightness":false,"use_mmol":false,"alert_enabled":true,"ambient_creature":0]
        model.status=["wifi":"Connected","trial":"idle","configuration_saved":true,"network_saved":true,"internet_dns":1,"provider_reachable":1,"data_received":true,"provider_http":200,"data_age_ms":42000,
                      "ota":["current_version":"0.3.0","state":"idle","progress":0,"pending_verification":false]]
        model.fields=[
            ["key":"data_source","type":"int","min":0,"max":2],
            ["key":"server_url","type":"secret","max_length":255],
            ["key":"auth_token","type":"secret","max_length":255],
            ["key":"dexcom_username","type":"text","max_length":63],
            ["key":"dexcom_us","type":"bool"],
            ["key":"poll_interval","type":"int","min":15,"max":3600],
            ["key":"stale_timeout_min","type":"int","min":5,"max":60],
            ["key":"default_mode","type":"int","min":0,"max":3],
            ["key":"show_delta","type":"bool"],
            ["key":"time_display_enabled","type":"bool"],
            ["key":"auto_cycle_enabled","type":"bool"],
            ["key":"auto_cycle_sec","type":"int","min":3,"max":300],
            ["key":"thresh_urgent_low","type":"int","min":20,"max":600],
            ["key":"thresh_low","type":"int","min":20,"max":600],
            ["key":"thresh_high","type":"int","min":20,"max":600],
            ["key":"thresh_urgent_high","type":"int","min":20,"max":600],
            ["key":"dexcom_password","type":"secret"],
            ["key":"brightness","type":"int","min":1,"max":255],
            ["key":"auto_brightness","type":"bool"],
            ["key":"use_mmol","type":"bool"],
            ["key":"alert_enabled","type":"bool"],
            ["key":"auto_update_enabled","type":"bool"],
            ["key":"auto_update_hour","type":"int","min":0,"max":23]
        ]
        model.settings.merge(["glucose_enabled":true,"timezone":"EST5EDT,M3.2.0,M11.1.0","use_24h":false,"date_on_time_screen":true,"date_format":0,"ambient_enabled":true,"ambient_creature":0,"ambient_seasonal":true,"alert_low":70,"alert_high":250,"alert_snooze_min":15,"auto_cycle_enabled":true,"auto_update_enabled":true,"auto_update_hour":3]) {_,new in new}
        model.fields += [
            ["key":"glucose_enabled","type":"bool"],
            ["key":"timezone","type":"text","max_length":63],
            ["key":"use_24h","type":"bool"],
            ["key":"date_on_time_screen","type":"bool"],
            ["key":"date_format","type":"int","min":0,"max":2],
            ["key":"ambient_enabled","type":"bool"],
            ["key":"ambient_creature","type":"int","min":0,"max":1],
            ["key":"ambient_seasonal","type":"bool"],
            ["key":"alert_low","type":"int","min":20,"max":600],
            ["key":"alert_high","type":"int","min":20,"max":600],
            ["key":"alert_snooze_min","type":"int","min":1,"max":120]
        ]
        model.settings.merge(["weather_enabled":false,"weather_city":"New York,US","weather_api_key_configured":false,"weather_use_f":true,"weather_poll_min":30,"timer_enabled":false,"timer_work_min":25,"timer_break_min":5,"timer_long_break_min":15,"timer_sessions":4,"timer_buzzer":true,"stopwatch_enabled":false,"countdown_enabled":false,"countdown_name":"Vacation","countdown_target":1800000000,"notify_enabled":false,"notify_default_duration":60,"notify_allow_buzzer":true,"sysmon_enabled":false,"sysmon_label":"CPU","sysmon_display_mode":0,"sysmon_warn_pct":70,"sysmon_crit_pct":90]) {_,new in new}
        model.fields += [
            ["key":"weather_enabled","type":"bool"],["key":"weather_city","type":"text","max_length":63],
            ["key":"weather_api_key","type":"secret","max_length":63],["key":"weather_use_f","type":"bool"],["key":"weather_poll_min","type":"int","min":5,"max":60],
            ["key":"timer_enabled","type":"bool"],["key":"timer_work_min","type":"int","min":1,"max":120],
            ["key":"timer_break_min","type":"int","min":1,"max":60],["key":"timer_long_break_min","type":"int","min":1,"max":60],
            ["key":"timer_sessions","type":"int","min":1,"max":12],["key":"timer_buzzer","type":"bool"],
            ["key":"stopwatch_enabled","type":"bool"],["key":"countdown_enabled","type":"bool"],
            ["key":"countdown_name","type":"text","max_length":31],["key":"countdown_target","type":"int","min":0,"max":2147483647],
            ["key":"notify_enabled","type":"bool"],["key":"notify_default_duration","type":"int","min":5,"max":600],["key":"notify_allow_buzzer","type":"bool"],
            ["key":"sysmon_enabled","type":"bool"],["key":"sysmon_label","type":"text","max_length":15],
            ["key":"sysmon_display_mode","type":"int","min":0,"max":1],["key":"sysmon_warn_pct","type":"int","min":0,"max":100],["key":"sysmon_crit_pct","type":"int","min":0,"max":100]
        ]
        model.networks=[["ssid":"Home Wi-Fi","rssi":-42],["ssid":"Guest Network","rssi":-61]]
        let screen=ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] ?? ""
        if let key=SettingsCategory.all.first(where:{$0.id==screen})?.enableKey {model.settings[key]=true}
        if ["glucose-off","alerts-blocked"].contains(screen) {model.settings["glucose_enabled"]=false;model.settings["alert_enabled"]=false}
        if screen=="time-off" {model.settings["time_display_enabled"]=false}
        if screen=="alerts-off" {model.settings["alert_enabled"]=false}
        if screen=="companions-off" {model.settings["ambient_enabled"]=false}
        model.previewConnection(ready:!["checking","quiet","loading","loading-large","offline"].contains(screen))
        if ["saved","saved-large"].contains(screen) {model.previewSave(.saved(Date()))}
        if screen=="checking" {model.previewSave(.checking)}
        if ["checking","quiet","loading","loading-large"].contains(screen) {model.reconnecting=true;model.connectionState="Loading settings…"}
        if ["loading","loading-large"].contains(screen) {model.settings=[:];model.fields=[]}
        if screen=="offline" {model.message="Move closer and try again."}
        if screen=="operation" {model.busy=true;model.operationTitle="Refreshing settings…"}
        model.updateMessage="Sample state: firmware is up to date."
        return model
    }
}
#endif
