// swift-tools-version: 5.9
import PackageDescription
let package = Package(name: "SugarClockCore", platforms: [.macOS(.v13), .iOS(.v17)], products: [.library(name: "SugarClockCore", targets: ["SugarClockCore"])], targets: [.target(name: "SugarClockCore", path: "SugarClock/Core"), .testTarget(name: "SugarClockCoreTests", dependencies: ["SugarClockCore"], path: "Tests")])
