import { renderFrame, describeFrame } from "./pixel-renderer.js";

/** Reusable 32 × 8 display. Set .state; listen for bubbling `clock-button` events. */
export class VirtualSugarClock extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" }).innerHTML = `<style>
      :host{display:block}.device{position:relative;border-radius:23px;background:linear-gradient(155deg,#3c4140,#171c1c 45%,#272c2c);padding:27px 27px 30px;border:1px solid #555b56;box-shadow:0 25px 34px -15px #18272065,inset 0 2px 2px #ffffff25,inset 0 -4px 3px #0008}.screen{background:#090d0c;border-radius:10px;padding:13px 17px;box-shadow:inset 0 2px 9px #000,0 1px 1px #ffffff1a;overflow:hidden}canvas{display:block;width:100%;height:auto;aspect-ratio:4;filter:brightness(.95)}.buttons{position:absolute;top:-10px;left:38%;right:38%;display:flex;gap:8px}.buttons button{width:33.33%;height:12px;border-radius:5px 5px 0 0;border:1px solid #6a7169;background:linear-gradient(#6d746c,#333a34);cursor:pointer;touch-action:none}.buttons button:active{transform:translateY(2px)}.buttons button:focus-visible{outline:3px solid #79aa87;outline-offset:3px}.signature{position:absolute;bottom:10px;left:0;right:0;text-align:center;font:6px system-ui;letter-spacing:3px;color:#68736b}.foot{position:absolute;bottom:-6px;background:#242b28;width:12%;height:7px;border-radius:0 0 5px 5px}.foot.left{left:14%}.foot.right{right:14%}@media(max-width:600px){.device{padding:17px 15px 22px;border-radius:16px}.screen{padding:7px 9px}.signature{bottom:7px;font-size:5px}}
      :host([pixels-only]) .device,:host([pixels-only]) .screen{padding:0;border:0;border-radius:0;box-shadow:none;background:#090d0c}
      :host([pixels-only]) .buttons,:host([pixels-only]) .signature,:host([pixels-only]) .foot{display:none}
      :host([pixels-only]) canvas{filter:none;opacity:1!important;image-rendering:pixelated}
      </style><div class="device"><div class="buttons"><button aria-label="Left physical button" title="Left: next · hold: glucose" data-button="left"></button><button aria-label="Middle physical button" title="Middle: brightness · double tap: IP · hold: snooze" data-button="middle"></button><button aria-label="Right physical button" title="Right: interact / start or pause · hold: reset" data-button="right"></button></div><div class="screen"><canvas width="640" height="160" role="img" aria-label="Virtual SugarClock pixel display"></canvas></div><div class="signature">S U G A R C L O C K</div><i class="foot left"></i><i class="foot right"></i></div>`;
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
    ctx.fillStyle = "#090d0c";
    ctx.fillRect(0, 0, 640, 160);
    ctx.save();
    ctx.globalAlpha = 0.35 + 0.65 * progress;
    ctx.translate(0, (1 - progress) * 12);
    for (let i = 0; i < 256; i++) {
      const color = frame[i];
      const pixelsOnly = this.hasAttribute("pixels-only");
      ctx.fillStyle = color || "#151b18";
      ctx.shadowBlur = color && !pixelsOnly ? 6 : 0;
      ctx.shadowColor = color || "transparent";
      ctx.beginPath();
      const inset = pixelsOnly ? 1 : 3;
      ctx.roundRect(
        (i % 32) * 20 + inset,
        Math.floor(i / 32) * 20 + inset,
        20 - inset * 2,
        20 - inset * 2,
        pixelsOnly ? 0 : 2,
      );
      ctx.fill();
    }
    ctx.restore();
    const description = describeFrame(this._state);
    if (this.canvas.getAttribute("aria-label") !== description)
      this.canvas.setAttribute("aria-label", description);
    this.canvas.style.opacity = String(
      Math.max(
        0.2,
        Math.min(1, Math.sqrt((this._state.brightness ?? 100) / 200)),
      ),
    );
  }
}
customElements.define("virtual-sugar-clock", VirtualSugarClock);
