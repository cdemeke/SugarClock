import SwiftUI

/// Step 6: Install SugarClock to the device.
///
/// Runs through the staged install process:
/// 1. Save settings (write config.json + copy web UI to temp dir)
/// 2. Prepare device files (mklittlefs)
/// 3. Install to device (esptool writes all partitions in one command)
/// 4. Wait for device to start up
/// 5. Apply settings via HTTP as belt-and-suspenders
struct FlashView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Install SugarClock")
                    .font(.largeTitle.bold())

                Text("Your settings and software will be installed to the device. This takes about a minute.")
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
                Text("Details")
                    .font(.subheadline.bold())

                ScrollViewReader { proxy in
                    ScrollView {
                        Text(state.flashLog.isEmpty ? "Ready to install..." : state.flashLog)
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
                    Button(action: { startInstall() }) {
                        HStack(spacing: 6) {
                            Image(systemName: "arrow.down.circle.fill")
                            Text(state.flashStage == .failed ? "Retry" : "Install")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                }

                if state.flashStage == .complete {
                    Label("Installation complete!", systemImage: "checkmark.circle.fill")
                        .foregroundColor(.green)

                    Spacer()

                    Button(action: { state.goNext() }) {
                        HStack(spacing: 4) {
                            Text("Continue")
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
        [.preparingConfig, .buildingFilesystem, .flashing, .waitingForBoot, .pushingConfig]
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

    // MARK: - Install process

    private func startInstall() {
        state.isFlashing = true
        state.flashError = nil
        state.flashLog = ""
        state.flashStage = .preparingConfig
        state.flashProgress = 0.0

        Task {
            guard let port = state.detectedPort else {
                failInstall("No device connected. Go back to the Connect step and plug in your device.")
                return
            }

            // Ensure bundled tools are executable
            do {
                try FirmwareManager.ensureToolsExecutable()
            } catch {
                failInstall("Could not prepare installation tools: \(error.localizedDescription)")
                return
            }

            let totalStages = 5.0

            // Stage 1: Prepare config
            state.flashStage = .preparingConfig
            state.flashProgress = 1.0 / totalStages
            appendLog("Saving your settings...\n")
            let config = state.buildConfigDictionary()
            appendLog("Settings ready.\n\n")

            // Stage 2: Build LittleFS image
            state.flashStage = .buildingFilesystem
            state.flashProgress = 2.0 / totalStages
            appendLog("Preparing device files...\n")

            let littlefsPath: String
            do {
                littlefsPath = try await FirmwareManager.buildLittleFSImage(config: config) { text in
                    appendLog(text)
                }
            } catch {
                failInstall("Could not prepare device files: \(error.localizedDescription)")
                return
            }
            appendLog("Device files ready.\n\n")

            // Stage 3: Flash all partitions
            state.flashStage = .flashing
            state.flashProgress = 3.0 / totalStages
            appendLog("Installing software to your device...\n")

            do {
                try await FirmwareManager.flashDevice(port: port, littlefsPath: littlefsPath) { text in
                    appendLog(text)
                }
            } catch {
                failInstall("Installation failed: \(error.localizedDescription)")
                return
            }
            appendLog("\nInstallation complete.\n\n")

            // Stage 4: Wait for device boot
            state.flashStage = .waitingForBoot
            state.flashProgress = 4.0 / totalStages
            appendLog("Waiting for your device to start up...\n")

            let ip = await waitForDeviceIP(port: port)
            if let ip = ip {
                state.deviceIP = ip
                appendLog("Device is online at \(ip)\n\n")
            } else {
                appendLog("Could not detect device address automatically. You can find it in your router settings.\n\n")
            }

            // Stage 5: Push config via HTTP (belt-and-suspenders)
            state.flashStage = .pushingConfig
            state.flashProgress = 4.5 / totalStages
            if !state.deviceIP.isEmpty {
                appendLog("Applying settings to device...\n")
                let pushed = await pushConfigToDevice()
                if pushed {
                    appendLog("Settings applied.\n\n")
                } else {
                    appendLog("Could not apply settings over the network. Your device will use the settings from the install.\n\n")
                }
            } else {
                appendLog("Skipping network settings push. Your device will use the settings from the install.\n\n")
            }

            // Done
            state.flashStage = .complete
            state.flashProgress = 1.0
            state.isFlashing = false
            FirmwareManager.cleanupTempFiles()
            appendLog("All done! Your SugarClock is ready.\n")
        }
    }

    private func appendLog(_ text: String) {
        state.flashLog += text
    }

    private func failInstall(_ message: String) {
        state.flashError = message
        state.flashStage = .failed
        state.isFlashing = false
        FirmwareManager.cleanupTempFiles()
        appendLog("\nError: \(message)\n")
    }

    // MARK: - Device verification

    /// After installing, polls mDNS for the device IP address.
    private func waitForDeviceIP(port: String) async -> String? {
        // Wait for the device to boot
        try? await Task.sleep(nanoseconds: 8_000_000_000)

        // Try mDNS resolution a few times
        for attempt in 1...6 {
            appendLog("Looking for device (attempt \(attempt) of 6)...\n")

            let result = await ProcessRunner.run(
                command: "timeout",
                arguments: ["5", "dns-sd", "-G", "v4", "sugarclock.local"]
            )

            if result.exitCode == 0,
               let range = result.output.range(of: #"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}"#, options: .regularExpression) {
                let ip = String(result.output[range])
                if ip != "0.0.0.0" && ip != "255.255.255.255" {
                    return ip
                }
            }

            try? await Task.sleep(nanoseconds: 5_000_000_000)
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
            appendLog("Could not send settings: \(error.localizedDescription)\n")
            return false
        }
    }
}

#Preview {
    FlashView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 520)
}
