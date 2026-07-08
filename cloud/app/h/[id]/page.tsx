"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { useParams } from "next/navigation";

interface Child {
  id: string;
  name: string;
  daily_allocation: number;
  remaining: number;
  color?: string;
  updated_at: string;
}
interface LogEntry {
  ts: string;
  child_id: string;
  action: string;
  delta: number;
  note: string;
}
interface Household {
  id: string;
  name: string;
  timezone: string;
  last_reset_date: string;
  created_at: string;
  children: Child[];
  log: LogEntry[];
}

export default function Dashboard() {
  const { id } = useParams<{ id: string }>();
  const pinKey = `sc_pin_${id}`;

  const [hh, setHh] = useState<Household | null>(null);
  const [loading, setLoading] = useState(true);
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  // PIN modal
  const [pinOpen, setPinOpen] = useState(false);
  const [pinInput, setPinInput] = useState("");
  const [pinError, setPinError] = useState<string | null>(null);
  const [pending, setPending] = useState<null | (() => Promise<void>)>(null);

  const load = useCallback(async () => {
    try {
      const res = await fetch(`/api/h/${id}`, { cache: "no-store" });
      if (!res.ok) throw new Error("Household not found");
      setHh(await res.json());
      setErr(null);
    } catch (e) {
      setErr(e instanceof Error ? e.message : "Failed to load");
    } finally {
      setLoading(false);
    }
  }, [id]);

  useEffect(() => {
    load();
  }, [load]);

  const write = useCallback(
    async (path: string, body: unknown) => {
      const pin =
        typeof window !== "undefined" ? sessionStorage.getItem(pinKey) : null;
      if (!pin) {
        setPinError(null);
        setPending(() => () => write(path, body));
        setPinOpen(true);
        return;
      }
      setBusy(true);
      try {
        const res = await fetch(path, {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-Parent-Pin": pin },
          body: JSON.stringify(body),
        });
        if (res.status === 401) {
          sessionStorage.removeItem(pinKey);
          setPinError("Wrong PIN — try again");
          setPending(() => () => write(path, body));
          setPinOpen(true);
          return;
        }
        if (!res.ok) {
          const d = await res.json().catch(() => ({}));
          throw new Error(d.error || "Request failed");
        }
        await load();
      } catch (e) {
        setErr(e instanceof Error ? e.message : "Request failed");
      } finally {
        setBusy(false);
      }
    },
    [pinKey, load],
  );

  async function submitPin() {
    if (!/^\d{4,6}$/.test(pinInput)) {
      setPinError("PIN is 4-6 digits");
      return;
    }
    sessionStorage.setItem(pinKey, pinInput);
    setPinOpen(false);
    setPinInput("");
    const act = pending;
    setPending(null);
    if (act) await act();
  }

  const adjust = (childId: string, delta: 1 | -1) =>
    write(`/api/h/${id}/adjust`, { child_id: childId, delta });
  const resetChild = (childId: string) =>
    write(`/api/h/${id}/reset`, { child_id: childId });

  if (loading) {
    return (
      <main className="mx-auto max-w-md px-5 py-10 text-slate-400">Loading…</main>
    );
  }
  if (err && !hh) {
    return (
      <main className="mx-auto max-w-md px-5 py-10 text-red-300">{err}</main>
    );
  }
  if (!hh) return null;

  return (
    <main className="mx-auto flex min-h-screen max-w-md flex-col gap-5 px-4 py-8">
      <header className="flex items-baseline justify-between">
        <h1 className="text-xl font-bold">{hh.name}</h1>
        <span className="text-xs text-slate-500">{hh.timezone}</span>
      </header>

      {err && (
        <p className="rounded-lg bg-red-950 px-3 py-2 text-sm text-red-300">
          {err}
        </p>
      )}

      <section className="flex flex-col gap-4">
        {hh.children.map((c) => (
          <ChildCard
            key={c.id}
            child={c}
            busy={busy}
            onMinus={() => adjust(c.id, -1)}
            onPlus={() => adjust(c.id, 1)}
            onReset={() => resetChild(c.id)}
          />
        ))}
      </section>

      <button
        onClick={() => write(`/api/h/${id}/reset`, { all: true })}
        disabled={busy}
        className="rounded-lg bg-slate-700 py-2 text-sm font-medium hover:bg-slate-600 disabled:opacity-50"
      >
        Reset everyone for the day
      </button>

      <Settings hh={hh} onSave={(body) => write(`/api/h/${id}/settings`, body)} busy={busy} />

      <LogView hh={hh} />

      <Links id={id} />

      {pinOpen && (
        <PinModal
          value={pinInput}
          error={pinError}
          onChange={setPinInput}
          onSubmit={submitPin}
          onCancel={() => {
            setPinOpen(false);
            setPinInput("");
            setPending(null);
          }}
        />
      )}
    </main>
  );
}

