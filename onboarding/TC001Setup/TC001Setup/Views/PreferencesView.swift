import SwiftUI

/// Step 6: Display and device preferences.
///
/// Lets the user configure timezone, glucose units, brightness,
/// display rotation, and alert thresholds.
struct PreferencesView: View {

    @EnvironmentObject var state: SetupState

    /// A selection of common timezones.
    private let commonTimezones: [String] = {
        let identifiers = TimeZone.knownTimeZoneIdentifiers.sorted()
        return identifiers
    }()

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Header
            VStack(alignment: .leading, spacing: 8) {
                Text("Preferences")
                    .font(.largeTitle.bold())

                Text("Configure how your SugarClock displays glucose information. You can always change these later from the device's web dashboard.")
                    .font(.body)
                    .foregroundStyle(.secondary)
            }

            Divider()

            ScrollView {
                VStack(alignment: .leading, spacing: 20) {

                    // Timezone
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Timezone")
                            .font(.headline)

                        Picker("Timezone", selection: $state.timezone) {
                            ForEach(commonTimezones, id: \.self) { tz in
                                Text(tz).tag(tz)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: 300)

                        Text("Used for displaying the correct local time on the device.")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }

                    Divider()

                    // Glucose units
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Glucose Units")
                            .font(.headline)

                        Picker("Units", selection: $state.glucoseUnit) {
                            ForEach(GlucoseUnit.allCases) { unit in
                                Text(unit.rawValue).tag(unit)
                            }
                        }
                        .pickerStyle(.segmented)
                        .frame(maxWidth: 200)
                        .labelsHidden()
                    }

                    Divider()

                    // Alert thresholds
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Alert Thresholds")
                            .font(.headline)

                        HStack(spacing: 24) {
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Low")
                                    .font(.subheadline.bold())
                                    .foregroundColor(.orange)
                                HStack {
                                    TextField("", value: $state.lowAlertThreshold, format: .number)
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 80)
                                    Text(state.glucoseUnit.rawValue)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }

                            VStack(alignment: .leading, spacing: 4) {
                                Text("High")
                                    .font(.subheadline.bold())
                                    .foregroundColor(.red)
                                HStack {
                                    TextField("", value: $state.highAlertThreshold, format: .number)
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 80)
                                    Text(state.glucoseUnit.rawValue)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                        }

                        Text("The display changes color when glucose is outside these thresholds.")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }

                    Divider()

                    // Brightness
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Display Brightness")
                            .font(.headline)

                        Picker("Brightness", selection: $state.brightness) {
                            ForEach(BrightnessPreset.allCases) { preset in
                                Text(preset.rawValue).tag(preset)
                            }
                        }
                        .pickerStyle(.segmented)
                        .frame(maxWidth: 250)
                        .labelsHidden()
                    }

                    Divider()

                    // Display rotation
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Display Rotation")
                            .font(.headline)

                        Picker("Rotation", selection: $state.displayRotation) {
                            Text("0 (Normal)").tag(0)
                            Text("90").tag(90)
                            Text("180 (Upside down)").tag(180)
                            Text("270").tag(270)
                        }
                        .pickerStyle(.segmented)
                        .frame(maxWidth: 400)
                        .labelsHidden()

                        Text("Rotate the display if you mount the device in a different orientation.")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                }
                .padding(.bottom, 16)
            }
        }
    }
}

#Preview {
    PreferencesView()
        .environmentObject(SetupState())
        .frame(width: 560, height: 480)
}
