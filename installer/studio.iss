; Instalador do ReplayDeck Studio (Inno Setup 6)
; iscc /DMyVersion=1.0.0 /DSrcDir=<pasta do programa montado> studio.iss
;
; Instala na pasta do usuário (sem pedir administrador) e em modo portátil:
; configurações, cenas e perfis ficam em <pasta do programa>\config, separados do OBS normal.

#ifndef MyVersion
  #define MyVersion "1.0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\pkg"
#endif
#define MyName "ReplayDeck Studio"

[Setup]
AppId={{3C9E5A1B-6F2D-4B8E-A7C4-1D9F0E2B6A53}
AppName={#MyName}
AppVersion={#MyVersion}
AppVerName={#MyName} {#MyVersion}
AppPublisher=ReplayDeck
DefaultDirName={autopf}\{#MyName}
DefaultGroupName={#MyName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=ReplayDeck-Studio-{#MyVersion}-Instalador
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\64bit\obs64.exe
CloseApplications=yes
LicenseFile={#SrcDir}\LICENCA.txt

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; versão 1.0.0 tinha o replay e o placar como plugins: apaga para não carregar duas vezes
Type: files; Name: "{app}\obs-plugins\64bit\replaydeck.dll"
Type: files; Name: "{app}\obs-plugins\64bit\futsal-placar.dll"
Type: filesandordirs; Name: "{app}\data\obs-plugins\replaydeck"
Type: filesandordirs; Name: "{app}\data\obs-plugins\futsal-placar"

[Dirs]
Name: "{app}\config"

[Icons]
Name: "{group}\{#MyName}"; Filename: "{app}\bin\64bit\obs64.exe"; WorkingDir: "{app}\bin\64bit"
Name: "{group}\Pasta de configurações"; Filename: "{app}\config"
Name: "{group}\{cm:UninstallProgram,{#MyName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyName}"; Filename: "{app}\bin\64bit\obs64.exe"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\64bit\obs64.exe"; WorkingDir: "{app}\bin\64bit"; Description: "{cm:LaunchProgram,{#MyName}}"; Flags: nowait postinstall skipifsilent
