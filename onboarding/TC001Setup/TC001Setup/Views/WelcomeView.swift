import SwiftUI

/// Step 1: Welcome screen with prerequisite checks.
///
/// Checks that PlatformIO CLI, esptool, and git are installed.
/// Offers one-click auto-install for missing tools.
/// Auto-detects the project folder from the app bundle location.
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

            // Project path — only shown if auto-detection failed
            if state.projectPath.isEmpty {
                VStack(alignment: .leading, spacing: 6) {
                    Label("Could not locate the SugarClock project folder automatically.", systemImage: "questionmark.folder")
                        .font(.subheadline)
                        .foregroundColor(.orange)

                    Text("Please select the folder containing platformio.ini and the src/ directory.")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    HStack {
                        TextField("Path to tc001 project root", text: $state.projectPath)
                            .textFieldStyle(.roundedBorder)

                        Button("Browse...") {
                            let panel = NSOpenPanel()
                            panel.canChooseFiles = false
                            panel.canChooseDirectories = true
                            panel.allowsMultipleSelection = false
                            panel.message = "Select the SugarClock project root directory (contains platformio.ini)"
                            if panel.runModal() == .OK, let url = panel.url {
                                state.projectPath = url.path
                            }
                        }
                    }
                }

                Divider()
            }

            // Prerequisites
            VStack(alignment: .leading, spacing: 12) {
                Text("Prerequisites")
                    .font(.headline)

                Text("The following tools must be installed on your Mac:")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                prerequisiteRow(
                    name: "Git",
                    isInstalled: state.hasGit,
                    isInstalling: state.installingItem == "git",
                    installAction: { Task { await installGit() } }
                )

                prerequisiteRow(
                    name: "PlatformIO CLI (pio)",
                    isInstalled: state.hasPlatformIO,
                    isInstalling: state.installingItem == "platformio",
                    installAction: { Task { await installPlatformIO() } }
                )

                prerequisiteRow(
                    name: "esptool",
                    isInstalled: state.hasEsptool,
                    isInstalling: state.installingItem == "platformio",
                    installAction: { Task { await installPlatformIO() } }
                )
            }

            // Install error banner
            if let error = state.installError {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.yellow)
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                }
                .padding(8)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Color.red.opacity(0.1))
                .cornerRadius(6)
            }

            // Install log area
            if !state.installLog.isEmpty {
                ScrollViewReader { proxy in
                    ScrollView {
                        Text(state.installLog)
                            .font(.system(size: 11, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id("logBottom")
                    }
                    .frame(maxHeight: 120)
                    .padding(6)
                    .background(Color(nsColor: .textBackgroundColor))
                    .cornerRadius(6)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .stroke(Color.gray.opacity(0.3), lineWidth: 1)
                    )
                    .onChange(of: state.installLog) { _ in
                        proxy.scrollTo("logBottom", anchor: .bottom)
                    }
                }
            }

            Spacer()

            HStack {
                Button(action: { checkPrerequisites() }) {
                    HStack(spacing: 6) {
                        if state.isCheckingPrereqs {
                            ProgressView()
                                .controlSize(.small)
                        }
                        Text(state.isCheckingPrereqs ? "Checking..." : "Re-check")
                    }
                }
                .buttonStyle(.bordered)
                .disabled(state.isCheckingPrereqs || state.isInstallingPrereqs)

                if !state.allPrereqsMet {
                    Button(action: { Task { await installAll() } }) {
                        HStack(spacing: 6) {
                            if state.isInstallingPrereqs {
                                ProgressView()
                                    .controlSize(.small)
                            }
                            Text(state.isInstallingPrereqs ? "Installing..." : "Install All Missing")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(state.isInstallingPrereqs || state.isCheckingPrereqs)
                }

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
            if state.projectPath.isEmpty {
                autoDetectProjectPath()
            }
        }
    }

    // MARK: - Project Path Auto-Detection

    private func autoDetectProjectPath() {
        let fm = FileManager.default

        func hasProject(_ dir: String) -> Bool {
            fm.fileExists(atPath: "\(dir)/platformio.ini")
        }

        // 1. Walk up from the app bundle (covers running from inside the repo)
        let bundlePath = Bundle.main.bundlePath
        var dir = bundlePath
        for _ in 0..<6 {
            dir = (dir as NSString).deletingLastPathComponent
            if hasProject(dir) {
                state.projectPath = dir
                return
            }
        }

        // 2. Check the current working directory
        let cwd = fm.currentDirectoryPath
        if hasProject(cwd) {
            state.projectPath = cwd
            return
        }

        // 3. Check common locations in the user's home directory
        let home = NSHomeDirectory()
        let candidates = [
            "\(home)/Documents/tc001",
            "\(home)/Desktop/tc001",
            "\(home)/Downloads/tc001",
            "\(home)/tc001",
            "\(home)/Projects/tc001",
            "\(home)/Developer/tc001",
            "\(home)/dev/tc001",
            "\(home)/src/tc001",
        ]

        for candidate in candidates {
            if hasProject(candidate) {
                state.projectPath = candidate
                return
            }
        }

        // If nothing found, projectPath stays empty and the fallback UI is shown
    }

    // MARK: - Prerequisite Row

    private func prerequisiteRow(
        name: String,
        isInstalled: Bool,
        isInstalling: Bool,
        installAction: @escaping () -> Void
    ) -> some View {
        HStack(spacing: 10) {
            Image(systemName: isInstalled ? "checkmark.circle.fill" : "xmark.circle")
                .foregroundColor(isInstalled ? .green : .red)
                .font(.title3)

            Text(name)
                .font(.system(size: 13, weight: .medium))

            Spacer()

            if isInstalling {
                HStack(spacing: 4) {
                    ProgressView()
                        .controlSize(.small)
                    Text("Installing...")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } else if !isInstalled && !state.isInstallingPrereqs {
                Button("Install") {
                    installAction()
                }
                .controlSize(.small)
                .buttonStyle(.bordered)
            }
        }
    }

    // MARK: - Check Prerequisites

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

    // MARK: - Install All Missing

    private func installAll() async {
        state.isInstallingPrereqs = true
        state.installError = nil
        state.installLog = ""

        if !state.hasGit {
            await installGit()
            if state.installError != nil {
                state.isInstallingPrereqs = false
                return
            }
        }

        if !state.hasPlatformIO || !state.hasEsptool {
            await installPlatformIO()
            if state.installError != nil {
                state.isInstallingPrereqs = false
                return
            }
        }

        checkPrerequisites()
        state.isInstallingPrereqs = false
    }

    // MARK: - Install Git (Xcode CLT)

    private func installGit() async {
        state.installingItem = "git"
        state.installError = nil
        appendLog("Installing Xcode Command Line Tools (provides Git)...\n")

        // Launch xcode-select --install which opens the native macOS dialog
        let launchResult = await ProcessRunner.run(command: "xcode-select", arguments: ["--install"]) { text in
            appendLog(text)
        }

        if launchResult.exitCode != 0 {
            // Exit code 1 can mean already installed or dialog launched — check for git
            let alreadyHasGit = await ProcessRunner.commandExists("git")
            if alreadyHasGit {
                appendLog("Git is already available.\n")
                state.hasGit = true
                state.installingItem = nil
                return
            }
        }

        appendLog("Xcode CLT dialog opened. Waiting for installation to complete...\n")

        // Poll for git availability (up to 10 minutes)
        let maxAttempts = 120  // 120 * 5s = 600s = 10 min
        for attempt in 1...maxAttempts {
            try? await Task.sleep(nanoseconds: 5_000_000_000) // 5 seconds

            let found = await ProcessRunner.commandExists("git")
            if found {
                appendLog("Git installed successfully!\n")
                state.hasGit = true
                state.installingItem = nil
                return
            }

            if attempt % 6 == 0 { // Log every 30 seconds
                let elapsed = attempt * 5
                appendLog("Waiting for Xcode CLT install... (\(elapsed)s)\n")
            }
        }

        state.installError = "Timed out waiting for Xcode CLT installation. Please complete the install dialog and click Re-check."
        appendLog("Timed out waiting for Xcode CLT.\n")
        state.installingItem = nil
    }

    // MARK: - Install PlatformIO (includes esptool)

    private func installPlatformIO() async {
        state.installingItem = "platformio"
        state.installError = nil
        appendLog("Installing PlatformIO (includes esptool)...\n")

        // First attempt: standard pip install --user
        appendLog("Running: python3 -m pip install --user platformio\n")
        let result = await ProcessRunner.run(
            command: "python3",
            arguments: ["-m", "pip", "install", "--user", "platformio"]
        ) { text in
            appendLog(text)
        }

        if result.exitCode != 0 {
            // Retry with --break-system-packages (needed on macOS 14+ Sonoma)
            appendLog("\nFirst attempt failed. Retrying with --break-system-packages...\n")
            appendLog("Running: python3 -m pip install --user --break-system-packages platformio\n")
            let retryResult = await ProcessRunner.run(
                command: "python3",
                arguments: ["-m", "pip", "install", "--user", "--break-system-packages", "platformio"]
            ) { text in
                appendLog(text)
            }

            if retryResult.exitCode != 0 {
                state.installError = "Failed to install PlatformIO. Check the log for details."
                appendLog("\nPlatformIO installation failed.\n")
                state.installingItem = nil
                return
            }
        }

        appendLog("\nPlatformIO pip install completed. Fixing PATH...\n")

        // Ensure the Python user scripts dir is on PATH
        await ensurePythonBinInPath()

        // Verify installation
        checkPrerequisites()
        state.installingItem = nil
    }

    // MARK: - PATH Fixup

    private func ensurePythonBinInPath() async {
        // Find the Python user scripts directory
        let result = await ProcessRunner.run(
            command: "python3",
            arguments: ["-c", "import sysconfig; print(sysconfig.get_path('scripts', 'posix_user'))"]
        )

        let scriptsDir = result.output.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !scriptsDir.isEmpty, result.exitCode == 0 else {
            appendLog("Could not determine Python user scripts directory.\n")
            return
        }

        appendLog("Python user scripts dir: \(scriptsDir)\n")

        // Check if pio exists in that directory
        let pioPath = "\(scriptsDir)/pio"
        let pioExists = FileManager.default.fileExists(atPath: pioPath)

        guard pioExists else {
            appendLog("pio not found at \(pioPath), skipping PATH fixup.\n")
            return
        }

        // Check if it's already on PATH
        let alreadyOnPath = await ProcessRunner.commandExists("pio")
        if alreadyOnPath {
            appendLog("pio is already on PATH.\n")
            return
        }

        // Append to ~/.zprofile so login shells pick it up
        let zprofilePath = NSHomeDirectory() + "/.zprofile"
        let exportLine = "\nexport PATH=\"\(scriptsDir):$PATH\"\n"

        appendLog("Adding \(scriptsDir) to PATH in ~/.zprofile\n")

        // Read existing content to avoid duplicate entries
        let existingContent = (try? String(contentsOfFile: zprofilePath, encoding: .utf8)) ?? ""
        if existingContent.contains(scriptsDir) {
            appendLog("PATH entry already exists in ~/.zprofile, skipping.\n")
            return
        }

        do {
            let fileHandle = try FileHandle(forWritingTo: URL(fileURLWithPath: zprofilePath))
            fileHandle.seekToEndOfFile()
            if let data = exportLine.data(using: .utf8) {
                fileHandle.write(data)
            }
            fileHandle.closeFile()
            appendLog("PATH updated in ~/.zprofile\n")
        } catch {
            // File might not exist yet, create it
            do {
                try exportLine.write(toFile: zprofilePath, atomically: true, encoding: .utf8)
                appendLog("Created ~/.zprofile with PATH entry\n")
            } catch {
                appendLog("Warning: Could not update ~/.zprofile: \(error.localizedDescription)\n")
            }
        }
    }

    // MARK: - Helpers

    private func appendLog(_ text: String) {
        state.installLog += text
    }
}

#Preview {
    WelcomeView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
