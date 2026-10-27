# XMB feature catalog

This inventory contains 307 indexed guide topics/actions, including group headings; 277 are leaves. It is a coverage baseline, not a claim that the complete firmware has only 307 features. Nested dialogs, state variants, dynamic content, device/plugin-specific actions and presentation requirements expand the test surface.

Source: [Sony PS3 manual index](https://manuals.playstation.net/document/en/ps3/current/manualindex.html). The guide covers 4.80 and later; local 4.93 visibility must be checked per model, region, account, connected devices and installed applications. Native extras such as a host folder picker are not counted as proof of original feature completion.

Status revision 2026-10-26: XMB-001, XMB-006, XMB-079, XMB-109, XMB-149, XMB-170, XMB-171, XMB-177,
XMB-210 and XMB-218 were re-read against the source and now state the implemented part as well as what
remains. Five native surfaces that are not Sony guide topics had no rows at all; they are listed in
"Native surfaces outside the 307-topic index" near the end of this file.

Status revision 2026-10-27: NATIVE-06..NATIVE-12 add the seven native surfaces that landed after the
2026-10-26 sweep - the Download Management page, the notification composite, the Triangle options menu, the
subtitle/audio-track player panel, Continue Watching, the volume HUD and the in-XMB Jellyfin sign-in page.
XMB-165 and XMB-201 were re-read against the source and now name the implemented part.

## About the XMB™ menu

Required backend: Menu state, input routing, overlays, text entry, concurrent activities.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-001 | About the XMB™ (XrossMediaBar) menu | Partial: category/page navigation, settings and item options panel, and the PS system menu; firmware panel animation and per-item actions open | [Guide](https://manuals.playstation.net/document/en/ps3/current/basicoperations/xmb.html) |
| XMB-002 | Changing the way content is displayed | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/basicoperations/changeview.html) |
| XMB-003 | Using the keyboard | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/basicoperations/keyboard.html) |
| XMB-004 | Using the parental control settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/basicoperations/parentallock.html) |

## Users

Required backend: Profiles, session ownership, user-scoped storage, login lifecycle.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-005 | Users category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/users.html) |
| XMB-006 | Turning off the system | Partial: the PS menu closes the native application after one confirmation; it does not power off the host | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/turnoff.html) |
| XMB-007 | Creating new users | Partial: local profiles implemented; PS3 session/account semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/newuser.html) |
| XMB-008 | Logging in | Partial: local profiles implemented; PS3 session/account semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/login.html) |
| XMB-009 | Logging out | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/logout.html) |
| XMB-010 | Editing user information | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/userinfo.html) |
| XMB-011 | Deleting users | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/users/deleteuser.html) |

## Settings

