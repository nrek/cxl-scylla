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
$logPath = 'D:\projects\cxl-scylla\.tmpcl\terminal-build\ui-stress.log'
$previousLines = @(Get-Content $logPath -ErrorAction SilentlyContinue).Count
$p = Start-Process -FilePath 'D:\projects\cxl-scylla\.tmpcl\terminal-build\scylla.exe' -ArgumentList '--ui-stress-test','--terminal-ui-test' -WindowStyle Hidden -PassThru
if (!$p.WaitForExit(25000)) { throw 'Terminal UI validation timed out' }
$newLog = @(Get-Content $logPath | Select-Object -Skip $previousLines)
$newLog | Select-String 'PASS|FAIL'
if (!($newLog -match 'PASS terminal UI:') -or ($newLog -match 'FAIL')) { throw 'Terminal UI checks failed; see ui-stress.log' }
exit $p.ExitCode

