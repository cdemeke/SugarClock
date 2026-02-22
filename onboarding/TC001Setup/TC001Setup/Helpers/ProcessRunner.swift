import Foundation

/// Runs shell commands (pio, esptool, git, etc.) asynchronously and streams
/// stdout / stderr output back to callers via a callback.
final class ProcessRunner {

    struct Result {
        let exitCode: Int32
        let output: String
    }

    /// Run a shell command asynchronously.
    ///
    /// - Parameters:
    ///   - command: The executable path or name (resolved via PATH).
    ///   - arguments: Arguments to pass.
    ///   - workingDirectory: Optional working directory.
    ///   - environment: Additional environment variables merged with the current env.
    ///   - onOutput: Called on the main actor with each chunk of stdout/stderr text.
    /// - Returns: A `Result` with the exit code and the full combined output.
    @MainActor
    static func run(
        command: String,
        arguments: [String] = [],
        workingDirectory: String? = nil,
        environment: [String: String]? = nil,
        onOutput: (@MainActor (String) -> Void)? = nil
    ) async -> Result {
        await withCheckedContinuation { continuation in
            let process = Process()
            let pipe = Pipe()

            // Use /bin/zsh -l -c to get a login shell so PATH includes Homebrew, etc.
            process.executableURL = URL(fileURLWithPath: "/bin/zsh")
            let fullCommand: String
            if arguments.isEmpty {
                fullCommand = command
            } else {
                let escapedArgs = arguments.map { arg in
                    "'\(arg.replacingOccurrences(of: "'", with: "'\\''"))'"
                }
                fullCommand = "\(command) \(escapedArgs.joined(separator: " "))"
            }
            process.arguments = ["-l", "-c", fullCommand]

            if let wd = workingDirectory {
                process.currentDirectoryURL = URL(fileURLWithPath: wd)
            }

            if let env = environment {
                var merged = ProcessInfo.processInfo.environment
                for (key, value) in env {
                    merged[key] = value
                }
                process.environment = merged
            }

            process.standardOutput = pipe
            process.standardError = pipe

            var accumulated = ""
            let outputHandler = onOutput

            pipe.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                guard !data.isEmpty else { return }
                if let text = String(data: data, encoding: .utf8) {
                    Task { @MainActor in
                        accumulated += text
                        outputHandler?(text)
                    }
                }
            }

            process.terminationHandler = { proc in
                // Give a moment for any remaining output to flush
                pipe.fileHandleForReading.readabilityHandler = nil
                let remaining = pipe.fileHandleForReading.readDataToEndOfFile()
                if let text = String(data: remaining, encoding: .utf8), !text.isEmpty {
                    Task { @MainActor in
                        accumulated += text
                        outputHandler?(text)
                    }
                }
                let exitCode = proc.terminationStatus
                // Small delay so the last output callback fires before we resume
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                    continuation.resume(returning: Result(exitCode: exitCode, output: accumulated))
                }
            }

            do {
                try process.run()
            } catch {
                let msg = "Failed to launch process: \(error.localizedDescription)"
                Task { @MainActor in
                    outputHandler?(msg)
                }
                continuation.resume(returning: Result(exitCode: -1, output: msg))
            }
        }
    }

    /// Convenience: check whether a command exists on the system.
    @MainActor
    static func commandExists(_ command: String) async -> Bool {
        let result = await run(command: "which", arguments: [command])
        return result.exitCode == 0
    }

    /// Convenience: run a command and just return success/failure.
    @MainActor
    static func runSimple(
        command: String,
        arguments: [String] = [],
        workingDirectory: String? = nil
    ) async -> Bool {
        let result = await run(
            command: command,
            arguments: arguments,
            workingDirectory: workingDirectory
        )
        return result.exitCode == 0
    }
}
