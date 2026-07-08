import { NextResponse } from "next/server";
import { loadHousehold, pushLog } from "@/lib/household";
import { saveHousehold } from "@/lib/store";
import { checkPin } from "@/lib/auth";
import { clampRemaining } from "@/lib/types";
import type { Child } from "@/lib/types";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// POST /api/h/{id}/reset — PIN required.
// Body: { child_id } to reset one child, or { all: true } to reset everyone.
// Sets remaining = daily_allocation and logs each reset.
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

  let body: { child_id?: string; all?: boolean };
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

  let targets: Child[];
  if (body.all === true) {
    targets = h.children;
  } else {
    const child = h.children.find((c) => c.id === body.child_id);
    if (!child) {
      return NextResponse.json({ error: "Unknown child" }, { status: 404 });
    }
    targets = [child];
  }

  const ts = new Date().toISOString();
  for (const child of targets) {
    const before = child.remaining;
    child.remaining = clampRemaining(child.daily_allocation);
    child.updated_at = ts;
    pushLog(h, {
      ts,
      child_id: child.id,
      action: "reset",
      delta: child.remaining - before,
      note: "",
    });
  }

  await saveHousehold(h);
  const { pin_hash: _pin_hash, ...safe } = h;
  return NextResponse.json(safe);
}
