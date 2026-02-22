import Foundation
import Combine
import IOKit
import IOKit.serial
import IOKit.usb

/// Monitors USB serial ports for SugarClock (ESP32-based) device connections.
///
/// Looks for /dev/cu.usbserial-* ports that match common ESP32 USB-serial
/// bridge chips (CP210x, CH340, FTDI).
@MainActor
final class USBDetector: ObservableObject {

    @Published var detectedPorts: [String] = []
    @Published var isScanning: Bool = false

    private var timer: Timer?
    private let knownPatterns = [
        "/dev/cu.usbserial-",
        "/dev/cu.wchusbserial",
        "/dev/cu.SLAB_USBtoUART",
        "/dev/cu.usbmodem",
    ]

    /// Start polling for USB serial devices every 2 seconds.
    func startScanning() {
        isScanning = true
        scan()
        timer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.scan()
            }
        }
    }

    /// Stop polling.
    func stopScanning() {
        isScanning = false
        timer?.invalidate()
        timer = nil
    }

    /// Perform a single scan using IOKit to enumerate serial ports.
    func scan() {
        var ports: [String] = []

        // Use IOKit to find serial ports
        var iterator: io_iterator_t = 0
        let matchingDict = IOServiceMatching(kIOSerialBSDServiceValue) as NSMutableDictionary
        matchingDict[kIOSerialBSDTypeKey] = kIOSerialBSDAllTypes

        let kernResult = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator)
        guard kernResult == KERN_SUCCESS else {
            // Fallback: check the filesystem directly
            ports = scanFilesystem()
            detectedPorts = ports
            return
        }

        var service: io_object_t = IOIteratorNext(iterator)
        while service != 0 {
            if let pathAsCFString = IORegistryEntryCreateCFProperty(
                service,
                kIOCalloutDeviceKey as CFString,
                kCFAllocatorDefault,
                0
            ) {
                let path = pathAsCFString.takeUnretainedValue() as! String
                if isESP32Port(path) {
                    ports.append(path)
                }
            }
            IOObjectRelease(service)
            service = IOIteratorNext(iterator)
        }
        IOObjectRelease(iterator)

        // If IOKit found nothing, try filesystem scan as fallback
        if ports.isEmpty {
            ports = scanFilesystem()
        }

        detectedPorts = ports
    }

    /// Check whether a given port path matches known ESP32 USB-serial patterns.
    private func isESP32Port(_ path: String) -> Bool {
        for pattern in knownPatterns {
            if path.hasPrefix(pattern) {
                return true
            }
        }
        return false
    }

    /// Fallback: list /dev/ for cu.usbserial-* entries.
    private func scanFilesystem() -> [String] {
        let fm = FileManager.default
        do {
            let devContents = try fm.contentsOfDirectory(atPath: "/dev")
            return devContents
                .filter { name in
                    knownPatterns.contains(where: { pattern in
                        let prefix = String(pattern.dropFirst(5))  // strip "/dev/"
                        return name.hasPrefix(prefix)
                    })
                }
                .map { "/dev/\($0)" }
                .sorted()
        } catch {
            return []
        }
    }

    deinit {
        timer?.invalidate()
    }
}
