const canvas = document.getElementById('pixel-canvas');
const ctx = canvas.getContext('2d');
const particles = [];
const ambientParticles = [];

// SugarClock Brand Colors
const colors = ['#82EDA6', '#AEFBFF', '#FCCDDC', '#FFFF94', '#94a3b8'];

let width, height;
// Offscreen canvas to render text and sample pixels
const offscreenCanvas = document.createElement('canvas');
const offscreenCtx = offscreenCanvas.getContext('2d');

let sequences = [];
let currentSequenceIndex = 0;
let transitionTimer = 0;
const STATE_FALLING = 0;
const STATE_SETTLED = 1;
const STATE_WAITING = 2;
let sequenceState = STATE_FALLING;

let borderAlpha = 0;
let boxWidth = 0;
let boxHeight = 0;
let boxX = 0;
let boxY = 0;

let mouseX = window.innerWidth / 2;
let mouseY = window.innerHeight / 2;
let currentTiltX = 0;
let currentTiltY = 0;

window.addEventListener('mousemove', (e) => {
    mouseX = e.clientX;
    mouseY = e.clientY;
});

function resize() {
    width = canvas.width = document.documentElement.clientWidth;
    height = canvas.height = document.querySelector('.hero').offsetHeight;
    initParticles();
    initAmbientParticles();
}

class AmbientParticle {
    constructor() {
        this.x = Math.random() * width;
        this.y = Math.random() * height;
        this.color = colors[Math.floor(Math.random() * colors.length)];
        this.size = Math.random() > 0.8 ? 3 : 2;
        // Slow drifting speed
        this.vx = (Math.random() - 0.5) * 0.5;
        this.vy = (Math.random() - 0.5) * 0.5;
        // For a slight subtle fade
        this.alpha = Math.random() * 0.5 + 0.1;
    }

    update() {
        this.x += this.vx;
        this.y += this.vy;

        // Wrap around screen edges
        if (this.x < 0) this.x = width;
        if (this.x > width) this.x = 0;
        if (this.y < 0) this.y = height;
        if (this.y > height) this.y = 0;
    }

    draw() {
        ctx.globalAlpha = this.alpha;
        ctx.fillStyle = this.color;
        ctx.fillRect(this.x, this.y, this.size, this.size);
        ctx.globalAlpha = 1.0; // Reset
    }
}

class Particle {
    constructor(x, y, color, size, isPartOfFirstState) {
        // Target position (grid aligned)
        this.targetX = x;
        this.targetY = y;

        // If it's part of the first screen, it falls from off-screen
        if (isPartOfFirstState) {
            const angle = Math.random() * Math.PI * 2;
            const distance = Math.max(width, height) + Math.random() * 500;
            this.x = width / 2 + Math.cos(angle) * distance;
            this.y = height / 2 + Math.sin(angle) * distance;
            this.settled = false;
        } else {
            // Otherwise, it starts already in position but invisible
            this.x = x;
            this.y = y;
            this.settled = true;
        }

        // Properties
        this.originalColor = color;
        this.color = color;
        this.size = size; // Enforce strict grid size
        this.speed = Math.random() * 0.05 + 0.02; // Fall speed / lerp factor

        // State
        this.activeInStates = [];
        this.alpha = isPartOfFirstState ? 1 : 0;
        this.targetAlpha = isPartOfFirstState ? 1 : 0;

        // Wobble effect while falling
        this.wobble = Math.random() * Math.PI * 2;
        this.wobbleSpeed = Math.random() * 0.1;
    }

    update(currentState) {
        // Find if this particle is active in the current state
        const shouldBeActive = this.activeInStates[currentState];
        this.targetAlpha = shouldBeActive ? 1 : 0;

        // Alpha fade for smooth gradient transition
        if (this.alpha < this.targetAlpha) {
            this.alpha = Math.min(1, this.alpha + 0.05);
        } else if (this.alpha > this.targetAlpha) {
            this.alpha = Math.max(0, this.alpha - 0.05);
        }

        // Move towards target smoothly only if not settled
        if (!this.settled) {
            const dx = this.targetX - this.x;
            const dy = this.targetY - this.y;

            this.x += dx * this.speed;
            this.y += dy * this.speed;

            // Add small wobble if it hasn't settled yet
            if (Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5) {
                this.x = this.targetX;
                this.y = this.targetY;
                this.settled = true;
            } else if (Math.abs(dy) > 1) {
                this.wobble += this.wobbleSpeed;
                this.x += Math.sin(this.wobble) * 0.5;
            }
        }
    }

