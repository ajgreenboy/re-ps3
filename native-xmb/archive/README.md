# Archive: dated handoffs and phase plans

This directory holds the **superseded, dated** documents. They are kept because the audits and
commit messages cite them, and because they record *why* decisions were made; they are not the
current plan. Nothing here should be edited to reflect new work - add a new dated document to the
parent directory instead.

Current documents (in the parent directory):

| Document | What it is |
|---|---|
| `../HANDOFF-2026-11-09.md` | the active handoff - state, build/run/verify, gates, work in flight, open work |
| `../audit/PHASES-2026-11-07.md` | the plan of record - done work, ranked backlog, parallel workstreams |
| `../HANDOFF-PHASES.md` | the 2026-10-25/27 phase record and RPCS3 capture how-to (the forward plan has moved on) |
| `../ISA.md` | the completion criteria |
| `../README.md` | build, run, architecture |
| `../INSTALL.md` | install and packaging |
| `../MANAGER.md` | the XMB Manager companion tool |
| `../WORKSPACES.md` | write fences and agent ownership |

Archived here:

| Document | Date | Kind | Superseded by |
|---|---|---|---|
| `HANDOFF-2026-10-24.md` | 2026-10-24 | handoff | the first application slice; replaced by every later handoff |
| `AUDIT-HANDOFF-2026-10-24.md` | 2026-10-24 | handoff | the functional-audit inventory; replaced by `../audit/FEATURE-STATUS-2026-11-04.md` |
| `MEDIA-CONTINUATION-2026-10-25.md` | 2026-10-25 | pass record | `../audit/SUBTITLES.md`, `../audit/MEDIA-CONTROLS.md`, `../audit/MEDIA-PANELS-FIRMWARE.md` |
| `HANDOFF-2026-10-26.md` | 2026-10-26 | handoff | `HANDOFF-2026-10-27.md` |
| `PHASE-CONTINUATION-2026-10-26.md` | 2026-10-26 | pass record | `PHASES-2026-10-28.md` ledger, `../audit/SESSION-POWER.md` |
| `PARITY-PHASES.md` | 2026-10-26 | phase plan | `PHASES-2026-10-28.md` |
| `HANDOFF-2026-10-27.md` | 2026-10-27 | handoff | `HANDOFF-2026-10-28.md` |
| `PHASES-2026-10-27.md` | 2026-10-27 | phase plan | `PHASES-2026-10-28.md` |
| `HANDOFF-2026-10-28.md` | 2026-10-28 | handoff | `HANDOFF-2026-11-08.md` |
| `PHASES-2026-10-28.md` | 2026-10-28 | phase plan | `../HANDOFF-PHASES.md` (ledger), `../audit/PHASES-2026-11-07.md` |
| `HANDOFF-CODEX-2026-11-06.md` | 2026-11-06 | handoff | `HANDOFF-2026-11-08.md` |
| `HANDOFF-2026-11-08.md` | 2026-11-08 | handoff | `../HANDOFF-2026-11-09.md` |

Links **between** the archived documents resolve inside this directory; links from outside use
`archive/<name>.md`. Plain-text mentions inside these files of a document that has since moved (for
example `HANDOFF.md`, `audit/HANDOFF.md`) name where it lived on the day it was written.
