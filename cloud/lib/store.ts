// Single storage module for households. Wraps @upstash/redis so the backing
// store can be swapped later without touching route handlers.
//
// All access goes through getHousehold(id) / saveHousehold(h).
//
// When KV_REST_API_URL / KV_REST_API_TOKEN are not set (local dev, tests), this
// falls back to a process-local in-memory map so the app runs without external
// infrastructure. The in-memory store is NOT persistent and NOT shared across
// serverless instances — production must always provide the KV env vars.

import { Redis } from "@upstash/redis";
import type { Household } from "./types";

// Hang the dev map off globalThis so it is a single instance across Next's
// per-route bundles (routes are compiled separately and would otherwise each
// get their own module-level Map).
const globalForStore = globalThis as unknown as {
  __householdMemory?: Map<string, Household>;
};
const memory =
  globalForStore.__householdMemory ??
  (globalForStore.__householdMemory = new Map<string, Household>());

let _redis: Redis | null = null;
let warnedMemory = false;

// Returns the Upstash client, or null if env vars are absent (dev fallback).
// Lazily constructed so importing this module never requires env to be present.
function redis(): Redis | null {
  const url = process.env.KV_REST_API_URL;
  const token = process.env.KV_REST_API_TOKEN;
  if (!url || !token) return null;
  if (!_redis) _redis = new Redis({ url, token });
  return _redis;
}

function warnMemory(): void {
  if (warnedMemory) return;
  warnedMemory = true;
  console.warn(
    "[store] KV_REST_API_URL / KV_REST_API_TOKEN not set — using in-memory " +
      "store (dev only, not persistent).",
  );
}

const householdKey = (id: string) => `household:${id}`;

export async function getHousehold(id: string): Promise<Household | null> {
  const r = redis();
  if (!r) {
    warnMemory();
    const h = memory.get(householdKey(id));
    return h ? structuredClone(h) : null;
  }
  // @upstash/redis auto-deserializes JSON values written with set().
  const data = await r.get<Household>(householdKey(id));
  return data ?? null;
}

export async function saveHousehold(h: Household): Promise<void> {
  const r = redis();
  if (!r) {
    warnMemory();
    // Clone so callers can't mutate the stored copy by reference.
    memory.set(householdKey(h.id), structuredClone(h));
    return;
  }
  await r.set(householdKey(h.id), h);
}
