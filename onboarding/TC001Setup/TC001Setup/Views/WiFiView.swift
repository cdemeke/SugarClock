import SwiftUI

/// Step 3: WiFi configuration.
///
/// Shows nearby WiFi networks for selection and captures the password.
/// Falls back to manual entry if scanning is unavailable.
struct WiFiView: View {

    @EnvironmentObject var state: SetupState
    @State private var availableNetworks: [String] = []
    @State private var isScanning: Bool = false
    @State private var showManualEntry: Bool = false
    @State private var scanAttempted: Bool = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("WiFi Network")
                    .font(.largeTitle.bold())

                Text("Choose the WiFi network your SugarClock should connect to. This is how it goes online to fetch your glucose data.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            GroupBox {
                HStack(alignment: .center, spacing: 12) {
                    Image(systemName: state.configureWiFiOnDevice ? "iphone.and.arrow.forward" : "building.2")
                        .foregroundStyle(state.configureWiFiOnDevice ? Color.accentColor : Color.secondary)

                    VStack(alignment: .leading, spacing: 3) {
                        Text(state.configureWiFiOnDevice ? "WiFi setup deferred" : "Using school or enterprise WiFi?")
                            .font(.subheadline.bold())
                        Text(state.configureWiFiOnDevice
                             ? "After installation, join SugarClock-Setup from your phone and open 192.168.4.1."
                             : "You can install now and choose the network later, where the clock can see it.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Spacer()

                    Button(state.configureWiFiOnDevice ? "Set up here instead" : "Skip — I’ll set this up on the device") {
                        state.configureWiFiOnDevice.toggle()
                        if state.configureWiFiOnDevice {
                            state.wifiSSID = ""
                            state.wifiPassword = ""
                        } else {
                            scanNetworks()
                        }
                    }
                    .buttonStyle(.borderedProminent)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // Network selection
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("Network")
                        .font(.headline)

                    Spacer()

                    if !showManualEntry {
                        Button(action: { scanNetworks() }) {
                            HStack(spacing: 4) {
                                if isScanning {
                                    ProgressView()
                                        .controlSize(.mini)
                                }
                                Text(isScanning ? "Scanning..." : "Refresh")
                            }
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .disabled(isScanning)
                    }
                }

                if showManualEntry {
                    // Manual entry mode
                    TextField("WiFi network name", text: $state.wifiSSID)
                        .textFieldStyle(.roundedBorder)

                    Button(action: {
                        showManualEntry = false
                        scanNetworks()
                    }) {
                        Text("Show available networks")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                } else if !availableNetworks.isEmpty {
                    // Network list
                    ScrollView {
                        VStack(spacing: 2) {
                            ForEach(availableNetworks, id: \.self) { ssid in
                                networkRow(ssid: ssid)
                            }
                        }
                    }
                    .frame(maxHeight: 160)
                    .background(Color(nsColor: .controlBackgroundColor))
                    .cornerRadius(8)

                    Button(action: { showManualEntry = true }) {
                        Text("Enter network name manually")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                } else if isScanning {
                    // Scanning in progress
                    HStack(spacing: 8) {
                        ProgressView()
                            .controlSize(.small)
                        Text("Looking for nearby networks...")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 8)
                } else {
                    // Scan returned nothing
                    VStack(alignment: .leading, spacing: 8) {
                        TextField("WiFi network name", text: $state.wifiSSID)
                            .textFieldStyle(.roundedBorder)

                        Text("No nearby networks found. Type your network name above, or try scanning again.")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                }
            }
            .disabled(state.configureWiFiOnDevice)
            .opacity(state.configureWiFiOnDevice ? 0.45 : 1)

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

                Text("Leave blank for open networks.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .disabled(state.configureWiFiOnDevice)
            .opacity(state.configureWiFiOnDevice ? 0.45 : 1)

            Spacer()

            // Note
            GroupBox {
                VStack(alignment: .leading, spacing: 6) {
                    Label("Good to know", systemImage: "info.circle")
                        .font(.subheadline.bold())

                    Text("SugarClock only supports 2.4 GHz WiFi networks. 5 GHz networks will not work.")
                        .font(.caption)
                    Text("Make sure the network is available where you plan to place the device.")
                        .font(.caption)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .onAppear {
            if state.wifiSSID.isEmpty {
                // Pre-fill with the currently connected network
                if let ssid = WiFiHelper.currentSSID() {
                    state.wifiSSID = ssid
                }
            }
            if !state.configureWiFiOnDevice {
                scanNetworks()
            }
        }
    }

    // MARK: - Network scanning

    private func scanNetworks() {
        isScanning = true
        DispatchQueue.global(qos: .userInitiated).async {
            let networks = WiFiHelper.scanForNetworks()
            DispatchQueue.main.async {
                availableNetworks = networks
                isScanning = false
                scanAttempted = true
            }
        }
    }

    private func networkRow(ssid: String) -> some View {
        let isSelected = state.wifiSSID == ssid
        return HStack(spacing: 10) {
            Image(systemName: isSelected ? "wifi.circle.fill" : "wifi")
                .foregroundColor(isSelected ? .accentColor : .secondary)
                .font(.body)

            Text(ssid)
                .font(.system(size: 13))

            Spacer()

            if isSelected {
                Image(systemName: "checkmark")
                    .foregroundColor(.accentColor)
                    .font(.system(size: 12, weight: .semibold))
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isSelected
                    ? Color.accentColor.opacity(0.08)
                    : Color.clear)
        )
        .contentShape(Rectangle())
        .onTapGesture {
            state.wifiSSID = ssid
        }
    }
}

#Preview {
    WiFiView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
