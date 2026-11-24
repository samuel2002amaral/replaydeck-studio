; Instalador do ReplayDeck Studio (Inno Setup 6)
; iscc /DMyVersion=1.0.0 /DSrcDir=<pasta do programa montado> studio.iss
;
; Instala na pasta do usuário (sem pedir administrador).
; Projeto e configurações ficam em <pasta do programa>\config\ReplayDeckStudio.

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
UninstallDisplayIcon={app}\bin\64bit\ReplayDeckStudio.exe
CloseApplications=yes
; só pede para fechar o próprio programa: o arquivo da câmera virtual fica aberto no Chrome,
; Claude, Zoom, Teams (eles listam as câmeras do Windows) e isso não pode travar a instalação
CloseApplicationsFilter=*.exe
LicenseFile={#SrcDir}\LICENCA.txt

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Excludes: "obs-virtualcam-module*.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
; câmera virtual: só troca se a versão mudou (a mesma versão em uso por outro programa fica como está)
Source: "{#SrcDir}\core\win-dshow\data\obs-virtualcam-module*.dll"; DestDir: "{app}\core\win-dshow\data"; Flags: skipifsourcedoesntexist

[InstallDelete]
; versões 1.x eram o OBS com plugins: tira o que não faz mais parte do programa
Type: files; Name: "{app}\bin\64bit\obs64.exe"
Type: files; Name: "{app}\portable_mode.txt"
Type: files; Name: "{app}\PLUGINS-INCLUIDOS.txt"
Type: filesandordirs; Name: "{app}\obs-plugins"
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\data\obs-plugins"
Type: filesandordirs; Name: "{app}\data\obs-studio"
Type: filesandordirs; Name: "{app}\core\replaydeck"
Type: filesandordirs; Name: "{app}\core\futsal-placar"
Type: filesandordirs; Name: "{app}\core\frontend-tools"

[Dirs]
Name: "{app}\config"

[Icons]
Name: "{group}\{#MyName}"; Filename: "{app}\bin\64bit\ReplayDeckStudio.exe"; WorkingDir: "{app}\bin\64bit"
Name: "{group}\Pasta de configurações"; Filename: "{app}\config"
Name: "{group}\{cm:UninstallProgram,{#MyName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyName}"; Filename: "{app}\bin\64bit\ReplayDeckStudio.exe"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\64bit\ReplayDeckStudio.exe"; WorkingDir: "{app}\bin\64bit"; Description: "{cm:LaunchProgram,{#MyName}}"; Flags: nowait postinstall skipifsilent
