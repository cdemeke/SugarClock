import SwiftUI

/// Optional firmware backup (not currently in the setup flow).
///
/// Kept for potential future use. Uses the bundled esptool to read flash contents.
struct BackupView: View {

    @EnvironmentObject var state: SetupState

    @State private var shouldBackup: Bool = true
    @State private var backupPath: String = ""
    @State private var backupInProgress: Bool = false
    @State private var backupComplete: Bool = false
    @State private var backupError: String? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            VStack(alignment: .leading, spacing: 8) {
                Text("Backup Existing Software")
                    .font(.largeTitle.bold())

                Text("Optionally save a copy of what's currently on the device so you can restore it later.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            Toggle(isOn: $shouldBackup) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Create a backup")
                        .font(.headline)
                    Text("Recommended if the device has existing software you want to keep.")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
            }
            .toggleStyle(.switch)

            if shouldBackup {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Save Location")
                        .font(.subheadline.bold())

                    HStack {
                        TextField("Backup file path", text: $backupPath)
                            .textFieldStyle(.roundedBorder)
                            .font(.system(.body, design: .monospaced))

                        Button("Browse...") {
                            let panel = NSSavePanel()
                            panel.nameFieldStringValue = "sugarclock_backup.bin"
                            panel.allowedContentTypes = [.data]
                            panel.message = "Choose where to save the backup"
                            if panel.runModal() == .OK, let url = panel.url {
                                backupPath = url.path
                            }
                        }
                    }
                }

                HStack(spacing: 12) {
                    Button(action: { startBackup() }) {
                        HStack(spacing: 6) {
                            if backupInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }
                            Text(backupInProgress ? "Saving..." : "Save Backup")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(backupPath.isEmpty || backupInProgress || backupComplete)

                    if backupComplete {
                        Label("Backup saved", systemImage: "checkmark.circle.fill")
                            .foregroundColor(.green)
                            .font(.callout)
                    }

                    if let error = backupError {
                        Label(error, systemImage: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                            .font(.callout)
                    }
                }
            }

            Spacer()
        }
        .onAppear {
            if backupPath.isEmpty {
                let desktop = FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
                backupPath = desktop?.appendingPathComponent("sugarclock_backup.bin").path ?? "~/Desktop/sugarclock_backup.bin"
            }
        }
    }

    private func startBackup() {
        guard let port = state.detectedPort else { return }
        guard let esptool = FirmwareManager.esptoolPath else {
            backupError = "Bundled esptool not found."
            return
        }

        backupInProgress = true
        backupError = nil

        Task {
            do {
                try FirmwareManager.ensureToolsExecutable()
            } catch {
                backupError = "Could not prepare tools: \(error.localizedDescription)"
                backupInProgress = false
                return
            }

            let result = await ProcessRunner.run(
                command: esptool,
                arguments: ["--chip", "esp32", "--port", port, "--baud", "921600", "read_flash", "0x0", "0x400000", backupPath]
            )

            if result.exitCode == 0 {
                backupComplete = true
            } else {
                backupError = "Backup failed. Check the device connection."
            }
            backupInProgress = false
        }
    }
}

#Preview {
    BackupView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
