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
$env:PATH='C:\Windows\System32;C:\Windows'
& 'C:/Program Files/dotnet/dotnet.exe' run --project tests/fluent-progress-tests/ProgressTests.csproj --no-restore
exit $LASTEXITCODE

