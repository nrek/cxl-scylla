# Scylla desktop application (`scylla.exe`)

C# / WinUI 3 unpackaged app. Workbench-class IDE over `scylla-core` (Files | Editor | Agent | History).

## Build

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build D:\projects\cxl-scylla\build --config Release --target scylla-core-shared

# Close Scylla before building so its loaded native DLL can be replaced.
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  D:\projects\cxl-scylla\scylla-fluent\Scylla.csproj /p:Configuration=Release /p:Platform=x64 /restore

$out = 'D:\projects\cxl-scylla\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0'
& "$out\scylla.exe"
```

Fluent builds deploy both `scylla-core.dll` and `scylla-broker.exe` from `build/<Configuration>`
and fail if either is missing. Override `ScyllaNativeOutputDir` for a different native build directory.
The broker must be beside `scylla.exe` when a session starts to register database and SSH tools.
After repairing a missing helper, restart Scylla to register those tools with the agent.

## Browsing and chat modes

- Chat and Markdown preview render shaded code fences, amber code text, teal clickable links, tables, quotations, and dividers. Local file links open in the editor (including `:line` destinations); HTTP(S) links open the external browser. Preview-relative links resolve beside the document; chat-relative links resolve from the project folder. Missing files display an error. Assistant rows omit a redundant speaker label.
- Review [the formatting fixture](../tests/chat-editor-formatting.md) in Markdown preview to check tables, links, code, and Mermaid together.

- Click **KNOWLEDGE** to give its tree the browser's height; **FILES** stays available above it.
- **FILES → Manage** opens folder visibility controls. Unchecked folders remain registered and can be shown again. **Add Folders** supports multiple folder selection. Open project roots are also the agent workspace grant; Knowledge Agent toggles are managed under **KNOWLEDGE → Manage**.
- The composer footer contains **Execute / Plan / Ask**, the model selector, and Send. Large, structured Execute requests offer a switch to Plan before sending.
- **Plan** and **Ask** currently require the ChatGPT provider. Both use a read-only sandbox and deny write escalation. Plan saves the completed final Markdown response under the app's `knowledge-plans` directory and registers it as project Knowledge. Ask only returns findings. Cancelled or failed plans are not saved.
- Saved ChatGPT sign-in is checked when the runtime starts on launch. The composer offers provider sign-in actions after account status loads; the top account strip is removed. See [OpenAI authentication](https://developers.openai.com/codex/auth) and [app-server account APIs](https://developers.openai.com/codex/app-server).
- Markdown Preview repairs common garbled punctuation for display, preserving source and code blocks.
- Markdown Preview displays `![alt text](images/photo.png)` images from paths relative to the Markdown file, absolute file paths, and HTTP(S) URLs. Use `<images/my photo.png>` for paths containing spaces. Images fit the pane, retain their proportions, and show alt text on failure; optional quoted titles appear as tooltips. Raster formats use Windows image codecs and SVG uses the native SVG renderer. See [the image fixture](../tests/markdown-images.md).

Validation: run `dotnet run --project ../tests/fluent-preview-tests/PreviewTests.csproj` from this directory and the native `scyllagpt-tests` target. Live checks: toggle Files/Knowledge, hide/show folders and restart, open the multi-folder picker, resize with a Markdown/Mermaid preview visible, and verify saved sign-in after relaunch. Send Plan and Ask turns, then check that only a successfully completed Plan creates a Knowledge artifact.

## Pane controls and file management

Files and Knowledge use borderless section buttons and icon-only settings controls. Knowledge and the editor content have 1px top dividers. The composer and history use compact solid icons; Markdown prose uses 17.6px line height with 24px above and 17.6px below headings.

Right-click files and folders for open, find text/filenames, new file/folder, rename, copy path, reveal in Explorer, browser, and confirmed Recycle Bin deletion. Find skips reparse points, searches content in files up to 2 MiB, returns at most 200 matching files, and can be cancelled. Browser launch currently detects Edge, Chrome, or Firefox. Renaming updates open editor paths; deleting preserves open buffers and blocks saving to the deleted path.

Chat History offers confirmed local deletion. Deletion erases local title, preview, and messages, retains an identifier tombstone to suppress provider rediscovery, and leaves provider-side history untouched. Busy chats must be stopped first. Disconnected services expose Sign In in the composer selector and use their existing login/key connection flows.

`scylla.exe` is the only supported desktop application. The retired native Win32 Workbench is not part of the build or distribution.
For brokered SSH commands, use Settings → Security → SSH Connections. Create an alias such as
`ssh_synq`, choose an enabled WSL profile, and select the Keyring username, private key and optional
key passphrase. Supply the pinned host public key. The selected distribution needs Python 3 and
OpenSSH. Connection command permission must be Auto and Terminal settings must allow agent
execution; otherwise calls are refused. Then ask the agent to use `@ssh_synq`; its `scylla_ssh`
tool returns the command exit code, stdout and stderr. This is a noninteractive command session.
