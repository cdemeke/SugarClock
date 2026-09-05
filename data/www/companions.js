// Pixel-for-pixel parity with include/companion.h is verified in test_companions.py.
(function(scope) {
    'use strict';
    const names=['Pip the goldfish','Boo the ghost','Mochi the axolotl','Sprout the dinosaur'];
    const palette={a:'#f6a644',r:'#f06460',i:'#94e6a1',u:'#263b32',W:'#d1fff1',M:'#86dfd1',S:'#4b9d9d',E:'#0b1725',O:'#ff9d48',Y:'#ffd47c',R:'#e57439',P:'#ffc8d9',G:'#ec779e',L:'#94e6a1',D:'#50ad7c',H:'#ff83ac',B:'#74bfdc'};
    const valid=id=>Number.isInteger(id)&&id>=0&&id<4;
    const validStyle=style=>Number.isInteger(style)&&style>=0&&style<3;
    const fish=['.....YY......','....OOOOO..Y.','..YOOOOOOOYY.','.OOOEOOOOOYYY','..OOOOOOOOYY.','...RRRORR..Y.','......Y......'];
    const ghost=['...WWW...','..WWWWW..','.WWWWWWW.','.WEWWEWW.','.WWWWWWW.','.MWWEWWM.','.MM.M.MM.'];
    const shortGhost=['...WWW...','..WWWWW..','.WEWWEWW.','.WWWWWWW.','.MWWEWWM.','.MM.M.MM.'];
    const axolotl=['.G.......G.','..GPPPPPG..','G.PPPPPPP.G','.GPEPPPEPG.','G.PGPEPGP.G','...PPPPP...','..P.....P..'];
    const dino=[
        ['............','......LLLLL.','.....LLLELLL','.....LLLLLLL','L...DLLLL...','LL.DLLLLLL..','.LLLLLYL....','...LL.LL....'],
        ['............','............','......LLLLL.','.....LLLELLL','L...DLLLLLLL','LLLDLLLLL...','..LLLYLLLL..','...LL.LL....'],
        ['......LLLLL.','.....LLLELLL','.....LLLLLLL','....DLLLL...','L..DLLLLLL..','LL.LLLYL....','.LLLLLLL....','....L..L....']
    ];
    const font={L:['100','100','100','100','111'],O:['111','101','101','101','111'],W:['101','101','101','111','101'],K:['101','101','110','101','101'],H:['101','101','111','101','101'],I:['111','010','010','010','111'],G:['111','100','101','101','111']};
    // Styles: 0=text, 1=range icon, 2=centered pet. Ranges: 0=in range, 1=low, 2=high.
    function frame(id,ms,sleepy,happy,style=0,range=0) {
        if(!valid(id))id=0;
        if(!validStyle(style))style=0;
        if(!Number.isInteger(range)||range<0||range>2)range=0;
        ms=ms>>>0;
        sleepy=sleepy&&range===0&&!happy;
        happy=happy&&range===0;
        const pet=Array.from({length:8},()=>Array(14).fill('.'));
        const pixels=Array.from({length:8},()=>Array(32).fill('.'));
        const put=(x,y,c)=>{if(x>=0&&x<14&&y>=0&&y<8)pet[y][x]=c;};
        const sprite=(rows,sx,sy)=>rows.forEach((row,y)=>[...row].forEach((c,x)=>{if(c!=='.')put(sx+x,sy+y,c);}));
        const phase=sleepy?0:Math.floor(ms/(happy?220:range===1?1400:range===2?450:650))%2;
        if(id===0) {
            const sy=sleepy||range===1?1:range===2?0:Math.floor(ms/1600)%2;
            sprite(fish,0,sy);
            if(phase){put(12,sy+3,'.');put(11,sy,'Y');put(11,sy+6,'Y');}
            if(sleepy){put(4,sy+3,'O');put(4,sy+4,'E');}
        }else if(id===1) {
            const rows=range===0?ghost:shortGhost;
            const sy=sleepy?1:range===1?2:range===2?0:Math.floor(ms/1400)%2;
            sprite(rows,3,sy);
            for(let x=1;x<8;x++)put(3+x,sy+rows.length-1,(x+phase)%3===0?'.':'M');
            if(range===2||happy){put(3,sy+3,'W');put(11,sy+3,'W');put(2,sy+(phase?2:3),'W');put(12,sy+(phase?2:3),'W');}
            if(sleepy)for(const x of [5,8]){put(x,sy+3,'W');put(x,sy+4,'E');}
        }else if(id===2) {
            const sy=sleepy||range===1?1:0;
            sprite(axolotl,2,sy);
            if(range===1){
                for(const [x,y] of [[1,0],[9,0],[0,2],[10,2],[0,4],[10,4]])put(2+x,sy+y,'.');
                if(phase){put(3,sy+3,'.');put(11,sy+3,'.');put(4,sy+2,'G');put(10,sy+2,'G');}
            }else if(range===2||happy){
                put(2,0,'G');put(12,0,'G');put(1,phase?1:3,'G');put(13,phase?1:3,'G');
                if(phase){put(3,0,'.');put(11,0,'.');}
            }else if(phase){put(3,0,'.');put(11,0,'.');put(2,1,'G');put(12,1,'G');}
            if(sleepy)for(const x of [5,9]){put(x,sy+3,'P');put(x,sy+4,'E');}
        }else{
            sprite(dino[range],1,0);
            if(phase){
                if(range===1){put(1,4,'.');put(1,5,'L');put(2,5,'.');put(2,6,'L');}
                else if(range===0){put(4,7,'.');put(8,7,'.');put(6,7,'D');put(9,7,'D');}
                else{put(5,7,'.');put(8,7,'.');put(6,7,'L');put(9,7,'L');}
            }
            if(sleepy){put(9,2,'L');put(9,3,'E');}
        }
        let left=14,right=-1;
        pet.forEach(row=>row.forEach((c,x)=>{if(c!=='.'){left=Math.min(left,x);right=Math.max(right,x);}}));
        const offset=Math.floor(((style===2?32:14)-(right-left+1))/2)-left;
        pet.forEach((row,y)=>row.forEach((c,x)=>{if(c!=='.')pixels[y][x+offset]=c;}));
        const color=range===1?'a':range===2?'r':'i';
        if(style===0){
            const word=range===1?'LOW':range===2?'HIGH':'OK';
            const start=15+Math.floor((17-(word.length*4-1))/2);
            [...word].forEach((c,i)=>font[c].forEach((row,y)=>[...row].forEach((bit,x)=>{if(bit==='1')pixels[y+2][start+i*4+x]=color;})));
        }else if(style===1){
            const lit=range===1?6:range===2?0:3;
            for(const y of [0,3,6])for(let x=23;x<27;x++){pixels[y][x]=y===lit?color:'u';if(y===lit)pixels[y+1][x]=color;}
        }
        return pixels;
    }
    function draw(canvas,id,ms,mood,style=0,range=0) {
        const ctx=canvas.getContext('2d');if(!ctx)return;
        ctx.imageSmoothingEnabled=false;
        frame(id,ms,mood==='sleepy',mood==='happy',style,range).forEach((row,y)=>row.forEach((c,x)=>{
            ctx.fillStyle=palette[c]||'#07090d';ctx.fillRect(x,y,1,1);
        }));
    }
    scope.PixelCompanions={names,palette,valid,validStyle,frame,draw};
})(globalThis);
