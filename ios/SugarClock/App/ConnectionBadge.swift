import SwiftUI

/// A glanceable connection indicator beside the clock's name. Tap for the
/// link in use, the current step or failure reason, and Retry when stopped.
struct ConnectionBadge:View {
    @EnvironmentObject var model:ClockModel
    @State private var showingDetails=false
    @ScaledMetric(relativeTo:.title2) private var size:CGFloat=24

    static func color(_ level:ConnectionStatus.Level)->Color {
        switch level {
        case .connected:return SugarTheme.accent
        case .connecting:return .orange
        case .failed:return .red
        case .idle:return SugarTheme.secondary
        }
    }
    static func symbol(_ level:ConnectionStatus.Level)->String {
        switch level {
        case .connected:return "checkmark.circle.fill"
        case .connecting:return "ellipsis.circle.fill"
        case .failed:return "exclamationmark.circle.fill"
        case .idle:return "circle"
        }
    }
    var body:some View {
        let status=model.connectionStatus
        Button {showingDetails=true} label:{
            Image(systemName:Self.symbol(status.level))
                .font(.system(size:size)).foregroundStyle(Self.color(status.level))
                .symbolEffect(.pulse,isActive:status.level == .connecting)
                .frame(minWidth:44,minHeight:44)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Connection: \(status.title)\(status.link.map {" over "+$0} ?? "")")
        .accessibilityHint("Shows connection details")
        .popover(isPresented:$showingDetails,arrowEdge:.top) {
            ConnectionDetails(status:status)
                .presentationCompactAdaptation(.popover)
        }
    }
}

private struct ConnectionDetails:View {
    @EnvironmentObject var model:ClockModel
    let status:ConnectionStatus
    var body:some View {
        VStack(alignment:.leading,spacing:8) {
            HStack(spacing:8) {
                Image(systemName:ConnectionBadge.symbol(status.level)).foregroundStyle(ConnectionBadge.color(status.level))
                Text(status.title).font(.headline)
            }
            if let link=status.link {
                Label(link,systemImage:link.hasPrefix("Wi-Fi") ? "wifi":"dot.radiowaves.left.and.right")
                    .font(.subheadline)
            }
            if !status.detail.isEmpty {
                Text(status.detail).font(.footnote).foregroundStyle(SugarTheme.secondary)
                    .fixedSize(horizontal:false,vertical:true)
            }
            if let synced=model.lastSettingsRefresh {
                Text("Last synced \(synced.formatted(.relative(presentation:.named)))")
                    .font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            if status.level == .failed || status.level == .idle {
                Button("Retry") {Task {await model.retrySelected()}}
                    .font(.subheadline.weight(.semibold)).padding(.top,2)
            }
        }
        .padding(16)
        .frame(minWidth:220,maxWidth:300,alignment:.leading)
        .foregroundStyle(SugarTheme.text)
        .tint(SugarTheme.accent)
    }
}
