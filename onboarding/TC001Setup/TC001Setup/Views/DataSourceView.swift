import SwiftUI

/// Step 5: Glucose data source configuration.
///
/// The user picks Dexcom Share, FreeStyle Libre, Nightscout, or a custom URL
/// and provides the necessary credentials.
struct DataSourceView: View {

    @EnvironmentObject var state: SetupState

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Glucose Data Source")
                    .font(.largeTitle.bold())

                Text("Choose where SugarClock should fetch glucose readings from.")
                    .font(.body)
                    .foregroundStyle(.secondary)
            }

            Divider()

            // Source picker
            Picker("Data Source", selection: $state.glucoseSource) {
                ForEach(GlucoseSource.allCases) { source in
                    Text(source.rawValue).tag(source)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            // Source-specific fields
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    switch state.glucoseSource {
                    case .dexcom:
                        dexcomFields
                    case .libre:
                        libreFields
                    case .nightscout:
                        nightscoutFields
                    case .customURL:
                        customURLFields
                    }
                }
            }

            Spacer()
        }
    }

    // MARK: - Dexcom

    private var dexcomFields: some View {
        VStack(alignment: .leading, spacing: 16) {
            GroupBox {
                VStack(alignment: .leading, spacing: 4) {
                    Label("Dexcom Share", systemImage: "heart.circle")
                        .font(.subheadline.bold())
                    Text("Uses the Dexcom Share API to fetch glucose values. Requires a Dexcom account with Share enabled in the Dexcom app.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Dexcom Username")
                    .font(.subheadline.bold())
                TextField("username", text: $state.dexcomUsername)
                    .textFieldStyle(.roundedBorder)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Dexcom Password")
                    .font(.subheadline.bold())
                SecureField("password", text: $state.dexcomPassword)
                    .textFieldStyle(.roundedBorder)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Dexcom Server")
                    .font(.subheadline.bold())
                Picker("Server", selection: $state.dexcomServer) {
                    Text("United States (US)").tag("US")
                    Text("Outside US (OUS)").tag("OUS")
                }
                .pickerStyle(.radioGroup)
                .labelsHidden()
                Text("Select OUS if your Dexcom account is outside the United States.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }

    // MARK: - FreeStyle Libre

    private var libreFields: some View {
        VStack(alignment: .leading, spacing: 16) {
            GroupBox {
                VStack(alignment: .leading, spacing: 4) {
                    Label("FreeStyle Libre", systemImage: "waveform.path.ecg")
                        .font(.subheadline.bold())
                    Text("Uses LibreLinkUp to fetch glucose values from Libre 2, 2 Plus, 3 and 3 Plus sensors. In the FreeStyle Libre app, go to Connected Apps → LibreLinkUp and invite an email, then sign in to the LibreLinkUp app once to accept the terms.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("LibreLinkUp Email")
                    .font(.subheadline.bold())
                TextField("email", text: $state.libreEmail)
                    .textFieldStyle(.roundedBorder)
                    .textContentType(.username)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("LibreLinkUp Password")
                    .font(.subheadline.bold())
                SecureField("password", text: $state.librePassword)
                    .textFieldStyle(.roundedBorder)
                Text("Use the LibreLinkUp login, not your LibreLink or LibreView one. Your region is detected automatically. If multiple people share with this account, choose the person in the clock's web Settings after setup.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }

    // MARK: - Nightscout

    private var nightscoutFields: some View {
        VStack(alignment: .leading, spacing: 16) {
            GroupBox {
                VStack(alignment: .leading, spacing: 4) {
                    Label("Nightscout", systemImage: "cloud")
                        .font(.subheadline.bold())
                    Text("Connects to your Nightscout instance to fetch glucose data via the REST API.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Nightscout URL")
                    .font(.subheadline.bold())
                TextField("https://your-site.herokuapp.com", text: $state.nightscoutURL)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.body, design: .monospaced))
                Text("The full URL to your Nightscout site, including https://")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("API Token (optional)")
                    .font(.subheadline.bold())
                SecureField("token", text: $state.nightscoutToken)
                    .textFieldStyle(.roundedBorder)
                Text("If your Nightscout site requires authentication, enter the API secret or token here.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }

    // MARK: - Custom URL

    private var customURLFields: some View {
        VStack(alignment: .leading, spacing: 16) {
            GroupBox {
                VStack(alignment: .leading, spacing: 4) {
                    Label("Custom URL", systemImage: "link")
                        .font(.subheadline.bold())
                    Text("Fetch glucose data from any HTTP endpoint that returns JSON with a glucose value.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Endpoint URL")
                    .font(.subheadline.bold())
                TextField("https://api.example.com/glucose", text: $state.customURL)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.body, design: .monospaced))
                Text("SugarClock will make GET requests to this URL at regular intervals.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
    }
}

#Preview {
    DataSourceView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
