import { NextResponse } from "next/server";
import { saveHousehold } from "@/lib/store";
import { buildHousehold, ValidationError } from "@/lib/household";

// Node runtime required for node:crypto and bcryptjs.
export const runtime = "nodejs";

// POST /api/household — create a household. No auth.
// Body: { name, timezone, pin, children: [{ name, daily_allocation }] }
// Returns: { id }
export async function POST(req: Request) {
  let body: unknown;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

  try {
    const household = buildHousehold(body as Record<string, unknown>);
    await saveHousehold(household);
    return NextResponse.json({ id: household.id }, { status: 201 });
  } catch (err) {
    if (err instanceof ValidationError) {
      return NextResponse.json({ error: err.message }, { status: 400 });
    }
    console.error("Failed to create household", err);
    return NextResponse.json(
      { error: "Failed to create household" },
      { status: 500 },
    );
  }
}
