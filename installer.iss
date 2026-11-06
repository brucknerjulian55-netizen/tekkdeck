; Installer fuer Tekkdeck. Wird von GitHub beim Veroeffentlichen gebaut.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{2A9D3F44-8E0B-4D57-B1C6-6E3F90C41B02}
AppName=Tekkdeck
AppVersion={#AppVersion}
AppPublisher=Tekkbox
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=Tekkdeck
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=Tekkdeck-Setup
Compression=lzma2
SolidCompression=yes
; Schliesst FL Studio nach Rueckfrage, falls das Plugin gerade geladen ist
CloseApplications=yes
CloseApplicationsFilter=*.vst3,*.dll
RestartApplications=no

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "build\Tekkdeck_artefacts\Release\VST3\Tekkdeck.vst3\*"; DestDir: "{commoncf64}\VST3\Tekkdeck.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=[name] ist installiert. Starte FL Studio neu, dann läuft die neue Version.
