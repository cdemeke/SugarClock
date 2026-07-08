import { verifyPin } from "./household";
import type { Household } from "./types";

export const PIN_HEADER = "x-parent-pin";

// Returns true if the request carries a valid parent PIN for this household.
export function checkPin(req: Request, h: Household): boolean {
  const pin = req.headers.get(PIN_HEADER) ?? "";
  if (!pin) return false;
  return verifyPin(pin, h.pin_hash);
}
