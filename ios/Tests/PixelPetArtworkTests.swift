import XCTest
@testable import SugarClockCore

final class PixelPetArtworkTests:XCTestCase {
    private struct Fixtures:Decodable {
        struct Frame:Decodable {let id:Int;let frame:Int;let rows:[String]}
        let palette:[String:UInt32]
        let frames:[Frame]
    }
    func testAllSevenProductionNamesAndStableIDs() {
        XCTAssertEqual(PixelPetArtwork.allCases.map(\.rawValue),Array(0...6))
        XCTAssertEqual(PixelPetArtwork.allCases.map(\.name),["Pip","Boo","Mochi","Sprout","Pebble","Inky","Maple"])
        XCTAssertEqual(PixelPetArtwork.ghost.species,"Ghost")
        XCTAssertEqual(PixelPetArtwork.redPanda.species,"Red panda")
        XCTAssertNil(PixelPetArtwork(rawValue:7))
    }
    func testPixelsMatchSharedFirmwareFixtures() throws {
        let root=URL(fileURLWithPath:#filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let fixtures=try JSONDecoder().decode(Fixtures.self,from:Data(contentsOf:root.appendingPathComponent("protocol/fixtures/companions.json")))
        XCTAssertEqual(fixtures.frames.count,77)
        for sample in fixtures.frames {
            let pet=try XCTUnwrap(PixelPetArtwork(rawValue:sample.id))
            let expected=sample.rows.flatMap {$0.map {fixtures.palette[String($0)] ?? 0}}
            XCTAssertEqual(pet.pixels(frame:sample.frame<0 ? nil:sample.frame),expected,"Pet \(sample.id), frame \(sample.frame)")
        }
    }
    func testReduceMotionKeepsDistinctStaticPoses() {
        let poses=PixelPetArtwork.allCases.map {$0.pixels(frame:nil)}
        XCTAssertEqual(Set(poses).count,7)
        for pet in PixelPetArtwork.allCases {
            XCTAssertEqual(pet.pixels(frame:nil).count,256)
            XCTAssertEqual(pet.rows.count,8)
            XCTAssertTrue(pet.rows.allSatisfy {$0.count==32})
            XCTAssertNotEqual(pet.pixels(frame:0),pet.pixels(frame:7))
        }
    }
}
