# TeamForces

A private **Codeforces group-contest tracker** for a small circle of friends,
built to run on your own machine over a [Tailscale](https://tailscale.com)
network. Spin up your own rounds from unsolved problems, watch everyone's
progress live, upsolve what you missed, and keep shared notes on your mistakes —
all with **no leaderboard**, because the point is to improve, not to rank.

- **C++17 backend** — [cpp-httplib](https://github.com/yhirose/cpp-httplib) +
  [nlohmann/json](https://github.com/nlohmann/json), both vendored as single headers.
- **Plain HTML/CSS/vanilla-JS frontend** — no framework, no build step, no npm.
- **JSON files on disk** — no database. Loaded on startup, written on change.
- **No OpenSSL** — outbound HTTPS to the Codeforces API is done by shelling out
  to the system `curl`. The local server speaks plain HTTP; Tailscale encrypts
  the link between your devices.

---

## Features

**Contests**
- Admin creates a round by listing ratings (e.g. `800, 900, 1000, 1000, 1200`)
  and a duration. The server picks one random problem per rating and
  **excludes anything any member has ever solved** (checked live via the CF API).
- Problems stay **hidden until the contest starts**, then are revealed to
  everyone at once.
- **Start now** or **schedule** a start time (the contest auto-opens then).
- **Pause / resume** (freezes the clock) and **finish early** — both keep solve
  times honest.
- Rounds are auto-numbered **C#01, C#02, …** and kept in a browsable archive.

**Live tracking**
- A background poller round-robins every member's `user.status`, respecting the
  CF rate limit, and records each solve.
- The standings show, per member per problem: **solve time**, **wrong attempts**,
  and an **↑ upsolved** marker — members always listed **alphabetically**, never
  ranked.
- Two time metrics per solve: **elapsed** (minutes into the contest) and
  **effort** (minutes actually spent on that problem — see [How it works](#how-it-works)).

**Upsolve & learning**
- Unsolved problems drop into each member's **private upsolve queue**; solve them
  later and the poller marks them done automatically.
- **Mistake logs** (shared): after a contest, write a note per problem with a tag
  (`misread`, `wrong-approach`, `edge-case`, `slow-start`, `other`). Browse them
  by member.
- **Bookmarks** (private): star problems to revisit.
- **Private stats**: per-rating-band attempted / solved / accuracy / average
  solve time, with simple vanilla-canvas charts.
- **Rules page**: edit `web/rules.html` and refresh — no rebuild.

**Interface**
- Custom **"Cyanotype"** theme with a **light/dark toggle** (persists, no flash).
- Sidebar dashboard navigation; contests open in a focused master-detail view.

---

## How it works

**No database.** Every collection is a JSON file under `data/`. On startup they're
loaded into memory; on change the affected file is rewritten (temp-file + rename,
so a crash mid-write can't corrupt it). One mutex guards all shared state, since
the HTTP handlers and the poller run on different threads.

**The poller** (`src/poller.cpp`) is split in two:
- `evaluate(...)` — a **pure function** that turns a member's submissions into
  per-problem results. Being pure makes it trivially testable (see
  `--poller-test`).
- A **background thread** that, while a contest is live, feeds each member's
  `user.status` into `evaluate`, finalises the contest when time runs out
  (filling upsolve queues), auto-starts scheduled contests, and re-checks upsolve
  queues every ~5 minutes.

**Effort time.** `solved_at` is elapsed time from the contest start, which is
misleading for a problem you reach late (it includes time spent on earlier ones).
So each result also carries `effort_min`, computed by **submission-gap
attribution**: the time gap before each submission is credited to that
submission's problem, summed up to the first accepted one.

**No OpenSSL.** cpp-httplib's HTTPS client needs a TLS library; rather than link
OpenSSL, `src/cf_api.cpp` shells out to the system `curl` (Schannel on Windows,
OpenSSL on Linux) for the two CF API calls it needs, and parses the JSON. The
server itself is plain HTTP.

---

## Project layout

```
src/          C++ server
                main.cpp      entry point, HTTP routes, CLI
                storage.*     JSON-file store + mutex
                auth.*        salted-SHA256 passwords, cookie sessions
                cf_api.*      rate-limited Codeforces client (via curl)
                poller.*      solve detection (pure evaluate + background thread)
                contest.*     contest lifecycle (create/start/pause/finish/…)
                stats.*       logs, bookmarks, private stats
                sha256.h      dependency-free SHA-256
web/          frontend: index.html, app.js, style.css, theme.css, rules.html
data/         JSON "database" (created at runtime — NOT in version control)
third_party/  vendored: httplib.h, json.hpp
ops/          optional: run as a hidden background service on Windows
build.ps1 / build.sh / Makefile / CMakeLists.txt   build entry points
```

---

## Build

**Windows** (MSYS2 / MinGW g++):

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

**Linux / Git-Bash:**

```bash
./build.sh        # or: make      # or: cmake -S . -B build && cmake --build build
```

Either way you get `teamforces` / `teamforces.exe`. The Windows build is
**statically linked**, so the `.exe` runs without the MSYS2 DLLs on your PATH.

Requirements: a C++17 compiler and `curl` available on the PATH.

---

## First-time setup

```powershell
# 1. Create your admin account (once):
.\teamforces.exe --add-user admin <username> <password> <your_cf_handle>

# 2. Cache the Codeforces problemset (used to pick contest problems):
.\teamforces.exe --refresh-cache

# 3. Start the server:
.\teamforces.exe                 # http://0.0.0.0:8080
.\teamforces.exe --port 8090     # or a custom port
```

Find your machine's tailnet address with `tailscale ip -4`, then open
`http://<that-ip>:<port>` from any device on your tailnet. Log in as your admin;
the **Admin** tab lets you register the rest of your group (create the account,
then send them their username & password).

> On first run, Windows Firewall may prompt — allow it on **Private networks** so
> other devices can connect.

---

## Running as a background service (optional, Windows)

`ops/` contains a launcher that runs TeamForces (and, in this setup, Label Studio)
as **hidden background processes** and optionally locks the screen — the machine
stays reachable over Tailscale while sitting locked. See
[`ops/README.md`](ops/README.md).

```
Start (and lock the screen)   →  double-click  ops\Start and Lock.vbs
Start, stay logged in         →  powershell -ExecutionPolicy Bypass -File ops\launch.ps1 -NoLock
Status                        →  powershell -ExecutionPolicy Bypass -File ops\status.ps1
Stop                          →  double-click  ops\Stop Services.vbs
```

---

## CLI reference

```
teamforces                         run the server on port 8080
teamforces --port N                run the server on port N
teamforces --add-user ROLE U P H   create a user (ROLE = admin | member)
teamforces --refresh-cache         fetch & cache the CF problemset
teamforces --poller-test HANDLE    test the CF client + poller against a real handle
teamforces --recompute             recompute results for past contests (run with the server stopped)
teamforces --help                  show this help
```

`--poller-test` prints a handle's recent submissions and a solved / elapsed /
effort / wrong-attempts / upsolved table — no server required:

```powershell
.\teamforces.exe --poller-test <your_cf_handle>
.\teamforces.exe --poller-test tourist 1781793000 120 2237A 2237F 2237G   # explicit window
```

`--recompute` re-derives every past contest's results (useful after changing how
results are computed). Stop the server first so two processes don't write
`contests.json` at once.

---

## API reference

All responses are JSON. The caller is always identified from the session cookie —
never from a parameter.

| Method | Path | Notes |
|--------|------|-------|
| POST | `/api/login` · `/api/logout` | sets / clears the session cookie |
| GET  | `/api/whoami` | resolve the caller from the cookie |
| GET  | `/api/state[?id=]` | current contest (or a specific one) + results + my upsolve |
| GET  | `/api/contests` | round archive (members see revealed rounds only) |
| GET  | `/api/upsolve` | caller's upsolve queue *(private)* |
| GET  | `/api/members` | member list (for the Logs grid) |
| POST | `/api/log` | write a mistake log *(shared)* |
| GET  | `/api/logs?problem=&user=` | logs, filterable *(shared)* |
| POST | `/api/log/skip` · `/api/log/unskip` | mark a problem "no log needed" *(private)* |
| GET  | `/api/pending_logs` | my un-logged, un-skipped problems *(private)* |
| POST/DELETE/GET | `/api/bookmark(s)` | bookmarks *(private)* |
| GET  | `/api/mystats` | per-rating stats *(private — no username param)* |
| POST | `/api/admin/register` | create a user *(admin)* |
| POST | `/api/admin/refresh_cache` | refresh the problemset cache *(admin)* |
| POST | `/api/admin/create_contest` | create a round; takes `ratings`, `duration_min`, optional `scheduled_at` *(admin)* |
| POST | `/api/admin/start` | reveal problems & start the clock *(admin)* |
| POST | `/api/admin/pause` · `/api/admin/resume` | freeze / unfreeze the clock *(admin)* |
| POST | `/api/admin/finish` | end a running contest early *(admin)* |
| POST | `/api/admin/delete_contest` | delete a contest *(admin)* |

---

## Privacy model

- **Shared** with the group: contest results and mistake logs.
- **Private** (derived only from the session cookie, never a username parameter):
  stats, bookmarks, upsolve queue, pending logs, log-skips.
- Problem **tags are never shown** anywhere in the UI.
- Members are always listed **alphabetically** — no ranks, no scores, no leaderboard.

## Data files (`data/`)

Created on demand, never committed: `users.json` (username, salted SHA-256 hash,
salt, CF handle, role), `sessions.json`, `contests.json`, `logs.json`,
`bookmarks.json`, `log_skips.json`, `problemset_cache.json`.

> ⚠️ **Do not commit `data/`.** It contains password hashes, salts, and session
> tokens. It's excluded via `.gitignore`; if it was ever committed, purge it from
> history and rotate all passwords.

---

## Notes & limitations

- Passwords are salted SHA-256 — fine for a private LAN tool, not hardened against
  a determined offline attacker. Use non-trivial passwords.
- There's no UI to change a user's CF handle yet — edit `data/users.json` and
  restart (handles are read at startup).
- Built for a handful of users on a trusted tailnet; it is not meant to face the
  public internet.
