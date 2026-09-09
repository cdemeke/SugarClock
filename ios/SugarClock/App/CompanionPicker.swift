import SwiftUI

/// Static awake poses from ambient_fish.cpp / ambient_ghost.cpp. These are local
/// illustrations, never a live device display or a preview of glucose status.
private enum CompanionArtwork:Int,CaseIterable,Identifiable {
    case fish=0,ghost=1
    var id:Int {rawValue}
    var name:String {self == .fish ? "Fish":"Ghost"}
    var description:String {self == .fish ? "Orange pixel fish with bubbles":"Lavender pixel ghost"}
    var rows:[String] {
        switch self {
        case .fish:return [
            "....C...................Y.......",
            "..........OOOOOOOOO....Y........",
            "..C.....OOOOOOOOOOOOOOY.........",
            ".......O.OOOOOOOOOOOOOOO........",
            "........OOOOOOOOOOOOOOY.........",
            "..........OOOOOOOOO....Y........",
            "............CCCCC.......Y.......",
            "................................"
        ]
        case .ghost:return [
            "............LLLLLLLL............",
            "..........LLLLLLLLLLLL..........",
            ".........LLLLLLLLLLLLLL.........",
            "........LLLL.LLLLLL.LLLL........",
            ".....LLLLLLLLLLLLLLLLLLLLLL.....",
            "........LLLLLLL..LLLLLLL........",
            "........LLLLLLLLLLLLLLLL........",
            "........LLL.LLL.LLL.LLLL........"
        ]
        }
    }
    func color(_ pixel:Character,x:Int)->Color {
        let rgb:(Double,Double,Double)
        switch pixel {
        case "O":rgb=(244,118,42)
        case "Y":rgb=(255,190,66)
        // The forward bubble is drawn separately in the firmware's idle pose.
        case "C":rgb=x==2 ? (48,154,198):(151,218,242)
        case "L":rgb=(180,140,255)
        default:return Color(white:0.055)
        }
        return Color(red:rgb.0/255,green:rgb.1/255,blue:rgb.2/255)
    }
}

struct CompanionPicker:View {
    @Binding var value:String
    var minimum=0
    var maximum=1
    var body:some View {
        VStack(alignment:.leading,spacing:12) {
            ForEach(CompanionArtwork.allCases.filter {$0.rawValue>=minimum && $0.rawValue<=maximum}) {artwork in
                let selected=value==String(artwork.rawValue)
                Button {value=String(artwork.rawValue)} label:{
                    VStack(alignment:.leading,spacing:12) {
                        HStack {
                            Text(artwork.name).font(.subheadline.weight(.semibold))
                            Spacer()
                            Image(systemName:selected ? "checkmark.circle.fill":"circle")
                                .foregroundStyle(selected ? SugarTheme.accent:SugarTheme.secondary)
                        }
                        Canvas {context,size in
                            let step=min(size.width/32,size.height/8)
                            let gap=step*0.15
                            for (y,row) in artwork.rows.enumerated() {
                                for (x,pixel) in row.enumerated() {
                                    let rect=CGRect(x:Double(x)*step+gap/2,y:Double(y)*step+gap/2,width:step-gap,height:step-gap)
                                    context.fill(Path(roundedRect:rect,cornerRadius:step*0.12),with:.color(artwork.color(pixel,x:x)))
                                }
                            }
                        }
                        .aspectRatio(4,contentMode:.fit).padding(10)
                        .background(.black,in:RoundedRectangle(cornerRadius:10))
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
                .accessibilityHint("Select this companion. Save changes to update your clock.")
            }
            Text("Preview only. Your clock changes when you save.")
                .font(.footnote).foregroundStyle(SugarTheme.secondary)
        }
    }
}
