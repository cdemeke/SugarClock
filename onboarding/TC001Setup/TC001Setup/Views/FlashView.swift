import SwiftUI

/// Step 7: Build and flash firmware.
///
/// Runs through the staged flash process:
/// 1. Write config (inject defaults into src/config_manager.cpp)
/// 2. Build firmware (pio run)
/// 3. Build filesystem (pio run --target buildfs)
/// 4. Flash firmware (pio run --target upload)
/// 5. Flash filesystem (pio run --target uploadfs)
/// 6. Verify (wait for device to reboot and respond)
/// 7. Push remaining config via POST /api/config
struct FlashView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Build & Flash")
                    .font(.largeTitle.bold())

                Text("The firmware will now be configured, built, and flashed to your device.")
                    .font(.body)
                    .foregroundStyle(.secondary)
            }

            Divider()

            // Stage progress
            VStack(alignment: .leading, spacing: 10) {
                ForEach(stagesInOrder, id: \.self) { stage in
                    stageRow(stage)
                }
            }

            // Overall progress bar
            if state.isFlashing || state.flashStage == .complete {
                VStack(alignment: .leading, spacing: 4) {
                    ProgressView(value: state.flashProgress, total: 1.0)
                        .progressViewStyle(.linear)

                    Text("\(Int(state.flashProgress * 100))% complete")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            // Error message
            if let error = state.flashError {
                GroupBox {
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.red)
                        Text(error)
                            .font(.caption)
                            .foregroundColor(.red)
                            .textSelection(.enabled)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }

            // Log output
            VStack(alignment: .leading, spacing: 4) {
                Text("Build Log")
                    .font(.subheadline.bold())

                ScrollViewReader { proxy in
                    ScrollView {
                        Text(state.flashLog.isEmpty ? "Waiting to start..." : state.flashLog)
                            .font(.system(size: 11, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .textSelection(.enabled)
                            .id("logBottom")
                    }
                    .frame(maxHeight: 160)
                    .background(Color(nsColor: .textBackgroundColor).opacity(0.5))
                    .cornerRadius(6)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .strokeBorder(Color.secondary.opacity(0.2))
                    )
                    .onChange(of: state.flashLog) { _ in
                        proxy.scrollTo("logBottom", anchor: .bottom)
                    }
                }
            }

            Spacer()

            // Action buttons
            HStack {
                if state.flashStage == .idle || state.flashStage == .failed {
                    Button(action: { startFlash() }) {
                        HStack(spacing: 6) {
                            Image(systemName: "bolt.fill")
                            Text("Start Flashing")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                }

                if state.flashStage == .complete {
                    Label("Flashing complete!", systemImage: "checkmark.circle.fill")
                        .foregroundColor(.green)

                    Spacer()

                    Button(action: { state.goNext() }) {
                        HStack(spacing: 4) {
                            Text("Finish")
                            Image(systemName: "chevron.right")
                                .font(.system(size: 11, weight: .semibold))
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                }
            }
        }
    }

    // MARK: - Stage definitions

    private var stagesInOrder: [FlashStage] {
        [.writeConfig, .buildFirmware, .buildFilesystem, .flashFirmware, .flashFilesystem, .verify, .pushConfig]
    }

    private func stageRow(_ stage: FlashStage) -> some View {
        HStack(spacing: 10) {
            if stage == state.flashStage && state.isFlashing {
                ProgressView()
                    .controlSize(.small)
                    .frame(width: 18, height: 18)
            } else if stage.index < state.flashStage.index {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundColor(.green)
                    .frame(width: 18, height: 18)
            } else if state.flashStage == .failed && stage.index >= state.flashStage.index {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.red)
                    .frame(width: 18, height: 18)
            } else {
                Image(systemName: "circle")
                    .foregroundStyle(.secondary)
                    .frame(width: 18, height: 18)
            }

            Text(stage.rawValue)
                .font(.system(size: 13, weight: stage == state.flashStage ? .semibold : .regular))
                .foregroundStyle(stage == state.flashStage ? .primary : .secondary)
        }
    }

    // MARK: - Flash process

    private func startFlash() {
        state.isFlashing = true
        state.flashError = nil
        state.flashLog = ""
        state.flashStage = .writeConfig
        state.flashProgress = 0.0

        Task {
            let projectPath = state.projectPath
            guard !projectPath.isEmpty else {
                state.flashError = "Project path is not set. Go back to the Welcome step."
                state.flashStage = .failed
                state.isFlashing = false
                return
            }

            guard let port = state.detectedPort else {
                state.flashError = "No device port selected. Go back to the Connect step."
                state.flashStage = .failed
                state.isFlashing = false
                return
            }

            let totalStages = 7.0

            // Stage 1: Write config
            state.flashStage = .writeConfig
            state.flashProgress = 1.0 / totalStages
            appendLog(">>> Writing configuration to src/config_manager.cpp...\n")
            let configWritten = writeConfigFile(projectPath: projectPath)
            if !configWritten {
                failFlash("Failed to write configuration file.")
                return
            }
            appendLog("Configuration written successfully.\n\n")

            // Stage 2: Build firmware
            state.flashStage = .buildFirmware
            state.flashProgress = 2.0 / totalStages
            appendLog(">>> Building firmware...\n")
            let buildResult = await ProcessRunner.run(
                command: "pio run",
                workingDirectory: projectPath,
                environment: ["PLATFORMIO_UPLOAD_PORT": port]
            ) { text in
                appendLog(text)
            }
            if buildResult.exitCode != 0 {
                failFlash("Firmware build failed with exit code \(buildResult.exitCode).")
                return
            }
            appendLog("\nFirmware build successful.\n\n")

            // Stage 3: Build filesystem
            state.flashStage = .buildFilesystem
            state.flashProgress = 3.0 / totalStages
            appendLog(">>> Building filesystem image...\n")
            let fsResult = await ProcessRunner.run(
                command: "pio run --target buildfs",
                workingDirectory: projectPath,
                environment: ["PLATFORMIO_UPLOAD_PORT": port]
            ) { text in
                appendLog(text)
            }
            if fsResult.exitCode != 0 {
                failFlash("Filesystem build failed with exit code \(fsResult.exitCode).")
                return
            }
            appendLog("\nFilesystem build successful.\n\n")

            // Stage 4: Flash firmware
            state.flashStage = .flashFirmware
            state.flashProgress = 4.0 / totalStages
            appendLog(">>> Flashing firmware to device...\n")
            let uploadResult = await ProcessRunner.run(
                command: "pio run --target upload --upload-port \(port)",
                workingDirectory: projectPath,
                environment: ["PLATFORMIO_UPLOAD_PORT": port]
            ) { text in
                appendLog(text)
            }
            if uploadResult.exitCode != 0 {
                failFlash("Firmware flash failed with exit code \(uploadResult.exitCode).")
                return
            }
            appendLog("\nFirmware flashed successfully.\n\n")

            // Stage 5: Flash filesystem
            state.flashStage = .flashFilesystem
            state.flashProgress = 5.0 / totalStages
            appendLog(">>> Flashing filesystem to device...\n")
            let uploadFsResult = await ProcessRunner.run(
                command: "pio run --target uploadfs --upload-port \(port)",
                workingDirectory: projectPath,
                environment: ["PLATFORMIO_UPLOAD_PORT": port]
            ) { text in
                appendLog(text)
            }
            if uploadFsResult.exitCode != 0 {
                failFlash("Filesystem flash failed with exit code \(uploadFsResult.exitCode).")
                return
            }
            appendLog("\nFilesystem flashed successfully.\n\n")

            // Stage 6: Verify - wait for the device to boot and get an IP
            state.flashStage = .verify
            state.flashProgress = 6.0 / totalStages
            appendLog(">>> Waiting for device to boot (this may take 30-60 seconds)...\n")

            // Monitor serial for the IP address
            let ip = await waitForDeviceIP(port: port, projectPath: projectPath)
            if let ip = ip {
                state.deviceIP = ip
                appendLog("Device booted successfully at IP: \(ip)\n\n")
            } else {
                appendLog("Could not detect device IP automatically. You can find it in your router's DHCP table.\n\n")
            }

            // Stage 7: Push remaining config via HTTP
            state.flashStage = .pushConfig
            state.flashProgress = 6.5 / totalStages
            if !state.deviceIP.isEmpty {
                appendLog(">>> Pushing configuration to device via HTTP...\n")
                let pushed = await pushConfigToDevice()
                if pushed {
                    appendLog("Configuration pushed successfully.\n\n")
                } else {
                    appendLog("Warning: Could not push config via HTTP. You can configure via the web UI later.\n\n")
                }
            } else {
                appendLog("Skipping HTTP config push (no IP detected). Configure via the web UI.\n\n")
            }

            // Done
            state.flashStage = .complete
            state.flashProgress = 1.0
            state.isFlashing = false
            appendLog(">>> All done! Your SugarClock is ready.\n")
        }
    }

    private func appendLog(_ text: String) {
        state.flashLog += text
    }

    private func failFlash(_ message: String) {
        state.flashError = message
        state.flashStage = .failed
        state.isFlashing = false
        appendLog("\nERROR: \(message)\n")
    }

    // MARK: - Config injection

    /// Writes user configuration into src/config_manager.cpp by replacing
    /// default values in the source before building.
    private func writeConfigFile(projectPath: String) -> Bool {
        let configPath = "\(projectPath)/src/config_manager.cpp"
        let fm = FileManager.default

        guard fm.fileExists(atPath: configPath) else {
            appendLog("Warning: \(configPath) not found. Creating config overlay file instead.\n")
            return writeConfigOverlay(projectPath: projectPath)
        }

        do {
            var content = try String(contentsOfFile: configPath, encoding: .utf8)
            let config = state.buildConfigDictionary()

            // Replace known default patterns
            for (key, value) in config {
                let stringValue: String
                if let s = value as? String {
                    stringValue = s
                } else if let i = value as? Int {
                    stringValue = String(i)
                } else {
                    stringValue = "\(value)"
                }

                // Try to find and replace: defaults["key"] = "value"  or defaults["key"] = value
                let patterns = [
                    "defaults[\"\\(\(key))\"]\\s*=\\s*\"[^\"]*\"",
                    "defaults[\"\\(\(key))\"]\\s*=\\s*[^;]*",
                ]

                for pattern in patterns {
                    if let regex = try? NSRegularExpression(pattern: "defaults\\[\"" + NSRegularExpression.escapedPattern(for: key) + "\"\\]\\s*=\\s*\"[^\"]*\"") {
                        let range = NSRange(content.startIndex..., in: content)
                        let replacement: String
                        if value is String {
                            replacement = "defaults[\"\(key)\"] = \"\(stringValue)\""
                        } else {
                            replacement = "defaults[\"\(key)\"] = \"\(stringValue)\""
                        }
                        content = regex.stringByReplacingMatches(in: content, range: range, withTemplate: NSRegularExpression.escapedTemplate(for: replacement))
                    }
                }
            }

            try content.write(toFile: configPath, atomically: true, encoding: .utf8)
            return true
        } catch {
            appendLog("Error modifying config file: \(error.localizedDescription)\n")
            return false
        }
    }

    /// Fallback: write a config.json overlay that the firmware reads at boot.
    private func writeConfigOverlay(projectPath: String) -> Bool {
        let dataDir = "\(projectPath)/data"
        let configFile = "\(dataDir)/config.json"
        let fm = FileManager.default

        do {
            if !fm.fileExists(atPath: dataDir) {
                try fm.createDirectory(atPath: dataDir, withIntermediateDirectories: true)
            }

            let config = state.buildConfigDictionary()
            let jsonData = try JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys])
            try jsonData.write(to: URL(fileURLWithPath: configFile))
            appendLog("Wrote config overlay to \(configFile)\n")
            return true
        } catch {
            appendLog("Error writing config overlay: \(error.localizedDescription)\n")
            return false
        }
    }

    // MARK: - Device verification

    /// After flashing, monitors serial output for the device IP address.
    private func waitForDeviceIP(port: String, projectPath: String) async -> String? {
        // Wait a few seconds for the device to boot
        try? await Task.sleep(nanoseconds: 5_000_000_000)

        // Try to read serial output for up to 60 seconds looking for an IP
        let result = await ProcessRunner.run(
            command: "timeout 60 pio device monitor --port \(port) --baud 115200 --filter direct",
            workingDirectory: projectPath
        ) { text in
            appendLog(text)
            // Look for IP pattern in output
            if let range = text.range(of: #"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}"#, options: .regularExpression) {
                let ip = String(text[range])
                if ip != "0.0.0.0" && ip != "255.255.255.255" {
                    Task { @MainActor in
                        state.deviceIP = ip
                    }
                }
            }
        }

        // If we got an IP from the output, return it
        if !state.deviceIP.isEmpty {
            return state.deviceIP
        }

        // Fallback: check common mDNS name
        let mdnsResult = await ProcessRunner.run(command: "dns-sd -G v4 tc001.local")
        if mdnsResult.exitCode == 0,
           let range = mdnsResult.output.range(of: #"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}"#, options: .regularExpression) {
            return String(mdnsResult.output[range])
        }

        return nil
    }

    // MARK: - HTTP config push

    /// POST configuration to the device's HTTP API after boot.
    private func pushConfigToDevice() async -> Bool {
        guard !state.deviceIP.isEmpty else { return false }

        let url = "http://\(state.deviceIP)/api/config"
        let config = state.buildConfigDictionary()

        do {
            let jsonData = try JSONSerialization.data(withJSONObject: config, options: [])
            let jsonString = String(data: jsonData, encoding: .utf8) ?? "{}"

            let result = await ProcessRunner.run(
                command: "curl",
                arguments: [
                    "-s", "-X", "POST",
                    "-H", "Content-Type: application/json",
                    "-d", jsonString,
                    "--connect-timeout", "10",
                    url,
                ]
            )
            return result.exitCode == 0
        } catch {
            appendLog("Error serializing config for HTTP push: \(error.localizedDescription)\n")
            return false
        }
    }
}

#Preview {
    FlashView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 520)
}
