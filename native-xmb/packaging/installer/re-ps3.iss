; Inno Setup script for the Windows installer (re-ps3-setup.exe).
;
; It installs the same folder that build-package.ps1 produces (and that re-ps3.zip carries), so the installed
; app and the portable zip are identical and the in-app updater keeps working on an installed copy:
;
; - Per user, no administrator rights, into %LOCALAPPDATA%\Programs\RE-PS3. The updater swaps the whole
;   install folder for the staged one beside it (apply-update.ps1), so the folder and its parent must be
;   writable by the user; Program Files would not be.
; - The uninstaller's own files live outside the install folder (UninstallFilesDir) because the updater's
;   folder swap would otherwise lose them and break "Uninstall" in Settings > Apps.
; - Autostart and the Start-menu shortcuts are made by install-autostart.ps1, the same script the XMB's
;   "Start at Sign-in" row runs, so the installer and the app agree on what they are.
; - xmb-config.json is written only when there isn't one yet; an upgrade never touches it or data\.
;
; Build: ISCC.exe /DAppVersion=0.3.0 /DSourceDir=C:\release\dist\re-ps3 /DOutputDir=C:\release\dist re-ps3.iss
; The release workflow does this; see packaging/README.md.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\..\dist\re-ps3"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\dist"
#endif

#define AppName "RE-PS3 XMB"
#define AppExe "re-ps3-native.exe"

