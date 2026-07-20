import { test } from "node:test";
import assert from "node:assert/strict";
import {
  applyLazyReset,
  buildHousehold,
  dateInTz,
  nextChildId,
  ValidationError,
} from "../lib/household";
import { clampRemaining, clampAllocation } from "../lib/types";

test("dateInTz returns YYYY-MM-DD", () => {
  const s = dateInTz("America/New_York", new Date("2026-07-08T12:00:00Z"));
  assert.match(s, /^\d{4}-\d{2}-\d{2}$/);
});

test("clamping keeps values in range", () => {
  assert.equal(clampRemaining(-5), 0);
  assert.equal(clampRemaining(99), 9);
  assert.equal(clampRemaining(3), 3);
  assert.equal(clampAllocation(0), 1);
  assert.equal(clampAllocation(50), 9);
});

test("buildHousehold validates and normalizes", () => {
  const h = buildHousehold({
    name: "Demeke Family",
    timezone: "America/New_York",
    pin: "1234",
    children: [
      { name: "ava", daily_allocation: 3 },
      { name: "leonardo!!", daily_allocation: 50 },
    ],
  });
  assert.ok(h.id.startsWith("hh_"));
  assert.ok(h.id.length >= 24);
  assert.equal(h.children[0].name, "AVA"); // uppercased
  assert.equal(h.children[1].name, "LEONAR"); // 6-char cap, punctuation stripped
  assert.equal(h.children[1].daily_allocation, 9); // clamped
  assert.equal(h.children[0].remaining, 3); // remaining seeded to allocation
  assert.notEqual(h.pin_hash, "1234"); // hashed
});

test("buildHousehold rejects bad input", () => {
  assert.throws(
    () => buildHousehold({ name: "", timezone: "UTC", pin: "1234", children: [{ name: "A" }] }),
    ValidationError,
  );
  assert.throws(
    () => buildHousehold({ name: "Fam", timezone: "UTC", pin: "12", children: [{ name: "A" }] }),
    ValidationError,
  );
  assert.throws(
    () => buildHousehold({ name: "Fam", timezone: "UTC", pin: "1234", children: [] }),
    ValidationError,
  );
});

test("applyLazyReset resets when the date changed", () => {
  const h = buildHousehold({
    name: "Fam",
    timezone: "America/New_York",
    pin: "1234",
    children: [{ name: "AVA", daily_allocation: 3 }],
  });
  h.children[0].remaining = 0; // simulate a spent day
  h.last_reset_date = "2000-01-01"; // stale
  const changed = applyLazyReset(h);
  assert.equal(changed, true);
  assert.equal(h.children[0].remaining, 3);
  assert.notEqual(h.last_reset_date, "2000-01-01");
});

test("applyLazyReset is a no-op when the date matches", () => {
  const h = buildHousehold({
    name: "Fam",
    timezone: "America/New_York",
    pin: "1234",
    children: [{ name: "AVA", daily_allocation: 3 }],
  });
  h.children[0].remaining = 1;
  const changed = applyLazyReset(h); // last_reset_date is today
  assert.equal(changed, false);
  assert.equal(h.children[0].remaining, 1);
});

test("nextChildId avoids collisions after removals", () => {
  assert.equal(nextChildId([{ id: "c1" }, { id: "c3" }]), "c4");
  assert.equal(nextChildId([]), "c1");
});
