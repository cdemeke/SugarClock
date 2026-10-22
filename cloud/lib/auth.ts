import { mutateHousehold, verifyPin } from "./household";

export const PIN_HEADER = "x-parent-pin";

// After this many consecutive wrong PINs the household is locked out. Each
// successive lockout doubles in length up to the cap, so enumerating the PIN
// space through the (publicly known) capability URL is impractical.
export const MAX_PIN_FAILURES = 5;
export const PIN_LOCKOUT_BASE_MS = 15 * 60 * 1000;
export const PIN_LOCKOUT_MAX_MS = 24 * 60 * 60 * 1000;

export type PinCheck = "ok" | "invalid" | "locked" | "not_found";

// Verifies the request's parent PIN for household `id`, enforcing the
// failed-attempt limit. Failure counters live on the household and are updated
// with the same compare-and-set as every other write, so concurrent guesses
// are all counted.
export async function checkPin(
  req: Request,
  id: string,
  now: Date = new Date(),
): Promise<PinCheck> {
  const pin = req.headers.get(PIN_HEADER) ?? "";
  const result = await mutateHousehold<PinCheck>(id, (h) => {
    if (h.pin_locked_until && Date.parse(h.pin_locked_until) > now.getTime()) {
      return { value: "locked", save: false };
    }
    if (pin && verifyPin(pin, h.pin_hash)) {
      if (!h.pin_failures && !h.pin_lockouts) return { value: "ok", save: false };
      h.pin_failures = 0;
      h.pin_lockouts = 0;
      delete h.pin_locked_until;
      return { value: "ok", save: true };
    }
    h.pin_failures = (h.pin_failures ?? 0) + 1;
    if (h.pin_failures >= MAX_PIN_FAILURES) {
      h.pin_lockouts = (h.pin_lockouts ?? 0) + 1;
      const ms = Math.min(
        PIN_LOCKOUT_BASE_MS * 2 ** (h.pin_lockouts - 1),
        PIN_LOCKOUT_MAX_MS,
      );
      h.pin_locked_until = new Date(now.getTime() + ms).toISOString();
      h.pin_failures = 0;
    }
    return { value: "invalid", save: true };
  });
  return result ? result.value : "not_found";
}
