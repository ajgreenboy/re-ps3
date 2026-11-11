# SETTINGS SUB-FLOWS + SAVE-DATA + COPY/MOVE/PROGRESS — implementation-ready spec

Breadth area: **B. "Whole surfaces missing"** (parity group). One of three sub-areas handed off from
`FIDELITY-VERDICT.md` §4 item 6 — the settings sub-flows (netconf / deviceconf / software update /
print / backup / set date-and-time), the save-data management surface, and the copy / move / progress
dialog family.

- Date: 2026-11-10. Read-only pass: this file is the only thing written. No source, no capture, no git
  command, no RPCS3 launch.
- Deliverable: this spec. It enumerates the firmware surfaces that are **absent** from the native build,
  gives each one's firmware provenance, the console's flow, the current native state, a verdict, and a
  ranked implementation plan mapped to its write-fence owner.

## 0. Conventions and evidence grades

Every claim is labelled **TRACED** (read from a firmware container or its XMBML definition), **measured**
(read from an RPCS3 1920x1080 frame already on disk), or **INFERRED** (a deduction with no local
artifact). RCO values are read from the container-derived tree dump `C:\dev\out\rco-dump\all.txt` (and
`sysconf.txt`), never retyped; page-object names below are the dump's exact identifiers, quoted short.
No number is fabricated and no online service is faked.

Firmware sources read this pass:

