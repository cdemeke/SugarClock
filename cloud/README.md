# SugarClock Cloud — Time Blocks

Cloud service and parent web UI for SugarClock **Time Blocks**: daily screen-time
credits that parents allocate per child and adjust from their phone. SugarClock
devices poll a read-only endpoint and display the current counts.

This is a self-contained Next.js (App Router, TypeScript, Tailwind) app. It shares
no code with the ESP32 firmware in the rest of the repository.

## Architecture

- **Storage:** one JSON document per household in Upstash Redis (Vercel KV), keyed
  `household:{id}`. All access goes through [`lib/store.ts`](lib/store.ts)
  (`getHousehold` / `saveHousehold`) so the backing store can be swapped later.
- **Auth: capability URL.** A household is identified by a long crypto-random id
  embedded in the URL. Anyone with the URL can *read*. *Write* operations
  additionally require the parent PIN, sent in the `X-Parent-Pin` header and
  verified against a bcrypt hash. No accounts, no login.
  **The URL is a secret — treat it like a password.**
- **Lazy daily reset, no cron.** Each household stores `last_reset_date`
  (YYYY-MM-DD in its timezone). On every read/write, if the current date in that
  timezone differs, all children reset to their `daily_allocation` before the
  request is served.
- **The clock is read-only.** Firmware never writes; all mutations come from the
  parent web UI.

## Environment variables

| Var | Purpose |
|---|---|
| `KV_REST_API_URL` | Upstash Redis REST URL |
| `KV_REST_API_TOKEN` | Upstash Redis REST token |

On Vercel, add the Upstash Redis (KV) integration and these are injected
automatically. Locally, copy `.env.example` to `.env.local` and fill them in.

## Local development

```bash
cd cloud
npm install
cp .env.example .env.local   # fill in KV_REST_API_URL / KV_REST_API_TOKEN
npm run dev                  # http://localhost:3000
npm test                     # domain-logic unit tests (no env required)
```

If the KV env vars are **not** set, the app falls back to a process-local
in-memory store (see [`lib/store.ts`](lib/store.ts)) so you can try the flows
without Upstash. That store is not persistent and not shared across instances —
production must always set the KV env vars.

## Deploying to Vercel

This app lives in the `cloud/` subdirectory of the repository. In the Vercel
project settings set **Root Directory** to `cloud`. Add the Upstash Redis (KV)
integration, then deploy. No other configuration is required.

## Notes

- PIN hashing uses `bcryptjs` (pure-JS) rather than the native `bcrypt` binding,
  to keep the serverless build free of native compilation. The hash format is
  compatible bcrypt.
