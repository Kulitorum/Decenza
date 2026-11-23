; User-specific paths - copy this file to setupvars.iss and modify
; setupvars.iss is not version controlled as it is user-specific

; Source code directory
#define SourceDir "C:\CODE\de1-qt"

; Build directory (where Decenza.exe is located after build)
#define AppBuildDir "C:\CODE\de1-qt\build\<your Qt Creator Release build dir>"

; Deploy directory (created automatically during installer build)
#define AppDeployDir "C:\CODE\de1-qt\installer\deploy"

; Qt installation directory, for the version in qt-version.txt
#define QtDir "C:\Qt\<version>\msvc2022_64"

; Directory for MSVC redistributable package (vcredist)
#define VcRedistDir "C:\Qt\vcredist"
#define VcRedistFile "vc14.50.35719_VC_redist.x64.exe"