Required backend: Typed settings, capability queries, transaction/rollback, device and service adapters.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-012 | System Update | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/update.html) |
| XMB-013 | Game Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/gamesettings.html) |
| XMB-014 | Game Settings → PS - Upscaler | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/gamesettings.html) |
| XMB-015 | Game Settings → PS - Smoothing | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/gamesettings.html) |
| XMB-016 | Video Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-017 | Video Settings → BD - Menu Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-018 | Video Settings → BD - Audio Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-019 | Video Settings → BD - Subtitle Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-020 | Video Settings → BD - Internet Connection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-021 | Video Settings → DVD - Menu Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-022 | Video Settings → DVD - Audio Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-023 | Video Settings → DVD - Subtitle Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-024 | Video Settings → Closed Captions | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-025 | Video Settings → Closed Captions Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-026 | Video Settings → BD / DVD - Cinema Conversion | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-027 | Video Settings → DVD - Wide Display | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-028 | Video Settings → BD / DVD - Upscaler | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-029 | Video Settings → BD / DVD - Video Output Format (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-030 | Video Settings → 1080p 24 Hz Output (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-031 | Video Settings → BD / DVD - Dynamic Range Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-032 | Video Settings → BD / DVD - Audio Output Format (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-033 | Video Settings → BD - Audio Output Format (Optical Digital) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-034 | Video Settings → System Storage - Content Audio Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-035 | Video Settings → System Storage - Content Subtitle Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-036 | Video Settings → Sequential Playback | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-037 | Video Settings → 50 Hz Video Output | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdsettings.html) |
| XMB-038 | Music Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-039 | Music Settings → Audio CD Import | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/cdimport.html) |
| XMB-040 | Music Settings → Crossfade Playback | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/crossfade.html) |
| XMB-041 | Music Settings → Output Frequency | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/outputfrequency.html) |
| XMB-042 | Music Settings → Dynamic Normalizer | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/dynamicnormalizer.html) |
| XMB-043 | Music Settings → Bitmapping | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bitmapping.html) |
| XMB-044 | Chat Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-045 | Chat Settings → Voice/Video Chat Start | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/chat.html) |
| XMB-046 | Theme Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/themesettings.html) |
| XMB-047 | Theme Settings → Theme | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/themesettings.html) |
| XMB-048 | Theme Settings → Color / Colour | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/themesettings.html) |
| XMB-049 | Theme Settings → Background | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/themesettings.html) |
| XMB-050 | Theme Settings → Font | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/themesettings.html) |
| XMB-051 | System Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-052 | System Settings → Automatic Update | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/autodownload.html) |
| XMB-053 | System Settings → System Name | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/systemname.html) |
| XMB-054 | System Settings → System Language | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/systemlanguage.html) |
| XMB-055 | System Settings → Character Set | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/characterset.html) |
| XMB-056 | System Settings → Dictionary Type | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/dictionarytype.html) |
| XMB-057 | System Settings → Add / Edit Term | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/addterm.html) |
| XMB-058 | System Settings → Delete Predictive Text Dictionary | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/deletehistory.html) |
| XMB-059 | System Settings → Notification Messages | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/notification.html) |
| XMB-060 | System Settings → Trophy Notifications | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/trophynotifications.html) |
| XMB-061 | System Settings → Display [What's New] | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/displaynew.html) |
| XMB-062 | System Settings → Disc Auto-Start | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/discboot.html) |
| XMB-063 | System Settings → Control for HDMI | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/ctrlhdmi.html) |
| XMB-064 | System Settings → Enable DTCP-IP | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/dtpc.html) |
| XMB-065 | System Settings → DivX® VOD Registration Code | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/divx.html) |
| XMB-066 | System Settings → Enable ATRAC | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/enableatrac.html) |
| XMB-067 | System Settings → Enable WMA Playback | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/enablewma.html) |
| XMB-068 | System Settings → Connect PS Vita System Using Network | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connectvita.html) |
| XMB-069 | System Settings → List of Registered PS Vita Systems | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/vitalist.html) |
| XMB-070 | System Settings → Delete PS Vita System's Backup Files | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/deletebackupfiles.html) |
| XMB-071 | System Settings → Format Utility | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/formatutility.html) |
| XMB-072 | System Settings → Backup Utility | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/backuputility.html) |
| XMB-073 | System Settings → Data Transfer Utility | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/transferutility.html) |
| XMB-074 | System Settings → Restore Default Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/restore.html) |
| XMB-075 | System Settings → Restore PS3™ System | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/restoreps3.html) |
| XMB-076 | System Settings → System Information | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/systeminfo.html) |
| XMB-077 | System Settings → About PlayStation®3 (PS3™) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/aboutps3.html) |
| XMB-078 | Date and Time Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-079 | Date and Time Settings → Date and Time | Partial: read-only report of the host clock formatted through the saved date/hour format and time zone; the host clock is never written | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-080 | Date and Time Settings → Set Automatically | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-081 | Date and Time Settings → Date Format | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-082 | Date and Time Settings → Time Format | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-083 | Date and Time Settings → Time Zone | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-084 | Date and Time Settings → Daylight Saving | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/date.html) |
| XMB-085 | Power Save Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/powersave.html) |
| XMB-086 | Power Save Settings → System Auto-Off | Partial: closes native application; does not power off host | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/powersave.html) |
| XMB-087 | Power Save Settings → Controller Auto-Off | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/powersave.html) |
| XMB-088 | Accessory Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-089 | Accessory Settings → Calibrate Motion Controller | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/mccalibration.html) |
| XMB-090 | Accessory Settings → Reassign Controllers | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/assign.html) |
| XMB-091 | Accessory Settings → Controller Vibration Function | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/vibration.html) |
| XMB-092 | Accessory Settings → Register BD Remote Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bdremotecontrol.html) |
| XMB-093 | Accessory Settings → Manage Bluetooth® Devices | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/bluetooth.html) |
| XMB-094 | Accessory Settings → Camera Device Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/cameratest.html) |
| XMB-095 | Accessory Settings → Audio Device Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/audiodevice.html) |
| XMB-096 | Accessory Settings → Voice Changer | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/voicechanger.html) |
| XMB-097 | Accessory Settings → Keyboard Type | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/keyboard.html) |
| XMB-098 | Accessory Settings → Keyboard Entry Method | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-099 | Accessory Settings → Key Binding | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-100 | Accessory Settings → Key Repeat Interval | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-101 | Accessory Settings → Key Repeat Speed | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-102 | Accessory Settings → Mouse Type | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-103 | Accessory Settings → Pointer Speed | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-104 | Printer Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/printersettings.html) |
| XMB-105 | Printer Settings → Printer Manager | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/printersettings.html) |
| XMB-106 | Printer Settings → Printer Selection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/printersettings.html) |
| XMB-107 | Display Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-108 | Display Settings → Video Output Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/videooutput.html) |
| XMB-109 | Display Settings → Screensaver | Partial: five Display Settings choices persist and native dim/wake is replayed; firmware brightness curve, fade and wake parity open (ISC-45.3) | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/screensaver.html) |
| XMB-110 | Display Settings → Cross Color Reduction Filter | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/crosscolor.html) |
| XMB-111 | Display Settings → RGB Full Range (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/rgbfullrange.html) |
| XMB-112 | Display Settings → Y Pb / Cb Pr / Cr Super-White (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/superwhite.html) |
| XMB-113 | Display Settings → Deep Color/Colour Output (HDMI) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/deepcolor.html) |
| XMB-114 | Sound Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-115 | Sound Settings → Audio Output Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/audiooutput.html) |
| XMB-116 | Sound Settings → Audio Multi-Output | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/audiomulti.html) |
| XMB-117 | Sound Settings → Key Tone | Partial: native adapter implemented; exact PS3 behavior and workflow coverage open | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/keytone.html) |
| XMB-118 | Security Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-119 | Security Settings → Change Password | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-120 | Security Settings → BD - Parental Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-121 | Security Settings → BD / DVD - Parental Control Region Code | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-122 | Security Settings → DVD - Parental Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-123 | Security Settings → Parental Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-124 | Security Settings → Internet Browser Start Control | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/security.html) |
| XMB-125 | Remote Play Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-126 | Remote Play Settings → Register Device | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/registerdevice.html) |
| XMB-127 | Remote Play Settings → Registered Device List | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/devicelist.html) |
| XMB-128 | Remote Play Settings → Custom Connection Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/costomsettings.html) |
| XMB-129 | Remote Play Settings → Remote Start | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/remotestart.html) |
| XMB-130 | Remote Play Settings → Audio Output Device | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/outputdevice.html) |
| XMB-131 | Network Settings | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/index.html) |
| XMB-132 | Network Settings → Settings and Connection Status List | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/statuslist.html) |
| XMB-133 | Network Settings → Internet Connection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connect.html) |
| XMB-134 | Network Settings → Internet Connection Settings (wired connection) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connectwired.html) |
| XMB-135 | Network Settings → Internet Connection Settings (wireless connection) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connectwireless.html) |
| XMB-136 | Network Settings → Internet Connection Settings (advanced settings) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connectadvance.html) |
| XMB-137 | Network Settings → Internet Connection Test | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connecttest.html) |
| XMB-138 | Network Settings → Media Server Connection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/settings/connectdlna.html) |

