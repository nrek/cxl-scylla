@call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
@set TEMP=D:\projects\cxl-scylla\.tmpcl\minimap-temp
@set TMP=D:\projects\cxl-scylla\.tmpcl\minimap-temp
@cl.exe /nologo /std:c++20 /EHsc /Zs /DUNICODE /D_UNICODE /I D:\projects\cxl-scylla\scyllagpt\include /I D:\projects\cxl-scylla\scyllagpt\third_party\scintilla\include /I D:\projects\cxl-scylla\scyllagpt\third_party\lexilla\include /I D:\projects\cxl-scylla\scyllagpt\third_party\scintilla\src /I D:\projects\cxl-scylla\scyllagpt\third_party\scintilla\win32 /I D:\projects\cxl-scylla\scyllagpt\third_party\lexilla\lexlib D:\projects\cxl-scylla\scyllagpt\src\ui\editor_host.cpp