| Source | What it is |
|---|---|
| `vsh/resource/explore/xmb/category_sysconf.xml` | the Settings column + `seg_network`/`seg_device` segment views (TRACED) |
| `vsh/resource/explore/xmb/category_game.xml` | the Game column's save-data / game-data / VMC views (TRACED) |
| `vsh/resource/explore/xmb/savedata_list.xml`, `download_list.xml` | the savedata / download query views (TRACED) |
| `vsh/resource/*.rco` (netconf, deviceconf, software_update, print, ps3_savedata, vmc_savedata, gamedata, filecopy, data_copy, download, autoupdateconf, sysconf) | the plugin page-object inventories (TRACED via `all.txt`) |
| `C:\dev\out\rpcs3-*-2026-10-28\frames\`, `C:\dev\out\rpcs3-ref*\settings\` | console stills (measured; cited by the audits) |

Write-fence owners (the task's mapping):

| Owner | Files | Owns |
|---|---|---|
| **Shell** | `native-xmb/src/shell.cpp`, `shell.h` | the `Action` enum, the `Entry`/`Page`/`MenuOption` model, every row's route through `activate()`, the settings-write chain, the copy/delete orchestration over `file_ops`, all `showMessage`/`showConfirm`/`showErrorPage` call sites |
| **View** | `native-xmb/src/view.cpp`, `dialogs.cpp`, `view.h` | page/band/dialog/busy rendering only; `drawBusySurface`, `drawMessageBox`, the message classes |
| **settings_menu** | `native-xmb/src/settings_menu.cpp`, `settings_menu.h` | the 16 settings pages' row sets, the `firmwareRows()`/`segmentRows()` tables, the choice lists |
| **new files** | e.g. `savedata_shell.cpp`, `backup_shell.cpp` | a new column-row or sub-flow surface with its own rows and model (the established pattern: `bluetooth_shell.cpp`, `wifi_shell.cpp`, `updater_shell.cpp`, `setup_shell.cpp`) |
| **file_ops** | `native-xmb/src/file_ops.cpp`, `file_ops.h` | the copy/delete worker (already exists and is safe) |

Note on `setup.cpp` (named in the brief): `native-xmb/src/setup.cpp` is the **first-run firmware check
and config helper** (`checkFirmware`, `findFirmwareCandidates`, `configPath`, `defaultLibraryFolder`,
`browseRoots`, `buttonNames`, `axisText`) — not a settings surface. The first-run *wizard* lives in
`setup_shell.cpp`. Neither is in scope here; the settings-dialog rendering the brief asks about is in
`view.cpp` + `dialogs.cpp`.

---

## 1. The current native state in one place (what already exists)

The breadth is better than the 2026-10-29 surface map implies, because three passes landed after it:

- **Settings pages are the firmware's own row sets.** `settings_menu.cpp` resolves the segment views
  `seg_game`/`seg_device`/`seg_network` from `category_sysconf.xml` at run time
  (`segmentRows()`, `settings_menu.cpp:484-599`) and every one of the 16 pages lists the console's rows
  with `sysconf_plugin` labels; a row this build cannot act on is `Action::Information` with the RCO's
  own explanation (the "list-unavailable" disposition). 143 rows, 68 real / 75 placeholder / 0 absent
  (`SETTINGS-COVERAGE.md` §2.1).
- **A settings-write chain exists.** `Shell::beginSettingWrite` → Ask (the two-answer Box) → Proceed
  (the `page_busy` state) → Finish (the page message), with `sysconf_plugin` strings
  (`shell.cpp:4436-4565`). Restore Default Settings runs the console's `top|proceed|finish` chain.
- **A copy/delete backend exists and is wired.** `FileOperations` (`file_ops.cpp`) streams copies on a
  worker thread, deletes to the Recycle Bin with a safety firewall (library-root confinement, protected
  system folders, no-overwrite numbered copies). The options menu lists `Copy` / `Delete`
  (`OptionCommand::Copy/CopyTarget/Delete`, `shell.cpp:2895-2904`, `:3461-3510`) and runs them through
  `loading_` + the indeterminate busy page, then a result notification/page
  (`shell.cpp:1778-1790`).
- **The five message/busy surfaces exist.** `MessageKind::{Inline,Page,Box,ErrorCode,Info}` and
  `drawBusySurface` are built (`DIALOGS-FIRMWARE.md` §7). `busyProgress()` / `busySpinnerPhase()` are
  declared on `Shell` and drawn by `View`, but **no caller publishes them** — both stay `-1`
  (indeterminate); `FileOperations::progress` is never connected (grep of `src/` this pass).

What is therefore **absent** (the subject of this spec): every *plugin wizard* behind the settings rows
(the 43-page netconf, the 29-page deviceconf, the 29-page software-update, the 27-page print manager, the
sysconf backup/restore chain), every *save-data / game-data / VMC utility* page behind the Game column's
inert rows, and the *copy-busy-progress + "file exists"* dialog family (the backend runs, but the console's
progress/overwrite surface is not drawn).

---

## 2. Settings sub-flows

### 2.1 Network configuration — `netconf_plugin` (43 pages)

- **Firmware surface (TRACED).** The Settings column row `network` (`category_sysconf.xml` `seg_fixed_items`
  `Table key="network"`, `child=segment`, `ch_pos=1`, `fo_lbl2_alpha=0`) opens `seg_network`; its
  `Table key="net_internet_setting"` (`msg_internet_connect_setting`, `bar_action=none`) hands off to the
  netconf wizard. The plugin's page inventory is `netconf_plugin.rco` (239 objects), whose `Page` objects
  read, in order: `page_default`, `page_wizard_base`, `page_network_infomation_entry`,
  `page_network_infomation_list`, `page_network_setting_entry`, `page_network_connect_mode`,
  `page_network_address_mode`, `page_network_cable_disconnect`, `page_network_cable_connect_wait`,
  `page_network_setting_list`, `page_network_setting_complete`, `page_network_initial_setting_list`,
  `page_network_initial_setting_complete`, `page_network_setting_mode`, `page_network_connect_option`,
  `page_network_address_option`, `page_network_dhcp_option`, `page_network_dhcphost_setting`,
  `page_network_address_setting`, `page_network_account_setting`, `page_network_dns_option`,
  `page_network_dns_setting`, `page_network_mtu_option`, `page_network_mtu_setting`,
  `page_network_proxy_option`, `page_network_proxy_setting`, `page_network_upnp_option`,
  `page_network_connect_test`, then the wireless group (`page_network_wireless_option` … `page_dummy`).
  **43 pages**, matching `FIDELITY-SURFACES-MAP.md` 2.11 ("netconf_plugin (43 pages)").
- **Console flow (INFERRED from the page spine; the wizard is `uncaptured`).** A forward-only wizard:
  connection-method select → (wireless: AP scan/list → SSID → security → WPA/WEP key → cert/EAP) → address
  mode (DHCP/manual) → address → DNS → MTU → proxy → UPnP → connection test → `page_network_setting_complete`.
  `FIDELITY-SURFACES-MAP.md` 2.11 counts it as one surface ("netconf's 43 pages are *the Network setup
  wizard*").
- **Native now (TRACED from `settings_menu.cpp`).** The `net_internet_setting` row is
  `Action::Information` (a host-report page, `host::rowDetail`) — the segment bindings at
  `settings_menu.cpp:494-498`. The **status list** (`net_netlist` → `host::networkStatus`) and the
  **connection test** (`net_internet_test` → the `msg_wait` "Please wait..." page, `shell.cpp:2202`) are
  real. The wizard itself has no page.
- **Verdict: BLOCKED.** `SETTINGS-COVERAGE.md` §4 (and `FIDELITY-SURFACES-MAP.md` 2.11) record the policy
  refusal: "a wizard that writes the host's network configuration is forbidden". The read-only half
  (status list + connection test) already exists and is the honest form; the write half must not be built
  and must not be faked. Owner: none (keep the refusal); the row keeps `Action::Information`.

### 2.2 Device configuration — `deviceconf_plugin` (29 pages)

- **Firmware surface (TRACED).** The Settings column row `device` (`seg_device`, `child=segment`) opens
  `seg_device` → `seg_device_fixed_upper|middle|lower` + the conditional USB-headset and keyboard-input
  groups. The rows' *actions* (`pad_assign`, `pad_vibration`, `audio_setting`, `bt_device_utility`,
  `voice_changer`, `usb_headset`, `bdremote_registration`) are the entry points into
  `deviceconf_plugin.rco` (250 objects), whose `Page` objects read: `page_deviceconf_wizard_background`,
  `page_audio_device_config`, `page_calibrate_microphone_menu`, `_start`, `_adjusting`, `_complete`,
  `_initialize`, `page_voice_change_config`, `page_bd_remocon_setting_start`, `_registering`, `_end`,
  `page_bt_device_manager_device_check`, `_registry_list`, `_optionmenu`, `_connecting`, `_disconnecting`,
  `_deleteing`, `_info`, `_register_start`, `_register_searching`, `_register_select`,
  `_register_connecting`, `_register_passkey_hid`, `_register_passkey_audio`, `_register_registering`,
  `_register_end`, `page_controller_assign`, `page_controller_vibration`, `page_optionmenu`. **29 pages**,
  matching `FIDELITY-SURFACES-MAP.md` 2.11 ("deviceconf_plugin (29 pages)").
- **Console flow (INFERRED from the page spine; `uncaptured`).** Each accessory row opens a small
  sub-wizard: microphone calibrate (menu → start → adjusting → complete), BD-remote register
  (start → registering → end), BT device manager (list → optionmenu → register-search/select/passkey →
  connecting/registering), controller assign, controller vibration, voice changer. The keyboard/mouse
  group (`pd_keyrepeat_start/speed`, `pd_mouse_type`, `pd_pointer_speed`) is a plain choice list, not a
  wizard.
- **Native now (TRACED).** The Accessory page lists every firmware row (`segmentRows()`,
  `settings_menu.cpp:484-599`). Four rows are **real host writes** (`host/keyRepeatDelay`,
  `host/keyRepeatSpeed`, `host/mouseButtons`, `host/pointerSpeed`); `pd_bt_utility` is displaced by the
  native-only `Bluetooth Devices` row (`Action::BluetoothApp`, `bluetooth_shell.cpp`, D10); everything
  else is `Action::Information` (host report). The 29-page wizard has no page.
- **Verdict: NEEDS BACKEND (device).** The keyboard/mouse sub-flow is already buildable and real; the BT
  manager has a partial native substitute (`bluetooth_shell.cpp`); the microphone calibrate, controller
  reassign/vibration and BD-remote register flows need a device backend this build does not model (the
  console binds its pad to PSN through the PS3 controller stack, `settings_menu.cpp:591-592`). The honest
  split: extend the real host rows, keep the rest `Action::Information`, and do not fabricate a
  calibration/registration UI.

### 2.3 Software update — `software_update_plugin` (29 pages)

- **Firmware surface (TRACED).** The Settings column row `update` (`Table key="update"`, `tex_update`,
  `msg_update`, `item_type=systop`, `bar_action=none`) is a leaf `xmlsysconf` row whose page is the
  software-update plugin, **not** `sysconf_plugin.rco` (`SETTINGS-FIRMWARE.md` 5.16). The plugin is
  `software_update_plugin.rco` (138 objects), whose `Page` objects read: `page_background`,
  `page_select_update_method`, `page_search_network`, `page_search_media`, `page_start_download`,
  `page_start_copy`, `page_start_same_version`, `page_copying_server`, `page_copying_media`,
  `page_copying_ready_update`, `wizard_page_get_resource_wait`, `wizard_page_license_read`,
  `wizard_page_license_ask`, `wizard_page_license_consent`, `wizard_page_information`,
  `wizard_page_start_confirm`, `wizard_background`, `page_boot_up_restart_update`,
  `page_search_media_arcade`, `page_copying_maintenance_arcade`, `page_search_shop_boot`,
  `page_search_gaikai`, `page_cancel_update`, `page_update_files_search`, `page_updater_select_list`,
  `page_prepare_for_updater_select`, `page_copy_for_updater_select`, `page_deleting_updater`,
  `page_delete_complete`. **29 pages**, matching `FIDELITY-SURFACES-MAP.md` 2.11.
- **Console flow (measured + TRACED).** First page captured:
  `settings/system-update-page.png` — title `System Update`, body `Select an update method.`, two rows
  `Update via Internet` / `Update via Storage Media`, `X Enter` / `O Cancel` (`SETTINGS-COVERAGE.md`
  1.2, `SETTINGS-FIRMWARE.md` 9.1/U8). The rest (search network/media → license → download/copy →
  install → restart) is a PSN update channel, `uncaptured`.
- **Native now (TRACED).** `settings_menu.cpp` binds `update` to `Action::SystemUpdate`
  (`updater_shell.cpp`, D7): a native GitHub-release check that reports a newer version. The two
  firmware rows (`update/net`, `update/media`) are *not* drawn as a method chooser.
- **Verdict: BLOCKED.** A PS3 system-software update channel does not exist on this rig; reproducing the
  license/download/install wizard would fake an online service. The native `updater_shell.cpp` is the
  honest substitute and already owns the row. Owner: `updater_shell.cpp` (no change); do not build the
  wizard.

### 2.4 Print — `print_plugin` (27 pages)

- **Firmware surface (TRACED).** The Settings column row `print` (`Table key="print"`, `tex_printer`,
  `msg_printer_settings`) opens `page_print_config` in `sysconf_plugin.rco`; its two rows — Printer
  Selection (`msg_printer_selection`) and Printer Manager (`msg_printer_manager`) — hand off to
  `print_plugin.rco` (163 objects). Its `Page` objects read: `wizard_background`, `page_printer_logo`,
  `page_print_manager_wait`, `page_print_manager`, `page_print_select_not_used`, `page_print_select_guide`,
  `page_print_select_maker`, `page_print_select_printer`, `page_print_before_preservation`,
  `page_print_setting_unregistration`, `page_print_setting_size`, `page_print_setting_form`,
  `page_print_setting_quality`, `page_print_setting_borderless`, `page_print_setting_color_correct`,
  `page_print_setting_confirmation`, `page_print_job_wait`, `page_print_job_confirm`,
  `page_print_job_confirm_optionmenu`, `page_print_job_printing`, `page_print_job_finish`,
  `page_print_job_cancelling`, `page_print_job_error`, `page_print_photo_select`,
  `page_print_photo_prepare`, `page_print_preview`, `main`. **27 pages**, matching
  `FIDELITY-SURFACES-MAP.md` 2.11 ("print_plugin (27 pages)").
- **Console flow (measured + INFERRED).** `settings/page-printer-settings.png` shows the two rows
  (`SETTINGS-COVERAGE.md` 1.12). Behind them: a printer-selection wizard (maker → printer), a
  settings wizard (size/form/quality/borderless/colour-correct → confirm), and a job wizard
  (photo-select → prepare → preview → confirm → printing → finish). `uncaptured` beyond the two-row page.
- **Native now (TRACED).** Both rows are `Action::Information` (host report,
  `settings_menu.cpp:366-367`); `host.cpp` has `readPrinters()` (host printer enumeration) but no
  selection/manager page binds it.
- **Verdict: NEEDS BACKEND (printer), with a buildable subset.** Listing real host printers
  (`QPrinterInfo`/`EnumPrintersW`) for Printer Selection and showing the default/spooler state for
  Printer Manager is buildable offline. The photo-select/preview/job wizard needs a real print pipeline
  (a spool job), which is out of scope and must not be faked. Owner: `settings_menu.cpp` (bind the two
  rows to a new `Action`) + `shell.cpp` (route) + `host.cpp` (read).

### 2.5 Backup — `page_backup_utility_*` in `sysconf_plugin.rco` (~20 pages)

- **Firmware surface (TRACED).** The System Settings row `backup_utility` (`msg_backup_utility`) opens
  the backup chain inside `sysconf_plugin.rco`. Its `Page` objects read: `page_backup_utility_select_ask`,
  `page_backup_utility_save_destination_select`, `page_backup_utility_backup_preparing`,
  `page_backup_utility_making_backup`, `page_backup_utility_backup_completed`,
  `page_backup_utility_restore_media_select`, `page_backup_utility_backup_data_select`,
  `page_backup_utility_error_no_backup_data`, `page_backup_utility_formatting_power`,
  `page_backup_utility_making_restore`, `page_backup_utility_restore_completed`,
  `page_backup_utility_delete_media_select`, `page_backup_utility_delete_data_select`,
  `page_backup_utility_deleting`, `page_backup_utility_deleted`, `page_backup_utility_error_dialog`,
  plus the `_ex` / `_press_ps_button` variants (`page_backup_utility_ex`, `page_backup_utility_press_ps_button`,
  `page_backup_utility_making_restore_ex`, `page_backup_utility_restore_completed_ex`). **20 objects**,
  matching `FIDELITY-SURFACES-MAP.md` 2.11 ("page_backup_utility_* (20 pages)").
- **Console flow (INFERRED from the page spine; `uncaptured`).** Backup to a chosen external medium:
  select-ask → save-destination-select → preparing → making-backup → completed; restore:
  restore-media-select → backup-data-select → making-restore → completed; delete-backup is the same
  select → deleting → deleted spine. Each is a select → proceed → finish chain (the same class as the
  confirm chains in `SETTINGS-FIRMWARE.md` §4).
- **Native now (TRACED).** The `backup_utility` row is `Action::Information` (`settings_menu.cpp:298`),
  `SETTINGS-COVERAGE.md` 1.7: "none on the host: a PC backup is a different product".
- **Verdict: BUILDABLE OFFLINE.** On a PC "backup" is a copy of the app's data directory (and/or the
  user's chosen folders) to a chosen destination volume/folder, which the existing `file_ops.cpp` copy
  worker already performs safely. No online service is involved; nothing needs to be faked. The
  firmware's "external MS/SD/CF device" media-select maps honestly to "choose a destination folder".
  Owner: new file `backup_shell.cpp` + `shell.h` `Action` + `shell.cpp` route + `settings_menu.cpp` bind
  (`backup_utility` → new `Action`).

### 2.6 Set date and time — `page_date_time_config_set_date_time`

- **Firmware surface (TRACED).** `sysconf_plugin.rco` carries the single `Page`
  `page_date_time_config_set_date_time` behind the Date and Time Settings row `datetime`
  (`msg_datetime`). `SETTINGS-FIRMWARE.md` 5.7 records the row opens this *set* page, not an info page;
  `SETTINGS-COVERAGE.md` 2.3 lists it among the pages that "do not exist natively at all".
- **Console flow (measured for the row, `uncaptured` for the set page).** The row shows the live clock
  `27/10/2026 21:40` (`F-date`, `SETTINGS-COVERAGE.md` 1.9); activating it opens the editable
  date/time set page. The sibling rows (Set Automatically, Date Format, Time Format, Time Zone, Daylight
  Saving) are already native.
- **Native now (TRACED).** `datetime` is `Action::Information` (a read-only page showing the live host
  clock; `settings_menu.cpp:268`).
- **Verdict: BUILDABLE OFFLINE (read-only) / BLOCKED (the editable set page).** The host clock must not
  be written (`SETTINGS-COVERAGE.md` 1.9 note, §4). A read-only page that shows the current time, the
  host's NTP/W32Time state, and the timezone is buildable offline and is the honest form; the editable
  date/time set page is blocked by the no-host-write policy and must stay out. Owner:
  `settings_menu.cpp` + `shell.cpp` (the `Information` body already exists via `host::rowDetail`).

---

## 3. Save-data management

The Game column's seven utility rows are all drawn at the console's sizes and are all inert
(`FIDELITY-SURFACES-MAP.md` 2.2: **Game verdict 0 of 8 pages**; `FIDELITY-SURFACES.md` §3.1b: five of the
seven bind the wrong art — a separate, already-specified render defect).

### 3.1 Saved Data Utility (PS3) — `ps3_savedata_plugin` (8 pages)

- **Firmware surface (TRACED).** `category_game.xml` `View id="seg_sdps3"`: `Table key="sdps3"`
  (`item_tex_ps3util`, `msg_savedata_utility_ps3`, `action=exec_ps3savedata`,
  `data_connection=xcb://localhost/query?limit=2048&ssort=root.sort_selected.savedata_ps3_all`) and the
  four `*_connection` queries to MS/SD/CF/USB-memory media. The utility page set is
  `ps3_savedata_plugin.rco` (51 objects), `Page` objects: `page_plugin_copy`, `page_plugin_delete`,
  `page_plugin_selection`, `page_plugin_select_ow_cnf`, `page_plugin_dialog_ow`, `page_utility_listdata`,
  `page_dialog`, `page_dialog_fg`. **8 pages**, matching `FIDELITY-SURFACES-MAP.md` 2.2.
