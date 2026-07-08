"use client";

import { useEffect, useState } from "react";
import { useParams } from "next/navigation";

// Minimal dashboard for A2: confirms creation and surfaces the two URLs to
// save. The full parent dashboard (cards, adjust/reset/settings) lands in A4.
export default function Dashboard() {
  const params = useParams<{ id: string }>();
  const id = params.id;
  const [origin, setOrigin] = useState("");

  useEffect(() => {
    setOrigin(window.location.origin);
  }, []);

  const dashboardUrl = origin ? `${origin}/h/${id}` : "";
  const deviceUrl = origin ? `${origin}/api/h/${id}/blocks` : "";

  return (
    <main className="mx-auto flex min-h-screen max-w-md flex-col gap-6 px-5 py-10">
      <h1 className="text-2xl font-bold">Household created 🎉</h1>
      <p className="text-sm text-slate-400">
        Save both links below. Anyone with a link can use it, so keep them
        private.
      </p>

      <UrlCard
        title="Dashboard URL"
        hint="Open this on your phone to manage blocks."
        url={dashboardUrl}
      />
      <UrlCard
        title="Device URL"
        hint="Paste this into each SugarClock's Time Blocks setting."
        url={deviceUrl}
      />
    </main>
  );
}

function UrlCard({
  title,
  hint,
  url,
}: {
  title: string;
  hint: string;
  url: string;
}) {
  const [copied, setCopied] = useState(false);
  async function copy() {
    try {
      await navigator.clipboard.writeText(url);
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      /* clipboard unavailable */
    }
  }
  return (
    <div className="flex flex-col gap-2 rounded-xl bg-slate-800/60 p-4 ring-1 ring-slate-700">
      <div className="flex items-center justify-between">
        <span className="text-sm font-semibold">{title}</span>
        <button
          onClick={copy}
          className="rounded-md bg-slate-700 px-2 py-1 text-xs hover:bg-slate-600"
        >
          {copied ? "Copied" : "Copy"}
        </button>
      </div>
      <code className="break-all text-xs text-sky-300">{url || "…"}</code>
      <span className="text-xs text-slate-500">{hint}</span>
    </div>
  );
}
