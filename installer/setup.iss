#include "setupvars.iss"
#include "version.iss"

; ============================================================
; Pre-build: Deploy application with all Qt dependencies
; ============================================================
#expr Exec("cmd.exe", "/c rmdir /s /q """ + AppDeployDir + """ 2>nul & mkdir """ + AppDeployDir + """", , , SW_HIDE)
#expr Exec("cmd.exe", "/c copy /y """ + AppBuildDir + "\" + TargetName + ".exe"" """ + AppDeployDir + "\""", , , SW_HIDE)
#expr Exec(QtDir + "\bin\windeployqt.exe", """" + AppDeployDir + "\" + TargetName + ".exe"" --qmldir """ + SourceDir + "\qml"" --no-translations --no-system-d3d-compiler --no-opengl-sw", , , SW_SHOW)
#expr Exec("cmd.exe", "/c copy /y """ + SourcePath + "\qt.conf"" """ + AppDeployDir + "\""", , , SW_HIDE)

; Copy OpenSSL DLLs (required for TLS in Remote Access)
#ifdef OpenSslDir
#expr Exec("cmd.exe", "/c copy /y """ + OpenSslDir + "\libssl-3-x64.dll"" """ + AppDeployDir + "\""", , , SW_HIDE)
#expr Exec("cmd.exe", "/c copy /y """ + OpenSslDir + "\libcrypto-3-x64.dll"" """ + AppDeployDir + "\""", , , SW_HIDE)
#endif

[Setup]
; Application identity
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#TargetProduct}
AppVersion={#VersionNumber}
AppVerName={#TargetProduct} v{#VersionNumber}
AppPublisher={#TargetCompany}
AppPublisherURL=https://github.com/Kulitorum/Decenza
AppSupportURL=https://github.com/Kulitorum/Decenza/issues

; Installation settings
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
DefaultDirName={autopf}\{#TargetCompany}\{#TargetProduct}
DefaultGroupName={#TargetCompany}\{#TargetProduct}
DisableProgramGroupPage=yes

; Output settings
OutputDir=Output
OutputBaseFilename={#TargetName}_v{#VersionNumber}_win{#TargetArch}_installer
Compression=lzma2
SolidCompression=yes
LZMAUseSeparateProcess=yes

; Visual settings
WizardStyle=modern
; SetupIconFile=..\resources\icons\de1.ico  ; Uncomment if you add a .ico file

; Privileges
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.InstallingVCRedist=Installing Visual C++ Runtime...

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main application - all files from deploy directory
Source: "{#AppDeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; VC++ Redistributable
Source: "{#VcRedistDir}\{#VcRedistFile}"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#TargetProduct}"; Filename: "{app}\{#TargetName}.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#TargetProduct}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#TargetProduct}"; Filename: "{app}\{#TargetName}.exe"; Tasks: desktopicon; WorkingDir: "{app}"

[Run]
; Install VC++ Runtime (silently, only if needed)
Filename: "{tmp}\{#VcRedistFile}"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:InstallingVCRedist}"; Flags: waituntilterminated skipifsilent

; Option to launch after install
Filename: "{app}\{#TargetName}.exe"; Description: "{cm:LaunchProgram,{#StringChange(TargetProduct, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Clean up any generated files
Type: filesandordirs; Name: "{app}"
