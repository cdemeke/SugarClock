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

    /// Builds a LittleFS image containing only the one-time config.json overlay.
    /// The web UI is gzip-compressed into firmware so OTA updates it atomically.
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
        let fm = FileManager.default
        let tmpDir = NSTemporaryDirectory() + "sugarclock_littlefs_\(ProcessInfo.processInfo.processIdentifier)"

        // Clean up any previous temp dir
        try? fm.removeItem(atPath: tmpDir)
        try fm.createDirectory(atPath: tmpDir, withIntermediateDirectories: true)

        // Write config.json
        let jsonData = try JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys])
        let configPath = tmpDir + "/config.json"
        try jsonData.write(to: URL(fileURLWithPath: configPath))
        onOutput?("Wrote config.json\n")

        // Run mklittlefs
        // Partition size from partitions_custom.csv: 0x70000 = 458752 bytes
        let outputPath = NSTemporaryDirectory() + "sugarclock_littlefs.bin"
        let result = await ProcessRunner.run(
            command: mklittlefs,
            arguments: ["-c", tmpDir, "-s", "458752", "-b", "4096", "-p", "256", outputPath]
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
    ///   0x10000 — firmware.bin (ota_0)
    ///   0x390000 — littlefs.bin
    @MainActor
    static func flashDevice(
        port: String,
        littlefsPath: String,
        preserveSettings: Bool = false,
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

        // Firmware-only upgrades preserve NVS and reconstruct LittleFS at the new
        // offset, including enterprise CA certificates. Nothing is flashed if the
        // old partition table or filesystem cannot be read safely.
        let installedFilesystem = preserveSettings
            ? try await preservedFilesystem(port: port, esptool: esptool, onOutput: onOutput)
            : littlefsPath

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
                "0x390000", installedFilesystem,
            ]
        ) { text in
            onOutput?(text)
        }

        if result.exitCode != 0 {
            throw FirmwareError.flashFailed("esptool failed with exit code \(result.exitCode)")
        }
    }

    @MainActor
    private static func preservedFilesystem(port: String, esptool: String,
        onOutput: (@MainActor (String) -> Void)?) async throws -> String {
        guard let tool=mklittlefsPath else {throw FirmwareError.missingResource("mklittlefs")}
        let fm=FileManager.default
        let backupDir=fm.homeDirectoryForCurrentUser.appendingPathComponent("Documents/SugarClock Backups")
        try fm.createDirectory(at:backupDir,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
        let backup=backupDir.appendingPathComponent("before-upgrade-\(UUID().uuidString).bin")
        let result=await ProcessRunner.run(command:esptool,arguments:["--chip","esp32","--port",port,"--baud","115200","read_flash","0x0","0x400000",backup.path]) {text in onOutput?(text)}
        guard result.exitCode==0 else {throw FirmwareError.flashFailed("Could not back up the clock. No firmware was written.")}
        try fm.setAttributes([.posixPermissions:0o600],ofItemAtPath:backup.path)
        let bytes=[UInt8](try Data(contentsOf:backup))
        guard bytes.count==0x400000 else {throw FirmwareError.flashFailed("Incomplete backup. No firmware was written.")}
        func word(_ i:Int)->Int {Int(bytes[i]) | Int(bytes[i+1])<<8 | Int(bytes[i+2])<<16 | Int(bytes[i+3])<<24}
        var nvsValid=false
        var filesystem:(Int,Int)?
        for pos in stride(from:0x8000,to:0x9000,by:32) {
            if bytes[pos]==0xff || bytes[pos]==0xeb {break}
            guard bytes[pos]==0xaa,bytes[pos+1]==0x50 else {throw FirmwareError.flashFailed("Unknown partition table. No firmware was written.")}
            let offset=word(pos+4),size=word(pos+8)
            guard offset>=0,size>0,offset+size<=bytes.count else {throw FirmwareError.flashFailed("Invalid partition bounds.")}
            if bytes[pos+2]==1 && bytes[pos+3]==2 {nvsValid=offset==0x9000 && size==0x5000}
            if bytes[pos+2]==1 && bytes[pos+3]==0x82 {filesystem=(offset,size)}
        }
        guard nvsValid,let (offset,size)=filesystem else {throw FirmwareError.flashFailed("This layout cannot be upgraded while preserving settings. No firmware was written.")}
        let temp=fm.temporaryDirectory.appendingPathComponent("sugarclock-migration-\(UUID().uuidString)")
        try fm.createDirectory(at:temp,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
        defer {try? fm.removeItem(at:temp)}
        let old=temp.appendingPathComponent("old.bin"),tree=temp.appendingPathComponent("files")
        try Data(bytes[offset..<offset+size]).write(to:old)
        try fm.createDirectory(at:tree,withIntermediateDirectories:true)
        let extracted=await ProcessRunner.run(command:tool,arguments:["-u",tree.path,"-s",String(size),"-b","4096","-p","256",old.path])
        guard extracted.exitCode==0 else {throw FirmwareError.buildFailed("Could not preserve the existing filesystem and certificates. No firmware was written. Backup: \(backup.path)")}
        let overlay=tree.appendingPathComponent("config.json")
        if fm.fileExists(atPath:overlay.path) {
            let retained=tree.appendingPathComponent("migration-config-\(UUID().uuidString).json")
            try fm.moveItem(at:overlay,to:retained)
        }
        let output=fm.temporaryDirectory.appendingPathComponent("sugarclock_preserved_littlefs.bin")
        let packed=await ProcessRunner.run(command:tool,arguments:["-c",tree.path,"-s","458752","-b","4096","-p","256",output.path])
        guard packed.exitCode==0 else {throw FirmwareError.buildFailed("Preserved files do not fit the new filesystem. No firmware was written.")}
        try fm.setAttributes([.posixPermissions:0o600],ofItemAtPath:output.path)
        onOutput?("Settings and certificates preserved. Private recovery backup: \(backup.path)\n")
        return output.path
    }

    // MARK: - Cleanup

    /// Removes temporary files created during the build/flash process.
    static func cleanupTempFiles() {
        let fm = FileManager.default
        let tmpDir = NSTemporaryDirectory() + "sugarclock_littlefs_\(ProcessInfo.processInfo.processIdentifier)"
        let tmpBin = NSTemporaryDirectory() + "sugarclock_littlefs.bin"
        try? fm.removeItem(atPath: tmpDir)
        try? fm.removeItem(atPath: tmpBin)
        try? fm.removeItem(at:fm.temporaryDirectory.appendingPathComponent("sugarclock_preserved_littlefs.bin"))
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
