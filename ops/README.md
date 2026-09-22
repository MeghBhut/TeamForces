# ops/ — background launcher

Runs TeamForces and Label Studio as **hidden background processes**, then locks the
screen. Locking does not stop them: Windows keeps background processes running
across a lock, so you stay reachable over Tailscale while the laptop sits locked.

## Use

| Action | Do this |
| --- | --- |
| Start both + lock the screen | Double-click **`Start and Lock.vbs`** |
| Start both, stay logged in | `powershell -ExecutionPolicy Bypass -File ops\launch.ps1 -NoLock` |
| See what's running and where | `powershell -ExecutionPolicy Bypass -File ops\status.ps1` |
| Stop both | Double-click **`Stop Services.vbs`** |

Nothing appears on screen during a normal start — no console, no browser tab.

## What it starts

| Service | Port | Command |
| --- | --- | --- |
| TeamForces | 8090 | `teamforces.exe --port 8090` (cwd = repo root) |
| Label Studio | 8080 | `D:\ls_env\Scripts\label-studio.exe start --no-browser -p 8080` |

Label Studio runs from the **`D:\ls_env` virtualenv**. The globally installed
`label-studio.exe` under `AppData\...\Python311` is broken — setuptools 82 removed
`pkg_resources`, which Label Studio 1.11 still imports. Don't point the launcher at it.

## Behaviour worth knowing

- **Idempotent.** If a port is already listening, that service is left alone rather
  than started twice. Safe to double-click when things are already up.
- **It waits before locking.** Each service must actually accept connections
  (TeamForces 20s, Label Studio 120s — Django is slow cold) before the screen locks.
  If either fails, you get a dialog and the screen is **left unlocked**, so a broken
  service never hides behind a lock screen.
- **Stop is guarded.** A PID is only killed if its command line still matches the
  service. PIDs get recycled, and blanket-killing `python.exe` would take out
  unrelated work. If the PID file is stale, it falls back to whoever owns the port —
  so services you started by hand (`start.bat`, a terminal) can still be stopped.
- Logs: `ops/logs/<Service>.out.log` and `.err.log`, overwritten each start.
  PIDs: `ops/run/<Service>.pid`.

## Limits

- **Lock only, not logout.** These run inside your login session, so they survive
  a lock but die on sign-out, restart, or shutdown. If you want them up before you
  even log in, they need to be Scheduled Tasks set to "run whether user is logged
  on or not" — a different setup.
- **Ports are open to the whole local network.** Both services bind `0.0.0.0`, so
  anyone on the same Wi-Fi can reach them, not just your tailnet. Locking the laptop
  does nothing about this. A Windows Firewall rule scoping 8080/8090 to the Tailscale
  interface + `127.0.0.1` would close it.
