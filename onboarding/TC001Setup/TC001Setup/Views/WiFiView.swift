import SwiftUI

/// Step 4: WiFi configuration.
///
/// Captures the WiFi SSID and password. Attempts to auto-detect the
/// current SSID via CoreWLAN.
struct WiFiView: View {

    @EnvironmentObject var state: SetupState
    @State private var autoDetectedSSID: String? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("WiFi Network")
                    .font(.largeTitle.bold())

                Text("Enter the WiFi credentials the TC001 should connect to. This is the network where the device will fetch glucose data.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            // SSID
            VStack(alignment: .leading, spacing: 8) {
                Text("Network Name (SSID)")
                    .font(.headline)

                HStack {
                    TextField("WiFi network name", text: $state.wifiSSID)
                        .textFieldStyle(.roundedBorder)

                    if let detected = autoDetectedSSID, detected != state.wifiSSID {
                        Button("Use Current") {
                            state.wifiSSID = detected
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }
                }

                if let detected = autoDetectedSSID {
                    Text("Your Mac is connected to: \(detected)")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
            }

            // Password
            VStack(alignment: .leading, spacing: 8) {
                Text("Password")
                    .font(.headline)

                HStack {
                    if state.showPassword {
                        TextField("WiFi password", text: $state.wifiPassword)
                            .textFieldStyle(.roundedBorder)
                    } else {
                        SecureField("WiFi password", text: $state.wifiPassword)
                            .textFieldStyle(.roundedBorder)
                    }

                    Button(action: { state.showPassword.toggle() }) {
                        Image(systemName: state.showPassword ? "eye.slash" : "eye")
                    }
                    .buttonStyle(.borderless)
                    .help(state.showPassword ? "Hide password" : "Show password")
                }

                Text("The password will be written to the device configuration. Leave blank for open networks.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

            Spacer()

            // Warnings
            GroupBox {
                VStack(alignment: .leading, spacing: 6) {
                    Label("Important Notes", systemImage: "exclamationmark.triangle")
                        .font(.subheadline.bold())

                    Text("The TC001 supports 2.4 GHz WiFi networks only. 5 GHz networks will not work.")
                        .font(.caption)
                    Text("Make sure the WiFi network is available where the TC001 will be placed.")
                        .font(.caption)
                    Text("The password is stored in plaintext on the device.")
                        .font(.caption)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .onAppear {
            // Try to detect current SSID
            autoDetectedSSID = WiFiHelper.currentSSID()
            if state.wifiSSID.isEmpty, let ssid = autoDetectedSSID {
                state.wifiSSID = ssid
            }
        }
    }
}

#Preview {
    WiFiView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