    draw(currentState, offsetX = 0, offsetY = 0) {
        // Optimization: don't draw if fully faded out and settled
        if (this.alpha <= 0.01 && this.settled) return;

        ctx.save();
        ctx.globalAlpha = this.alpha;

        // Once settled AND active, become a solid brand green color
        const isActive = this.activeInStates[currentState];
        ctx.fillStyle = (this.settled && isActive) ? '#82EDA6' : this.color;

        ctx.fillRect(this.x + offsetX, this.y + offsetY, this.size, this.size);
        ctx.restore();
    }
}

function generateTargets(text, step) {
    offscreenCtx.clearRect(0, 0, width, height);
    const centerY = width <= 480 ? height / 4 + 20 : height / 2 - 50;
    offscreenCtx.fillText(text, width / 2, centerY);

    const imgData = offscreenCtx.getImageData(0, 0, width, height);
    const data = imgData.data;
    const targets = [];

    for (let y = 0; y < height; y += step) {
        for (let x = 0; x < width; x += step) {
            const index = (y * width + x) * 4;
            if (data[index + 3] > 128) {
                targets.push({ x, y });
            }
        }
    }
    return targets;
}

function initParticles() {
    particles.length = 0;
    sequences = [];
    currentSequenceIndex = 0;
    sequenceState = STATE_FALLING;

    offscreenCanvas.width = width;
    offscreenCanvas.height = height;

    // Increase font size to fill more vertical and horizontal space in the box
    let fontSize = width > 768 ? 160 : (width > 480 ? 100 : 50);
    offscreenCtx.fillStyle = 'white';
    offscreenCtx.font = `800 ${fontSize}px "Outfit", sans-serif`;
    offscreenCtx.textAlign = 'center';
    offscreenCtx.textBaseline = 'middle';

    // Measure "SugarClock" to define max bounds and step size
    const metrics = offscreenCtx.measureText('SugarClock');
    const textHeight = Math.abs(metrics.actualBoundingBoxAscent) + Math.abs(metrics.actualBoundingBoxDescent);
    const step = Math.max(3, Math.ceil(textHeight / 20)); // Keep 20 vertical blocks constant but sample more often
    const particleSize = step - 1;

    // Define the bounding box for the rounded rectangle device
    const textWidth = metrics.width;
    const paddingX = 20; // tighter framing
    const paddingY = 15; // tighter framing
    const centerY = width <= 480 ? height / 4 + 20 : height / 2 - 50;
    boxWidth = textWidth + paddingX * 2;
    boxHeight = textHeight + paddingY * 2;
    boxX = (width - boxWidth) / 2;
    boxY = centerY - textHeight / 2 - paddingY;

    // Generate coordinate targets for all phrases
    const phrases = ['SugarClock', '102 ->', '+2'];
    const allCoords = new Map();

    for (let i = 0; i < phrases.length; i++) {
        const targets = generateTargets(phrases[i], step);
        sequences.push(targets);

        for (const t of targets) {
            const key = `${t.x},${t.y}`;
            if (!allCoords.has(key)) {
                const color = colors[Math.floor(Math.random() * colors.length)];
                // isPartOfFirstState is true if i === 0
                const p = new Particle(t.x, t.y, color, particleSize, i === 0);
                // Initialize state array
                p.activeInStates = new Array(phrases.length).fill(false);
                allCoords.set(key, p);
                particles.push(p);
            }
            // Mark it active for this phrase sequence
            allCoords.get(key).activeInStates[i] = true;
        }
    }
}

function initAmbientParticles() {
    ambientParticles.length = 0;
    // Number of ambient particles scales with screen size
    const numAmbient = Math.floor((width * height) / 10000);

    for (let i = 0; i < numAmbient; i++) {
        ambientParticles.push(new AmbientParticle());
    }
}