- **Console flow (INFERRED from the page spine; `uncaptured`).** A list of per-user save-data entries
  (title + size); per entry a selection page → Copy (to another user or media) / Delete; copy shows
  `page_plugin_copy` (copy progress + the overwrite confirm `page_plugin_select_ow_cnf` /
  `page_plugin_dialog_ow`). Strings live in the same RCO (`msg_copy`, `msg_copying`, `msg_copied`,
  `msg_delete*` — `DIALOGS-FIRMWARE.md` 3.3/3.4).
- **Native now (TRACED).** The `ps3` row is `Action::Unavailable` (inert); `shell.h:96` records the
  options-menu Copy/Delete rows as "listed but not selectable" — superseded for local *files* by the new
  backend, but there is no save-data *model*, no per-user save folder, and no page.
- **Verdict: BUILDABLE OFFLINE.** Save data maps to a per-user folder (e.g. `<data>/users/<id>/savedata`),
  and copy/delete reuse `file_ops.cpp` (already confined to the library roots; a savedata root is added
  the same way). The console's "copy between users and media" becomes "copy between user folders / to a
  chosen destination". No service to fake. Owner: new file `savedata_shell.cpp` + `shell.h` `Action` +
  `shell.cpp` route (bind `ps3` row), reusing `file_ops` and the existing Box.