function ChildCard({
  child,
  busy,
  onMinus,
  onPlus,
  onReset,
}: {
  child: Child;
  busy: boolean;
  onMinus: () => void;
  onPlus: () => void;
  onReset: () => void;
}) {
  const out = child.remaining === 0;
  return (
    <div className="flex flex-col gap-3 rounded-2xl bg-slate-800/60 p-4 ring-1 ring-slate-700">
      <div className="flex items-center justify-between">
        <span className="text-lg font-semibold tracking-wide">{child.name}</span>
        <span
          className={`text-4xl font-bold tabular-nums ${
            out ? "text-red-400" : "text-emerald-400"
          }`}
        >
          {child.remaining}
        </span>
      </div>
      <Pips remaining={child.remaining} total={child.daily_allocation} />
      <div className="flex gap-2">
        <button
          onClick={onMinus}
          disabled={busy}
          className="flex-1 rounded-lg bg-slate-700 py-2 text-lg font-bold hover:bg-slate-600 disabled:opacity-50"
        >
          −1
        </button>
        <button
          onClick={onPlus}
          disabled={busy}
          className="flex-1 rounded-lg bg-slate-700 py-2 text-lg font-bold hover:bg-slate-600 disabled:opacity-50"
        >
          +1
        </button>
        <button
          onClick={onReset}
          disabled={busy}
          className="rounded-lg bg-slate-700 px-3 py-2 text-sm hover:bg-slate-600 disabled:opacity-50"
        >
          Reset
        </button>
      </div>
    </div>
  );
}

function Pips({ remaining, total }: { remaining: number; total: number }) {
  return (
    <div className="flex gap-1.5">
      {Array.from({ length: total }).map((_, i) => (
        <span
          key={i}
          className={`h-3 w-3 rounded-full ${
            i < remaining
              ? "bg-emerald-400"
              : "border border-slate-600 bg-transparent"
          }`}
        />
      ))}
    </div>
  );
}

interface SettingsChildRow {
  id?: string;
  name: string;
  daily_allocation: number;
}

function Settings({
  hh,
  onSave,
  busy,
}: {
  hh: Household;
  onSave: (body: unknown) => Promise<void>;
  busy: boolean;
}) {
  const [name, setName] = useState(hh.name);
  const [timezone, setTimezone] = useState(hh.timezone);
  const [rows, setRows] = useState<SettingsChildRow[]>([]);

  // Reseed the editor whenever the household changes (e.g. after a save).
  useEffect(() => {
    setName(hh.name);
    setTimezone(hh.timezone);
    setRows(
      hh.children.map((c) => ({
        id: c.id,
        name: c.name,
        daily_allocation: c.daily_allocation,
      })),
    );
  }, [hh]);

  const timezones = useMemo(() => {
    const supported = (
      Intl as unknown as { supportedValuesOf?: (k: string) => string[] }
    ).supportedValuesOf;
    if (typeof supported === "function") {
      try {
        return supported("timeZone");
      } catch {
        /* ignore */
      }
    }
    return [hh.timezone, "UTC"];
  }, [hh.timezone]);

  function updateRow(i: number, patch: Partial<SettingsChildRow>) {
    setRows((rs) => rs.map((r, idx) => (idx === i ? { ...r, ...patch } : r)));
  }
  function addRow() {
    setRows((rs) =>
      rs.length >= 6 ? rs : [...rs, { name: "", daily_allocation: 3 }],
    );
  }
  function removeRow(i: number) {
    setRows((rs) => (rs.length <= 1 ? rs : rs.filter((_, idx) => idx !== i)));
  }

  return (
    <details className="rounded-2xl bg-slate-800/40 ring-1 ring-slate-700">
      <summary className="cursor-pointer px-4 py-3 text-sm font-semibold">
        Settings
      </summary>
      <div className="flex flex-col gap-4 px-4 pb-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs text-slate-400">Family name</span>
          <input
            className="rounded-lg bg-slate-800 px-3 py-2 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
            value={name}
            maxLength={40}
            onChange={(e) => setName(e.target.value)}
          />
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs text-slate-400">Timezone</span>
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
        </label>

        <div className="flex flex-col gap-2">
          <div className="flex items-center justify-between">
            <span className="text-xs text-slate-400">Children</span>
            <button
              type="button"
              onClick={addRow}
              disabled={rows.length >= 6}
              className="rounded-md bg-slate-700 px-2 py-1 text-xs disabled:opacity-40"
            >
              + Add
            </button>
          </div>
          {rows.map((r, i) => (
            <div key={r.id ?? `new-${i}`} className="flex items-end gap-2">
              <label className="flex flex-1 flex-col gap-1">
                <span className="text-[10px] text-slate-500">Name</span>
                <input
                  className="rounded-lg bg-slate-800 px-2 py-1.5 uppercase outline-none ring-1 ring-slate-700 focus:ring-sky-500"
                  value={r.name}
                  maxLength={6}
                  onChange={(e) => updateRow(i, { name: e.target.value })}
                />
              </label>
              <label className="flex w-16 flex-col gap-1">
                <span className="text-[10px] text-slate-500">Blocks</span>
                <input
                  type="number"
                  min={1}
                  max={9}
                  className="rounded-lg bg-slate-800 px-2 py-1.5 outline-none ring-1 ring-slate-700 focus:ring-sky-500"
                  value={r.daily_allocation}
                  onChange={(e) =>
                    updateRow(i, {
                      daily_allocation: parseInt(e.target.value || "1", 10),
                    })
                  }
                />
              </label>
              <button
                type="button"
                onClick={() => removeRow(i)}
                disabled={rows.length <= 1}
                className="mb-1.5 px-1 text-slate-500 hover:text-red-400 disabled:opacity-30"
                aria-label="Remove child"
              >
                ✕
              </button>
            </div>
          ))}
        </div>

        <button
          onClick={() => onSave({ name, timezone, children: rows })}
          disabled={busy}
          className="rounded-lg bg-sky-600 py-2 text-sm font-semibold hover:bg-sky-500 disabled:opacity-50"
        >
          Save settings
        </button>
      </div>
    </details>
  );
}

