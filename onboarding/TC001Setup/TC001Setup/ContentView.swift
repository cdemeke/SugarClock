import SwiftUI

/// Main wizard container. Shows a sidebar with step indicators and a
/// content area that displays the current step's view.
struct ContentView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        HStack(spacing: 0) {
            // MARK: - Sidebar
            sidebar
                .frame(width: 200)
                .background(.ultraThinMaterial)

            Divider()

            // MARK: - Main content area
            VStack(spacing: 0) {
                stepContent
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .padding(28)

                Divider()

                // MARK: - Bottom navigation bar
                navigationBar
                    .padding(.horizontal, 28)
                    .padding(.vertical, 14)
                    .background(.ultraThinMaterial)
            }
        }
        .frame(minWidth: 780, minHeight: 560)
    }

    // MARK: - Sidebar

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("SugarClock Setup")
                .font(.headline)
                .padding(.horizontal, 20)
                .padding(.top, 24)
                .padding(.bottom, 20)

            ForEach(0..<state.totalSteps, id: \.self) { index in
                stepRow(index: index)
            }

            Spacer()

            Text("v1.0.0")
                .font(.caption2)
                .foregroundStyle(.secondary)
                .padding(20)
        }
    }

    private func stepRow(index: Int) -> some View {
        HStack(spacing: 10) {
            ZStack {
                if index < state.currentStep {
                    // Completed
                    Circle()
                        .fill(Color.accentColor)
                        .frame(width: 24, height: 24)
                    Image(systemName: "checkmark")
                        .font(.system(size: 11, weight: .bold))
                        .foregroundColor(.white)
                } else if index == state.currentStep {
                    // Current
                    Circle()
                        .fill(Color.accentColor)
                        .frame(width: 24, height: 24)
                    Text("\(index + 1)")
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(.white)
                } else {
                    // Upcoming
                    Circle()
                        .strokeBorder(Color.secondary.opacity(0.4), lineWidth: 1.5)
                        .frame(width: 24, height: 24)
                    Text("\(index + 1)")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(.secondary)
                }
            }

            Text(state.stepTitle(for: index))
                .font(.system(size: 13, weight: index == state.currentStep ? .semibold : .regular))
                .foregroundStyle(index == state.currentStep ? .primary : .secondary)

            Spacer()
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 6)
        .background(
            index == state.currentStep
                ? Color.accentColor.opacity(0.08)
                : Color.clear
        )
        .cornerRadius(6)
        .padding(.horizontal, 8)
    }

    // MARK: - Step content

    @ViewBuilder
    private var stepContent: some View {
        switch state.currentStep {
        case 0:
            WelcomeView()
        case 1:
            ConnectView()
        case 2:
            BackupView()
        case 3:
            WiFiView()
        case 4:
            DataSourceView()
        case 5:
            PreferencesView()
        case 6:
            FlashView()
        case 7:
            DoneView()
        default:
            Text("Unknown step")
        }
    }

    // MARK: - Navigation bar

    private var navigationBar: some View {
        HStack {
            if state.currentStep > 0 && state.currentStep < 7 {
                Button(action: { state.goBack() }) {
                    HStack(spacing: 4) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 11, weight: .semibold))
                        Text("Back")
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
            }

            Spacer()

            if state.currentStep < 7 {
                // Step indicator text
                Text("Step \(state.currentStep + 1) of \(state.totalSteps)")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Spacer()

                if state.currentStep < 6 {
                    Button(action: { state.goNext() }) {
                        HStack(spacing: 4) {
                            Text("Next")
                            Image(systemName: "chevron.right")
                                .font(.system(size: 11, weight: .semibold))
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(!state.canAdvance)
                }
            }
        }
    }
}

#Preview {
    ContentView()
        .environmentObject(SetupState())
}
