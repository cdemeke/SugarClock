import Foundation
import CoreWLAN

/// Helper that reads the current WiFi SSID from macOS via CoreWLAN.
///
/// Note: Reading the SSID requires the app to have Location Services
/// permission on macOS 14+. If the permission is missing, the SSID
/// will return nil, and the user can type it manually.
final class WiFiHelper {

    /// Returns the SSID of the currently connected WiFi network, or nil.
    static func currentSSID() -> String? {
        guard let wifiClient = CWWiFiClient.shared().interface() else {
            return nil
        }
        return wifiClient.ssid()
    }

    /// Returns the BSSID of the currently connected WiFi network, or nil.
    static func currentBSSID() -> String? {
        guard let wifiClient = CWWiFiClient.shared().interface() else {
            return nil
        }
        return wifiClient.bssid()
    }

    /// Returns the name of the WiFi interface (usually "en0").
    static func interfaceName() -> String? {
        return CWWiFiClient.shared().interface()?.interfaceName
    }

    /// Returns true if WiFi is powered on.
    static func isWiFiPoweredOn() -> Bool {
        guard let wifiClient = CWWiFiClient.shared().interface() else {
            return false
        }
        return wifiClient.powerOn()
    }

    /// Returns a list of all available WiFi interface names.
    static func allInterfaceNames() -> [String] {
        return CWWiFiClient.shared().interfaceNames() ?? []
    }
}