function LogView({ hh }: { hh: Household }) {
  const nameById = useMemo(() => {
    const m = new Map<string, string>();
    for (const c of hh.children) m.set(c.id, c.name);
    return m;
  }, [hh.children]);

  const recent = [...hh.log].reverse().slice(0, 10);
  if (recent.length === 0) return null;

  return (
    <section className="flex flex-col gap-2">
      <h2 className="text-sm font-semibold text-slate-300">Recent activity</h2>
      <ul className="flex flex-col gap-1 text-xs text-slate-400">
        {recent.map((e, i) => (
          <li key={i} className="flex justify-between gap-2">
            <span>
              {nameById.get(e.child_id) ?? e.child_id} · {e.action}
              {e.delta ? ` (${e.delta > 0 ? "+" : ""}${e.delta})` : ""}
            </span>
            <span className="shrink-0 text-slate-600">
              {new Date(e.ts).toLocaleString()}
            </span>
          </li>
        ))}
      </ul>
    </section>
  );
}

function Links({ id }: { id: string }) {
  const [origin, setOrigin] = useState("");
  useEffect(() => setOrigin(window.location.origin), []);
  const deviceUrl = origin ? `${origin}/api/h/${id}/blocks` : "";
  return (
    <details className="rounded-2xl bg-slate-800/40 ring-1 ring-slate-700">
      <summary className="cursor-pointer px-4 py-3 text-sm font-semibold">
        Device link
      </summary>
      <div className="flex flex-col gap-2 px-4 pb-4">
        <p className="text-xs text-slate-500">
          Paste this URL into each SugarClock&apos;s Time Blocks setting. Keep it
          private — anyone with it can read your counts.
        </p>
        <code className="break-all rounded-lg bg-slate-900 p-2 text-xs text-sky-300">
          {deviceUrl || "…"}
        </code>
      </div>
    </details>
  );
}

function PinModal({
  value,
  error,
  onChange,
  onSubmit,
  onCancel,
}: {
  value: string;
  error: string | null;
  onChange: (v: string) => void;
  onSubmit: () => void;
  onCancel: () => void;
}) {
  return (
    <div className="fixed inset-0 z-10 flex items-center justify-center bg-black/60 px-6">
      <div className="flex w-full max-w-xs flex-col gap-3 rounded-2xl bg-slate-800 p-5 ring-1 ring-slate-700">
        <h3 className="text-sm font-semibold">Enter parent PIN</h3>
        <input
          autoFocus
          type="password"
          inputMode="numeric"
          className="rounded-lg bg-slate-900 px-3 py-2 text-center text-lg tracking-widest outline-none ring-1 ring-slate-700 focus:ring-sky-500"
          value={value}
          onChange={(e) => onChange(e.target.value.replace(/\D/g, ""))}
          onKeyDown={(e) => e.key === "Enter" && onSubmit()}
          maxLength={6}
        />
        {error && <p className="text-xs text-red-400">{error}</p>}
        <div className="flex gap-2">
          <button
            onClick={onCancel}
            className="flex-1 rounded-lg bg-slate-700 py-2 text-sm hover:bg-slate-600"
          >
            Cancel
          </button>
          <button
            onClick={onSubmit}
            className="flex-1 rounded-lg bg-sky-600 py-2 text-sm font-semibold hover:bg-sky-500"
          >
            Unlock
          </button>
        </div>
      </div>
    </div>
  );
}
