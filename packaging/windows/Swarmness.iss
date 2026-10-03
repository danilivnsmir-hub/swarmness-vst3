; Inno Setup script for Swarmness (Windows x64)
; Build: ISCC.exe /DAppVersion=1.2.0 /DBuildDir=<path to build\Swarmness_artefacts\Release> Swarmness.iss

#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Swarmness_artefacts\Release"
#endif

[Setup]
AppId={{6C0B6C8E-2E7B-4C39-9A5E-5A1D5F2B7A11}
AppName=Swarmness
AppVersion={#AppVersion}
AppPublisher=Insect Audio
AppPublisherURL=https://github.com/danilivnsmir-hub/swarmness-vst3
DefaultDirName={autopf}\Swarmness
DefaultGroupName=Swarmness
DisableProgramGroupPage=yes
OutputBaseFilename=Swarmness-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Swarmness {#AppVersion}

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[InstallDelete]
; Older copies (1.x / 2.x were shared as a bare .vst3 file and copied by hand) carry the same plug-in ID,
; so a DAW keeps loading them instead of this version - and a lone Swarmness.vst3 *file* at the install
; path even stops the bundle folder from being created. Clear the standard VST3 folders first.
Type: filesandordirs; Name: "{commoncf64}\VST3\Swarmness.vst3"
Type: filesandordirs; Name: "{commoncf32}\VST3\Swarmness.vst3"
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\Swarmness.vst3"

[Files]
Source: "{#BuildDir}\VST3\Swarmness.vst3\*"; DestDir: "{commoncf64}\VST3\Swarmness.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Swarmness.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\Swarmness"; Filename: "{app}\Swarmness.exe"; Components: standalone
Name: "{group}\Uninstall Swarmness"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Swarmness.vst3"
