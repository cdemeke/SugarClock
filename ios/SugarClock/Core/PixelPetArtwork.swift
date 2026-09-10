import Foundation

/// Local sample of the awake animations in ambient_fish.cpp and ambient_ghost.cpp.
/// No live readings, weather effects or glucose-status overlays are simulated.
enum PixelPetArtwork:Int,CaseIterable,Identifiable {
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

    /// A nil frame gives the calm static pose for Reduce Motion.
    func pixels(frame:Int?)->[UInt32] {
        let phase=max(0,frame ?? 0)
        let lively=frame != nil && phase%80<16
        let tailFlick=(phase/(lively ? 2:12))%2==1
        let body:UInt32=frame != nil && (phase/12)%2==1 ? 0xD8C5FF:0xB48CFF
        var result=[UInt32](repeating:0,count:32*8)
        func put(_ x:Int,_ y:Int,_ color:UInt32) {result[y*32+x]=color}
        for (y,row) in rows.enumerated() {
            for (x,pixel) in row.enumerated() {
                switch pixel {
                case "O":put(x,y,0xF4762A)
                case "Y":if !tailFlick {put(x,y,0xFFBE42)}
                case "C":if x>=8 {put(x,y,0x97DAF2)}
                case "L":
                    if y != 4 || ![5,6,25,26].contains(x) {put(x,y,body)}
                default:break
                }
            }
        }
        if self == .fish {
            if tailFlick {
                for (x,y) in [(24,1),(23,2),(22,3),(23,4),(24,5),(25,6)] {put(x,y,0xFFBE42)}
            }
            let drift=(phase/12)%3
            put(4,(3-drift)%3,0x97DAF2)
            put(2,2-drift/2,0x309AC6)
        } else {
            let armY=lively ? (phase/2%2==1 ? 3:5):4
            for x in [5,6] {put(x,armY,body)}
            for x in [25,26] {put(x,lively ? 7-armY:4,body)}
        }
        return result
    }
}
