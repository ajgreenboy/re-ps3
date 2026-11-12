# Windows automatic sign-in status (ZF3), 2026-11-11

Part of [PHASES-ZERO-FRICTION-2026-11-11.md](PHASES-ZERO-FRICTION-2026-11-11.md). A controller-only HTPC only boots
straight into the XMB when Windows signs the account in by itself. The package never sets that up (it never asks for,
reads or stores a password, see `packaging/README.md` "Windows automatic sign-in"), so this adds a **read-only status**
and the manual steps, in Settings and in first-run setup. INFERRED: the firmware has no such row.

## What is read

Exactly two values, as `REG_SZ`, from `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`
(64-bit view, `RegGetValueW`, read access only):

| Value | Used as |
|---|---|
| `AutoAdminLogon` | the string `1` means on; anything else, or absent, means off |
| `DefaultUserName` | the account Windows signs in; compared with the current Windows user (`GetUserNameW`) |

## What it never does

- Never reads, writes or tests for `DefaultPassword`, and never reads any other Winlogon value.
- Never opens a key for writing, never changes `AutoAdminLogon`, `DefaultUserName` or any other system, security or
  registry setting, and never starts `netplwiz` or Autologon for the user.
- Never asks for a password and never stores or logs one. The boot log line carries the state and the account name only.
- Never runs a process or a shell string to find out; it is two registry reads and one user-name call.

## Mapping (pure, `autologon::classify`)

| AutoAdminLogon | DefaultUserName vs current user | State | Row value |
|---|---|---|---|
| not `1` / absent | any | Off | `Off` |
| `1` | same (case-insensitive, `DOMAIN\` prefix ignored) | OnThisUser | `On for <user>` |
| `1` | different | OnOtherUser | `On for a different user` |
| `1` | empty, or an e-mail address that does not match | OnUnverifiedUser | `On` / `On for <e-mail>` |
| key unreadable | | Unreadable | `Unknown` |

A Microsoft account is stored as its e-mail address while the profile name is shorter, so an e-mail name that does not
match is shown as stored and is **not** reported as a different user.

## Where it shows

- **Settings > System Settings > Windows automatic sign-in** (next to Start at Sign-in): value column as above;
  Cross opens an information page with the status, the netplwiz and Sysinternals Autologon steps (wording from
  `packaging/README.md`) and the plain statement that the XMB never asks for, reads or stores the password and never
  changes the setting. Code: `settings_menu.cpp` (row), `host.cpp` (`rowValue` / `rowDetail`).
- **First-run setup**, the step after Start at Sign-in (`setup_shell.cpp`, step `autologon`): the status, guidance for
  the state (Off says what to do), Continue and Skip This Step; Circle goes back. First-run still only appears on a
  plain first start or with `--first-run`, never in `--script` runs, so existing captures are unchanged.
- **Boot log**: one line, `autologon: on for <user>` / `autologon: off` / `autologon: on for a different user (<name>)`.

## Verification

- `tests/autologon_tests.cpp` (build with `-DXMB_AUTOLOGON_BUILD_TESTS=ON`, target `xmb-autologon-tests`): the mapping
  with no registry, and that the page states the guarantees and the log never mentions a password.
- Scripted runs (2026-11-11, scratch `--data-dir`, `XMB_SINGLE_INSTANCE=0`): the Settings row (System Settings, after Start at
  Sign-in) shows the value and its description; Cross opens the information page, Circle returns; the first-run step
  (`--first-run`) follows Start at Sign-in, shows the status and guidance, Cross opens a short message page, Circle goes
  back, and Skip This Step moves on to Services. `XMB_AUTOLOGON_FAKE="<AutoAdminLogon>|<DefaultUserName>|<current user>"`
  (or `unreadable`) replaces the reading, for checking the Off and different-user wording without touching the registry.
- The real registry read on a machine is a device check; turning auto-logon on or off stays the owner's manual step.
