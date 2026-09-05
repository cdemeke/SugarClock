#ifndef COMPANION_H
#define COMPANION_H

#include <stdint.h>
#include <string.h>
#include <initializer_list>

// Persisted IDs: keep Pip at zero for upgrades from Ambient Fish.
enum Companion { COMPANION_FISH, COMPANION_GHOST, COMPANION_AXOLOTL, COMPANION_DINO, COMPANION_COUNT };
inline bool companion_valid(int id) { return id >= 0 && id < COMPANION_COUNT; }
inline int companion_or_default(int id) { return companion_valid(id) ? id : COMPANION_FISH; }

enum CompanionStyle { COMPANION_TEXT, COMPANION_ICON, COMPANION_ONLY, COMPANION_STYLE_COUNT };
enum CompanionRange { COMPANION_IN_RANGE, COMPANION_LOW, COMPANION_HIGH };
inline bool companion_style_valid(int style) { return style >= 0 && style < COMPANION_STYLE_COUNT; }
inline int companion_style_or_default(int style) { return companion_style_valid(style) ? style : COMPANION_TEXT; }

inline uint32_t companion_color(char key) {
    switch (key) {
        case 'a': return 0xf6a644; // low: orange
        case 'r': return 0xf06460; // high: red
        case 'i': return 0x94e6a1; // in range: green
        case 'u': return 0x263b32; // unlit range marker
        case 'W': return 0xd1fff1;
        case 'M': return 0x86dfd1;
        case 'S': return 0x4b9d9d;
        case 'E': return 0x0b1725;
        case 'O': return 0xff9d48;
        case 'Y': return 0xffd47c;
        case 'R': return 0xe57439;
        case 'P': return 0xffc8d9;
        case 'G': return 0xec779e;
        case 'L': return 0x94e6a1;
        case 'D': return 0x50ad7c;
        case 'H': return 0xff83ac;
        case 'B': return 0x74bfdc;
        default: return 0;
    }
}

