// Cloud data model for SugarClock Time Blocks.
// A single JSON document per household is stored under key `household:{id}`.

export type LogAction = "deduct" | "add" | "reset" | "create";

export interface Child {
  id: string; // short stable id, e.g. "c1"
  name: string; // UPPERCASE, ASCII, max 6 chars (fits LED matrix)
  daily_allocation: number; // clamped [1, 9]
  remaining: number; // clamped [0, 9]
  color?: string; // optional hex, e.g. "#34A853"
  updated_at: string; // ISO timestamp
}

export interface LogEntry {
  ts: string; // ISO timestamp
  child_id: string;
  action: LogAction;
  delta: number;
  note: string;
}

export interface Household {
  id: string; // e.g. "hh_8f3kq9x2mzp4w7v1n5t0"
  name: string;
  pin_hash: string; // bcrypt hash of the parent PIN
  timezone: string; // IANA tz, used for lazy daily reset
  last_reset_date: string; // YYYY-MM-DD in the household timezone
  created_at: string; // ISO timestamp
  children: Child[];
  log: LogEntry[]; // append-only, capped to most recent 50 entries
}

// ---- Constraints (enforced in code, not just types) ----
export const MAX_CHILDREN = 6;
export const MAX_LOG_ENTRIES = 50;
export const REMAINING_MIN = 0;
export const REMAINING_MAX = 9;
export const ALLOCATION_MIN = 1;
export const ALLOCATION_MAX = 9;
export const NAME_MAX_CHARS = 6;

export function clampRemaining(n: number): number {
  return Math.max(REMAINING_MIN, Math.min(REMAINING_MAX, Math.round(n)));
}

export function clampAllocation(n: number): number {
  return Math.max(ALLOCATION_MIN, Math.min(ALLOCATION_MAX, Math.round(n)));
}

// Store names uppercase, ASCII-only, capped to NAME_MAX_CHARS.
export function normalizeName(raw: string): string {
  return raw
    .toUpperCase()
    .replace(/[^\x20-\x7E]/g, "") // ASCII printable only
    .replace(/[^A-Z0-9]/g, "") // letters/digits for a clean LED render
    .slice(0, NAME_MAX_CHARS);
}
