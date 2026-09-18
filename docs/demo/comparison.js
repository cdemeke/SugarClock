export const COMPARISON_OPTIONS = Object.freeze({
  pet: [
    ["goldfish", "Pip · Goldfish"],
    ["ghost", "Boo · Ghost"],
    ["axolotl", "Mochi · Axolotl"],
    ["dinosaur", "Sprout · Dinosaur"],
    ["turtle", "Pebble · Turtle"],
    ["octopus", "Inky · Octopus"],
    ["red-panda", "Maple · Red panda"],
  ],
  weather: [
    ["sunny", "Clear sky"],
    ["partly-cloudy", "Partly cloudy"],
    ["cloudy", "Cloudy"],
    ["drizzle", "Drizzle"],
    ["rain", "Rain / mist / fog"],
    ["sleet", "Sleet"],
    ["snow", "Snow"],
    ["storm", "Thunderstorm"],
    ["tornado", "Tornado"],
    ["cloud-fallback", "Smoke / dust"],
  ],
});
/** All previews share controls and timing, varying only the option being compared. */
export function comparisonState(state, key) {
  if (state.mode === "pet")
    return { ...state, pet: { ...state.pet, kind: key } };
  if (state.mode === "weather")
    return { ...state, weather: { ...state.weather, condition: key } };
  return state;
}