// Pure renderer shared by firmware and the parity-tested web preview. Draw the
// pet in a local 14-column area, then center its actual silhouette in its region.
inline void companion_frame(int id, uint32_t ms, bool sleepy, bool happy, char (&pixels)[8][32],
                            int style = COMPANION_TEXT, int range = COMPANION_IN_RANGE) {
    static const char* const fish[] = {".....YY......","....OOOOO..Y.","..YOOOOOOOYY.",".OOOEOOOOOYYY","..OOOOOOOOYY.","...RRRORR..Y.","......Y......"};
    static const char* const ghost[] = {"...WWW...","..WWWWW..",".WWWWWWW.",".WEWWEWW.",".WWWWWWW.",".MWWEWWM.",".MM.M.MM."};
    static const char* const short_ghost[] = {"...WWW...","..WWWWW..",".WEWWEWW.",".WWWWWWW.",".MWWEWWM.",".MM.M.MM."};
    static const char* const axolotl[] = {".G.......G.","..GPPPPPG..","G.PPPPPPP.G",".GPEPPPEPG.","G.PGPEPGP.G","...PPPPP...","..P.....P.."};
    static const char* const dino[3][8] = {
        {"............","......LLLLL.",".....LLLELLL",".....LLLLLLL","L...DLLLL...","LL.DLLLLLL..",".LLLLLYL....","...LL.LL...."},
        {"............","............","......LLLLL.",".....LLLELLL","L...DLLLLLLL","LLLDLLLLL...","..LLLYLLLL..","...LL.LL...."},
        {"......LLLLL.",".....LLLELLL",".....LLLLLLL","....DLLLL...","L..DLLLLLL..","LL.LLLYL....",".LLLLLLL....","....L..L...."}
    };
    id = companion_or_default(id);
    style = companion_style_or_default(style);
    if (range < COMPANION_IN_RANGE || range > COMPANION_HIGH) range = COMPANION_IN_RANGE;
    // An out-of-range pose stays awake and recognizable during rest hours.
    sleepy = sleepy && range == COMPANION_IN_RANGE && !happy;
    happy = happy && range == COMPANION_IN_RANGE;
    char pet[8][14]; memset(pet, '.', sizeof(pet)); memset(pixels, '.', sizeof(pixels));
    auto put = [&](int x, int y, char c) { if (x >= 0 && x < 14 && y >= 0 && y < 8) pet[y][x] = c; };
    auto sprite = [&](const char* const* rows, int count, int sx, int sy) {
        for (int y = 0; y < count; ++y) for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] != '.') put(sx + x, sy + y, rows[y][x]);
    };
    int phase = sleepy ? 0 : (ms / (happy ? 220 : range == COMPANION_LOW ? 1400 : range == COMPANION_HIGH ? 450 : 650)) % 2;
    if (id == COMPANION_FISH) {
        int sy = sleepy || range == COMPANION_LOW ? 1 : range == COMPANION_HIGH ? 0 : (ms / 1600) % 2;
        sprite(fish, 7, 0, sy);
        if (phase) { put(12, sy+3, '.'); put(11, sy, 'Y'); put(11, sy+6, 'Y'); }
        if (sleepy) { put(4, sy+3, 'O'); put(4, sy+4, 'E'); }
    } else if (id == COMPANION_GHOST) {
        bool full = range == COMPANION_IN_RANGE;
        int sy = sleepy ? 1 : range == COMPANION_LOW ? 2 : range == COMPANION_HIGH ? 0 : (ms / 1400) % 2;
        int height = full ? 7 : 6;
        sprite(full ? ghost : short_ghost, height, 3, sy);
        for (int x=1; x<8; ++x) put(3+x, sy+height-1, (x+phase)%3 == 0 ? '.' : 'M');
        if (range == COMPANION_HIGH || happy) {
            put(3, sy+3, 'W'); put(11, sy+3, 'W'); put(2, sy+(phase?2:3), 'W'); put(12, sy+(phase?2:3), 'W');
        }
        if (sleepy) for (int x : {5, 8}) { put(x, sy+3, 'W'); put(x, sy+4, 'E'); }
    } else if (id == COMPANION_AXOLOTL) {
        int sy = sleepy || range == COMPANION_LOW ? 1 : 0;
        sprite(axolotl, 7, 2, sy);
        if (range == COMPANION_LOW) {
            static const int tucked[][2] = {{1,0},{9,0},{0,2},{10,2},{0,4},{10,4}};
            for (const auto& p : tucked) put(2+p[0], sy+p[1], '.');
            if (phase) { put(3, sy+3, '.'); put(11, sy+3, '.'); put(4, sy+2, 'G'); put(10, sy+2, 'G'); }
        } else if (range == COMPANION_HIGH || happy) {
            put(2,0,'G'); put(12,0,'G'); put(1,phase?1:3,'G'); put(13,phase?1:3,'G');
            if (phase) { put(3,0,'.'); put(11,0,'.'); }
        } else if (phase) { put(3,0,'.'); put(11,0,'.'); put(2,1,'G'); put(12,1,'G'); }
        if (sleepy) for (int x : {5, 9}) { put(x, sy+3, 'P'); put(x, sy+4, 'E'); }
    } else {
        sprite(dino[range], 8, 1, 0);
        if (phase) {
            if (range == COMPANION_LOW) { put(1,4,'.'); put(1,5,'L'); put(2,5,'.'); put(2,6,'L'); }
            else if (range == COMPANION_IN_RANGE) { put(4,7,'.'); put(8,7,'.'); put(6,7,'D'); put(9,7,'D'); }
            else { put(5,7,'.'); put(8,7,'.'); put(6,7,'L'); put(9,7,'L'); }
        }
        if (sleepy) { put(9,2,'L'); put(9,3,'E'); }
    }
    int left=14, right=-1;
    for (int y=0; y<8; ++y) for (int x=0; x<14; ++x) if (pet[y][x] != '.') {
        if (x<left) left=x; if (x>right) right=x;
    }
    int offset = ((style == COMPANION_ONLY ? 32 : 14) - (right-left+1))/2 - left;
    for (int y=0; y<8; ++y) for (int x=0; x<14; ++x)
        if (pet[y][x] != '.') pixels[y][x+offset]=pet[y][x];
    char color = range == COMPANION_LOW ? 'a' : range == COMPANION_HIGH ? 'r' : 'i';
    if (style == COMPANION_TEXT) {
        static const char letters[] = "LOWKHIG";
        static const char* const font[7][5] = {
            {"100","100","100","100","111"}, {"111","101","101","101","111"},
            {"101","101","101","111","101"}, {"101","101","110","101","101"},
            {"101","101","111","101","101"}, {"111","010","010","010","111"},
            {"111","100","101","101","111"}
        };
        const char* word = range == COMPANION_LOW ? "LOW" : range == COMPANION_HIGH ? "HIGH" : "OK";
        int start = 15 + (17 - (strlen(word)*4-1))/2;
        for (int i=0; word[i]; ++i) {
            int glyph = strchr(letters, word[i])-letters;
            for (int y=0; y<5; ++y) for (int x=0; x<3; ++x)
                if (font[glyph][y][x]=='1') pixels[y+2][start+i*4+x]=color;
        }
    } else if (style == COMPANION_ICON) {
        int lit = range == COMPANION_LOW ? 6 : range == COMPANION_HIGH ? 0 : 3;
        for (int y : {0,3,6}) for (int x=23; x<27; ++x) {
            pixels[y][x] = y==lit ? color : 'u';
            if (y==lit) pixels[y+1][x]=color;
        }
    }
}

#endif
