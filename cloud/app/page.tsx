"use client";

import { useMemo, useState } from "react";
import { useRouter } from "next/navigation";

interface ChildRow {
  name: string;
  daily_allocation: number;
}

const MAX_CHILDREN = 6;

function useTimezones(): string[] {
  return useMemo(() => {
    // Full IANA list where supported, otherwise a small sensible fallback.
    const supported = (
      Intl as unknown as { supportedValuesOf?: (k: string) => string[] }
    ).supportedValuesOf;
    if (typeof supported === "function") {
      try {
        return supported("timeZone");
      } catch {
        /* fall through */
      }
    }
    return [
      "America/New_York",
      "America/Chicago",
      "America/Denver",
      "America/Los_Angeles",
      "Europe/London",
      "UTC",
    ];
  }, []);
}

function browserTz(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().timeZone || "UTC";
  } catch {
    return "UTC";
  }
}

export default function Home() {
  const router = useRouter();
  const timezones = useTimezones();

  const [name, setName] = useState("");
  const [timezone, setTimezone] = useState(browserTz());
  const [pin, setPin] = useState("");
  const [children, setChildren] = useState<ChildRow[]>([
    { name: "", daily_allocation: 3 },
  ]);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  function updateChild(i: number, patch: Partial<ChildRow>) {
    setChildren((cs) => cs.map((c, idx) => (idx === i ? { ...c, ...patch } : c)));
  }
  function addChild() {
    setChildren((cs) =>
      cs.length >= MAX_CHILDREN
        ? cs
        : [...cs, { name: "", daily_allocation: 3 }],
    );
  }
  function removeChild(i: number) {
    setChildren((cs) => (cs.length <= 1 ? cs : cs.filter((_, idx) => idx !== i)));
  }

  async function onSubmit(e: React.FormEvent) {
    e.preventDefault();
    setError(null);
    setSubmitting(true);
    try {
      const res = await fetch("/api/household", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, timezone, pin, children }),
      });
      const data = await res.json();
      if (!res.ok) {
        throw new Error(data?.error || "Failed to create household");
      }
      router.push(`/h/${data.id}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Something went wrong");
      setSubmitting(false);
    }
  }

  return (
    <main className="mx-auto flex min-h-screen max-w-md flex-col gap-6 px-5 py-10">
      <header className="flex flex-col gap-1">
        <h1 className="text-2xl font-bold">SugarClock Time Blocks</h1>
        <p className="text-sm text-slate-400">
          Create a household to manage daily screen-time blocks for your kids.
        </p>
      </header>

      <form onSubmit={onSubmit} className="flex flex-col gap-5">
        <label className="flex flex-col gap-1">
          <span className="text-sm font-medium">Family name</span>
          <input
            className="rounded-lg bg-slate-800 px-3 py-2 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="Demeke Family"
            maxLength={40}
            required
          />
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-sm font-medium">Timezone</span>
          <select
            className="rounded-lg bg-slate-800 px-3 py-2 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
            value={timezone}
            onChange={(e) => setTimezone(e.target.value)}
          >
            {timezones.map((tz) => (
              <option key={tz} value={tz}>
                {tz}
              </option>
            ))}
          </select>
          <span className="text-xs text-slate-500">
            Used to reset blocks at local midnight.
          </span>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-sm font-medium">Parent PIN</span>
          <input
            className="rounded-lg bg-slate-800 px-3 py-2 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
            value={pin}
            onChange={(e) => setPin(e.target.value.replace(/\D/g, ""))}
            placeholder="4-6 digits"
            inputMode="numeric"
            pattern="\d{4,6}"
            minLength={4}
            maxLength={6}
            required
          />
          <span className="text-xs text-slate-500">
            Required to change block counts. Keep it private.
          </span>
        </label>

        <div className="flex flex-col gap-3">
          <div className="flex items-center justify-between">
            <span className="text-sm font-medium">Children</span>
            <button
              type="button"
              onClick={addChild}
              disabled={children.length >= MAX_CHILDREN}
              className="rounded-md bg-slate-700 px-2 py-1 text-xs disabled:opacity-40"
            >
              + Add child
            </button>
          </div>

          {children.map((c, i) => (
            <div key={i} className="flex items-end gap-2">
              <label className="flex flex-1 flex-col gap-1">
                <span className="text-xs text-slate-400">Name</span>
                <input
                  className="rounded-lg bg-slate-800 px-3 py-2 uppercase outline-none ring-1 ring-slate-700 focus:ring-sky-500"
                  value={c.name}
                  onChange={(e) => updateChild(i, { name: e.target.value })}
                  placeholder="AVA"
                  maxLength={6}
                  required
                />
              </label>
              <label className="flex w-20 flex-col gap-1">
                <span className="text-xs text-slate-400">Blocks/day</span>
                <input
                  type="number"
                  className="rounded-lg bg-slate-800 px-3 py-2 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
                  value={c.daily_allocation}
                  onChange={(e) =>
                    updateChild(i, {
                      daily_allocation: parseInt(e.target.value || "1", 10),
                    })
                  }
                  min={1}
                  max={9}
                  required
                />
              </label>
              <button
                type="button"
                onClick={() => removeChild(i)}
                disabled={children.length <= 1}
                className="mb-2 rounded-md px-2 py-1 text-slate-500 hover:text-red-400 disabled:opacity-30"
                aria-label="Remove child"
              >
                ✕
              </button>
            </div>
          ))}
        </div>

        {error && (
          <p className="rounded-lg bg-red-950 px-3 py-2 text-sm text-red-300">
            {error}
          </p>
        )}

        <button
          type="submit"
          disabled={submitting}
          className="rounded-lg bg-sky-600 px-4 py-3 font-semibold hover:bg-sky-500 disabled:opacity-50"
        >
          {submitting ? "Creating…" : "Create household"}
        </button>
      </form>
    </main>
  );
}
