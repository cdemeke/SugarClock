import SwiftUI

/// Step 3: Optional firmware backup.
///
/// Lets the user back up the current firmware on the TC001 before flashing
/// new firmware. Uses esptool to read the flash contents.
struct BackupView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Backup Existing Firmware")
                    .font(.largeTitle.bold())

                Text("Optionally save a copy of the current firmware so you can restore it later if needed.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            // Toggle
            Toggle(isOn: $state.shouldBackup) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Create firmware backup")
                        .font(.headline)
                    Text("Recommended if the device has existing firmware you want to keep.")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
            }
            .toggleStyle(.switch)

            if state.shouldBackup {
                // Backup destination
                VStack(alignment: .leading, spacing: 8) {
                    Text("Save Location")
                        .font(.subheadline.bold())

                    HStack {
                        TextField("Backup file path", text: $state.backupPath)
                            .textFieldStyle(.roundedBorder)
                            .font(.system(.body, design: .monospaced))

                        Button("Browse...") {
                            let panel = NSSavePanel()
                            panel.nameFieldStringValue = "tc001_backup.bin"
                            panel.allowedContentTypes = [.data]
                            panel.message = "Choose where to save the firmware backup"
                            if panel.runModal() == .OK, let url = panel.url {
                                state.backupPath = url.path
                            }
                        }
                    }
                }

                // Backup action
                HStack(spacing: 12) {
                    Button(action: { startBackup() }) {
                        HStack(spacing: 6) {
                            if state.backupInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }
                            Text(state.backupInProgress ? "Backing up..." : "Start Backup")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(state.backupPath.isEmpty || state.backupInProgress || state.backupComplete)

                    if state.backupComplete {
                        Label("Backup saved successfully", systemImage: "checkmark.circle.fill")
                            .foregroundColor(.green)
                            .font(.callout)
                    }

                    if let error = state.backupError {
                        Label(error, systemImage: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                            .font(.callout)
                    }
                }
            }

            Spacer()

            if !state.shouldBackup {
                GroupBox {
                    HStack(spacing: 8) {
                        Image(systemName: "info.circle")
                            .foregroundColor(.blue)
                        Text("You can skip this step if the device is new or you do not need to restore the original firmware.")
                            .font(.caption)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
        .onAppear {
            if state.backupPath.isEmpty {
                let desktop = FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
                state.backupPath = desktop?.appendingPathComponent("tc001_backup.bin").path ?? "~/Desktop/tc001_backup.bin"
            }
        }
    }

    private func startBackup() {
        guard let port = state.detectedPort else { return }
        state.backupInProgress = true
        state.backupError = nil

        Task {
            let result = await ProcessRunner.run(
                command: "esptool.py",
                arguments: ["--port", port, "--baud", "921600", "read_flash", "0x0", "0x400000", state.backupPath]
            )

            if result.exitCode == 0 {
                state.backupComplete = true
            } else {
                // Try with 'esptool' instead of 'esptool.py'
                let fallback = await ProcessRunner.run(
                    command: "esptool",
                    arguments: ["--port", port, "--baud", "921600", "read_flash", "0x0", "0x400000", state.backupPath]
                )
                if fallback.exitCode == 0 {
                    state.backupComplete = true
                } else {
                    state.backupError = "Backup failed. Check the device connection."
                }
            }
            state.backupInProgress = false
        }
    }
}

#Preview {
    BackupView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
