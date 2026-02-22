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

/// Stages during the install process.
enum FlashStage: String, CaseIterable, Identifiable {
    case idle = "Ready to install"
    case preparingConfig = "Saving your settings"
    case buildingFilesystem = "Preparing device files"
    case flashing = "Installing to device"
    case waitingForBoot = "Starting up"
    case pushingConfig = "Applying settings"
    case complete = "Done"
    case failed = "Something went wrong"

    var id: String { rawValue }

    /// A zero-based index used for progress calculation.
    var index: Int {
        switch self {
        case .idle: return 0
        case .preparingConfig: return 1
        case .buildingFilesystem: return 2
        case .flashing: return 3
        case .waitingForBoot: return 4
        case .pushingConfig: return 5
        case .complete: return 6
        case .failed: return -1
        }
    }
}

/// Central observable state object shared across all wizard steps.
@MainActor
final class SetupState: ObservableObject {

    // MARK: - Navigation

    @Published var currentStep: Int = 0
    let totalSteps: Int = 7

    // MARK: - Step 1: Connect

    @Published var detectedPort: String? = nil
    @Published var isScanning: Bool = false

    var isDeviceConnected: Bool {
        detectedPort != nil
    }

    // MARK: - Step 2: WiFi

    @Published var wifiSSID: String = ""
    @Published var wifiPassword: String = ""
    @Published var showPassword: Bool = false

    var isWiFiValid: Bool {
        !wifiSSID.trimmingCharacters(in: .whitespaces).isEmpty
    }

    // MARK: - Step 3: Data Source

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

    // MARK: - Step 4: Preferences

    @Published var timezone: String = TimeZone.current.identifier
    @Published var glucoseUnit: GlucoseUnit = .mgdl
    @Published var brightness: BrightnessPreset = .medium
    @Published var displayRotation: Int = 0  // 0, 90, 180, 270
    @Published var lowAlertThreshold: Int = 70
    @Published var highAlertThreshold: Int = 180

    // MARK: - Step 5: Install

    @Published var flashStage: FlashStage = .idle
    @Published var flashProgress: Double = 0.0
    @Published var flashLog: String = ""
    @Published var flashError: String? = nil
    @Published var isFlashing: Bool = false

    // MARK: - Step 6: Done

    @Published var deviceIP: String = ""
    @Published var isDeviceReachable: Bool = false

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
        case 0: return true
        case 1: return isDeviceConnected
        case 2: return isWiFiValid
        case 3: return isDataSourceValid
        case 4: return true  // preferences always valid
        case 5: return flashStage == .complete
        case 6: return true
        default: return false
        }
    }

    /// Returns the display title for a given step index.
    func stepTitle(for index: Int) -> String {
        switch index {
        case 0: return "Welcome"
        case 1: return "Connect"
        case 2: return "WiFi"
        case 3: return "Data Source"
        case 4: return "Preferences"
        case 5: return "Install"
        case 6: return "Done"
        default: return ""
        }
    }

    // MARK: - Config generation

    /// Builds the dictionary that gets written to config.json in the LittleFS image
    /// and later POST-ed to /api/config.
    func buildConfigDictionary() -> [String: Any] {
        var config: [String: Any] = [
            "wifi_ssid": wifiSSID,
            "wifi_password": wifiPassword,
            "timezone": timezone,
            "use_mmol": glucoseUnit == .mmol,
            "brightness": brightness.value,
            "alert_low": lowAlertThreshold,
            "alert_high": highAlertThreshold,
        ]

        switch glucoseSource {
        case .dexcom:
            config["data_source"] = 1
            config["dexcom_username"] = dexcomUsername
            config["dexcom_password"] = dexcomPassword
            config["dexcom_server"] = dexcomServer
        case .nightscout:
            config["data_source"] = 0
            config["server_url"] = nightscoutURL
            config["auth_token"] = nightscoutToken
        case .customURL:
            config["data_source"] = 0
            config["server_url"] = customURL
        }

        return config
    }
}