### 3.2 Saved Data Utility (PS2) — `seg_sdps2` / `sel://localhost/ps2emusave`

- **Firmware surface (TRACED).** `category_game.xml` `View id="seg_sdps2"` (`msg_savedata_utility_ps2`,
  `action=exec_sdps2`), reached via `sel://localhost/ps2emusave?category_game.xml#seg_sdps2`. The
  four `*_connection` queries target MS/SD/CF/USB media.
- **Console flow.** The row is **not drawn** on the captured console (no PS2 data) —
  `FIDELITY-SURFACES-MAP.md` 2.2 marks the row itself native-only.
- **Native now (TRACED).** The `ps2` row exists (`shell.cpp` Game column) and is inert.
- **Verdict: NEEDS BACKEND (PS2 emulation), effectively native-only.** There is no PS2 save-data store on
  this host and no PS2 emulator surface to read one from. Keep the row inert (or drop it toward console
  parity); do not invent PS2 save entries. Owner: `shell.cpp` (Game column row set).

### 3.3 Saved Data Utility (minis/PSP) — `seg_sdpsp` / `ps3_savedata_plugin_game_mini`

- **Firmware surface (TRACED).** `category_game.xml` `View id="seg_sdpsp"`
  (`msg_savedata_utility_minis_psp`, `action=exec_pspsavedata`,
  `data_connection=…savedata_psp_all`), plus the small `ps3_savedata_plugin_game_mini.rco` (3 objects).
