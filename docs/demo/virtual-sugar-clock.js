import { renderFrame, describeFrame } from "./pixel-renderer.js?v=neon-1";
import { DEVICE_GEOMETRY as geometry } from "./device-geometry.js";

/** Reusable 32 × 8 display. Set .state; listen for bubbling `clock-button` events. */
export class VirtualSugarClock extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" }).innerHTML = `<style>
      :host {display:block;container-type:inline-size}
      .device {position:relative;aspect-ratio:${geometry.widthMm}/${geometry.heightMm};border-radius:3.49% / 9.96%;background:linear-gradient(150deg,#f4f3ef 0%,#d6d7d4 40%,#b9bbb9 100%);box-shadow:0 1px 1px #fff9,inset 0 1px 2px #fff,0 12px 18px -12px #18202080}
      .face {position:absolute;inset:2% .7%;border-radius:2.8% / 8.3%;background:linear-gradient(145deg,#101214 0%,#060809 55%,#0c0e10 100%);box-shadow:inset 0 0 2px #000,0 0 1px #666;overflow:hidden}
      .screen {position:absolute;left:${(1 - geometry.matrixWidthFraction) * 50}%;top:50%;width:${geometry.matrixWidthFraction * 100}%;aspect-ratio:4;transform:translateY(-50%);background:#07090a}
      canvas {display:block;width:100%;height:auto;aspect-ratio:4}
      .buttons {position:absolute;top:-.9%;left:34%;width:29%;height:1.8%;display:flex;gap:1%;z-index:1}
      .buttons button {position:relative;width:33.33%;height:100%;padding:0;border:0;border-radius:2px 2px 0 0;background:linear-gradient(#e8e7e3,#c7c9c5);box-shadow:inset 0 1px 1px #fff9;cursor:pointer;touch-action:none}
      .buttons button::before {content:"";position:absolute;inset:-12px 0}
      .buttons button:active {transform:translateY(1px)}
      .buttons button:focus-visible {outline:2px solid #7199b1;outline-offset:3px}
      .sensor {position:absolute;top:-.25%;left:66%;width:.9%;height:.9%;border-radius:50%;background:#555956;box-shadow:0 0 0 1px #d1d2cf}
      :host([pixels-only]) .device {aspect-ratio:4;border-radius:0;background:#07090a;box-shadow:none}
      :host([pixels-only]) .face,:host([pixels-only]) .buttons,:host([pixels-only]) .sensor {display:none}
      :host([pixels-only]) .screen {left:0;top:0;width:100%;transform:none}
      </style><div class="device"><div class="buttons"><button aria-label="Left physical button" title="Left: next · hold: glucose" data-button="left"></button><button aria-label="Middle physical button" title="Middle: brightness · double tap: IP · hold: snooze" data-button="middle"></button><button aria-label="Right physical button" title="Right: interact / start or pause · hold: reset" data-button="right"></button></div><i class="sensor" aria-hidden="true"></i><div class="face" aria-hidden="true"></div><div class="screen"><canvas width="1280" height="320" role="img" aria-label="Virtual SugarClock pixel display"></canvas></div></div>`;
    this.canvas = this.shadowRoot.querySelector("canvas");
    this.ctx = this.canvas.getContext("2d");
    this.shadowRoot.querySelectorAll("button").forEach((button) => {
      let held = false,
        timer;
      const cancel = () => {
        clearTimeout(timer);
      };
      button.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        held = false;
        button.setPointerCapture(e.pointerId);
        timer = setTimeout(() => {
          held = true;
          clearTimeout(this.singleTap);
          this.singleTap = null;
          this.fire(button.dataset.button, "long");
        }, 1000);
      });
      button.addEventListener("pointerup", cancel);
      button.addEventListener("pointercancel", () => {
        cancel();
        held = true;
      });
      button.addEventListener("lostpointercapture", cancel);
      button.addEventListener("click", () => {
        if (held) {
          held = false;
          return;
        }
        const name = button.dataset.button;
        if (name === "middle") {
          if (this.singleTap) {
            clearTimeout(this.singleTap);
            this.singleTap = null;
            this.fire(name, "double");
          } else
            this.singleTap = setTimeout(() => {
              this.singleTap = null;
              this.fire(name, "short");
            }, 350);
        } else this.fire(name, "short");
      });
    });
  }
  fire(name, kind) {
    this.dispatchEvent(
      new CustomEvent("clock-button", {
        detail: { name, kind },
        bubbles: true,
        composed: true,
      }),
    );
  }
  disconnectedCallback() {
    clearTimeout(this.singleTap);
  }
  set state(value) {
    this._state = value;
    this.draw();
  }
  get state() {
    return this._state;
  }
  draw() {
    if (!this._state) return;
    const now = Date.now(),
      frame = renderFrame(this._state, now),
      ctx = this.ctx;
    const key =
      this._state.mode +
      ":" +
      (this._state.mode === "glucose" ? this._state.glucose.value : "");
    if (key !== this.lastKey) {
      this.transition = now;
      this.lastKey = key;
    }
    const progress = Math.min(1, (now - this.transition) / 230);
    const pitch = this.canvas.width / geometry.columns;
    const fill = pitch * geometry.pixelFillFraction;
    const inset = (pitch - fill) / 2;
    const brightness = Math.max(
      0.12,
      Math.min(1, Math.sqrt((this._state.brightness ?? 200) / 200)),
    );
    ctx.fillStyle = "#07090a";
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.save();
    // Fade intensity only: fractional-pixel vertical slides cannot occur on the matrix.
    ctx.globalAlpha = (0.35 + 0.65 * progress) * brightness;
    for (let i = 0; i < 256; i++) {
      const color = frame[i];
      const x = (i % 32) * pitch + inset;
      const y = Math.floor(i / 32) * pitch + inset;
      if (!color) {
        ctx.fillStyle = "#0b0e10";
        ctx.fillRect(x, y, fill, fill);
        continue;
      }
      // Preserve source hue and saturation: mixing every LED toward white washed out colors.
      const rgb = color.match(/\w\w/g).map((v) => parseInt(v, 16));
      const tint = (factor) =>
        `rgb(${rgb.map((c) => Math.round(c * factor)).join(",")})`;
      // Keep most of the square at full intensity; soften only the diffuser edge.
      const gradient = ctx.createRadialGradient(
        x + fill / 2,
        y + fill / 2,
        0,
        x + fill / 2,
        y + fill / 2,
        fill * 0.7,
      );
      gradient.addColorStop(0, tint(1));
      gradient.addColorStop(0.85, tint(1));
      gradient.addColorStop(0.95, tint(0.98));
      gradient.addColorStop(1, tint(0.94));
      ctx.fillStyle = gradient;
      ctx.beginPath();
      ctx.roundRect(x, y, fill, fill, pitch * 0.025);
      ctx.fill();
    }
    ctx.restore();
    const description = describeFrame(this._state);
    if (this.canvas.getAttribute("aria-label") !== description)
      this.canvas.setAttribute("aria-label", description);
  }
}
customElements.define("virtual-sugar-clock", VirtualSugarClock);
