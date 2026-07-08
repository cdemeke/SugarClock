import { NextResponse } from "next/server";
import { loadHousehold, pushLog } from "@/lib/household";
import { saveHousehold } from "@/lib/store";
import { checkPin } from "@/lib/auth";
import { clampRemaining } from "@/lib/types";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// POST /api/h/{id}/adjust — PIN required.
// Body: { child_id, delta } where delta ∈ {-1, +1}. Clamps remaining, logs, and
// returns the updated child.
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

  let body: { child_id?: string; delta?: number };
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

  const delta = body.delta;
  if (delta !== 1 && delta !== -1) {
    return NextResponse.json(
      { error: "delta must be +1 or -1" },
      { status: 400 },
    );
  }

  const child = h.children.find((c) => c.id === body.child_id);
  if (!child) {
    return NextResponse.json({ error: "Unknown child" }, { status: 404 });
  }

  const before = child.remaining;
  const after = clampRemaining(before + delta);
  const applied = after - before;

  const ts = new Date().toISOString();
  child.remaining = after;
  child.updated_at = ts;

  if (applied !== 0) {
    pushLog(h, {
      ts,
      child_id: child.id,
      action: delta < 0 ? "deduct" : "add",
      delta: applied,
      note: "",
    });
  }

  await saveHousehold(h);
  return NextResponse.json({ child });
}
