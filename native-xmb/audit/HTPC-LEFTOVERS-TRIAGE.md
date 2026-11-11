# HTPC leftovers triage: P6 editor field, P6 stale browser profile, D3 Start-at-Sign-in

Read-only inspection of `native-xmb/src/`. Every claim cites `file:line`. Labels: TRACED (read from the
tree), measured (reported timing, not re-measured here), INFERRED (reasoning/standard platform behaviour,
not read from code). No code was changed and nothing was built.

---

## Item 1 — P6 web-app window style: the editor field already exists (doc drift only)

**Root cause.** The brief says "there is no editor field for a web app's window style". That premise is
stale: the field landed after `audit/WEB-APPS.md` was written. The registry, launch and editor layers all
carry `window` today, and the editor dialog has a `Launch window` combo. The only "leftover" is a
now-wrong sentence in `WEB-APPS.md`.

**Where web apps are defined/edited (TRACED).**

- Data model: `ContentApp::window` in `content_registry.h:19`; parsed/validated in
  `content_registry.cpp:221-228` (validated `:100-102`, `:225-226`), serialised in
  `content_registry.cpp:369` (`serializeApp`).
- Launch semantics: `web_launch.h:6-13` and `web_launch.cpp:7-15` (`normalizeWindow`/`isValidWindow`),
  argument assembly `web_launch.cpp:31-46`; consumed by `Shell::launchWebApp` `shell.cpp:616-631`
  (reads `app->window` at `:621`).
- Editor (the "in-XMB app editor"): `AppEditor` QDialog in `app_editor.cpp`, opened from the XMB through
  `Action::ImportApps` (`shell.cpp:2296` → `requestImportApps` → `view.cpp:166`).

**The field is present (TRACED).**

- `app_editor.h:52` declares `QComboBox* windowCombo_ = nullptr; // web apps only: app / kiosk / browser (P6)`.
- `app_editor.cpp:139-144` creates it, labelled `Launch window`, items `App window`/`Kiosk window`/
  `Default browser` (`app`/`kiosk`/`browser`).
- `app_editor.cpp:267` loads it in `populateEditor` (empty value → `"app"`).
- `app_editor.cpp:287-288` enables it only when `kind == "web"` (`updateFieldState`).
- `app_editor.cpp:313-316` writes it in `currentApp` (maps `"app"` back to the empty default).
- Introduced by commit `db5362b` "Web apps: editor field for the launch window style (P6)"; `app_editor.*`
  is unmodified in the working tree, so it is on `main`.

