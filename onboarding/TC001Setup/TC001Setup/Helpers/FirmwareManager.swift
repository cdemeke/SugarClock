import Foundation

/// Manages bundled firmware resources, LittleFS image building, and device flashing.
/// All tools (esptool, mklittlefs) and firmware binaries are bundled in the app.
final class FirmwareManager {

    // MARK: - Bundled tool paths

    /// Path to the bundled esptool binary.
    static var esptoolPath: String? {
        Bundle.main.path(forResource: "esptool", ofType: nil, inDirectory: "Tools")
    }

    /// Path to the bundled mklittlefs binary.
    static var mklittlefsPath: String? {
        Bundle.main.path(forResource: "mklittlefs", ofType: nil, inDirectory: "Tools")
    }

    // MARK: - Bundled resource directories

    /// Path to the directory containing pre-built firmware binaries.
    static var firmwareDir: String? {
        Bundle.main.path(forResource: "Firmware", ofType: nil)
    }

    /// Path to the directory containing web UI files.
    static var webUIDir: String? {
        Bundle.main.path(forResource: "www", ofType: nil, inDirectory: "WebUI")
    }

    // MARK: - Tool setup

    /// Ensures bundled binaries are executable.
    static func ensureToolsExecutable() throws {
        let fm = FileManager.default
        for path in [esptoolPath, mklittlefsPath].compactMap({ $0 }) {
            var attrs = try fm.attributesOfItem(atPath: path)
            attrs[.posixPermissions] = 0o755
            try fm.setAttributes(attrs, ofItemAtPath: path)
        }
    }

    // MARK: - LittleFS image building

    /// Builds a LittleFS image containing the web UI and a config.json overlay.
    ///
    /// - Parameters:
    ///   - config: Dictionary of configuration values to write as config.json.
    ///   - onOutput: Callback for streaming build output.
    /// - Returns: Path to the generated littlefs.bin file.
    @MainActor
    static func buildLittleFSImage(
        config: [String: Any],
        onOutput: (@MainActor (String) -> Void)? = nil
    ) async throws -> String {
        guard let mklittlefs = mklittlefsPath else {
            throw FirmwareError.missingResource("mklittlefs")
        }
        guard let webUI = webUIDir else {
            throw FirmwareError.missingResource("WebUI/www")
        }

        let fm = FileManager.default
        let tmpDir = NSTemporaryDirectory() + "sugarclock_littlefs_\(ProcessInfo.processInfo.processIdentifier)"

        // Clean up any previous temp dir
        try? fm.removeItem(atPath: tmpDir)
        try fm.createDirectory(atPath: tmpDir, withIntermediateDirectories: true)

        // Create www/ subdirectory and copy web UI files
        let wwwDest = tmpDir + "/www"
        try fm.copyItem(atPath: webUI, toPath: wwwDest)
        onOutput?("Copied web UI files to temp directory\n")

        // Write config.json
        let jsonData = try JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys])
        let configPath = tmpDir + "/config.json"
        try jsonData.write(to: URL(fileURLWithPath: configPath))
        onOutput?("Wrote config.json\n")

        // Run mklittlefs
        // Partition size from partitions_custom.csv: 0x1F0000 = 2031616 bytes
        let outputPath = NSTemporaryDirectory() + "sugarclock_littlefs.bin"
        let result = await ProcessRunner.run(
            command: mklittlefs,
            arguments: ["-c", tmpDir, "-s", "2031616", "-b", "4096", "-p", "256", outputPath]
        ) { text in
            onOutput?(text)
        }

        if result.exitCode != 0 {
            throw FirmwareError.buildFailed("mklittlefs failed with exit code \(result.exitCode)")
        }

        onOutput?("LittleFS image built successfully\n")
        return outputPath
    }

    // MARK: - Device flashing

    /// Flashes all firmware partitions to the device in a single esptool command.
    ///
    /// Flash layout (from partitions_custom.csv):
    ///   0x1000  — bootloader.bin
    ///   0x8000  — partitions.bin
    ///   0xe000  — boot_app0.bin
    ///   0x10000 — firmware.bin
    ///   0x210000 — littlefs.bin
    @MainActor
    static func flashDevice(
        port: String,
        littlefsPath: String,
        onOutput: (@MainActor (String) -> Void)? = nil
    ) async throws {
        guard let esptool = esptoolPath else {
            throw FirmwareError.missingResource("esptool")
        }
        guard let fwDir = firmwareDir else {
            throw FirmwareError.missingResource("Firmware")
        }

        let bootloader = fwDir + "/bootloader.bin"
        let partitions = fwDir + "/partitions.bin"
        let bootApp0   = fwDir + "/boot_app0.bin"
        let firmware   = fwDir + "/firmware.bin"

        // Verify all files exist
        let fm = FileManager.default
        for (name, path) in [("bootloader.bin", bootloader), ("partitions.bin", partitions),
                              ("boot_app0.bin", bootApp0), ("firmware.bin", firmware)] {
            guard fm.fileExists(atPath: path) else {
                throw FirmwareError.missingResource(name)
            }
        }

        let result = await ProcessRunner.run(
            command: esptool,
            arguments: [
                "--chip", "esp32",
                "--port", port,
                "--baud", "115200",
                "write_flash",
                "--flash_mode", "dio",
                "--flash_size", "4MB",
                "0x1000",   bootloader,
                "0x8000",   partitions,
                "0xe000",   bootApp0,
                "0x10000",  firmware,
                "0x210000", littlefsPath,
            ]
        ) { text in
            onOutput?(text)
        }

        if result.exitCode != 0 {
            throw FirmwareError.flashFailed("esptool failed with exit code \(result.exitCode)")
        }
    }

    // MARK: - Cleanup

    /// Removes temporary files created during the build/flash process.
    static func cleanupTempFiles() {
        let fm = FileManager.default
        let tmpDir = NSTemporaryDirectory() + "sugarclock_littlefs_\(ProcessInfo.processInfo.processIdentifier)"
        let tmpBin = NSTemporaryDirectory() + "sugarclock_littlefs.bin"
        try? fm.removeItem(atPath: tmpDir)
        try? fm.removeItem(atPath: tmpBin)
    }

    // MARK: - Errors

    enum FirmwareError: LocalizedError {
        case missingResource(String)
        case buildFailed(String)
        case flashFailed(String)

        var errorDescription: String? {
            switch self {
            case .missingResource(let name):
                return "Missing bundled resource: \(name). The app bundle may be incomplete."
            case .buildFailed(let detail):
                return "Filesystem build failed: \(detail)"
            case .flashFailed(let detail):
                return "Device flash failed: \(detail)"
            }
        }
    }
}
