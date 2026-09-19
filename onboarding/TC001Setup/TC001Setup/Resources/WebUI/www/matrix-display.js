// TC001 geometry and diffuser treatment adapted from PR #50's demo.
// Accept the device's RGB frame unchanged; presentation never invents readings.
(function (scope) {
    const pitch = 40, fill = pitch * 0.9, inset = (pitch - fill) / 2;
    function draw(canvas, rgb) {
        if (rgb.length !== 32 * 8 * 3) throw new Error('Invalid matrix frame');
        if (canvas.width !== 1280 || canvas.height !== 320) {
            canvas.width = 1280;
            canvas.height = 320;
        }
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#07090a';
        ctx.fillRect(0, 0, 1280, 320);
        for (let i = 0; i < 256; i++) {
            const channels = Array.from(rgb.slice(i * 3, i * 3 + 3));
            ctx.save();
            ctx.translate((i % 32) * pitch + inset, Math.floor(i / 32) * pitch + inset);
            if (channels.every(c => c === 0)) {
                ctx.fillStyle = '#0b0e10';
                ctx.fillRect(0, 0, fill, fill);
            } else {
                const gradient = ctx.createRadialGradient(fill / 2, fill / 2, 0, fill / 2, fill / 2, fill * 0.7);
                for (const [stop, factor] of [[0,1],[0.65,1],[0.9,0.93],[1,0.82]]) {
                    gradient.addColorStop(stop, `rgb(${channels.map(c => Math.round(c * factor)).join(',')})`);
                }
                ctx.fillStyle = gradient;
                ctx.beginPath();
                ctx.roundRect(0, 0, fill, fill, pitch * 0.025);
                ctx.fill();
            }
            ctx.restore();
        }
    }
    scope.MatrixDisplay = {draw};
})(globalThis);
