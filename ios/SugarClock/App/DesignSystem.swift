import SwiftUI

// Color values and illustrated assets follow web design PR #30.
enum SugarTheme {
    static func adaptive(_ light:UInt32,_ dark:UInt32)->Color {
        Color(uiColor:UIColor { traits in
            let hex=traits.userInterfaceStyle == .dark ? dark:light
            return UIColor(red:CGFloat((hex>>16)&255)/255,green:CGFloat((hex>>8)&255)/255,blue:CGFloat(hex&255)/255,alpha:1)
        })
    }
    static let background=adaptive(0xf3f7f4,0x0f1117)
    static let card=adaptive(0xffffff,0x1e2128)
    static let input=adaptive(0xf4f4f5,0x111318)
    static let border=adaptive(0xe4e4e7,0x333338)
    static let accent=adaptive(0x16894a,0x39d97a)
    static let text=adaptive(0x18181b,0xe4e4e7)
    static let secondary=adaptive(0x52525b,0xa1a1aa)
    static let buttonText=adaptive(0xffffff,0x0f2316)
}

struct BrandIcon:View {
    let name:String
    var size:CGFloat=48
    private var artwork:Image {
        #if DEBUG
        if ProcessInfo.processInfo.environment["SUGARCLOCK_SCREENSHOT"] != nil,
           let path=Bundle.main.path(forResource:name,ofType:"png"),let image=UIImage(contentsOfFile:path) {return Image(uiImage:image)}
        #endif
        return Image(name)
    }
    var body:some View {artwork.resizable().scaledToFit().frame(width:size,height:size).accessibilityHidden(true)}
}

struct SugarScreen<Content:View>:View {
    @ViewBuilder let content:Content
    var body:some View {
        ScrollView {
            VStack(alignment:.leading,spacing:20) {content}
                .frame(maxWidth:760).padding(.horizontal,20).padding(.top,16).padding(.bottom,32)
                .frame(maxWidth:.infinity)
        }
        .background(SugarTheme.background.ignoresSafeArea())
        .foregroundStyle(SugarTheme.text).tint(SugarTheme.accent)
        .navigationBarTitleDisplayMode(.inline)
        .toolbarBackground(SugarTheme.background,for:.navigationBar)
        .scrollDismissesKeyboard(.interactively)
    }
}

struct PageHeading:View {
    let title:String
    let subtitle:String
    var icon="ConfigurationIcon"
    var body:some View {
        HStack(alignment:.top,spacing:14) {
            BrandIcon(name:icon,size:46)
            VStack(alignment:.leading,spacing:6) {
                Text(title).font(.title2.bold()).accessibilityAddTraits(.isHeader)
                Text(subtitle).font(.subheadline).foregroundStyle(SugarTheme.secondary).fixedSize(horizontal:false,vertical:true)
            }
        }.padding(.vertical,6)
    }
}

struct SugarCard<Content:View>:View {
    var title:String?=nil
    @ViewBuilder let content:Content
    var body:some View {
        VStack(alignment:.leading,spacing:18) {
            if let title {Text(title).font(.headline).accessibilityAddTraits(.isHeader);Divider()}
            content
        }.frame(maxWidth:.infinity,alignment:.leading).padding(20)
            .background(SugarTheme.card,in:RoundedRectangle(cornerRadius:20))
            .overlay(RoundedRectangle(cornerRadius:20).strokeBorder(SugarTheme.border.opacity(0.65),lineWidth:1))
            .shadow(color:Color.black.opacity(0.035),radius:12,x:0,y:5)
    }
}

struct SugarButtonStyle:ButtonStyle {
    var prominent=true
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration:Configuration)->some View {
        configuration.label.font(.subheadline.weight(.semibold))
            .frame(maxWidth:.infinity,minHeight:24).padding(.horizontal,16).padding(.vertical,12)
            .foregroundStyle(prominent ? SugarTheme.buttonText:SugarTheme.accent)
            .background(prominent ? SugarTheme.accent:SugarTheme.accent.opacity(0.1),in:RoundedRectangle(cornerRadius:12))
            .opacity(enabled ? (configuration.isPressed ? 0.75:1):0.4)
    }
}

struct FieldSurface:ViewModifier {
    func body(content:Content)->some View {
        content.padding(13).frame(minHeight:46)
            .background(SugarTheme.input,in:RoundedRectangle(cornerRadius:10))
            .overlay(RoundedRectangle(cornerRadius:10).strokeBorder(SugarTheme.border,lineWidth:1))
    }
}
extension View {func fieldSurface()->some View {modifier(FieldSurface())}}

struct StatusPill:View {
    let text:String
    var active=true
    var body:some View {
        HStack(spacing:6) {Circle().frame(width:6,height:6);Text(text).font(.caption.weight(.semibold))}
            .foregroundStyle(active ? SugarTheme.accent:SugarTheme.secondary)
            .padding(.horizontal,10).padding(.vertical,7)
            .background((active ? SugarTheme.accent:SugarTheme.secondary).opacity(0.1),in:Capsule())
            .fixedSize(horizontal:false,vertical:true)
    }
}

struct DetailRow:View {
    let title:String
    let value:String
    var body:some View {
        ViewThatFits(in:.horizontal) {
            HStack(alignment:.firstTextBaseline) {Text(title);Spacer(minLength:16);Text(value).foregroundStyle(SugarTheme.secondary)}
            VStack(alignment:.leading,spacing:4) {Text(title);Text(value).foregroundStyle(SugarTheme.secondary)}
        }.font(.subheadline).accessibilityElement(children:.combine)
    }
}

struct DestinationRow:View {
    let title:String
    let subtitle:String
    let symbol:String
    var body:some View {
        HStack(spacing:14) {
            Image(systemName:symbol).font(.title3).foregroundStyle(SugarTheme.accent)
                .frame(width:42,height:42).background(SugarTheme.accent.opacity(0.09),in:RoundedRectangle(cornerRadius:12)).accessibilityHidden(true)
            VStack(alignment:.leading,spacing:4) {
                Text(title).font(.subheadline.weight(.semibold)).foregroundStyle(SugarTheme.text)
                Text(subtitle).font(.caption).foregroundStyle(SugarTheme.secondary)
            }
            Spacer(minLength:0)
            Image(systemName:"chevron.right").font(.caption.weight(.semibold)).foregroundStyle(SugarTheme.secondary).accessibilityHidden(true)
        }.frame(minHeight:48).contentShape(Rectangle())
    }
}

struct OperationFeedback:View {
    @EnvironmentObject var model:ClockModel
    var body:some View {
        if model.busy {ProgressView(model.reconnecting ? "Reconnecting to your clock…" : "Working with your clock…").frame(maxWidth:.infinity).tint(SugarTheme.accent)}
        if model.reconnecting {
            Text("You can keep browsing and editing. Saving resumes when the clock reconnects.").font(.footnote).foregroundStyle(SugarTheme.secondary)
            Button("Stop reconnecting") {model.stopReconnecting()}.buttonStyle(SugarButtonStyle(prominent:false))
        } else if model.selected != nil,!model.bluetooth.connected,!model.busy {
            Button("Reconnect") {Task {await model.retrySelected()}}.buttonStyle(SugarButtonStyle(prominent:false))
        }
        if !model.message.isEmpty {
            Text(model.message).font(.footnote).foregroundStyle(SugarTheme.secondary)
                .accessibilityLabel("Operation result: \(model.message)").frame(maxWidth:.infinity,alignment:.leading)
        }
    }
}
