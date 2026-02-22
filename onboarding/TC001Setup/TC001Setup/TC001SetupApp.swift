import SwiftUI

@main
struct TC001SetupApp: App {

    @StateObject private var setupState = SetupState()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(setupState)
                .frame(minWidth: 780, minHeight: 560)
        }
        .windowResizability(.contentSize)
        .defaultSize(width: 780, height: 560)
    }
}
