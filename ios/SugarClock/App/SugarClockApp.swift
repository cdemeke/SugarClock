import SwiftUI

@main struct SugarClockApp: App {
    @StateObject private var model=ClockModel()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup { MyClocksView().environmentObject(model).onChange(of:phase) { _,new in
            if new != .active { model.disconnect() }
        } }
    }
}
