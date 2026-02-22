import Foundation
import CoreWLAN

/// Helper that reads WiFi information from macOS.
///
/// Uses CoreWLAN when Location Services is available, with shell-command
/// fallbacks (`airport -s`, `networksetup`) so the network list works
/// even without Location Services permission.
final class WiFiHelper {

    // MARK: - Public API

    /// Returns the SSID of the currently connected WiFi network, or nil.
    static func currentSSID() -> String? {
        // Try CoreWLAN first (needs Location Services on macOS 14+)
        if let ssid = CWWiFiClient.shared().interface()?.ssid(), !ssid.isEmpty {
            return ssid
        }
        // Fallback: parse `networksetup` output
        return currentSSIDViaCommand()
    }

    /// Scans for nearby WiFi networks and returns their SSIDs (deduplicated, sorted).
    /// Tries multiple methods and merges results.
    static func scanForNetworks() -> [String] {
        var allSSIDs = Set<String>()

        // 1. Try CoreWLAN cached results (less permission-sensitive)
        allSSIDs.formUnion(cachedCoreWLANResults())

        // 2. Try CoreWLAN active scan
        if allSSIDs.isEmpty {
            allSSIDs.formUnion(activeCoreWLANScan())
        }

        // 3. Try the airport command
        if allSSIDs.isEmpty {
            allSSIDs.formUnion(scanViaAirportCommand())
        }

        // 4. Always include the currently connected network
        if let current = currentSSID() {
            allSSIDs.insert(current)
        }

        return allSSIDs.sorted()
    }

    /// Returns true if WiFi is powered on.
    static func isWiFiPoweredOn() -> Bool {
        CWWiFiClient.shared().interface()?.powerOn() ?? false
    }

    // MARK: - CoreWLAN

    private static func cachedCoreWLANResults() -> [String] {
        guard let interface = CWWiFiClient.shared().interface(),
              let cached = interface.cachedScanResults(), !cached.isEmpty else {
            return []
        }
        return cached.compactMap { $0.ssid }.filter { !$0.isEmpty }
    }

    private static func activeCoreWLANScan() -> [String] {
        guard let interface = CWWiFiClient.shared().interface() else { return [] }
        do {
            let networks = try interface.scanForNetworks(withName: nil)
            return networks.compactMap { $0.ssid }.filter { !$0.isEmpty }
        } catch {
            return []
        }
    }

    // MARK: - Shell command fallbacks

    private static func currentSSIDViaCommand() -> String? {
        let output = runCommand("/usr/sbin/networksetup", arguments: ["-getairportnetwork", "en0"])
        // Output: "Current Wi-Fi Network: NetworkName"
        // Or:     "You are not associated with an AirPort network."
        guard let line = output.components(separatedBy: "\n").first,
              line.contains(": ") else { return nil }
        let parts = line.components(separatedBy: ": ")
        guard parts.count >= 2 else { return nil }
        let ssid = parts.dropFirst().joined(separator: ": ").trimmingCharacters(in: .whitespaces)
        return ssid.isEmpty ? nil : ssid
    }

    private static func scanViaAirportCommand() -> [String] {
        let airportPath = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport"
        guard FileManager.default.fileExists(atPath: airportPath) else { return [] }

        let output = runCommand(airportPath, arguments: ["-s"])
        // First line is a header row, then each subsequent line has:
        //     SSID (right-padded)  BSSID (aa:bb:cc:dd:ee:ff)  RSSI  CHANNEL ...
        // SSIDs can contain spaces so we locate the BSSID pattern to split.
        var ssids: [String] = []
        let lines = output.components(separatedBy: "\n")
        for (index, line) in lines.enumerated() {
            if index == 0 { continue }
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty { continue }
            // Find the BSSID (MAC address) to determine where the SSID ends
            if let range = line.range(of: #"[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}"#, options: .regularExpression) {
                let ssid = String(line[line.startIndex..<range.lowerBound]).trimmingCharacters(in: .whitespaces)
                if !ssid.isEmpty {
                    ssids.append(ssid)
                }
            }
        }
        return ssids
    }

    /// Synchronous helper to run a command and capture stdout.
    private static func runCommand(_ command: String, arguments: [String]) -> String {
        let process = Process()
        let pipe = Pipe()

        process.executableURL = URL(fileURLWithPath: command)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice

        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            return ""
        }

        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8) ?? ""
    }
}
