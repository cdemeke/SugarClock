import { NextResponse } from "next/server";
import { mutateHousehold, pushLog } from "@/lib/household";
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

  // Apply the change as an atomic read-modify-write so concurrent adjustments
  // from two parents both land.
  const result = await mutateHousehold(id, (fresh) => {
    const child = fresh.children.find((c) => c.id === body.child_id);
    if (!child) return { value: null, save: false };

    const before = child.remaining;
    const after = clampRemaining(before + delta);
    const applied = after - before;

    const ts = new Date().toISOString();
    child.remaining = after;
    child.updated_at = ts;

    // Append a log entry unconditionally (§4.3). When clamping makes the change
    // a no-op, applied is 0 and is recorded as such.
    pushLog(fresh, {
      ts,
      child_id: child.id,
      action: delta < 0 ? "deduct" : "add",
      delta: applied,
      note: "",
    });
    return { value: child, save: true };
  });
  if (!result) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }
  if (!result.value) {
    return NextResponse.json({ error: "Unknown child" }, { status: 404 });
  }
  return NextResponse.json({ child: result.value });
}
