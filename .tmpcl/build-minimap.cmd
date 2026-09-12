@set PATH=C:\Windows\System32;C:\Windows\System32\WindowsPowerShell\v1.0;%PATH%
@set TEMP=D:\projects\cxl-scylla\.tmpcl\minimap-temp
@set TMP=D:\projects\cxl-scylla\.tmpcl\minimap-temp
@call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
@"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S D:\projects\cxl-scylla -B D:\projects\cxl-scylla\.tmpcl\minimap-ninja -G Ninja -DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -DCMAKE_BUILD_TYPE=Release
@if errorlevel 1 exit /b %errorlevel%
@"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:\projects\cxl-scylla\.tmpcl\minimap-ninja --target scylla-core-shared scyllagpt-settings-policy-tests