- **Console flow.** Same class as 3.1, `uncaptured`; the console drops `minis/` from the label when no
  PSP data is present (`FIDELITY-SURFACES-MAP.md` 2.2).
- **Native now (TRACED).** The `psp` row exists and is inert.
- **Verdict: BUILDABLE OFFLINE (surface) / empty-state.** The list page and its copy/delete spine can
  reuse 3.1's model; with no PSP saves the honest form is the console's own empty state
  (`msg_error_no_savedata`, "There is no saved data."). Owner: `savedata_shell.cpp` (shared with 3.1).

### 3.4 Memory Card Utility (VMC) — `vmc_savedata_plugin` (13 pages)

- **Firmware surface (TRACED).** `category_game.xml` `View id="seg_mcutility"`
  (`item_tex_ps12util`, `msg_memory_card_utility`, `action=exec_vmcsavedata`, `child=segment`,
  `vmc=promote`) → `#sdvmc_2nd` → `#seg_newvmc` / `#sdvmc_copy_newvmc`. The plugin is
  `vmc_savedata_plugin.rco` (41 objects), `Page` objects: `page_dialog`, `page_copy`, `page_delete`,
  `page_mc_import`, `page_osk`, `page_vmc_create`, `page_vmc_create_dlg`, `page_vmc_delete`,
  `page_vmc_convert`, `page_mc_format`, `page_vmc_appear_set`, `page_new_delete`, `page_new_copy`.
  **13 pages**, matching `FIDELITY-SURFACES-MAP.md` 2.2.
