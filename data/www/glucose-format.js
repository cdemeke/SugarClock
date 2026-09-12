// Match include/glucose_format.h: APIs stay in mg/dL; convert only display text.
globalThis.GlucoseFormat = Object.freeze({
    value(mgdl, mmol, signed = false) {
        const value = Number(mgdl);
        if (!Number.isFinite(value)) return '---';
        const sign = value < 0 ? '-' : signed ? '+' : '';
        const magnitude = Math.abs(value);
        if (!mmol) return sign + magnitude;
        const tenths = Math.floor((magnitude * 5 + 4) / 9);
        return sign + Math.floor(tenths / 10) + '.' + tenths % 10;
    }
});