## Photo

Required backend: Image decoder, metadata, transforms, image database, playlists, printer and storage adapters.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-139 | Photo category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/photo.html) |
| XMB-140 | Using the control panel | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-141 | Using the control panel → Display Mode | Partial: keyboard/native viewer route; original control panel and full workflow unobserved | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-142 | Using the control panel → Change Effect | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-143 | Using the control panel → Trimming | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-144 | Using the control panel → Add to Playlist | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-145 | Using the control panel → Print | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-146 | Using the control panel → Set as Wallpaper | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-147 | Using the control panel → Delete | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-148 | Using the control panel → Display | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-149 | Using the control panel → Zoom In / Zoom Out | Partial: native photo zoom in/out and reset work; the original control-panel presentation is open | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-150 | Using the control panel → Rotate Left / Rotate Right | Partial: keyboard/native viewer route; original control panel and full workflow unobserved | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-151 | Using the control panel → Up / Down / Left / Right | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-152 | Using the control panel → Previous / Next | Partial: keyboard/native viewer route; original control panel and full workflow unobserved | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-153 | Using the control panel → Slideshow | Partial: keyboard/native viewer route; original control panel and full workflow unobserved | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html) |
| XMB-154 | Viewing images as a slideshow | Partial: keyboard/native viewer route; original control panel and full workflow unobserved | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/slideshow.html) |
| XMB-155 | Copying image files | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/copy.html) |
| XMB-156 | Using playlists | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/index.html) |
| XMB-157 | Using playlists → Creating or playing playlists | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/playlist.html) |
| XMB-158 | Using playlists → Editing playlists | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/editplaylist.html) |
| XMB-159 | Using playlists → Copying playlists to a PSP™ system | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/exportplaylist.html) |
| XMB-160 | Printing images | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/print.html) |
| XMB-161 | Using Photo Gallery | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/photogallery.html) |
| XMB-162 | Types of files that can be displayed | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/photo/filetypes.html) |