[Setup]
; Never change AppId: Windows uses it to recognise upgrades of the same app.
AppId={{579E0505-1F6F-49BB-809B-4899F021DACE}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=RE-PS3
VersionInfoVersion={#AppVersion}
; Per user, and the user can't switch to a machine-wide install (see the header for why).
PrivilegesRequired=lowest
DefaultDirName={autopf}\RE-PS3
DisableProgramGroupPage=yes
UninstallFilesDir={localappdata}\RE-PS3\uninstall
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=re-ps3-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Stopping the app is done in PrepareToInstall (the watchdog would restart it otherwise).
CloseApplications=no

[Tasks]
Name: "autostart"; Description: "Start the XMB full-screen when I sign in to Windows (with crash watchdog)"
Name: "shortcuts"; Description: "Add ""XMB"" and ""Return to XMB"" to the Start menu"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
  Excludes: "xmb-config.json,data\*,watchdog.log*"

[Run]
; Both are per user and need no administrator rights. -NoTask makes only the shortcuts.
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-autostart.ps1"" -Display {code:DisplayArg} -Shortcuts"; \
  Flags: runhidden waituntilterminated; Tasks: autostart and shortcuts; StatusMsg: "Registering autostart and shortcuts..."
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-autostart.ps1"" -Display {code:DisplayArg}"; \
  Flags: runhidden waituntilterminated; Tasks: autostart and not shortcuts; StatusMsg: "Registering autostart..."
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-autostart.ps1"" -Display {code:DisplayArg} -NoTask"; \
  Flags: runhidden waituntilterminated; Tasks: not autostart and shortcuts; StatusMsg: "Adding shortcuts..."
; An upgrade that unticks a task removes what an earlier install made.
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-autostart.ps1"" -Uninstall"; \
  Flags: runhidden waituntilterminated; Tasks: not autostart and not shortcuts
; Start it now the way the logon task would, when there is a config to start with.
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\watchdog.ps1"" -Display {code:DisplayArg}"; \
  WorkingDir: "{app}"; Description: "Start the XMB now"; Flags: postinstall nowait runhidden skipifsilent; Check: HasConfig

[UninstallRun]
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-autostart.ps1"" -Uninstall"; \
  Flags: runhidden waituntilterminated; RunOnceId: "RemoveAutostart"

[UninstallDelete]
; The previous copy the in-app updater keeps after an update.
Type: filesandordirs; Name: "{app}.old"

[Code]
var
  FirmwarePage: TInputDirWizardPage;
  DisplayPage: TInputQueryWizardPage;

function ConfigPath(): String;
begin
  Result := ExpandConstant('{app}\xmb-config.json');
end;

function HasConfig(): Boolean;
begin
  Result := FileExists(ConfigPath());
end;

procedure InitializeWizard();
begin
  FirmwarePage := CreateInputDirPage(wpSelectDir,
    'Firmware folders', 'Where are the PS3 firmware files on this PC?',
    'The XMB reads the system software from a dev_flash folder that you supply; the installer never copies ' +
    'or downloads firmware. Leave a box empty to set it later in xmb-config.json in the install folder.' + #13#10#13#10 +
    'If xmb-config.json already exists (an upgrade), these are ignored and the file is kept as it is.',
    False, '');
  FirmwarePage.Add('dev_flash folder (required to run):');
  FirmwarePage.Add('Decrypted vsh_module folder (optional):');

  DisplayPage := CreateInputQueryPage(FirmwarePage.ID,
    'Display', 'Which screen is the TV?',
    'The XMB runs full-screen on one display, numbered from 0 in the order Windows lists them. On a PC with ' +
    'one screen use 0; with a monitor and a TV the TV is usually 1.');
  DisplayPage.Add('Display number:', False);
  DisplayPage.Values[0] := GetPreviousData('Display', '1');
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Display', DisplayPage.Values[0]);
end;

function DisplayArg(Param: String): String;
begin
  Result := IntToStr(StrToIntDef(Trim(DisplayPage.Values[0]), 1));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // An upgrade keeps its config, so don't ask for firmware paths again.
  Result := (PageID = FirmwarePage.ID) and FileExists(ConfigPath());
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = DisplayPage.ID) and (StrToIntDef(Trim(DisplayPage.Values[0]), -1) < 0) then begin
    MsgBox('Enter a display number: 0, 1, 2 ...', mbError, MB_OK);
    Result := False;
  end;
end;

function JsonString(S: String): String;
begin
  StringChangeEx(S, '\', '\\', True);
  StringChangeEx(S, '"', '\"', True);
  Result := '"' + S + '"';
end;

procedure WriteConfigIfMissing();
var
  Lines: String;
  DevFlash, Vsh: String;
begin
  if FileExists(ConfigPath()) then exit;
  DevFlash := Trim(FirmwarePage.Values[0]);
  Vsh := Trim(FirmwarePage.Values[1]);
  if (DevFlash = '') and (Vsh = '') then exit;
  Lines := '{' + #13#10 + '  "devFlash": ' + JsonString(DevFlash);
  if Vsh <> '' then
    Lines := Lines + ',' + #13#10 + '  "vshModules": ' + JsonString(Vsh);
  Lines := Lines + #13#10 + '}' + #13#10;
  SaveStringToFile(ConfigPath(), Lines, False);
end;

// Stop a running XMB from this folder, and the watchdog that would restart it, so its files can be replaced.
procedure StopRunningXmb(AppDir: String);
var
  Script: String;
  ResultCode: Integer;
begin
  StringChangeEx(AppDir, '''', '''''', True);
  Script := '$d = ''' + AppDir + '\''; ' +
    'Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID } | Where-Object { ' +
    '($_.CommandLine -and $_.CommandLine.IndexOf($d + ''watchdog.ps1'', [StringComparison]::OrdinalIgnoreCase) -ge 0) -or ' +
    '($_.ExecutablePath -and $_.ExecutablePath.StartsWith($d, [StringComparison]::OrdinalIgnoreCase)) } | ' +
    'Sort-Object { if ($_.Name -eq ''powershell.exe'') { 0 } else { 1 } } | ' +
    'ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }';
  Exec('powershell.exe', '-NoProfile -ExecutionPolicy Bypass -Command "' + Script + '"', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if DirExists(ExpandConstant('{app}')) then StopRunningXmb(ExpandConstant('{app}'));
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then WriteConfigIfMissing();
end;

function InitializeUninstall(): Boolean;
begin
  StopRunningXmb(ExpandConstant('{app}'));
  Result := True;
end;

// The in-app updater replaces the install folder with the files of a newer release, which the uninstall log
// doesn't know about. Remove everything in the folder except the user's own config and data, which stay (as
// most Windows apps do) so a reinstall picks up where it left off.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
  FindRec: TFindRec;
  Name: String;
begin
  if CurUninstallStep <> usPostUninstall then exit;
  App := ExpandConstant('{app}');
  if FindFirst(App + '\*', FindRec) then begin
    try
      repeat
        Name := FindRec.Name;
        if (Name <> '.') and (Name <> '..') and (CompareText(Name, 'data') <> 0) and
           (CompareText(Name, 'xmb-config.json') <> 0) then begin
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            DelTree(App + '\' + Name, True, True, True)
          else
            DeleteFile(App + '\' + Name);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
  RemoveDir(App);   // only succeeds when nothing was kept
end;
