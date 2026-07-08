// Single storage module for households. Wraps @upstash/redis so the backing
// store can be swapped later without touching route handlers.
//
// All access goes through getHousehold(id) / saveHousehold(h).

import { Redis } from "@upstash/redis";
import type { Household } from "./types";

let _redis: Redis | null = null;

// Lazily construct the client so importing this module (e.g. during
// `next build`, or in unit tests) does not require env vars to be present.
function redis(): Redis {
  if (_redis) return _redis;
  const url = process.env.KV_REST_API_URL;
  const token = process.env.KV_REST_API_TOKEN;
  if (!url || !token) {
    throw new Error(
      "Missing KV_REST_API_URL / KV_REST_API_TOKEN environment variables",
    );
  }
  _redis = new Redis({ url, token });
  return _redis;
}

const householdKey = (id: string) => `household:${id}`;

export async function getHousehold(id: string): Promise<Household | null> {
  // @upstash/redis auto-deserializes JSON values written with set().
  const data = await redis().get<Household>(householdKey(id));
  return data ?? null;
}

export async function saveHousehold(h: Household): Promise<void> {
  await redis().set(householdKey(h.id), h);
}
