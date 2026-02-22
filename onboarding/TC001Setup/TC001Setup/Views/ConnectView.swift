import SwiftUI

/// Step 2: USB device detection.
///
/// Scans for TC001 connected via USB serial and lets the user select
/// the correct port if multiple are found.
struct ConnectView: View {

    @EnvironmentObject var state: SetupState
    @StateObject private var usbDetector = USBDetector()

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Connect Your TC001")
                    .font(.largeTitle.bold())

                Text("Plug the TC001 into your Mac using a USB-C cable. The device should appear below automatically.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            // Connection status
            VStack(alignment: .leading, spacing: 16) {
                HStack(spacing: 12) {
                    if usbDetector.isScanning {
                        ProgressView()
                            .controlSize(.small)
                        Text("Scanning for devices...")
                            .foregroundStyle(.secondary)
                    } else if usbDetector.detectedPorts.isEmpty {
                        Image(systemName: "cable.connector")
                            .font(.title2)
                            .foregroundStyle(.secondary)
                        Text("No device detected")
                            .foregroundStyle(.secondary)
                    }
                }

                if !usbDetector.detectedPorts.isEmpty {
                    Text("Detected Devices")
                        .font(.headline)

                    ForEach(usbDetector.detectedPorts, id: \.self) { port in
                        deviceRow(port: port)
                    }
                }
            }

            Divider()

            // Manual port entry
            VStack(alignment: .leading, spacing: 8) {
                Text("Manual Port Entry")
                    .font(.headline)

                Text("If your device is not detected automatically, you can enter the port path manually.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)

                HStack {
                    TextField("/dev/cu.usbserial-...", text: Binding(
                        get: { state.detectedPort ?? "" },
                        set: { state.detectedPort = $0.isEmpty ? nil : $0 }
                    ))
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.body, design: .monospaced))
                }
            }

            Spacer()

            // Tips
            GroupBox {
                VStack(alignment: .leading, spacing: 6) {
                    Label("Troubleshooting Tips", systemImage: "lightbulb")
                        .font(.subheadline.bold())

                    Text("Make sure you are using a data-capable USB cable (not charge-only).")
                        .font(.caption)
                    Text("Try a different USB port on your Mac.")
                        .font(.caption)
                    Text("You may need to install the CP210x or CH340 driver for your USB-serial chip.")
                        .font(.caption)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .onAppear {
            usbDetector.startScanning()
        }
        .onDisappear {
            usbDetector.stopScanning()
        }
        .onChange(of: usbDetector.detectedPorts) { newPorts in
            // Auto-select the first port if nothing is selected yet
            if state.detectedPort == nil, let first = newPorts.first {
                state.detectedPort = first
            }
        }
    }

    private func deviceRow(port: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: state.detectedPort == port
                ? "checkmark.circle.fill"
                : "circle")
                .foregroundColor(state.detectedPort == port ? .accentColor : .secondary)
                .font(.title3)

            VStack(alignment: .leading, spacing: 2) {
                Text(port)
                    .font(.system(size: 13, design: .monospaced))
                Text("USB Serial Device")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

            Spacer()
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(state.detectedPort == port
                    ? Color.accentColor.opacity(0.08)
                    : Color.clear)
        )
        .contentShape(Rectangle())
        .onTapGesture {
            state.detectedPort = port
        }
    }
}

#Preview {
    ConnectView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
