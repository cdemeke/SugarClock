import SwiftUI

/// Step 8: Success screen with dashboard link and next steps.
struct DoneView: View {

    @EnvironmentObject var state: SetupState
    @State private var isCheckingReachability: Bool = false

    var body: some View {
        VStack(spacing: 24) {
            Spacer()

            // Success icon
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 64))
                .foregroundColor(.green)

            VStack(spacing: 8) {
                Text("Setup Complete!")
                    .font(.largeTitle.bold())

                Text("Your TC001 glucose monitor is configured and running.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }

            Divider()
                .frame(maxWidth: 300)

            // Device info
            VStack(spacing: 12) {
                if !state.deviceIP.isEmpty {
                    infoRow(label: "Device IP", value: state.deviceIP)
                    infoRow(label: "Dashboard", value: "http://\(state.deviceIP)")
                }
                infoRow(label: "WiFi Network", value: state.wifiSSID)
                infoRow(label: "Data Source", value: state.glucoseSource.rawValue)
                infoRow(label: "Units", value: state.glucoseUnit.rawValue)
            }
            .padding()
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(Color(nsColor: .controlBackgroundColor))
            )
            .frame(maxWidth: 400)

            // Open dashboard button
            if !state.deviceIP.isEmpty {
                HStack(spacing: 12) {
                    Button(action: { openDashboard() }) {
                        HStack(spacing: 6) {
                            Image(systemName: "globe")
                            Text("Open Dashboard")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)

                    Button(action: { checkReachability() }) {
                        HStack(spacing: 6) {
                            if isCheckingReachability {
                                ProgressView()
                                    .controlSize(.small)
                            }
                            Text("Check Connection")
                        }
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                    .disabled(isCheckingReachability)
                }

                if state.isDeviceReachable {
                    Label("Device is reachable", systemImage: "wifi")
                        .foregroundColor(.green)
                        .font(.callout)
                }
            }

            Spacer()

            // Next steps
            GroupBox {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Next Steps")
                        .font(.subheadline.bold())

                    bulletPoint("Place your TC001 where you can easily see the display.")
                    bulletPoint("The device will automatically fetch glucose data at regular intervals.")
                    bulletPoint("Use the web dashboard to adjust settings at any time.")
                    bulletPoint("If the device loses WiFi, it will reconnect automatically.")
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxWidth: 500)
        }
        .onAppear {
            if !state.deviceIP.isEmpty {
                checkReachability()
            }
        }
    }

    private func infoRow(label: String, value: String) -> some View {
        HStack {
            Text(label)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(.secondary)
                .frame(width: 120, alignment: .trailing)
            Text(value)
                .font(.system(size: 13, design: .monospaced))
                .textSelection(.enabled)
        }
    }

    private func bulletPoint(_ text: String) -> some View {
        HStack(alignment: .top, spacing: 6) {
            Text("\u{2022}")
                .font(.caption)
            Text(text)
                .font(.caption)
        }
    }

    private func openDashboard() {
        guard !state.deviceIP.isEmpty,
              let url = URL(string: "http://\(state.deviceIP)") else { return }
        NSWorkspace.shared.open(url)
    }

    private func checkReachability() {
        isCheckingReachability = true
        Task {
            let result = await ProcessRunner.run(
                command: "curl",
                arguments: [
                    "-s", "-o", "/dev/null", "-w", "%{http_code}",
                    "--connect-timeout", "5",
                    "http://\(state.deviceIP)/",
                ]
            )
            state.isDeviceReachable = result.exitCode == 0 && result.output.trimmingCharacters(in: .whitespacesAndNewlines) == "200"
            isCheckingReachability = false
        }
    }
}

#Preview {
    DoneView()
        .environmentObject({
            let s = SetupState()
            s.deviceIP = "192.168.1.42"
            s.wifiSSID = "MyNetwork"
            s.glucoseSource = .dexcom
            return s
        }())
        .frame(width: 560, height: 520)
}
