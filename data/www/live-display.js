// One small RGB request at a time. Background tabs stop polling the device.
globalThis.startLiveDisplay = function ({canvas, status, onMode}) {
    const context = canvas.getContext('2d');
    const image = context.createImageData(32, 8);
    let timer, controller, inFlight = false, stopped = false;
    let etag = null;
    function setStatus(message) {
        if (status.textContent !== message) status.textContent = message;
    }

    async function refresh() {
        if (stopped || document.hidden || inFlight) return;
        clearTimeout(timer);
        inFlight = true;
        controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 3000);
        let delay = 250;
        try {
            const response = await fetch('/api/display/frame', {cache: 'no-store', signal: controller.signal,
                headers: etag ? {'If-None-Match': etag} : {}});
            if (response.status === 204) {
                if (!stopped && !document.hidden) setStatus('Waiting for a fresh display frame…');
                return;
            }
            if (response.status === 304 && etag) {
                if (stopped || document.hidden) return;
                const mode = response.headers.get('X-Display-Mode');
                if (mode) onMode(mode);
                setStatus('Live from your clock');
                return;
            }
            if (!response.ok) throw new Error('Display unavailable');
            const rgb = new Uint8Array(await response.arrayBuffer());
            if (rgb.length !== 32 * 8 * 3) throw new Error('Invalid display frame');
            if (stopped || document.hidden) return;
            for (let pixel = 0; pixel < 256; pixel++) {
                image.data[pixel * 4] = rgb[pixel * 3];
                image.data[pixel * 4 + 1] = rgb[pixel * 3 + 1];
                image.data[pixel * 4 + 2] = rgb[pixel * 3 + 2];
                image.data[pixel * 4 + 3] = 255;
            }
            context.putImageData(image, 0, 0);
            etag = response.headers.get('ETag');
            canvas.dataset.frameSequence = response.headers.get('X-Display-Sequence') || '';
            const mode = response.headers.get('X-Display-Mode');
            if (mode) onMode(mode);
            setStatus('Live from your clock');
        } catch (error) {
            delay = 1500;
            if (!stopped && !document.hidden) {
                setStatus('Live display disconnected — reconnecting…');
                canvas.setAttribute('aria-label', 'Last display frame; connection lost');
            }
        } finally {
            clearTimeout(timeout);
            inFlight = false;
            if (!stopped && !document.hidden) timer = setTimeout(refresh, delay);
        }
    }

    function visibilityChanged() {
        clearTimeout(timer);
        if (document.hidden) {
            if (controller) controller.abort();
        } else refresh();
    }

    document.addEventListener('visibilitychange', visibilityChanged);
    refresh();
    return {
        refresh,
        stop() {
            stopped = true;
            clearTimeout(timer);
            if (controller) controller.abort();
            document.removeEventListener('visibilitychange', visibilityChanged);
        }
    };
};
