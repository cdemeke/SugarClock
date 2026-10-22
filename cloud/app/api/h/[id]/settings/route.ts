import { NextResponse } from "next/server";
import {
  dateInTz,
  isValidTimezone,
  mutateHousehold,
  nextChildId,
} from "@/lib/household";
import { checkPin } from "@/lib/auth";
import {
  clampAllocation,
  clampRemaining,
  MAX_CHILDREN,
  normalizeName,
} from "@/lib/types";
import type { Child, Household } from "@/lib/types";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

interface ChildInput {
  id?: string;
  name?: string;
  daily_allocation?: number;
}

interface SettingsBody {
  name?: string;
  timezone?: string;
  children?: ChildInput[];
}

// POST /api/h/{id}/settings — PIN required.
// Updates household name/timezone and/or the full children list. New children
// (no id) are assigned the next c{n} id; removed children just drop out of the
// list — their log entries are preserved. Returns the updated household.
export async function POST(
  req: Request,
  ctx: { params: Promise<{ id: string }> },
) {
  const { id } = await ctx.params;
  const auth = await checkPin(req, id);
  if (auth === "not_found") {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }
  if (auth === "locked") {
    return NextResponse.json(
      { error: "Too many wrong PINs — try again later" },
      { status: 429 },
    );
  }
  if (auth === "invalid") {
    return NextResponse.json({ error: "Invalid PIN" }, { status: 401 });
  }

  let body: SettingsBody;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

  // Atomic read-modify-write so a concurrent change isn't overwritten.
  const result = await mutateHousehold(id, (fresh) => {
    const error = applySettings(fresh, body);
    return { value: error, save: error === null };
  });
  if (!result) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }
  if (result.value) return result.value;
  const { pin_hash: _pin_hash, ...safe } = result.household;
  return NextResponse.json(safe);
}

// Applies validated settings to h in place. Returns an error response, or null
// on success.
function applySettings(h: Household, body: SettingsBody): NextResponse | null {
  if (typeof body.name === "string") {
    const name = body.name.trim();
    if (!name || name.length > 40) {
      return NextResponse.json({ error: "Invalid family name" }, { status: 400 });
    }
    h.name = name;
  }

  if (typeof body.timezone === "string") {
    if (!isValidTimezone(body.timezone)) {
      return NextResponse.json({ error: "Invalid timezone" }, { status: 400 });
    }
    if (body.timezone !== h.timezone) {
      // The lazy reset already ran in the old timezone; mark today (in the new
      // one) as reset so the next load doesn't refill the current balances.
      h.timezone = body.timezone;
      h.last_reset_date = dateInTz(h.timezone);
    }
  }

  if (Array.isArray(body.children)) {
    if (body.children.length < 1 || body.children.length > MAX_CHILDREN) {
      return NextResponse.json(
        { error: `Must have 1-${MAX_CHILDREN} children` },
        { status: 400 },
      );
    }

    const existing = new Map(h.children.map((c) => [c.id, c]));
    const ts = new Date().toISOString();
    const newList: Child[] = [];

    for (const ci of body.children) {
      const name = normalizeName(ci.name ?? "");
      if (!name) {
        return NextResponse.json(
          { error: "Each child needs a name (letters/digits only)" },
          { status: 400 },
        );
      }
      const allocation = clampAllocation(
        Number.isFinite(ci.daily_allocation)
          ? (ci.daily_allocation as number)
          : 1,
      );

      const current = ci.id ? existing.get(ci.id) : undefined;
      if (current) {
        current.name = name;
        // Only clamp remaining when the allocation is reduced, so bonus
        // credits above an unchanged allocation survive unrelated edits.
        if (allocation < current.daily_allocation) {
          current.remaining = Math.min(current.remaining, allocation);
        }
        current.remaining = clampRemaining(current.remaining);
        current.daily_allocation = allocation;
        current.updated_at = ts;
        newList.push(current);
      } else {
        // Log ids are included so households created before next_child_seq
        // existed also avoid reusing a removed child's id.
        const cid = nextChildId(
          [...h.children, ...newList, ...h.log.map((e) => ({ id: e.child_id }))],
          h.next_child_seq,
        );
        h.next_child_seq = parseInt(cid.slice(1), 10) + 1;
        newList.push({
          id: cid,
          name,
          daily_allocation: allocation,
          remaining: allocation,
          updated_at: ts,
        });
      }
    }

    h.children = newList;
  }

  return null;
}