## Music

Required backend: Audio decoder/mixer, media database, queue, DSP, visualization, optical and storage adapters.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-163 | Music category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/music.html) |
| XMB-164 | Using the control panel | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-165 | Using the control panel → Volume Control | Partial: real Windows master volume plus the volume HUD and L2/R2, media-key and `--script` ramps exist (`View::pollVolumeRamp()`, `View::drawVolumeHud()`, `host::masterVolume()`; audit/HTPC-IDEAS.md section 6), and volume/mute/key tone are real settings rows; the original control-panel page, its projection and a per-track mixer are open | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-166 | Using the control panel → Visual Player | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-167 | Using the control panel → Add to Playlist | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-168 | Using the control panel → Delete | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-169 | Using the control panel → Display | Partial: host player path; original panel and repeat-all/queue semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-170 | Using the control panel → Previous | Partial: the owned playback queue provides Previous (`Shell::skipMedia`); original control-panel presentation open | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-171 | Using the control panel → Next | Partial: the owned playback queue provides Next (`Shell::skipMedia`); original control-panel presentation open | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-172 | Using the control panel → Fast Reverse / Fast Forward | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-173 | Using the control panel → Play | Partial: host player path; original panel and repeat-all/queue semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-174 | Using the control panel → Pause | Partial: host player path; original panel and repeat-all/queue semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-175 | Using the control panel → Stop | Partial: host player path; original panel and repeat-all/queue semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-176 | Using the control panel → Repeat | Partial: host player path; original panel and repeat-all/queue semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-177 | Using the control panel → Shuffle | Partial: queue shuffle is implemented and persisted (`src/playback_queue.cpp:58-67`); original control-panel presentation open | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) |
| XMB-178 | Using the mini-size control panel | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/minipanel.html) |
| XMB-179 | Importing audio CDs to the system storage | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/cdimport.html) |
| XMB-180 | Changing icons | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/icon.html) |
| XMB-181 | Copying music files | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/copy.html) |
| XMB-182 | Using playlists | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/index.html) |
| XMB-183 | Using playlists → Creating or playing playlists | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/playlist.html) |
| XMB-184 | Using playlists → Editing playlists | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/editplaylist.html) |
| XMB-185 | Using playlists → Copying playlists to a PSP™ system | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/exportplaylist.html) |
| XMB-186 | Types of files that can be played | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/music/filetypes.html) |

## Video

