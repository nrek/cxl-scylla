$env:SystemRoot='C:\Windows'
$env:WINDIR='C:\Windows'
$env:SystemDrive='C:'
$env:USERPROFILE='C:\Users\nrek0'
$env:LOCALAPPDATA='C:\Users\nrek0\AppData\Local'
$env:APPDATA='C:\Users\nrek0\AppData\Roaming'
$env:ProgramData='C:\ProgramData'
$env:ALLUSERSPROFILE='C:\ProgramData'
$env:ProgramFiles='C:\Program Files'
${env:ProgramFiles(x86)}='C:\Program Files (x86)'
$env:TEMP='D:\projects\cxl-scylla\.tmpcl'
$env:TMP=$env:TEMP
$env:PATH='C:\Windows\System32;C:\Windows;C:\Program Files\dotnet'
& 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe' scylla-fluent/Scylla.csproj /p:Configuration=Release /p:Platform=x64 /p:OutDir=D:/projects/cxl-scylla/.tmpcl/terminal-navigation/ /verbosity:minimal /nologo
exit $LASTEXITCODE




