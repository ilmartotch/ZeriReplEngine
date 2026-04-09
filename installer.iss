; installer.iss — Inno Setup script for Zeri Windows installer
; Requires: Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
; Usage:    iscc installer.iss
; Output:   installer\zeri-setup.exe

#define AppName      "Zeri"
#define AppVersion   "1.0.0"
#define AppPublisher "your-username"
#define AppURL       "https://github.com/your-username/zeri"
#define AppExeName   "zeri.exe"

[Setup]
AppId={{A7F3C2D1-4E8B-4F0A-9C6D-2B5E1A3F7890}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=LICENSE
OutputDir=installer
OutputBaseFilename=zeri-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.16299

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; \
  Description: "Add {#AppName} to the system PATH (recommended)"; \
  GroupDescription: "Additional options:"; \
  Flags: checked

[Files]
; Main binaries — must be in dist/ before running this script (run build.ps1 first)
Source: "dist\{#AppExeName}";   DestDir: "{app}"; Flags: ignoreversion
Source: "dist\ZeriEngine.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "dist\vcruntime*.dll";  DestDir: "{app}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\dist\vcruntime140.dll'))
Source: "dist\msvcp*.dll";      DestDir: "{app}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\dist\msvcp140.dll'))

; Runtime sidecar scripts
Source: "dist\runtime\*"; DestDir: "{app}\runtime"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; \
  Check: DirExists(ExpandConstant('{src}\dist\runtime'))

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Registry]
; Add app directory to user PATH when task is selected
Root: HKCU; \
  Subkey: "Environment"; \
  ValueType: expandsz; \
  ValueName: "Path"; \
  ValueData: "{olddata};{app}"; \
  Check: NeedsAddPath(ExpandConstant('{app}')); \
  Tasks: addtopath

[UninstallDelete]
; Remove socket file if left behind
Type: files; Name: "{%TEMP}\zeri-core.sock"

[Code]
// Checks whether the given path is already present in the user PATH variable.
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';',
                ';' + Uppercase(OrigPath) + ';') = 0;
end;

// After install: broadcast WM_SETTINGCHANGE so open terminals pick up the new PATH.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Dummy: DWORD;
begin
  if CurStep = ssPostInstall then
    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
      PChar('Environment'), SMTO_ABORTIFHUNG, 5000, Dummy);
end;

// After uninstall: remove app dir from PATH.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  OrigPath, NewPath, AppDir: string;
begin
  if CurUninstallStep = usPostUninstall then begin
    AppDir := ExpandConstant('{app}');
    if RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then begin
      NewPath := StringReplace(OrigPath, ';' + AppDir, '', [rfReplaceAll, rfIgnoreCase]);
      NewPath := StringReplace(NewPath, AppDir + ';', '', [rfReplaceAll, rfIgnoreCase]);
      RegWriteStringValue(HKCU, 'Environment', 'Path', NewPath);
    end;
  end;
end;
