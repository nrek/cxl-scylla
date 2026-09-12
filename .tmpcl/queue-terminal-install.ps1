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
$ErrorActionPreference = 'Stop'
$app = Get-Process -Name scylla | Where-Object Path -EQ 'D:\projects\cxl-scylla\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0\scylla.exe'
if (!$app) { throw 'Normal Scylla process was not found' }
$helper = Start-Process -FilePath 'C:\Program Files\PowerShell\7\pwsh.exe' -ArgumentList '-NoProfile','-File','D:\projects\cxl-scylla\tests\install-terminal-fix.ps1','-WaitForProcessId',$app.Id -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
if ($helper.HasExited) { throw 'Installer helper exited before app shutdown' }
"Installer helper $($helper.Id) waiting for Scylla $($app.Id) to close normally."

