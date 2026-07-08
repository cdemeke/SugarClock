// Domain logic for households: id/date helpers, lazy daily reset, validation,
// and construction from create-form input. Kept free of Next/request concerns
// so it stays unit-testable.

import { randomBytes } from "node:crypto";
import bcrypt from "bcryptjs";
import { getHousehold, saveHousehold } from "./store";
import {
  ALLOCATION_MAX,
  ALLOCATION_MIN,
  clampAllocation,
  clampRemaining,
  MAX_CHILDREN,
  MAX_LOG_ENTRIES,
  normalizeName,
} from "./types";
import type { Household, LogEntry } from "./types";

// ---- ids ----

export function newHouseholdId(): string {
  // base64url of 16 random bytes -> 22 chars; with the "hh_" prefix this is
  // 25 chars, comfortably past the 24-char capability-URL minimum.
  return "hh_" + randomBytes(16).toString("base64url");
}

export function nextChildId(children: { id: string }[]): string {
  // Assign the next c{n} id that is not already in use so removals never cause
  // a collision with a previously-used id preserved in the log.
  let max = 0;
  for (const c of children) {
    const m = /^c(\d+)$/.exec(c.id);
    if (m) max = Math.max(max, parseInt(m[1], 10));
  }
  return `c${max + 1}`;
}

// ---- timezone-aware date ----

// Returns YYYY-MM-DD for the given IANA timezone. en-CA formats as ISO date.
export function dateInTz(timezone: string, now: Date = new Date()): string {
  try {
    return new Intl.DateTimeFormat("en-CA", {
      timeZone: timezone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
    }).format(now);
  } catch {
    return new Intl.DateTimeFormat("en-CA", {
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
    }).format(now);
  }
}

export function isValidTimezone(tz: string): boolean {
  try {
    new Intl.DateTimeFormat("en-CA", { timeZone: tz });
    return true;
  } catch {
    return false;
  }
}

// ---- lazy daily reset ----

// If the current date in the household timezone differs from last_reset_date,
// reset every child's remaining to its allocation. Returns true if it mutated.
export function applyLazyReset(h: Household, now: Date = new Date()): boolean {
  const today = dateInTz(h.timezone, now);
  if (h.last_reset_date === today) return false;
  const ts = now.toISOString();
  for (const c of h.children) {
    c.remaining = clampRemaining(c.daily_allocation);
    c.updated_at = ts;
  }
  h.last_reset_date = today;
  return true;
}

// Load a household, applying (and persisting) the lazy reset before returning.
// This is the entry point every route handler should use for reads/writes.
export async function loadHousehold(id: string): Promise<Household | null> {
  const h = await getHousehold(id);
  if (!h) return null;
  if (applyLazyReset(h)) await saveHousehold(h);
  return h;
}

// ---- log ----

export function pushLog(h: Household, entry: LogEntry): void {
  h.log.push(entry);
  if (h.log.length > MAX_LOG_ENTRIES) {
    h.log = h.log.slice(h.log.length - MAX_LOG_ENTRIES);
  }
}

// ---- PIN ----

export function hashPin(pin: string): string {
  return bcrypt.hashSync(pin, 10);
}

export function verifyPin(pin: string, hash: string): boolean {
  try {
    return bcrypt.compareSync(pin, hash);
  } catch {
    return false;
  }
}

// ---- creation ----

export interface CreateChildInput {
  name?: string;
  daily_allocation?: number;
}

export interface CreateHouseholdInput {
  name?: string;
  timezone?: string;
  pin?: string;
  children?: CreateChildInput[];
}

export class ValidationError extends Error {}

// Build a fully-formed Household from create-form input, or throw
// ValidationError with a human-readable message.
export function buildHousehold(input: CreateHouseholdInput): Household {
  const name = (input.name ?? "").trim();
  if (!name) throw new ValidationError("Family name is required");
  if (name.length > 40) throw new ValidationError("Family name is too long");

  const timezone = (input.timezone ?? "").trim();
  if (!timezone || !isValidTimezone(timezone)) {
    throw new ValidationError("A valid timezone is required");
  }

  const pin = (input.pin ?? "").trim();
  if (!/^\d{4,6}$/.test(pin)) {
    throw new ValidationError("PIN must be 4-6 digits");
  }

  const childInputs = input.children ?? [];
  if (childInputs.length < 1) {
    throw new ValidationError("At least one child is required");
  }
  if (childInputs.length > MAX_CHILDREN) {
    throw new ValidationError(`At most ${MAX_CHILDREN} children are allowed`);
  }

  const now = new Date();
  const ts = now.toISOString();

  const children = childInputs.map((ci, i) => {
    const cname = normalizeName(ci.name ?? "");
    if (!cname) {
      throw new ValidationError(
        `Child ${i + 1} needs a name (letters/digits only)`,
      );
    }
    const allocation = clampAllocation(
      Number.isFinite(ci.daily_allocation)
        ? (ci.daily_allocation as number)
        : ALLOCATION_MIN,
    );
    return {
      id: `c${i + 1}`,
      name: cname,
      daily_allocation: allocation,
      remaining: allocation,
      updated_at: ts,
    };
  });

  return {
    id: newHouseholdId(),
    name,
    pin_hash: hashPin(pin),
    timezone,
    last_reset_date: dateInTz(timezone, now),
    created_at: ts,
    children,
    log: [],
  };
}

export { ALLOCATION_MIN, ALLOCATION_MAX };
