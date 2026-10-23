; Kodo QR Instant installer (Inno Setup 6).
;
; Per-user install without administrator rights. "Start with Windows" is
; enabled on a fresh install only; upgrades keep whatever the user chose.
; Uninstalling removes the startup entry, the settings and the history.
;
; Built by build.ps1, which passes /DBuildDir=<folder with Kodo.exe>.

#define AppVersion "1.0.0"
#define AppName "Kodo QR Instant"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define StartupApprovedKey "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"

#ifndef BuildDir
  #define BuildDir "..\build\app"
#endif

[Setup]
AppId={{B3E5A1C7-2D84-4F6B-9A0E-7C1D5F38E462}
AppName={#AppName}
AppVerName={#AppName} {#AppVersion}
AppVersion={#AppVersion}
AppPublisher=VitomDev
AppPublisherURL=https://github.com/VitomDev/Kodo-QR-Instant
AppSupportURL=https://github.com/VitomDev/Kodo-QR-Instant/issues
AppCopyright=Copyright (c) 2026 VitomDev
VersionInfoVersion={#AppVersion}
VersionInfoCompany=VitomDev
VersionInfoDescription={#AppName} - Read and create QR codes Setup
DefaultDirName={localappdata}\Programs\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=KodoSetup
SetupIconFile=..\assets\kodo.ico
UninstallDisplayIcon={app}\Kodo.exe
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Kodo is closed by the code below, through its own window.
CloseApplications=no

[Files]
Source: "{#BuildDir}\Kodo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\Kodo.exe"

[Run]
; The usual "Launch" checkbox on the last page, checked by default. Kodo
; starts once Finish is clicked; silent upgrades start it as well.
Filename: "{app}\Kodo.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Kodo.exe"; Flags: nowait; Check: WizardSilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\{#AppName}"

[Code]
var
  IsUpgrade: Boolean;

// Kodo closes cleanly (tray icon, shortcuts) when its window gets WM_CLOSE.
procedure CloseRunningKodo;
var
  Window: HWND;
  Attempts: Integer;
begin
  Window := FindWindowByClassName('KodoQRInstantTray');
  if Window = 0 then Exit;
  PostMessage(Window, 16 { WM_CLOSE }, 0, 0);
  Attempts := 0;
  while (FindWindowByClassName('KodoQRInstantTray') <> 0) and (Attempts < 50) do
  begin
    Sleep(100);
    Attempts := Attempts + 1;
  end;
  // Give the process a moment to exit after its window is gone.
  Sleep(300);
end;

function InitializeSetup: Boolean;
begin
  IsUpgrade := RegKeyExists(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1');
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningKodo;
  Result := '';
end;

function AutostartCommand: String;
begin
  Result := '"' + ExpandConstant('{app}\Kodo.exe') + '" --background';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not IsUpgrade then
      // Fresh install: Start with Windows is on by default.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand)
    else if RegValueExists(HKCU, '{#RunKey}', '{#AppName}') then
      // Upgrade: keep the user's choice, only refresh the path.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand);
  end;
end;

function InitializeUninstall: Boolean;
begin
  CloseRunningKodo;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKCU, '{#RunKey}', '{#AppName}');
    RegDeleteValue(HKCU, '{#StartupApprovedKey}', '{#AppName}');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\{#AppName}');
  end;
end;
