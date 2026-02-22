import SwiftUI

/// Step 1: Welcome screen.
struct WelcomeView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            HStack(spacing: 16) {
                Image("AppLogo")
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: 80, height: 80)
                    .clipShape(RoundedRectangle(cornerRadius: 16))
                    .shadow(color: .black.opacity(0.15), radius: 6, x: 0, y: 3)

                VStack(alignment: .leading, spacing: 8) {
                    Text("Welcome to SugarClock")
                        .font(.largeTitle.bold())

                    Text("This setup will get your glucose monitor up and running. It only takes a few minutes.")
                        .font(.body)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Divider()

            // What to expect
            VStack(alignment: .leading, spacing: 16) {
                Text("What to expect")
                    .font(.headline)

                expectRow(icon: "cable.connector", title: "Connect your device", detail: "Plug your SugarClock into your Mac via USB")
                expectRow(icon: "wifi", title: "Choose your WiFi network", detail: "So your device can go online")
                expectRow(icon: "heart.text.square", title: "Set up your glucose source", detail: "Dexcom Share, Nightscout, or a custom URL")
                expectRow(icon: "arrow.down.circle", title: "Install", detail: "We'll put everything on your device in one step")
            }

            Spacer()
        }
    }

    private func expectRow(icon: String, title: String, detail: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.title3)
                .foregroundColor(.accentColor)
                .frame(width: 24)

            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.system(size: 13, weight: .medium))
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }
}

#Preview {
    WelcomeView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
