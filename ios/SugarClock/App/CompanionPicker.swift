import SwiftUI

struct CompanionPicker:View {
    @Environment(\.dynamicTypeSize) private var typeSize
    @Binding var value:String
    var minimum=0
    var maximum=1
    private var choices:[PixelPetArtwork] {
        PixelPetArtwork.allCases.filter {$0.rawValue>=minimum && $0.rawValue<=maximum}
    }
    var body:some View {
        VStack(alignment:.leading,spacing:16) {
            LazyVGrid(columns:Array(repeating:GridItem(.flexible(),spacing:10),count:typeSize.isAccessibilitySize ? 1:2),spacing:10) {
                ForEach(choices) {pet in
                    let selected=value==String(pet.rawValue)
                    Button {value=String(pet.rawValue)} label:{
                        VStack(alignment:.leading,spacing:8) {
                            PixelPetDisplay(artwork:pet,animated:selected)
                            HStack {
                                Text(pet.name).font(.subheadline.weight(.semibold))
                                Spacer(minLength:4)
                                Image(systemName:selected ? "checkmark.circle.fill":"circle")
                                    .foregroundStyle(selected ? SugarTheme.accent:SugarTheme.secondary)
                            }
                            Text(pet.species).font(.caption).foregroundStyle(SugarTheme.secondary)
                        }.padding(10).frame(maxWidth:.infinity,alignment:.leading)
                            .background(selected ? SugarTheme.accent.opacity(0.10):Color.clear,in:RoundedRectangle(cornerRadius:14))
                            .overlay(RoundedRectangle(cornerRadius:14).strokeBorder(selected ? SugarTheme.accent:SugarTheme.border,lineWidth:selected ? 2:1))
                            .contentShape(RoundedRectangle(cornerRadius:14))
                    }.buttonStyle(.plain)
                        .accessibilityLabel("\(pet.name), \(pet.species)")
                        .accessibilityValue(pet.description)
                        .accessibilityAddTraits(selected ? .isSelected:[])
                        .accessibilityHint("Choose this pet for your pending update.")
                }
            }
            if !choices.contains(where:{String($0.rawValue)==value}) {
                Text("Current pet (\(value)) is not in this app’s catalog.")
                    .font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
            if maximum<(PixelPetArtwork.allCases.map(\.rawValue).max() ?? 1) {
                Text("Update your clock’s firmware for more pets.")
                    .font(.footnote).foregroundStyle(SugarTheme.secondary)
            }
        }
    }
}

private struct PixelPetDisplay:View {
    let artwork:PixelPetArtwork
    var animated=false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.scenePhase) private var scenePhase
    @State private var visible=false
    var body:some View {
        TimelineView(.animation(minimumInterval:0.1,paused:!animated || reduceMotion || scenePhase != .active || !visible)) {timeline in
            let frame=(!animated || reduceMotion) ? nil:Int(timeline.date.timeIntervalSinceReferenceDate*10)%720
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
