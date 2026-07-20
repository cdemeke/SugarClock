import { NextResponse } from "next/server";
import { loadHousehold } from "@/lib/household";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/h/{id} — full household JSON minus pin_hash. Used by the dashboard.
// Capability-URL auth: anyone with the id can read. Runs the lazy reset first.
export async function GET(
  _req: Request,
  ctx: { params: Promise<{ id: string }> },
) {
  const { id } = await ctx.params;
  const noStore = { "Cache-Control": "no-store" };

  const h = await loadHousehold(id);
  if (!h) {
    return NextResponse.json(
      { error: "Not found" },
      { status: 404, headers: noStore },
    );
  }

  const { pin_hash: _pin_hash, ...safe } = h;
  return NextResponse.json(safe, { headers: noStore });
}