- **Console flow (INFERRED; `uncaptured`).** Create a virtual memory card (name via OSK → create),
  assign/format it, convert a PS1 memory-card image, import/export to USB memory, copy/delete.
- **Native now (TRACED).** The `memory` row is inert; no VMC model exists.
- **Verdict: NEEDS BACKEND (VMC image format).** The create/delete/list subset can map to a local folder,
  but `format`, `convert` and `import` operate on a PS1/PS2 virtual-memory-card image that has no host
  analogue — fabricating one is forbidden. Owner: new file (VMC subset) if the list/delete subset is
  wanted; otherwise keep inert.

### 3.5 Game Data Utility — `gamedata_plugin` (3 pages)

- **Firmware surface (TRACED).** `category_game.xml` `View id="seg_gamedata"`
  (`tex_album_icon`, `msg_gamedata_utility`, `action=exec_game`,
  `str_noitem=msg_error_no_gamedata`). The plugin is `gamedata_plugin.rco` (9 objects), `Page` objects:
  `page_game_main`, `page_delete`, `page_delete_psp2`. **3 pages**, matching
  `FIDELITY-SURFACES-MAP.md` 2.2.
- **Console flow (INFERRED; `uncaptured`).** A list of installed game data (title + size) with a Delete
  (and the delete-confirm `page_delete`); empty state `msg_error_no_gamedata`.
- **Native now (TRACED).** The `data` row is inert; the installed-apps scan (`addInstalledApps()`,
  `shell.cpp`) populates the *All Games* folder, not a game-data list.
- **Verdict: BUILDABLE OFFLINE.** Installed game data maps to the app's installed-titles data
  (`apps.json` + per-app folders); Delete reuses `file_ops.cpp`. Owner: new file or `shell.cpp`
  (bind `data` row to a list built from `ContentRegistry` + `file_ops` delete).

---

## 4. Copy / move / progress dialogs

The console's copy/delete *backend* already exists here (`file_ops.cpp`) and the options menu already
lists the rows; what is absent is the **console's progress surface and the "file exists"/overwrite
confirm**, plus the generic move/backup "data copy" and the download queue (which is PSN-only).

### 4.1 File copy dialog — `filecopy_plugin` (5) + `explore_plugin_full` `expage_fcopy_dialog`

- **Firmware surface (TRACED).** `filecopy_plugin.rco` (23 objects), `Page` objects: `expage_fcopy_plgin`,
  `fcopy_basedlg`, `fcopy_exdlg`, `fcopy_res_exdlg`, `fcopy_msgdlg`. The page plumbing lives in
  `explore_plugin_full.rco` `expage_fcopy_dialog` with `thumb_info_base`, `filedialog_listbase`,
  `text_info_msg`, `text_wait`, `plane_exist_dimmer`, `plane_exist_dialog_btn`
  (`cmdbtn_rename`/`cmdbtn_overwrite`/`cmdbtn_nosave`), `plane_exist_chkbase` (`cmdbtn_chk_applyall`) —
  all `posX/posY/width/height = 0` (laid out by code). `DIALOGS-FIRMWARE.md` 2.7/D7.
- **Console flow (TRACED + `uncaptured`).** Copy → `msg_copying` ("Copying... Please wait.") → `msg_copied`
  ("Copy completed."); if the destination already has the name, the "file exists" dialog
  (`plane_exist_dimmer` + the rename/overwrite/do-not-copy buttons); `msg_copy_cancel_ask`,
  `msg_copy_incomplete_*_ask`, `msg_error_copy` for the failure paths.
- **Native now (TRACED).** `OptionCommand::Copy` opens a destination level, `CopyTarget` runs
  `fileOps_.copy()`; the busy page shows `msg_copying` (indeterminate) and the result is a notification
  or page (`shell.cpp:3461-3497`, `:1778-1790`). No "file exists" dialog (the backend never overwrites:
  it writes "name (2).ext", `file_ops.h`).
- **Verdict: BUILDABLE OFFLINE.** The copy backend is done; the missing pieces are (a) the determinate
  progress (4.6) and (b) a destination-conflict confirm — which the honest form renders as the existing
  numbered-copy result or a rename/overwrite/cancel box, not a fabricated overwrite. Owner:
  `shell.cpp` (Box + wiring) + `dialogs.cpp`/`view.cpp` (the fcopy result page).

### 4.2 File delete dialog — `explore_plugin_full` `expage_fdelete_dialog`

- **Firmware surface (TRACED).** `expage_fdelete_dialog` with `filedel_dialog_listbase`,
  `filedel_dialog_icon`, `filedel_dialog_info`, `filedel_dialog_msgdialog`, `fdel_plane_src_cmdbtn`
  (`fdel_cmdbtn_select_all`/`deselect_all`/`decide_src`), `fdel_text_info_msg`. Strings: `msg_del_*_ask`,
  `msg_deleting`, `msg_deleted`, `msg_error_del`. `DIALOGS-FIRMWARE.md` 2.6/D6.
