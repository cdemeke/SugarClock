// One small RGB request at a time. Background tabs stop polling the device.
globalThis.startLiveDisplay = function ({canvas, status, onMode}) {
    let timer, controller, inFlight = false, stopped = false;
    let etag = null;
    function setStatus(message) {
        if (status.textContent !== message) status.textContent = message;
    }

    function render(rgb) {
        try {
            if (MatrixDisplay.draw(canvas, rgb) === false) throw new Error('Canvas unavailable');
            return true;
        } catch (error) {
            setStatus('This browser could not render the clock display.');
            canvas.setAttribute('aria-label', 'Display rendering unavailable');
            return false;
        }
    }

    render(new Uint8Array(768));

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
                canvas.setAttribute('aria-label', 'Live display from your clock');
                const mode = response.headers.get('X-Display-Mode');
                if (mode) onMode(mode);
                setStatus('Live from your clock');
                return;
            }
            if (!response.ok) throw new Error('Display unavailable');
            const rgb = new Uint8Array(await response.arrayBuffer());
            if (rgb.length !== 32 * 8 * 3) throw new Error('Invalid display frame');
            if (stopped || document.hidden) return;
            if (!render(rgb)) {
                etag = null; // Retry the frame rather than accepting a 304 for an undrawn image.
                delay = 1500;
                return;
            }
            canvas.setAttribute('aria-label', 'Live display from your clock');
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