Required backend: Video decoder, tracks, subtitles, timeline/chapter/scene model, disc navigation and storage adapters.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-187 | Video category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/video.html) |
| XMB-188 | Using the control panel | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-189 | Using the control panel → Red / green / blue / yellow icons | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-190 | Using the control panel → Up / Down / Left / Right | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-191 | Using the control panel → Enter | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-192 | Using the control panel → Numeric Pad | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-193 | Using the control panel → Pop-Up Menu | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-194 | Using the control panel → Menu | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-195 | Using the control panel → Top Menu | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-196 | Using the control panel → Return | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-197 | Using the control panel → Scene Search | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-198 | Using the control panel → Go To | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-199 | Using the control panel → Angle Options | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-200 | Using the control panel → Audio Options | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-201 | Using the control panel → Subtitle Options | Partial: sidecar SRT/WebVTT subtitles are discovered, parsed and drawn, and the Triangle player panel selects them and shows the audio tracks the player reports (audit/SUBTITLES.md); embedded stream subtitles (`QMediaPlayer::subtitleTracks`), disc/BD subtitle options and Jellyfin subtitle fetching are not implemented | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-202 | Using the control panel → Subtitle Style Options | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-203 | Using the control panel → Volume Control | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-204 | Using the control panel → AV Settings | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-205 | Using the control panel → Time Options | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-206 | Using the control panel → Screen Mode | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-207 | Using the control panel → Change Icon | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-208 | Using the control panel → Delete | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-209 | Using the control panel → Display | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-210 | Using the control panel → Previous (Return to Beginning) / Next | Partial: the owned queue provides Previous/Next (`Shell::skipMedia`); return-to-beginning and original panel semantics open | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-211 | Using the control panel → Fast Reverse / Fast Forward | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-212 | Using the control panel → Play | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-213 | Using the control panel → Pause | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-214 | Using the control panel → Stop | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-215 | Using the control panel → Instant Replay / Instant Advance | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-216 | Using the control panel → Slow (Reverse) / Slow (Forward) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-217 | Using the control panel → Frame Reverse / Frame Advance | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-218 | Using the control panel → A-B Repeat | Not implemented: no A-B repeat code exists in `src/`; the earlier Partial status overstated it | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-219 | Using the control panel → Repeat | Partial: host player path; original panel and format-specific semantics incomplete | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/panel.html) |
| XMB-220 | Copying or moving video files | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/copy.html) |
| XMB-221 | Backing up or restoring video files | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/videobackup.html) |
| XMB-222 | Editing video content | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/videoupload.html) |
| XMB-223 | Types of files that can be played | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/filetypes.html) |
| XMB-224 | Renewing the AACS encryption key | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/video/aacs.html) |

## TV / Video Services

Required backend: Region catalog, application install/launch, service availability.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-225 | Features under the TV / Video Services category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/tv/tvvideoservices.html) |

## Game

Required backend: Content/save databases, emulator/process lifecycle, disc detection, memory cards, rights and parental controls.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-226 | Game category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/game.html) |
| XMB-227 | Playing PlayStation®3 format software | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/index.html) |
| XMB-228 | Playing PlayStation®3 format software → Starting / quitting games | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/3start.html) |
| XMB-229 | Playing PlayStation®3 format software → Adjusting settings for gameplay | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/3settings.html) |
| XMB-230 | Playing PlayStation®3 format software → Using saved data | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/3savedata.html) |
| XMB-231 | Playing PlayStation®2 / PlayStation® format software | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/index.html) |
| XMB-232 | Playing PlayStation®2 / PlayStation® format software → Starting / quitting games | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/2start.html) |
| XMB-233 | Playing PlayStation®2 / PlayStation® format software → Adjusting settings for gameplay | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/2settings.html) |
| XMB-234 | Playing PlayStation®2 / PlayStation® format software → Using saved data | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/2savedata.html) |
| XMB-235 | Playing PlayStation®2 / PlayStation® format software → Using saved data on memory cards | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/mcsavedata.html) |
| XMB-236 | Playing PlayStation®2 / PlayStation® format software → Installing PlayStation®2 format software | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/hddinstall.html) |
| XMB-237 | Playing games downloaded from PlayStation®Store | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/index.html) |
| XMB-238 | Playing games downloaded from PlayStation®Store → Downloading games | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/downloadgame.html) |
| XMB-239 | Playing games downloaded from PlayStation®Store → Playing downloaded games | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/dlgamestart.html) |
| XMB-240 | Playing games downloaded from PlayStation®Store → Copying games to play on a PSP™ system | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/copypsp.html) |
| XMB-241 | Using saved data for minis and PSP™Game software | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/psavedata.html) |
| XMB-242 | Copying saved data to storage media (making a backup) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/game/copysavedata.html) |

