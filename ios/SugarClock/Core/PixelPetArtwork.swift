import Foundation

/// Centered awake samples of the production companion renderer in companion.h.
/// Shared fixtures verify the sprite geometry, animation and palette. No live
/// reading, weather, nighttime or alert state is simulated by these previews.
enum PixelPetArtwork:Int,CaseIterable,Identifiable {
    case fish=0,ghost=1,axolotl=2,dinosaur=3,turtle=4,octopus=5,redPanda=6
    var id:Int {rawValue}
    var name:String {["Pip","Boo","Mochi","Sprout","Pebble","Inky","Maple"][rawValue]}
    var species:String {["Goldfish","Ghost","Axolotl","Dinosaur","Turtle","Octopus","Red panda"][rawValue]}
    var description:String {[
        "A goldfish with a golden tail", "A floating mint-white ghost",
        "A pink axolotl with rosy gills", "A green dinosaur with tiny feet",
        "A gentle turtle with a patterned shell", "A lavender octopus with waving arms",
        "A red panda with cream cheeks and a striped tail"
    ][rawValue]}

    private var sprite:[String] {
        switch self {
        case .fish:return [".....YY......","....OOOOO..Y.","..YOOOOOOOYY.",".OOOEOOOOOYYY","..OOOOOOOOYY.","...RRRORR..Y.","......Y......"]
        case .ghost:return ["...WWW...","..WWWWW..",".WWWWWWW.",".WEWWEWW.",".WWWWWWW.",".MWWEWWM.",".MM.M.MM."]
        case .axolotl:return [".G.......G.","..GPPPPPG..","G.PPPPPPP.G",".GPEPPPEPG.","G.PGPEPGP.G","...PPPPP...","..P.....P.."]
        case .dinosaur:return ["............","......LLLLL.",".....LLLELLL",".....LLLLLLL","L...DLLLL...","LL.DLLLLLL..",".LLLLLYL....","...LL.LL...."]
        case .turtle:return ["..............","....LLLL......","...LDLLDL.....","..LDDLDDDL.LL.","..LDLLLDLL.LEL",".LLLLLLLLLLLL.","...L....L.....","..LL...LL....."]
        case .octopus:return ["....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","..VVVVVVVV....","...VVVVVV.....","..VV.VV.VV....",".VV..VV..VV..."]
        case .redPanda:return [".O...O........",".OCCCO........","OOOOOOO.......","OCECECO....OO.","OCCYCCO...OYY.",".OOOOO...YYOO.",".TTTTT.OOYY...",".TT.TT.OO....."]
        }
    }

    static let palette:[Character:UInt32] = [
        "W":0xd1fff1,"M":0x86dfd1,"S":0x4b9d9d,"E":0x0b1725,
        "O":0xff9d48,"Y":0xffd47c,"R":0xe57439,"P":0xffc8d9,
        "G":0xec779e,"L":0x94e6a1,"D":0x50ad7c,"H":0xff83ac,
        "C":0xfff1d7,"V":0xc997ff,"T":0x8f5c47,"B":0x74bfdc
    ]

    /// A nil frame gives the calm static pose for Reduce Motion. Animated frames
    /// advance at the clock's 100 ms frame cadence, including short happy bursts.
    func pixels(frame:Int?)->[UInt32] {
        glyphs(frame:frame).flatMap {$0.map {Self.palette[$0] ?? 0}}
    }

    var rows:[String] {glyphs(frame:nil).map {String($0)}}

    private func glyphs(frame:Int?)->[[Character]] {
        let tick=max(0,frame ?? 0)%1_000_000
        let ms=tick*100
        let happy=frame != nil && tick%80<16
        let phase=(ms/(happy ? 220:650))%2
        var pet=Array(repeating:Array(repeating:Character("."),count:14),count:8)
        func put(_ x:Int,_ y:Int,_ color:Character) {
            if (0..<14).contains(x) && (0..<8).contains(y) {pet[y][x]=color}
        }
        func draw(_ x:Int,_ y:Int) {
            for (sy,row) in sprite.enumerated() {
                for (sx,c) in row.enumerated() where c != "." {put(x+sx,y+sy,c)}
            }
        }
        switch self {
        case .fish:
            let sy=(ms/1600)%2
            draw(0,sy)
            if phase==1 {put(12,sy+3,".");put(11,sy,"Y");put(11,sy+6,"Y")}
        case .ghost:
            let sy=(ms/1400)%2
            draw(3,sy)
            for x in 1..<8 {put(3+x,sy+6,(x+phase)%3==0 ? ".":"M")}
            if happy {put(3,sy+3,"W");put(11,sy+3,"W");put(2,sy+(phase==1 ? 2:3),"W");put(12,sy+(phase==1 ? 2:3),"W")}
        case .axolotl:
            draw(2,0)
            if happy {
                put(2,0,"G");put(12,0,"G");put(1,phase==1 ? 1:3,"G");put(13,phase==1 ? 1:3,"G")
                if phase==1 {put(3,0,".");put(11,0,".")}
            } else if phase==1 {put(3,0,".");put(11,0,".");put(2,1,"G");put(12,1,"G")}
        case .dinosaur:
            draw(1,0)
            if phase==1 {put(4,7,".");put(8,7,".");put(6,7,"D");put(9,7,"D")}
        case .turtle:
            draw(0,0)
            if phase==1 {put(2,7,".");put(7,7,".");put(4,7,"L");put(9,7,"L")}
            if happy {put(10,6,"L");put(11,phase==1 ? 5:6,"L")}
        case .octopus:
            draw(0,0)
            if phase==1 {put(1,7,".");put(10,7,".");put(3,7,"V");put(8,7,"V")}
            if happy {put(0,4,"V");put(11,4,"V");put(1,phase==1 ? 3:5,"V");put(10,phase==1 ? 3:5,"V")}
        case .redPanda:
            draw(0,0)
            if phase==1 {put(7,7,".");put(8,7,".");put(9,7,"O");put(10,6,"Y")}
            if happy {put(0,phase==1 ? 4:5,"O");put(6,phase==1 ? 4:5,"O")}
        }
        let columns=pet.flatMap {row in row.indices.filter {row[$0] != "."}}
        guard let left=columns.min(),let right=columns.max() else {return Array(repeating:Array(repeating:".",count:32),count:8)}
        let offset=(32-(right-left+1))/2-left
        var result=Array(repeating:Array(repeating:Character("."),count:32),count:8)
        for y in 0..<8 {for x in 0..<14 where pet[y][x] != "." {result[y][x+offset]=pet[y][x]}}
        return result
    }
}