- **Console flow (TRACED + `uncaptured`).** The options `Delete` row → the D1 confirm box
  ("The image will be deleted. Are you sure you want to continue?" etc.) → `msg_deleting` → `msg_deleted`.
- **Native now (TRACED).** `OptionCommand::Delete` runs `deleteLocalFile()` (Recycle Bin) with no confirm
  box; the result is a notification (`shell.cpp:3499-3506`, `:1784`). `S9` in `FIDELITY-SURFACES-MAP.md`
  already noted the `Delete` row is "listed but not selectable" — now selectable, still no confirm.
- **Verdict: BUILDABLE OFFLINE.** Add the `msg_del_*_ask` two-answer Box before `deleteLocalFile()`; the
  backend needs no change. Owner: `shell.cpp` (one `showConfirm` call site).

### 4.3 Copy / move / progress "busy" surface — `filecopy_plugin` `fcopy_exdlg` + `page_busy`

- **Firmware surface (TRACED).** `fcopy_exdlg` / `fcopy_res_exdlg` (the copy progress + result pages) and
  `system_plugin.rco` `page_busy` (`busy_icon`, `busy_short`, `busy_long`, `tex_busy` 36x36, `tex_busy_s`/
  `tex_busy_l` 8x36) — `DIALOGS-FIRMWARE.md` 2.8/D8.
- **Console flow (TRACED).** A determinate copy progress with a percentage and the current file name; the
  busy ring (`tex_busy`) sweeps during an indeterminate wait. `uncaptured` (no producer on the rig).
- **Native now (TRACED).** `View::drawBusySurface(p, progress, spinnerPhase)` already draws both the
  indeterminate wait page and the `tex_busy` sweep when `progress >= 0`; but `Shell::busyProgress_` /
  `busySpinnerPhase_` are never assigned and `FileOperations::progress` is never connected, so the copy
  busy page is permanently indeterminate and shows no percentage or file name.
- **Verdict: BUILDABLE OFFLINE.** Connect `FileOperations::progress` → `busyProgress_` (and publish the
  current file name to `status_`); no new renderer is needed. Owner: `shell.cpp` (one `connect` + setter).

### 4.4 Data copy (move between media) — `data_copy_plugin` (4 pages)

- **Firmware surface (TRACED).** `data_copy_plugin.rco` (9 objects), `Page` objects: `page_dialog`,
  `page_filelist`, `page_select_destination`, `page_progress`. This is the console's generic
  "copy/move content between the HDD and a storage medium" wizard (the Save Data / backup utilities
  share its shape).
- **Console flow (INFERRED; `uncaptured`).** filelist → select-destination → progress → done.
- **Native now (TRACED).** No generic move surface; the local copy is the media-library copy only.
- **Verdict: BUILDABLE OFFLINE.** Reuses `file_ops.cpp` (copy) — a "move" is copy-then-delete, which the
  backend already supports as two calls. Owner: `shell.cpp` (route) + `dialogs.cpp` (the filelist page).

### 4.5 Download progress — `download_plugin` (9 pages)

- **Firmware surface (TRACED).** `download_plugin.rco` (60 objects), `Page` objects: `page_dialog`,
  `page_filelist`, `page_select_device`, `page_select_instant_play`, `page_check_headers`,
  `new_page_url_select`, `page_select_destination`, `page_progress`, `page_basic_auth`. This is the
  PSN Download Management queue (`#seg_dlctrl`), reachable from the Network column.
- **Console flow (measured for the column, `uncaptured` for the queue).** The `Download Management` row
  is **not drawn** on the captured console (no queue, no network) — `FIDELITY-SURFACES-MAP.md` 2.7.
- **Native now (TRACED).** The row is `Action::SteamDownloads` (`shell.cpp:805`, `steam_downloads`
  via `openSteamDownloads()`), a native substitute listing Steam's queue.
- **Verdict: BLOCKED (PSN queue).** Reproducing `page_select_device` / `page_basic_auth` / `page_progress`
  for the PS3 download service would fake a PSN channel. The Steam-download substitute is the honest form
  and already owns the row. Owner: none.

### 4.6 Busy / progress determinate form — `system_plugin` `page_busy` (the publisher gap)

- **Firmware surface (TRACED).** `system_plugin.rco` `page_busy` (see 4.3) — the one determinate/progress
  primitive the whole console shares (copy, delete, download, sign-in).
- **Console flow (TRACED).** Same as 4.3.
- **Native now (TRACED).** The draw path exists (`drawBusySurface`), the data path does not
  (`busyProgress_`/`busySpinnerPhase_` never written).
- **Verdict: BUILDABLE OFFLINE.** Folded into 4.3; the single change is in `shell.cpp`.

---

## 5. Summary table

