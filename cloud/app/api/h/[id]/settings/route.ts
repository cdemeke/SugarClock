import { NextResponse } from "next/server";
import {
  isValidTimezone,
  loadHousehold,
  nextChildId,
} from "@/lib/household";
import { saveHousehold } from "@/lib/store";
import { checkPin } from "@/lib/auth";
import {
  clampAllocation,
  clampRemaining,
  MAX_CHILDREN,
  normalizeName,
} from "@/lib/types";
import type { Child } from "@/lib/types";

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
  const h = await loadHousehold(id);
  if (!h) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }
  if (!checkPin(req, h)) {
    return NextResponse.json({ error: "Invalid PIN" }, { status: 401 });
  }

  let body: SettingsBody;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

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
    h.timezone = body.timezone;
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
        current.daily_allocation = allocation;
        // Keep remaining within the (possibly reduced) allocation and range.
        current.remaining = clampRemaining(
          Math.min(current.remaining, allocation),
        );
        current.updated_at = ts;
        newList.push(current);
      } else {
        newList.push({
          id: nextChildId([...h.children, ...newList]),
          name,
          daily_allocation: allocation,
          remaining: allocation,
          updated_at: ts,
        });
      }
    }

    h.children = newList;
  }

  await saveHousehold(h);
  const { pin_hash: _pin_hash, ...safe } = h;
  return NextResponse.json(safe);
}