**Proposed minimal fix.** None in code — the feature is done. Update the stale doc line
`WEB-APPS.md:19` ("The in-XMB app editor has no field for it; an edit preserves the value already in
apps.json") to state the field exists and describe it. Doc-only.

**Write-fence owner.** `app_editor.*` (Shell owner, per workstream C) — already shipped, no source change
pending. `WEB-APPS.md` is the only file to touch.

---

## Item 2 — P6 stale browser profile: fixed shared profile + Chromium single-instance forward → instant false return

**Root cause.** `Shell::launchWebApp` (`shell.cpp:616-631`) always launches Chromium with one fixed
`--user-data-dir=<data>/browser-profile` (`shell.cpp:625`), which `webLaunch::arguments` puts first on the
command line (`web_launch.cpp:33`). Chromium enforces one instance per user-data-dir: if a Chromium started
by an earlier XMB run is still alive — it outlives a killed parent, because `startTracked` starts it as a
plain child `QProcess` (`shell.cpp:557`, `:599`) and Windows does not kill child processes when the parent
dies — the new launch forwards its URL to that already-running instance and the new process exits
immediately with code 0. The `finished` handler (`shell.cpp:575-598`) treats any exit 0 as a normal return
and logs `returned from … exit 0 normal` (`shell.cpp:579`), so the XMB clears `game_` and returns to the row
while the orphaned browser window stays open and untracked. (Symptom documented in `WEB-APPS.md:23`; the
forward + instant-exit behaviour is INFERRED from standard Chromium single-instance semantics, matching that
documented symptom.) The guard at `shell.cpp:627` (`gameRunning()`, `shell.cpp:632-635`) only sees the
current tracked process, which is null after a crash, so it never catches the orphan.

**Exact fix location (TRACED).** Primary: `Shell::launchWebApp` `shell.cpp:616-631` — the fixed profile at
`:625` and the unconditional `startTracked(...)` at `:629`. Secondary (mitigation only): the `finished`
handler in `Shell::startTracked` `shell.cpp:575-598`, and the argument builder `web_launch.cpp:31-46`.

**Proposed minimal fix.** Before launching, detect that `browser-profile` is already held by a live Chromium
and do not start a tracked process that will instantly exit — either reuse/bring the existing window forward
or close the leftover instance, then launch cleanly. The robust form: record the launched Chromium pid when
the web app starts and, on the next launch, if that pid is still alive (it owns the profile), close it
(`WM_CLOSE`/terminate, reusing the existing `TitleSession` quit-tree logic) before starting a new tracked
process. A lighter mitigation is to detect the instant exit in `startTracked` (process finished within ~1 s
with code 0) and treat it as "forwarded to an existing instance" rather than a normal return, so the XMB
stops mis-reporting — but that leaves the orphan window untracked, so the close-the-leftover approach is the
real fix.

**Write-fence owner.** `shell.cpp`/`shell.h` (Shell owner), plus a helper in `web_launch.*` if the lock/pid
probe is added there. No `view.cpp`, `autostart.*`, `setup.*` or `main.cpp` involvement.

---

## Item 3 — D3 "Start at Sign-in" runs PowerShell on the GUI thread (~1 s hitch)

**Root cause.** Toggling the row runs the whole settings write chain synchronously on the GUI thread, and
the host choice for `host/startAtSignIn` blocks in a PowerShell run. Chain (TRACED):

- Row: `settings_menu.cpp:430` registers `"start_at_signin"` → `Action::SettingChoices`, key
  `host/startAtSignIn`.
- `shell.cpp:2085` `openChoices(...)` → choice → `beginSettingWrite` `shell.cpp:4436-4476`; `write.host`
  is set for `host/*` keys (`shell.cpp:4441`) and `write.slow=false` (`shell.cpp:4457`).
- `startSettingsWritePage` `shell.cpp:4506-4523` runs `runSettingWrite()` directly (`shell.cpp:4522`; the
  `slow` QTimer path at `:4519` is not taken here).
- `runSettingWrite` `shell.cpp:4524` → `host::setHostChoice` `shell.cpp:4539`.
- `host.cpp:1144-1145` `setHostChoice("host/startAtSignIn", index)` → `autostart::setEnabled(index==1, &error)`.
- `autostart.cpp:94-119` `setEnabled` runs `run(powershell(), args, &output, 60000)` (`autostart.cpp:108`);
  `run()` blocks in `waitForFinished(60000)` (`autostart.cpp:42`) and then `setEnabled` does a second
  blocking `readTask()` schtasks run (`autostart.cpp:111`, `run(...,8000)` at `autostart.cpp:50`).

All of `runSettingWrite` and `host::setHostChoice` execute on the GUI thread (Shell is a GUI-thread QObject;
the `slow` variant is dispatched by `QTimer::singleShot`, `shell.cpp:4519`, which fires on the GUI thread).
The hitch (reported ~1 s) is PowerShell start-up + script + the schtasks read-back, all on that thread. The
read-back at `shell.cpp:4540-4561` (`host::hostChoiceIndex` `shell.cpp:4555` → `autostart::enabled()`
`host.cpp:1111`, reading the `gState` set at `autostart.cpp:112`) depends on the write having finished
synchronously. A second GUI-thread call site is the first-run wizard: `setup_shell.cpp:328-333`
(`auto:toggle` → `autostart::setEnabled`, same block).

**Exact fix location (TRACED).** Blocking primitive: `autostart.cpp:94-119` (`setEnabled`), specifically the
synchronous `run(...)` at `:108` and `readTask()` at `:111`. GUI-thread call sites: `host.cpp:1145` and
`setup_shell.cpp:331`. Read-back that must move with it: `shell.cpp:4540-4561` (`:4555`).

**Proposed minimal fix.** Move the PowerShell run + scheduler read-back off the GUI thread. Either
(a) add an async `autostart::setEnabled` that does the script run and `readTask()` on a worker (reusing the
existing `prefetch()` worker pattern `autostart.cpp:82-88`), updates `gState`, and reports the result through
a callback/signal; or (b) wrap the blocking call at each call site in `QtConcurrent::run` + `QFutureWatcher`
(the pattern already used for service downloads at `setup_shell.cpp:257-267`). Both require relocating the
`runSettingWrite` read-back (`shell.cpp:4540-4561`) into the completion handler, since `host::setHostChoice`
can no longer return the final state synchronously.

**Write-fence owner.** `autostart.*` (autostart owner), plus the two call sites `setup_shell.cpp`
(`setup.*`) and `host.cpp`. Note: `host.cpp` is not in the brief's label list — it is the System/input
owner (workstream B) and must sign off the `setHostChoice`/read-back change, which also touches
`shell.cpp:4539`/`:4540-4561` (Shell owner). `main.cpp` is not involved.

---

status: triage only, no code changed, not built
