import SwiftUI

struct CompanionPicker:View {
    @Binding var value:String
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var paused=false
    var minimum=0
    var maximum=1
    var body:some View {
        VStack(alignment:.leading,spacing:12) {
            ForEach(PixelPetArtwork.allCases.filter {$0.rawValue>=minimum && $0.rawValue<=maximum}) {artwork in
                let selected=value==String(artwork.rawValue)
                Button {value=String(artwork.rawValue)} label:{
                    VStack(alignment:.leading,spacing:12) {
                        HStack {
                            Text(artwork.name).font(.subheadline.weight(.semibold))
                            Spacer()
                            Image(systemName:selected ? "checkmark.circle.fill":"circle")
                                .foregroundStyle(selected ? SugarTheme.accent:SugarTheme.secondary)
                        }
                        PixelPetDisplay(artwork:artwork,paused:paused)
                    }
                    .padding(12).foregroundStyle(SugarTheme.text)
                    .background(selected ? SugarTheme.accent.opacity(0.08):SugarTheme.input,in:RoundedRectangle(cornerRadius:14))
                    .overlay {RoundedRectangle(cornerRadius:14).strokeBorder(selected ? SugarTheme.accent:SugarTheme.border,lineWidth:selected ? 2:1)}
                }
                .buttonStyle(.plain)
                .accessibilityElement(children:.ignore)
                .accessibilityLabel(artwork.name)
                .accessibilityValue(artwork.description)
                .accessibilityAddTraits(selected ? [.isSelected]:[])
                .accessibilityHint("Select this pet. Save changes to update your clock.")
            }
            if !reduceMotion {
                Button {paused.toggle()} label:{Label(paused ? "Play previews":"Pause previews",systemImage:paused ? "play.fill":"pause.fill")}
                    .font(.footnote).foregroundStyle(SugarTheme.accent)
            }
            Text("Sample appearance. Your clock changes when you save.")
                .font(.footnote).foregroundStyle(SugarTheme.secondary)
        }
    }
}

private struct PixelPetDisplay:View {
    let artwork:PixelPetArtwork
    let paused:Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.scenePhase) private var scenePhase
    @State private var visible=false
    var body:some View {
        TimelineView(.animation(minimumInterval:0.1,paused:paused || reduceMotion || scenePhase != .active || !visible)) {timeline in
            let frame=reduceMotion ? nil:Int(timeline.date.timeIntervalSinceReferenceDate*10)%720
            let pixels=artwork.pixels(frame:frame)
            Canvas {context,size in
                let step=min(size.width/32,size.height/8)
                let gap=step*0.15
                for (index,rgb) in pixels.enumerated() {
                    let rect=CGRect(x:Double(index%32)*step+gap/2,y:Double(index/32)*step+gap/2,width:step-gap,height:step-gap)
                    let color=rgb==0 ? Color(white:0.055):Color(red:Double(rgb>>16)/255,green:Double((rgb>>8)&255)/255,blue:Double(rgb&255)/255)
                    context.fill(Path(roundedRect:rect,cornerRadius:step*0.12),with:.color(color))
                }
            }
        }
        .aspectRatio(4,contentMode:.fit).padding(10)
        .background(.black,in:RoundedRectangle(cornerRadius:10))
        .onAppear {visible=true}.onDisappear {visible=false}
        .accessibilityHidden(true)
    }
}