| # | Surface | Firmware source | Native now | Verdict |
|--:|---|---|---|---|
| 2.1 | Network setup wizard | `netconf_plugin.rco` (43 pages) | `Action::Information` (status list + test real) | **BLOCKED** (host network write forbidden) |
| 2.2 | Device setup wizard | `deviceconf_plugin.rco` (29 pages) | rows listed; 4 host writes real, BT partial | **NEEDS BACKEND** (device) |
| 2.3 | Software update wizard | `software_update_plugin.rco` (29 pages) | `Action::SystemUpdate` (GitHub updater) | **BLOCKED** (PS3 update channel) |
| 2.4 | Print manager / selection | `print_plugin.rco` (27 pages) | both rows `Action::Information` | **NEEDS BACKEND** (printer; list subset buildable) |
| 2.5 | Backup utility | `sysconf_plugin` `page_backup_utility_*` (20) | `Action::Information` | **BUILDABLE OFFLINE** (folder copy) |
| 2.6 | Set date and time | `sysconf_plugin` `page_date_time_config_set_date_time` | read-only info page | **BUILDABLE (read-only) / BLOCKED (write)** |
| 3.1 | Saved Data Utility (PS3) | `ps3_savedata_plugin.rco` (8 pages) | `ps3` row inert | **BUILDABLE OFFLINE** |
| 3.2 | Saved Data Utility (PS2) | `seg_sdps2` (`sel://…/ps2emusave`) | `ps2` row inert (native-only) | **NEEDS BACKEND** (PS2 emulation) |
| 3.3 | Saved Data Utility (minis/PSP) | `seg_sdpsp` + `ps3_savedata_plugin_game_mini.rco` | `psp` row inert | **BUILDABLE OFFLINE** (surface/empty) |
| 3.4 | Memory Card Utility (VMC) | `vmc_savedata_plugin.rco` (13 pages) | `memory` row inert | **NEEDS BACKEND** (VMC image) |
| 3.5 | Game Data Utility | `gamedata_plugin.rco` (3 pages) | `data` row inert | **BUILDABLE OFFLINE** |
| 4.1 | File copy dialog | `filecopy_plugin.rco` (5) + `expage_fcopy_dialog` | copy backend done; no conflict dialog | **BUILDABLE OFFLINE** |
| 4.2 | File delete dialog | `explore_plugin_full` `expage_fdelete_dialog` | delete backend done; no confirm box | **BUILDABLE OFFLINE** |
| 4.3 | Copy/move progress busy | `fcopy_exdlg` + `page_busy` | draw path done; no publisher | **BUILDABLE OFFLINE** |
| 4.4 | Data copy (move) | `data_copy_plugin.rco` (4 pages) | none | **BUILDABLE OFFLINE** |
| 4.5 | Download progress | `download_plugin.rco` (9 pages) | `Action::SteamDownloads` substitute | **BLOCKED** (PSN queue) |

**17 surfaces spec'd: 10 buildable-offline, 4 need a backend, 3 blocked.**

---

## 6. Ranked implementation plan (write-fence owners)

Order is by measured yield per unit of work, the same rule `FIDELITY-VERDICT.md` §7 uses.

1. **Copy/move/progress + delete confirm (4.1–4.4, 4.6).** Highest yield, backend already done.
   - `shell.cpp` / `shell.h`: connect `FileOperations::progress` → `busyProgress_`/`busySpinnerPhase_`
     and publish the current file name to `status_`; add the `msg_del_*_ask` confirm Box before
     `deleteLocalFile()`; add a destination-conflict Box (rename/overwrite/cancel → numbered copy).
   - `dialogs.cpp` / `view.cpp`: the fcopy result page and the determinate busy sweep (already drawn).
   - `file_ops.cpp`: no change (it is already safe and correct).

2. **Saved Data Utility (PS3) + minis/PSP (3.1, 3.3).** New per-user save folder + list page, reusing
   `file_ops` copy/delete and the existing Box. `shell.h` `Action` + `shell.cpp` route + new file
   `savedata_shell.cpp`.

3. **Game Data Utility (3.5).** List installed app data + delete. `shell.cpp` (bind `data` row) +
   `file_ops`.

4. **Set date and time read-only page (2.6).** Fill the `Information` body with the host clock + NTP
   state. `settings_menu.cpp` + `shell.cpp` + `host.cpp`.

5. **Backup utility (2.5).** Folder copy/restore via `file_ops`, firmware select→proceed→finish chain
   reusing the existing settings-write chain. New file `backup_shell.cpp` + `settings_menu.cpp` bind +
   `shell.cpp` route.

6. **Print selection/manager list subset (2.4).** Real host printer enumeration behind the two rows.
   `settings_menu.cpp` + `shell.cpp` + `host.cpp`.

7. **Device wizard host subset (2.2).** Keep the four real host rows; do not build the calibrate /
   reassign / BD-remote / voice-changer flows. `settings_menu.cpp` + `shell.cpp` (+ `bluetooth_shell.cpp`
   already covers the BT list).

8. **Memory Card Utility list/delete subset (3.4).** Only if a VMC-as-folder model is wanted; the
   format/convert/import half is blocked. New file.

9. **BLOCKED, no owner (documented refusals):** the netconf wizard (2.1, host network write), the
   software-update wizard (2.3, PS3 channel; `updater_shell.cpp` is the substitute), the download queue
   (4.5, PSN; `SteamDownloads` is the substitute). Each keeps its `Action::Information` / native
   substitute row and a logged reason.

---

status: groundwork only, no code changed, not built
