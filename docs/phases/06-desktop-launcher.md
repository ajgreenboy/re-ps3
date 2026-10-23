# Phase 6 handoff — desktop launcher

Status: future phase. Product direction: native XMB usable as an HTPC launcher.

## Entry checkpoint

Phase 4 supplies reliable navigation and visual fidelity. Phase 5 supplies a
documented set of working services/features and persistent state. Local firmware
and external tools remain prerequisites until packaging work defines their handling.

## Work next

1. Define the smallest useful desktop experience: launch, controller connection,
   category navigation, the selected local features, and clean exit/restart.
2. Separate persistent user state from replaceable build products.
3. Add prerequisite checks and actionable startup errors based on actual failures.
4. Define display/fullscreen behavior and controller reconnection using the user's
   host setup; preserve a working keyboard fallback.
5. Package source/tooling and document locally supplied firmware/dependencies.

## Exit criteria

- A user can follow the quick start from a documented machine setup.
- Supported actions work without developer intervention or hidden success stubs.
- Settings and identity survive updates; exit and restart do not corrupt state.
- Troubleshooting identifies the relevant session log and reproduction steps.
- Known unsupported actions are documented accurately.

## Final handoff

Provide the build/version identity, prerequisites, launch command, state/backup
locations, supported-feature matrix, current screenshots and known issues. Include
the rollback path and instructions for returning to the diagnostic workflow.
Do not treat PSN or online-service support as proven by a local menu boot.
