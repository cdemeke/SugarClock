import Foundation
import Combine

/// Represents which glucose data source the user has chosen.
enum GlucoseSource: String, CaseIterable, Identifiable {
    case dexcom = "Dexcom Share"
    case nightscout = "Nightscout"
    case customURL = "Custom URL"

    var id: String { rawValue }
}

/// Glucose display units.
enum GlucoseUnit: String, CaseIterable, Identifiable {
    case mgdl = "mg/dL"
    case mmol = "mmol/L"

    var id: String { rawValue }
}

/// Display brightness preset.
enum BrightnessPreset: String, CaseIterable, Identifiable {
    case low = "Low"
    case medium = "Medium"
    case high = "High"

    var id: String { rawValue }

    var value: Int {
        switch self {
        case .low: return 40
        case .medium: return 128
        case .high: return 255
        }
    }
}

/// Stages during the flash process.
enum FlashStage: String, CaseIterable, Identifiable {
    case idle = "Waiting to start"
    case writeConfig = "Writing configuration"
    case buildFirmware = "Building firmware"
    case buildFilesystem = "Building filesystem"
    case flashFirmware = "Flashing firmware"
    case flashFilesystem = "Flashing filesystem"
    case verify = "Verifying installation"
    case pushConfig = "Pushing config to device"
    case complete = "Complete"
    case failed = "Failed"

    var id: String { rawValue }

    /// A zero-based index used for progress calculation.
    var index: Int {
        switch self {
        case .idle: return 0
        case .writeConfig: return 1
        case .buildFirmware: return 2
        case .buildFilesystem: return 3
        case .flashFirmware: return 4
        case .flashFilesystem: return 5
        case .verify: return 6
        case .pushConfig: return 7
        case .complete: return 8
        case .failed: return -1
        }
    }
}

/// Central observable state object shared across all wizard steps.
@MainActor
final class SetupState: ObservableObject {

    // MARK: - Navigation

    @Published var currentStep: Int = 0
    let totalSteps: Int = 8

    // MARK: - Step 1: Welcome / Prerequisites

    @Published var hasPlatformIO: Bool = false
    @Published var hasEsptool: Bool = false
    @Published var hasGit: Bool = false
    @Published var isCheckingPrereqs: Bool = false

    var allPrereqsMet: Bool {
        hasPlatformIO && hasEsptool && hasGit
    }

    // MARK: - Step 2: Connect

    @Published var detectedPort: String? = nil
    @Published var isScanning: Bool = false

    var isDeviceConnected: Bool {
        detectedPort != nil
    }

    // MARK: - Step 3: Backup

    @Published var shouldBackup: Bool = true
    @Published var backupPath: String = ""
    @Published var backupInProgress: Bool = false
    @Published var backupComplete: Bool = false
    @Published var backupError: String? = nil

    // MARK: - Step 4: WiFi

    @Published var wifiSSID: String = ""
    @Published var wifiPassword: String = ""
    @Published var showPassword: Bool = false

    var isWiFiValid: Bool {
        !wifiSSID.trimmingCharacters(in: .whitespaces).isEmpty
    }

    // MARK: - Step 5: Data Source

    @Published var glucoseSource: GlucoseSource = .dexcom

    // Dexcom
    @Published var dexcomUsername: String = ""
    @Published var dexcomPassword: String = ""
    @Published var dexcomServer: String = "US"  // "US" or "OUS"

    // Nightscout / Custom URL
    @Published var nightscoutURL: String = ""
    @Published var nightscoutToken: String = ""
    @Published var customURL: String = ""

    var isDataSourceValid: Bool {
        switch glucoseSource {
        case .dexcom:
            return !dexcomUsername.isEmpty && !dexcomPassword.isEmpty
        case .nightscout:
            return !nightscoutURL.isEmpty
        case .customURL:
            return !customURL.isEmpty
        }
    }

    // MARK: - Step 6: Preferences

    @Published var timezone: String = TimeZone.current.identifier
    @Published var glucoseUnit: GlucoseUnit = .mgdl
    @Published var brightness: BrightnessPreset = .medium
    @Published var displayRotation: Int = 0  // 0, 90, 180, 270
    @Published var lowAlertThreshold: Int = 70
    @Published var highAlertThreshold: Int = 180

    // MARK: - Step 7: Flash

    @Published var flashStage: FlashStage = .idle
    @Published var flashProgress: Double = 0.0
    @Published var flashLog: String = ""
    @Published var flashError: String? = nil
    @Published var isFlashing: Bool = false

    // MARK: - Step 8: Done

    @Published var deviceIP: String = ""
    @Published var isDeviceReachable: Bool = false

    // MARK: - Project path

    @Published var projectPath: String = ""

    // MARK: - Navigation helpers

    func goNext() {
        if currentStep < totalSteps - 1 {
            currentStep += 1
        }
    }

    func goBack() {
        if currentStep > 0 {
            currentStep -= 1
        }
    }

    /// Whether the Next button should be enabled for the current step.
    var canAdvance: Bool {
        switch currentStep {
        case 0: return allPrereqsMet
        case 1: return isDeviceConnected
        case 2: return !shouldBackup || backupComplete
        case 3: return isWiFiValid
        case 4: return isDataSourceValid
        case 5: return true  // preferences always valid
        case 6: return flashStage == .complete
        case 7: return true
        default: return false
        }
    }

    /// Returns the display title for a given step index.
    func stepTitle(for index: Int) -> String {
        switch index {
        case 0: return "Welcome"
        case 1: return "Connect"
        case 2: return "Backup"
        case 3: return "WiFi"
        case 4: return "Data Source"
        case 5: return "Preferences"
        case 6: return "Flash"
        case 7: return "Done"
        default: return ""
        }
    }

    // MARK: - Config generation

    /// Builds the dictionary that gets written to src/config_manager.cpp defaults
    /// and later POST-ed to /api/config.
    func buildConfigDictionary() -> [String: Any] {
        var config: [String: Any] = [
            "wifi_ssid": wifiSSID,
            "wifi_password": wifiPassword,
            "timezone": timezone,
            "glucose_unit": glucoseUnit.rawValue,
            "brightness": brightness.value,
            "display_rotation": displayRotation,
            "low_alert": lowAlertThreshold,
            "high_alert": highAlertThreshold,
        ]

        switch glucoseSource {
        case .dexcom:
            config["data_source"] = "dexcom"
            config["dexcom_username"] = dexcomUsername
            config["dexcom_password"] = dexcomPassword
            config["dexcom_server"] = dexcomServer
        case .nightscout:
            config["data_source"] = "nightscout"
            config["nightscout_url"] = nightscoutURL
            config["nightscout_token"] = nightscoutToken
        case .customURL:
            config["data_source"] = "custom"
            config["custom_url"] = customURL
        }

        return config
    }
}
