import SwiftUI

@main struct SugarClockApp: App {
    @StateObject private var model:ClockModel
    @Environment(\.scenePhase) private var phase
    init() {
        #if DEBUG
        if ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil {
            _model=StateObject(wrappedValue:ScreenshotPreview.makeModel())
            return
        }
        #endif
        _model=StateObject(wrappedValue:ClockModel())
    }
    var body: some Scene {
        WindowGroup { content.environmentObject(model).task {model.resume()}.onChange(of:phase) { _,new in
            let sessionPhase:ForegroundSessionPhase = new == .background ? .background : new == .inactive ? .inactive : .active
            if sessionPhase.mustDisconnect {model.suspend()}
            else if new == .active {model.resume()}
        } }
    }
    @ViewBuilder private var content:some View {
        #if DEBUG
        if let screen=ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] {
            ScreenshotPreview(screen:screen)
        } else {MyClocksView()}
        #else
        MyClocksView()
        #endif
    }
}
