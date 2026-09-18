import "./companions.js";

// companions.js is an exact copy of the device dashboard's data/www/companions.js.
// It exposes the same frame/palette API used by the production preview and its
// firmware parity tests. Keep the source copy unchanged when syncing updates.
const companions = globalThis.PixelCompanions;
const kinds = ["goldfish", "ghost", "axolotl", "dinosaur", "turtle", "octopus", "red-panda"];

/** Render the production in-range pet animation as 256 CSS colors/null.
 * Numeric high/low takeover remains the responsibility of renderFrame.
 * The production pet keeps its OK label during both naps and greetings.
 */
export function renderCompanionFrame(state, now = Date.now()) {
  const pet = state.pet || {};
  const id = Math.max(0, kinds.indexOf(pet.kind));
  const happy = Number.isFinite(Number(pet.greetingUntil)) && now < Number(pet.greetingUntil);
  return companions.frame(id, now, Boolean(pet.nap), happy, 0, 0)
    .flat()
    .map(key => key === "." ? null : companions.resolveColor(key));
}