## Network

Required backend: Browser engine, downloads, discovery, network status, remote-play session transport.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-243 | Online Instruction Manuals → Viewing online manuals | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/onlinemanual/onlinemanual.html) |
| XMB-244 | Remote Play → About remote play | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remoteplay.html) |
| XMB-245 | Remote Play → Using remote play (via the wireless LAN feature of the PS3™ system) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remotewireless.html) |
| XMB-246 | Remote Play → Using remote play (via an access point) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remotewired.html) |
| XMB-247 | Remote Play → Using remote play (via the Internet) | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remoteinternet.html) |
| XMB-248 | Remote Play → Operations during remote play | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remotemenu.html) |
| XMB-249 | Remote Play → Closing the remote play connection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/remoteplay/remoteoff.html) |
| XMB-250 | Internet Browser → Basic operation of the Internet browser | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/browser.html) |
| XMB-251 | Internet Browser → Browse mode / window mode | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/mode.html) |
| XMB-252 | Internet Browser → Browse mode menu | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-253 | Internet Browser → Browse mode menu → View | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-254 | Internet Browser → Browse mode menu → Tools | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-255 | Internet Browser → Browse mode menu → Window List | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-256 | Internet Browser → Browse mode menu → Search | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-257 | Internet Browser → Browse mode menu → File | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-258 | Internet Browser → Browse mode menu → Back | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-259 | Internet Browser → Browse mode menu → Forward | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-260 | Internet Browser → Browse mode menu → Refresh / Cancel | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-261 | Internet Browser → Browse mode menu → Home | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-262 | Internet Browser → Browse mode menu → Bookmarks | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-263 | Internet Browser → Browse mode menu → Browser Security | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-264 | Internet Browser → Browse mode menu → History | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-265 | Internet Browser → Browse mode menu → Exit | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menub.html) |
| XMB-266 | Internet Browser → Window mode menu | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menuw.html) |
| XMB-267 | Internet Browser → Window mode menu → Open New Window | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menuw.html) |
| XMB-268 | Internet Browser → Window mode menu → Close Window | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menuw.html) |
| XMB-269 | Internet Browser → Window mode menu → Close All Other Windows | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/menuw.html) |
| XMB-270 | Internet Browser → Changing the display size of a Web page | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/size.html) |
| XMB-271 | Internet Browser → Using shortcut keys / mouse / keyboard | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/shortcut.html) |
| XMB-272 | Internet Browser → Upload / Download | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/upload.html) |
| XMB-273 | Internet Browser → Internet Browser User Agreement | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/browser/eula.html) |
| XMB-274 | Internet Search → Searching the Internet | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/search/search.html) |
| XMB-275 | Download Management → Background Download | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/background/background.html) |

## PlayStation™Network

Required backend: Authenticated account/session, network service adapters, trophies, entitlements and commerce.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-276 | Signing up | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/account/signup.html) |
| XMB-277 | Signing in / signing out | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/account/signin.html) |
| XMB-278 | Saving your password / Signing in automatically | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/account/savepw.html) |
| XMB-279 | If you forget your password | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/account/forgotpw.html) |
| XMB-280 | Using an existing account | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/account/associate.html) |
| XMB-281 | Account Management → Managing account information | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/accountinfo/accountinfo.html) |
| XMB-282 | Account Management → About system activation | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/accountinfo/systemactivation.html) |
| XMB-283 | What's New → About What's New | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/whatsnew/whatsnew.html) |
| XMB-284 | Trophy Collection | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/trophy/trophy.html) |
| XMB-285 | PlayStation®Plus → About PlayStation®Plus | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/plus/plus.html) |
| XMB-286 | PlayStation®Store → PlayStation®Store overview | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/store/store.html) |
| XMB-287 | PlayStation®Store → Using the wallet | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/store/wallet.html) |
| XMB-288 | PlayStation®Store → Checking PlayStation®Store usage | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/store/purchasehistory.html) |

