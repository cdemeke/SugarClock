import { NextResponse } from "next/server";
import { loadHousehold } from "@/lib/household";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/h/{id}/blocks — the SugarClock device endpoint.
// Compact, fixed-shape payload (single-letter keys) so the ESP32 ArduinoJson
// buffer stays small. No log, no PIN hash. Runs the lazy daily reset first.
// The firmware parser depends on this exact shape — keep it stable.
export async function GET(
  _req: Request,
  ctx: { params: Promise<{ id: string }> },
) {
  const { id } = await ctx.params;
  const noStore = { "Cache-Control": "no-store" };

  const h = await loadHousehold(id);
  if (!h) {
    return NextResponse.json({ ok: false }, { status: 404, headers: noStore });
  }

  const kids = h.children.map((c) => ({
    n: c.name,
    r: c.remaining,
    a: c.daily_allocation,
  }));

  return NextResponse.json(
    { ok: true, date: h.last_reset_date, kids },
    { headers: noStore },
  );
}
