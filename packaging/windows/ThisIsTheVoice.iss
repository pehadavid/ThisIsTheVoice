; SPDX-License-Identifier: GPL-3.0-or-later
;
; Windows installer (Inno Setup 6). Build with:
;   iscc /DAppVersion=0.1.0 /DBuildDir=C:\path\to\build packaging\windows\ThisIsTheVoice.iss
; Output: dist\ThisIsTheVoice-<version>-windows-x64-setup.exe
;
; VST3 and CLAP go to the standard shared folders (Common Files\VST3, Common Files\CLAP)
; that every host scans; the standalone application goes to Program Files.

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#ifndef BuildDir
  #error Pass /DBuildDir=<CMake build directory>
#endif

#define AppName "This Is The Voice"
#define BinDir BuildDir + "\bin"

[Setup]
AppId={{6C3E2A51-8B4D-4F3A-9E57-7A1F0C2D9B64}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Peha
AppPublisherURL=https://github.com/pehadavid/ThisIsTheVoice
AppSupportURL=https://github.com/pehadavid/ThisIsTheVoice/issues
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=ThisIsTheVoice-{#AppVersion}-windows-x64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}

[Types]
Name: "full"; Description: "All formats"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#BinDir}\ThisIsTheVoice.vst3\*"; DestDir: "{commoncf64}\VST3\ThisIsTheVoice.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\ThisIsTheVoice.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#BinDir}\ThisIsTheVoice.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\external\DPF\LICENSE"; DestDir: "{app}"; DestName: "LICENSE-DPF.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\ThisIsTheVoice.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ThisIsTheVoice.vst3"
