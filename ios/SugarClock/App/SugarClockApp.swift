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
        WindowGroup { content.environmentObject(model).onChange(of:phase) { _,new in
            if new != .active { model.disconnect() }
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
