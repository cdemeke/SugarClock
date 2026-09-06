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
                    case "glucose":ConfigurationView(category:SettingsCategory.all[0])
                    case "wifi":WiFiView()
                    case "firmware":FirmwareView()
                    case "brightness":SettingEditor(field:["key":"brightness","type":"int","min":1,"max":255])
                    case "secret":SettingEditor(field:["key":"dexcom_password","type":"secret","max_length":63])
                    case "troubleshooting":TroubleshootingView()
                    default:DeviceView()
                    }
                }
            }
        }
        .tint(SugarTheme.accent)
        .dynamicTypeSize(screen=="display-accessibility" ? .accessibility3:.large)
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
            ["key":"auto_update_enabled","type":"bool"]
        ]
        model.networks=[["ssid":"Home Wi-Fi","rssi":-42],["ssid":"Guest Network","rssi":-61]]
        model.message="Sample clock state for screenshots. No Bluetooth connection."
        model.updateMessage="Sample state: firmware is up to date."
        return model
    }
}
#endif
