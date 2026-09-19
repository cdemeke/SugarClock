// TC001 geometry and diffuser treatment adapted from PR #50's demo.
// Accept the device's RGB frame unchanged; presentation never invents readings.
(function (scope) {
    const pitch = 40, fill = pitch * 0.9, inset = (pitch - fill) / 2;
    const diffuserStops = [[0,1],[0.65,1],[0.9,0.93],[1,0.82]];
    function draw(canvas, rgb) {
        if (rgb.length !== 32 * 8 * 3) throw new Error('Invalid matrix frame');
        if (canvas.width !== 1280 || canvas.height !== 320) {
            canvas.width = 1280;
            canvas.height = 320;
        }
        const ctx = canvas.getContext('2d');
        if (!ctx) return false;
        ctx.fillStyle = '#07090a';
        ctx.fillRect(0, 0, 1280, 320);
        for (let i = 0; i < 256; i++) {
            const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
            ctx.save();
            ctx.translate((i % 32) * pitch + inset, Math.floor(i / 32) * pitch + inset);
            if (r === 0 && g === 0 && b === 0) {
                ctx.fillStyle = '#0b0e10';
                ctx.fillRect(0, 0, fill, fill);
            } else {
                const gradient = ctx.createRadialGradient(fill / 2, fill / 2, 0, fill / 2, fill / 2, fill * 0.7);
                for (const [stop, factor] of diffuserStops) {
                    gradient.addColorStop(stop, `rgb(${Math.round(r * factor)},${Math.round(g * factor)},${Math.round(b * factor)})`);
                }
                ctx.fillStyle = gradient;
                ctx.fillRect(0, 0, fill, fill);
            }
            ctx.restore();
        }
        return true;
    }
    scope.MatrixDisplay = {draw};
})(globalThis);
