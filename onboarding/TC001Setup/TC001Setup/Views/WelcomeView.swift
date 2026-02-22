import SwiftUI

/// Step 1: Welcome screen with prerequisite checks.
///
/// Checks that PlatformIO CLI, esptool, and git are installed.
struct WelcomeView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Welcome to SugarClock Setup")
                    .font(.largeTitle.bold())

                Text("This wizard will help you configure and flash your SugarClock glucose monitor. It only takes a few minutes.")
                    .font(.body)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Divider()

            // Project path
            VStack(alignment: .leading, spacing: 6) {
                Text("SugarClock Project Folder")
                    .font(.headline)

                HStack {
                    TextField("Path to tc001 project root", text: $state.projectPath)
                        .textFieldStyle(.roundedBorder)

                    Button("Browse...") {
                        let panel = NSOpenPanel()
                        panel.canChooseFiles = false
                        panel.canChooseDirectories = true
                        panel.allowsMultipleSelection = false
                        panel.message = "Select the SugarClock project root directory"
                        if panel.runModal() == .OK, let url = panel.url {
                            state.projectPath = url.path
                        }
                    }
                }

                Text("This should contain platformio.ini and the src/ folder.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

            Divider()

            // Prerequisites
            VStack(alignment: .leading, spacing: 12) {
                Text("Prerequisites")
                    .font(.headline)

                Text("The following tools must be installed on your Mac:")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                prerequisiteRow(
                    name: "PlatformIO CLI (pio)",
                    detail: "Install via: pip install platformio",
                    isInstalled: state.hasPlatformIO
                )

                prerequisiteRow(
                    name: "esptool",
                    detail: "Install via: pip install esptool",
                    isInstalled: state.hasEsptool
                )

                prerequisiteRow(
                    name: "Git",
                    detail: "Install via: xcode-select --install",
                    isInstalled: state.hasGit
                )
            }

            Spacer()

            HStack {
                Button(action: { checkPrerequisites() }) {
                    HStack(spacing: 6) {
                        if state.isCheckingPrereqs {
                            ProgressView()
                                .controlSize(.small)
                        }
                        Text(state.isCheckingPrereqs ? "Checking..." : "Check Prerequisites")
                    }
                }
                .buttonStyle(.bordered)
                .disabled(state.isCheckingPrereqs)

                if state.allPrereqsMet {
                    Label("All prerequisites met", systemImage: "checkmark.circle.fill")
                        .foregroundColor(.green)
                        .font(.callout)
                }
            }
        }
        .onAppear {
            if !state.allPrereqsMet {
                checkPrerequisites()
            }
            // Pre-fill project path if empty
            if state.projectPath.isEmpty {
                let candidate = FileManager.default.currentDirectoryPath
                if FileManager.default.fileExists(atPath: "\(candidate)/platformio.ini") {
                    state.projectPath = candidate
                } else {
                    // Try the parent of this app bundle
                    let bundlePath = Bundle.main.bundlePath
                    let parent = (bundlePath as NSString).deletingLastPathComponent
                    let grandparent = (parent as NSString).deletingLastPathComponent
                    let greatGrandparent = (grandparent as NSString).deletingLastPathComponent
                    for dir in [parent, grandparent, greatGrandparent] {
                        if FileManager.default.fileExists(atPath: "\(dir)/platformio.ini") {
                            state.projectPath = dir
                            break
                        }
                    }
                }
            }
        }
    }

    private func prerequisiteRow(name: String, detail: String, isInstalled: Bool) -> some View {
        HStack(spacing: 10) {
            Image(systemName: isInstalled ? "checkmark.circle.fill" : "xmark.circle")
                .foregroundColor(isInstalled ? .green : .red)
                .font(.title3)

            VStack(alignment: .leading, spacing: 2) {
                Text(name)
                    .font(.system(size: 13, weight: .medium))
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }

    private func checkPrerequisites() {
        state.isCheckingPrereqs = true
        Task {
            async let pioCheck = ProcessRunner.commandExists("pio")
            async let esptoolCheck = ProcessRunner.commandExists("esptool.py")
            async let gitCheck = ProcessRunner.commandExists("git")

            state.hasPlatformIO = await pioCheck
            state.hasEsptool = await esptoolCheck
            state.hasGit = await gitCheck
            state.isCheckingPrereqs = false

            // If esptool.py not found, try just esptool
            if !state.hasEsptool {
                state.hasEsptool = await ProcessRunner.commandExists("esptool")
            }
        }
    }
}

#Preview {
    WelcomeView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
