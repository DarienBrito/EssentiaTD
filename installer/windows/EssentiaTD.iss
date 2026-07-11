; EssentiaTD Windows installer (Inno Setup 6.3+)
;
; Build:
;   iscc /DAppVersion=1.2.0 /DPluginDir=C:\path\to\dlls /DOutputDir=C:\path\to\out EssentiaTD.iss
; All defines are optional; defaults build against a local src\build\Release.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef PluginDir
  #define PluginDir "..\..\src\build\Release"
#endif
#ifndef OutputDir
  #define OutputDir "Output"
#endif

[Setup]
AppId={{8C24E7B1-5D96-4A0F-B2E3-7F1A9C64D5E8}
AppName=EssentiaTD
AppVersion={#AppVersion}
AppPublisher=Darien Brito
AppPublisherURL=https://github.com/DarienBrito/EssentiaTD
AppSupportURL=https://github.com/DarienBrito/EssentiaTD/issues
DefaultDirName={userdocs}\Derivative\Plugins\Essentia
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
; per-user install into Documents: no admin rights, no UAC prompt
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=EssentiaTD-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={uninstallexe}

[Files]
Source: "{#PluginDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; wipe stale plugins in our folder so upgrades never leave orphaned DLLs
; (TD would register duplicate operators from leftovers)
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\*.dll.old"
; remove copies from the flat layout the pre-installer README recommended
Type: files; Name: "{userdocs}\Derivative\Plugins\EssentiaSpectrumCHOP.dll"
Type: files; Name: "{userdocs}\Derivative\Plugins\EssentiaSpectralCHOP.dll"
Type: files; Name: "{userdocs}\Derivative\Plugins\EssentiaTonalCHOP.dll"
Type: files; Name: "{userdocs}\Derivative\Plugins\EssentiaRhythmCHOP.dll"
Type: files; Name: "{userdocs}\Derivative\Plugins\EssentiaLoudnessCHOP.dll"

[UninstallDelete]
Type: files; Name: "{app}\*.dll.old"
Type: dirifempty; Name: "{app}"

[Code]
// A running TouchDesigner keeps the plugin DLLs mapped: deletes/overwrites
// fail and TD would keep using the old module until restart.
function TouchDesignerRunning(): Boolean;
var
  Locator, WMI, Results: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    WMI := Locator.ConnectServer('', 'root\CIMV2');
    Results := WMI.ExecQuery('SELECT Name FROM Win32_Process WHERE Name LIKE ''TouchDesigner%''');
    Result := (Results.Count > 0);
  except
    // WMI unavailable: do not block the install
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  while TouchDesignerRunning() do
  begin
    if MsgBox('TouchDesigner is running. Its plugin files are locked, so the installer cannot update them.'
              + #13#10 + #13#10 + 'Close TouchDesigner, then press Retry.',
              mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := 'Installation cancelled because TouchDesigner is running.';
      Exit;
    end;
  end;
end;
