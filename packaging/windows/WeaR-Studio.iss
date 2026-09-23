#define AppName "WeaR Studio"
#define AppVersion "0.1"
#define AppPublisher "WeaR Studio"
#define AppExeName "WeaR-Studio.exe"

#ifndef SourceDir
  #define SourceDir "build\\installer-stage"
#endif

#ifndef OutputDir
  #define OutputDir "build\\installer"
#endif

[Setup]
AppId={{B7E4B7E4-8A7A-4FC2-8CF0-5A5C84C9F9AA}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\\Programs\\WeaR Studio
DefaultGroupName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=WeaR-Studio-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
UninstallDisplayIcon={app}\\{#AppExeName}
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\\{#AppName}\\{#AppName}"; Filename: "{app}\\{#AppExeName}"
Name: "{autodesktop}\\{#AppName}"; Filename: "{app}\\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