## Friends

Required backend: Presence, profiles, relationships, messaging, chat and media sessions.

| ID | Original topic or action | Native status | Reference |
|---|---|---|---|
| XMB-289 | Friends category | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/friends.html) |
| XMB-290 | Friends list | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/index.html) |
| XMB-291 | Friends list → Adding to your Friends list | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/addfriend.html) |
| XMB-292 | Friends list → Checking status | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/status.html) |
| XMB-293 | Friends list → Viewing profiles | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/profile.html) |
| XMB-294 | Friends list → Deleting from your Friends list | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/deletefriend.html) |
| XMB-295 | Block List | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/index.html) |
| XMB-296 | Block List → Adding to your block list | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/addblocklist.html) |
| XMB-297 | Block List → Deleting from your block list | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/deleteblocklist.html) |
| XMB-298 | Text chat | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/index.html) |
| XMB-299 | Text chat → Starting or quitting text chat | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/textchat.html) |
| XMB-300 | Voice / video chat | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/index.html) |
| XMB-301 | Voice / video chat → Starting or quitting voice / video chat | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/startchat.html) |
| XMB-302 | Voice / video chat → Using the control panel | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/panel.html) |
| XMB-303 | Message Box | Group: child capabilities must be assessed independently | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/index.html) |
| XMB-304 | Message Box → Creating or sending messages | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/sendmsg.html) |
| XMB-305 | Message Box → Checking for new messages | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/checkmsg.html) |
| XMB-306 | Message Box → Protecting messages | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/protectmsg.html) |
| XMB-307 | Message Box → Deleting messages | Not implemented as a complete original workflow | [Guide](https://manuals.playstation.net/document/en/ps3/current/friends/deletemsg.html) |

## Native surfaces outside the 307-topic index (added 2026-10-26, extended 2026-10-27)

These are not Sony guide topics, so they are not counted in the 307 above and were missing from this
catalogue until 2026-10-26. Statuses are read from the source and from the recorded runs the cited audit
documents; nothing here was re-executed for this catalogue.

| ID | Native surface | Status | Evidence |
|---|---|---|---|
| NATIVE-01 | Steam installed/owned titles in Game | Partial: installed titles render in the PKG slot, with owned-but-not-installed rows and one Not Installed folder, store sections and an install handoff; a real launch-return capture is still open (ISC-37) | `Shell::steamFolderEntries` / `Shell::startSteamTracked` and `src/steam_library.*`; audit/STEAM-INTEGRATION.md |
| NATIVE-02 | Steam Friends | Partial: a signed-in local client helper returns real friends/presence without changing profile privacy; the Web API fallback currently returns 401 on the friend list | `src/steam_friends.cpp`; audit/STEAM-CLIENT-FRIENDS.md |
| NATIVE-03 | Native Jellyfin service and player | Partial: local sign-in, library/folder pages, direct and server-negotiated HLS playback, pause/seek/Back and sign-out verified against Jellyfin 12.4; posters, selectors and resume UI open (ISC-46.6) | `src/jellyfin_*.cpp`; audit/JELLYFIN-NATIVE.md |
| NATIVE-04 | Imported-app registry and editor | Done for the recorded checks: versioned JSON registry, atomic merge, executables/browser apps/Jellyfin services and a native editor; invalid input preserves the existing bytes | `src/content_registry.*`, `src/app_editor.cpp`; audit/APP-EDITOR.md, CONTENT-REGISTRY.md |
| NATIVE-05 | PS system menu and global PS hook | Partial: a held PS button opens the system menu with Turn Off the System and Quit Game; a focus-independent 20 ms HID watcher brings the XMB forward from a running title; controller charge/slot data is still static | `src/ps_watcher.*`, `Shell::psMenuActivate`, `View::paintGL` PS menu; audit/PS-BUTTON.md, PS-INGAME-HOOK.md |
| NATIVE-06 | Download Management page (Steam download queue) | Partial: the Network column's firmware `dlctrl` row (`seg_dlctrl`, `tex_dl_manage`, classified `RowKind::DownloadControl` from the `ese://` `type=download_control` provider) opens a real page over Steam's own queue - title, state text, progress from the manifest counters or `no progress published`, cached header artwork, F5 reload that cannot duplicate rows, per-row Information with the documented no-pause limit and a `.dl.json` dump. No per-row progress bar; no pause/resume/start/cancel; the scan runs on the UI thread (6-8 ms here) | `Shell::openSteamDownloads` / `reloadSteamDownloads`, `Action::SteamDownloads`; audit/STEAM-DOWNLOADS.md section 10 |
| NATIVE-07 | XMB notification pop-up composite | Partial: the `system_plugin` `page_notification` composite (plaque `tex_notification` plus the 100x100 type glyphs) driven by a 4-entry FIFO queue with a 4 s lifetime and a 200 ms mode-5 slide, from two real producers (tracked-title return, library rescan). Sound, the firmware overlap policy, per-type suppression, trophy/friend triggers and the icon shadow plane are open | `Shell::notify()`, `View::drawNotification()`; audit/NOTIFICATIONS.md |
| NATIVE-08 | Item-level Triangle options menu | Partial: a right-hand panel on content rows with `Open`/`Play`/`View`/`Start`, `Information`, `Sort By` (a second level that really reorders the page) and `Close`, derived from the selected entry and owning input while open. Steam folder rows get no Sort By (no `Page::directory`), and the firmware's other item commands (Delete, Copy, Add to Playlist, Eject Disc) are deliberately absent | `Shell::buildOptions()` / `optionsActivate()`; audit/OPTIONS-MENU.md |
| NATIVE-09 | Sidecar subtitles and the in-player panel | Partial: SRT/WebVTT discovery beside local video, bounded tolerant parsing, cues drawn through the GL glyph atlas in a lower third, and a Triangle panel for subtitles/audio tracks/Repeat/Shuffle that verifies its own read-back before claiming an audio switch. Embedded and disc subtitle streams, Jellyfin subtitles and a real multi-track switch are open | `src/subtitles.*`, `Shell::buildPlayerOptions()`, `View::drawSubtitle()`; audit/SUBTITLES.md |
| NATIVE-10 | Continue Watching | Partial: the Video column's first row lists remembered positions newest-first (at most 20, 30-day age) from the `media/resume` records, with resume/Information/Remove, disabled rows for missing files and an empty state; the old hash-only map is migrated by hashing the configured libraries and unresolved entries are dropped with a logged reason | `Shell::openContinueWatching()`, `migrateResumePoints()`; audit/MEDIA-CONTROLS.md "Continue Watching" |
| NATIVE-11 | Volume HUD and the couch volume path | Partial: L2/R2 ramp the real Windows master volume (read back from `host::masterVolume()`, never a cached value), volume media keys pass through a native event filter that swallows its own message, and a firmware-shaped HUD (Volume Control string, level meter, 3 s + 250 ms fade) draws in the band left of the info bar. The page projection is fitted, and XMB-165's original control-panel page is still absent | `View::pollVolumeRamp()`, `View::drawVolumeHud()`; audit/HTPC-IDEAS.md section 6 |
| NATIVE-12 | Jellyfin sign-in page in the XMB | Done for the recorded checks: a signed-out Jellyfin opens an ordinary page (Server URL / Username / Password / Sign In / Cancel) with a masked on-screen keyboard, client errors on the page, a Cancel that aborts the in-flight request and no modal widget; verified against a stub and a closed port, not against a real account | `Shell::openJellyfinSignInPage()` / `submitJellyfinSignIn()`; audit/JELLYFIN-NATIVE.md "Sign-in inside the XMB" |

## Reference coverage limits

The bulk source collector retrieved 111 of 179 linked pages. The other 68 returned HTTP 403 even after a slower retry. Important overview, media-panel, trophy, account and Friends pages were additionally read through the web tool. A successful index link is not proof that every nested setting value or conditional behavior was verified. Per-feature `referenceAccess` records this distinction.

No original media asset, firmware image, credential or console identity is included in this catalog. Raw downloaded HTML, original resource trees and frame extracts stay in ignored local output folders.