function animate() {
    // Clear canvas with a slight trail effect (alpha 0.2)
    ctx.fillStyle = 'var(--bg-base)';
    ctx.clearRect(0, 0, width, height);

    for (let i = 0; i < ambientParticles.length; i++) {
        ambientParticles[i].update();
        ambientParticles[i].draw();
    }

    // Calculate Parallax Tilt for 3D Effect
    let targetTiltX = ((mouseX / width) - 0.5) * 40; // up to 20px tilt
    let targetTiltY = ((mouseY / height) - 0.5) * 40;

    // Auto-breathe effect for mobile/tablet where mouse isn't active
    if (width <= 768) {
        if (borderAlpha >= 1) {
            const time = Date.now() / 1000;
            targetTiltX = Math.sin(time * 0.75) * 20;
            targetTiltY = (Math.cos(time * 0.6) * 2.5) + 4; // Positive Y to reveal the top buttons
        } else {
            targetTiltX = 0;
            targetTiltY = 0;
        }
    }
    currentTiltX += (targetTiltX - currentTiltX) * 0.1;
    currentTiltY += (targetTiltY - currentTiltY) * 0.1;

    const frontOffsetX = currentTiltX;
    const frontOffsetY = currentTiltY;
    const backOffsetX = -currentTiltX * 0.5;
    const backOffsetY = -currentTiltY * 0.5;

    // Draw Ulanzi TC001 background border
    if (borderAlpha > 0) {
        ctx.save();
        ctx.globalAlpha = borderAlpha;

        const devicePadding = 24;

        // 3D Extrusion Side Body
        const layers = 10;
        for (let i = 0; i <= layers; i++) {
            let t = i / layers;
            let ox = backOffsetX + (frontOffsetX - backOffsetX) * t;
            let oy = backOffsetY + (frontOffsetY - backOffsetY) * t;

            // White front, soft light gray/cream sides
            ctx.fillStyle = i === layers ? '#ffffff' : '#e2e8f0';

            // Draw Main Casing FIRST so buttons can be drawn over it in the middle layers
            ctx.beginPath();
            if (ctx.roundRect) {
                ctx.roundRect(boxX - devicePadding + ox, boxY - devicePadding + oy, boxWidth + devicePadding * 2, boxHeight + devicePadding * 2, 20);
            } else {
                ctx.rect(boxX - devicePadding + ox, boxY - devicePadding + oy, boxWidth + devicePadding * 2, boxHeight + devicePadding * 2);
            }
            ctx.fill();
        }

        // Draw Flat Isometric Top Buttons mapping to the top surface
        // We only draw them when looking down at the device (currentTiltY > 0)
        if (currentTiltY > 0) {
            const depth = 20;
            const btnW = 36;
            const btnH = 8;
            const spacing = 16;
            const totalBtnsW = (btnW * 3) + (spacing * 2);
            const startU = ((boxWidth + devicePadding * 2) / 2) - (totalBtnsW / 2);
            const startV = 6;

            for (let extrude = 0; extrude < 3; extrude++) {
                ctx.save();
                // Extrude slightly UP to create a very shallow indented lip/edge
                ctx.translate(0, -extrude * 0.5);

                // Transform maps flat 2D shapes perfectly onto the perspective top-plane
                ctx.transform(
                    1, 0,
                    (frontOffsetX - backOffsetX) / depth, (frontOffsetY - backOffsetY) / depth,
                    boxX - devicePadding + backOffsetX, boxY - devicePadding + backOffsetY
                );

                // Color: sides match the casing cream (#e2e8f0), top face is soft white/gray (#f1f5f9)
                // This creates a subtle indentation effect rather than a dark overlay block.
                ctx.fillStyle = extrude === 2 ? '#f1f5f9' : '#e2e8f0';

                ctx.beginPath();
                if (ctx.roundRect) {
                    ctx.roundRect(startU, startV, btnW, btnH, 4);
                    ctx.roundRect(startU + btnW + spacing, startV, btnW, btnH, 4);
                    ctx.roundRect(startU + (btnW + spacing) * 2, startV, btnW, btnH, 4);
                } else {
                    ctx.rect(startU, startV, btnW, btnH);
                    ctx.rect(startU + btnW + spacing, startV, btnW, btnH);
                    ctx.rect(startU + (btnW + spacing) * 2, startV, btnW, btnH);
                }
                ctx.fill();

                if (extrude === 2) {
                    ctx.fillStyle = '#94a3b8'; // softer slate for symbols to match the lighter aesthetic
                    ctx.font = '8px Arial';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';

                    // Because affine transform naturally squashes coordinates vertically to the perspective,
                    // text drawn here automatically looks "painted on" the flat top surface perfectly!
                    ctx.fillText('◀', startU + btnW / 2, startV + btnH / 2 + 0.5);
                    ctx.fillText('●', startU + btnW + spacing + btnW / 2, startV + btnH / 2 + 0.5);
                    ctx.fillText('▶', startU + (btnW + spacing) * 2 + btnW / 2, startV + btnH / 2 + 0.5);
                }

                ctx.restore();
            }
        }

        // Inner display screen (dark glass)
        ctx.fillStyle = '#020617'; // slate-950 (almost black)
        ctx.beginPath();
        if (ctx.roundRect) {
            ctx.roundRect(boxX + frontOffsetX, boxY + frontOffsetY, boxWidth, boxHeight, 8);
        } else {
            ctx.rect(boxX + frontOffsetX, boxY + frontOffsetY, boxWidth, boxHeight);
        }
        ctx.fill();

        // Very subtle glare/reflection on the glass to look like an actual screen
        // Angle the glare based on tilt
        ctx.fillStyle = 'rgba(255, 255, 255, 0.02)';
        ctx.beginPath();
        if (ctx.roundRect) {
            ctx.roundRect(boxX + frontOffsetX, boxY + frontOffsetY - (currentTiltY * 0.2), boxWidth, boxHeight / 2, 8);
        } else {
            ctx.rect(boxX + frontOffsetX, boxY + frontOffsetY - (currentTiltY * 0.2), boxWidth, boxHeight / 2);
        }
        ctx.fill();

        ctx.restore();
    }

    let allSettled = true;
    for (let i = 0; i < particles.length; i++) {
        particles[i].update(currentSequenceIndex);
        particles[i].draw(currentSequenceIndex, frontOffsetX, frontOffsetY);

        // Check if particles active in the current sequence are settled
        if (particles[i].activeInStates[currentSequenceIndex] && !particles[i].settled) {
            allSettled = false;
        }
    }

    // State machine logic
    if (sequenceState === STATE_FALLING) {
        if (allSettled) {
            sequenceState = STATE_SETTLED;
            transitionTimer = Date.now();
        }
    } else if (sequenceState === STATE_SETTLED) {
        // Fade in border once first word settles
        if (currentSequenceIndex === 0 && borderAlpha < 1) {
            borderAlpha += 0.05;
        }

        // Wait 3 seconds then transition
        if (Date.now() - transitionTimer > 3000) {
            sequenceState = STATE_WAITING;
            currentSequenceIndex = (currentSequenceIndex + 1) % sequences.length;
            transitionTimer = Date.now(); // Reset timer for the next screen
            // Since particles just fade, they instantly transition state logic,
            // we keep it in SETTLED to wait another 3s. No more FALLING needed.
            sequenceState = STATE_SETTLED;
        }
    }

    requestAnimationFrame(animate);
}

// Initial setup
let resizeTimeout;
let lastWidth = window.innerWidth;
let lastHeight = window.innerHeight;

window.addEventListener('resize', () => {
    const currentWidth = window.innerWidth;
    const currentHeight = window.innerHeight;

    // On mobile devices, scrolling often triggers a resize event because the address bar 
    // hiding/showing changes innerHeight. To prevent the animation from resetting,
    // we ignore height-only changes on small screens.
    const isMobile = currentWidth <= 768;
    const widthChanged = currentWidth !== lastWidth;
    const heightChanged = currentHeight !== lastHeight;

    if (widthChanged || (!isMobile && heightChanged)) {
        lastWidth = currentWidth;
        lastHeight = currentHeight;

        clearTimeout(resizeTimeout);
        resizeTimeout = setTimeout(resize, 250);
    }
});

resize();
animate();
