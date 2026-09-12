using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Scylla;

internal static class NativeCore
{
    private const string Dll = "scylla-core";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_version")]
    private static extern IntPtr VersionPtr();

    public static string? Version()
    {
        var p = VersionPtr();
        return p == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(p);
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_try_acquire_instance")]
    public static extern int TryAcquireInstance();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_release_instance")]
    public static extern void ReleaseInstance();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_activate_existing")]
    public static extern int ActivateExisting();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_data_dir_utf8")]
    private static extern int DataDirUtf8Raw(byte[] buf, int bufLen);

    public static string? DataDirUtf8() => ReadBuf(DataDirUtf8Raw);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_settings_json")]
    private static extern int SettingsJsonRaw(byte[] buf, int bufLen);

    public static string? SettingsJson() => ReadBuf(SettingsJsonRaw, 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_security_json")]
    private static extern int SecurityJsonRaw(byte[] buf, int length);
    public static string? SecurityJson() => ReadBuf(SecurityJsonRaw, 1024 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_security_action")]
    private static extern int SecurityActionRaw(byte[] action, byte[] metadata, byte[] value, byte[] error, int length);
    public static bool SecurityAction(string action, string metadata, string value, out string error)
    {
        var secret = Encoding.UTF8.GetBytes(value + "\0");
        var output = new byte[2048];
        try {
            var ok = SecurityActionRaw(Encoding.UTF8.GetBytes(action + "\0"),
                Encoding.UTF8.GetBytes(metadata + "\0"), secret, output, output.Length) != 0;
            error = DecodeZ(output);
            return ok;
        } finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(secret); }
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_patch_settings")]
    private static extern int PatchSettingsRaw(byte[] jsonUtf8, byte[] err, int errLen);

    public static bool PatchSettings(string json, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(json + "\0");
        var err = new byte[2048];
        var ok = PatchSettingsRaw(payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_set_project_folder")]
    private static extern int SetProjectFolderRaw(byte[] pathUtf8, byte[] err, int errLen);

    public static bool SetProjectFolder(string path, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = SetProjectFolderRaw(payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_project_roots_json")]
    private static extern int ProjectRootsJsonRaw(byte[] buf, int bufLen);

    public static string? ProjectRootsJson() => ReadBuf(ProjectRootsJsonRaw, 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_pick_folders")]
    private static extern int PickFoldersRaw(IntPtr owner, byte[] buf, int bufLen);
    public static List<string> PickFolders(IntPtr owner)
    {
        var json = ReadBuf((b, n) => PickFoldersRaw(owner, b, n), 1024 * 1024);
        if (json is null) throw new InvalidOperationException("Could not open the folder picker.");
        return JsonSerializer.Deserialize<List<string>>(json) ?? new();
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_add_project_root")]
    private static extern int AddProjectRootRaw(byte[] pathUtf8, byte[] err, int errLen);

    public static bool AddProjectRoot(string path, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = AddProjectRootRaw(payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_remove_project_root")]
    private static extern int RemoveProjectRootRaw(byte[] pathUtf8, byte[] err, int errLen);

    public static bool RemoveProjectRoot(string path, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = RemoveProjectRootRaw(payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_knowledge_json")]
    private static extern int KnowledgeJsonRaw(byte[] buf, int bufLen);

    public static string? KnowledgeJson() => ReadBuf(KnowledgeJsonRaw, 128 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_knowledge_add")]
    private static extern int KnowledgeAddRaw(byte[] labelUtf8, byte[] pathUtf8, byte[] err, int errLen);

    public static bool KnowledgeAdd(string label, string path, out string error)
    {
        var labelBytes = Encoding.UTF8.GetBytes((label ?? "") + "\0");
        var pathBytes = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = KnowledgeAddRaw(labelBytes, pathBytes, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_knowledge_remove")]
    private static extern int KnowledgeRemoveRaw(byte[] idUtf8, byte[] err, int errLen);

    public static bool KnowledgeRemove(string id, out string error)
    {
        var idBytes = Encoding.UTF8.GetBytes(id + "\0");
        var err = new byte[2048];
        var ok = KnowledgeRemoveRaw(idBytes, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_knowledge_set_flags")]
    private static extern int KnowledgeSetFlagsRaw(byte[] idUtf8, int enabled, int agentAvailable, byte[] err, int errLen);

    public static bool KnowledgeSetFlags(string id, bool enabled, bool agentAvailable, out string error)
    {
        var idBytes = Encoding.UTF8.GetBytes(id + "\0");
        var err = new byte[2048];
        var ok = KnowledgeSetFlagsRaw(idBytes, enabled ? 1 : 0, agentAvailable ? 1 : 0, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_markdown_runs_json")]
    private static extern int MarkdownRunsJsonRaw(byte[] textUtf8, byte[] buf, int bufLen);

    public static string? MarkdownRunsJson(string text)
    {
        var payload = Encoding.UTF8.GetBytes(text + "\0");
        return ReadBuf((b, n) => MarkdownRunsJsonRaw(payload, b, n), 512 * 1024);
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_mcp_json")]
    private static extern int McpJsonRaw(byte[] buf, int bufLen);

    public static string? McpJson() => ReadBuf(McpJsonRaw, 128 * 1024);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_mcp_catalog_json")]
    private static extern int McpCatalogRaw(byte[] buf, int length);
    public static string? McpCatalogJson() => ReadBuf(McpCatalogRaw, 128 * 1024);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_mcp_scopes_json")]
    private static extern int McpScopesRaw(byte[] endpoint, byte[] buf, int length);
    public static string? McpScopesJson(string endpoint) => ReadBuf(
        (buf, length) => McpScopesRaw(Encoding.UTF8.GetBytes(endpoint + "\0"), buf, length), 128 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_file_mentions")]
    private static extern int FileMentionsRaw(byte[] query, byte[] buf, int length);
    public static string? FileMentions(string query) => ReadBuf(
        (buf, length) => FileMentionsRaw(Encoding.UTF8.GetBytes(query + "\0"), buf, length), 128 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_mcp_action")]
    private static extern int McpActionRaw(byte[] action, byte[] jsonUtf8, byte[] err, int errLen);

    public static bool McpAction(string action, string json, out string error)
    {
        var act = Encoding.UTF8.GetBytes(action + "\0");
        var payload = Encoding.UTF8.GetBytes(json + "\0");
        var err = new byte[2048];
        try {
            var ok = McpActionRaw(act, payload, err, err.Length) != 0;
            error = DecodeZ(err);
            return ok;
        } finally { Array.Clear(payload); }
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_strata_json")]
    private static extern int StrataJsonRaw(byte[] buf, int bufLen);

    public static string? StrataJson() => ReadBuf(StrataJsonRaw, 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_strata_action")]
    private static extern int StrataActionRaw(byte[] action, byte[] jsonUtf8, byte[] buf, int bufLen, byte[] err, int errLen);

    public static string? StrataAction(string action, string json, out string error)
    {
        var act = Encoding.UTF8.GetBytes(action + "\0");
        var payload = Encoding.UTF8.GetBytes(json + "\0");
        var err = new byte[2048];
        var buf = new byte[256 * 1024];
        var n = StrataActionRaw(act, payload, buf, buf.Length, err, err.Length);
        error = DecodeZ(err);
        if (n <= 0) return null;
        return Encoding.UTF8.GetString(buf, 0, n);
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_terminal_profiles_json")]
    private static extern int TerminalProfilesJsonRaw(byte[] buf, int bufLen);

    public static string? TerminalProfilesJson() => ReadBuf(TerminalProfilesJsonRaw, 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_terminal_profiles_save")]
    private static extern int TerminalProfilesSaveRaw(byte[] jsonUtf8, byte[] err, int errLen);

    public static bool TerminalProfilesSave(string json, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(json + "\0");
        var err = new byte[2048];
        var ok = TerminalProfilesSaveRaw(payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_core_list_dir")]
    private static extern int ListDirRaw(byte[] pathUtf8, byte[] buf, int bufLen);

    public static string? ListDir(string path)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        return ReadBuf((b, n) => ListDirRaw(payload, b, n), 512 * 1024);
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_create")]
    public static extern IntPtr SessionCreate();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_set_project_roots")]
    private static extern int SessionSetProjectRootsRaw(IntPtr session, byte[] json, byte[] err, int errLen);
    public static bool SessionSetProjectRoots(IntPtr session, IEnumerable<string> roots, out string error)
    {
        var err = new byte[2048];
        var ok = SessionSetProjectRootsRaw(session, Encoding.UTF8.GetBytes(JsonSerializer.Serialize(roots) + "\0"), err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_destroy")]
    public static extern void SessionDestroy(IntPtr session);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_start")]
    private static extern int SessionStartRaw(IntPtr session, byte[] err, int errLen);

    public static bool SessionStart(IntPtr session, out string error)
    {
        var err = new byte[2048];
        var ok = SessionStartRaw(session, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_stop")]
    public static extern void SessionStop(IntPtr session);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_send_user")]
    private static extern int SessionSendUserRaw(IntPtr session, byte[] textUtf8, byte[] err, int errLen);

    public static bool SessionSendUser(IntPtr session, string text, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(text + "\0");
        var err = new byte[2048];
        var ok = SessionSendUserRaw(session, payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_send_user_mode")]
    private static extern int SessionSendUserModeRaw(IntPtr session, byte[] text, byte[] mode, byte[] err, int errLen);
    public static bool SessionSendUserMode(IntPtr session, string text, string mode, out string error)
    {
        var err = new byte[2048];
        var ok = SessionSendUserModeRaw(session, Encoding.UTF8.GetBytes(text + "\0"), Encoding.UTF8.GetBytes(mode + "\0"), err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_cancel")]
    public static extern void SessionCancel(IntPtr session);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_new_chat")]
    private static extern int SessionNewChatRaw(IntPtr session, byte[] err, int errLen);

    public static bool SessionNewChat(IntPtr session, out string error)
    {
        var err = new byte[2048];
        var ok = SessionNewChatRaw(session, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_open_thread")]
    private static extern int SessionOpenThreadRaw(IntPtr session, byte[] idUtf8, byte[] err, int errLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_delete_thread")]
    private static extern int SessionDeleteThreadRaw(IntPtr session, byte[] idUtf8, byte[] err, int errLen);

    public static bool SessionDeleteThread(IntPtr session, string threadId, out string error)
    {
        var err = new byte[4096];
        var ok = SessionDeleteThreadRaw(session, Encoding.UTF8.GetBytes(threadId + "\0"), err, err.Length) != 0;
        error = Encoding.UTF8.GetString(err).TrimEnd('\0');
        return ok;
    }

    public static bool SessionOpenThread(IntPtr session, string threadId, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(threadId + "\0");
        var err = new byte[2048];
        var ok = SessionOpenThreadRaw(session, payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_poll")]
    private static extern int SessionPollRaw(IntPtr session, byte[] buf, int bufLen);

    public static string? SessionPoll(IntPtr session) =>
        ReadBuf((b, n) => SessionPollRaw(session, b, n), 2 * 1024 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_providers_json")]
    private static extern int SessionProvidersJsonRaw(IntPtr session, byte[] buf, int bufLen);

    public static string? SessionProvidersJson(IntPtr session) =>
        ReadBuf((b, n) => SessionProvidersJsonRaw(session, b, n), 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_session_provider_action")]
    private static extern int SessionProviderActionRaw(IntPtr session, byte[] providerId, byte[] action,
        byte[]? secretUtf8, byte[] err, int errLen);

    public static bool SessionProviderAction(IntPtr session, string providerId, string action,
        string? secret, out string error)
    {
        var pid = Encoding.UTF8.GetBytes(providerId + "\0");
        var act = Encoding.UTF8.GetBytes(action + "\0");
        byte[]? secretBytes = secret is null ? null : Encoding.UTF8.GetBytes(secret + "\0");
        var err = new byte[2048];
        var ok = SessionProviderActionRaw(session, pid, act, secretBytes, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_create")]
    public static extern IntPtr EditorCreate(IntPtr parentHwnd, int controlId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_destroy")]
    public static extern void EditorDestroy(IntPtr editorHwnd);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_move")]
    public static extern void EditorMove(IntPtr editorHwnd, int x, int y, int w, int h);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_load_path")]
    private static extern int EditorLoadPathRaw(IntPtr editorHwnd, byte[] pathUtf8, byte[] err, int errLen);

    public static bool EditorLoadPath(IntPtr editorHwnd, string path, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = EditorLoadPathRaw(editorHwnd, payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_save_path")]
    private static extern int EditorSavePathRaw(IntPtr editorHwnd, byte[] pathUtf8, byte[] err, int errLen);

    public static bool EditorSavePath(IntPtr editorHwnd, string path, out string error)
    {
        var payload = Encoding.UTF8.GetBytes(path + "\0");
        var err = new byte[2048];
        var ok = EditorSavePathRaw(editorHwnd, payload, err, err.Length) != 0;
        error = DecodeZ(err);
        return ok;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_apply_chrome")]
    public static extern void EditorApplyChrome(IntPtr editorHwnd, int dpi);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_set_word_wrap")]
    public static extern void EditorSetWordWrap(IntPtr editorHwnd, int wrap);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_set_whitespace")]
    public static extern void EditorSetWhitespace(IntPtr editorHwnd, int show);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_create_minimap")]
    public static extern IntPtr EditorCreateMinimap(IntPtr parentHwnd, IntPtr editorHwnd, int controlId, int dpi);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_editor_sync_minimap")]
    public static extern void EditorSyncMinimap(IntPtr editorHwnd, IntPtr minimapHwnd);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_create_profile")]
    private static extern IntPtr TerminalCreateRaw(IntPtr parentHwnd, IntPtr notifyHwnd, int controlId,
        byte[] cwdUtf8, byte[] profileUtf8, byte[] err, int errLen);

    public static IntPtr TerminalCreate(IntPtr parentHwnd, IntPtr notifyHwnd, int controlId, string cwd, out string error, string profileId = "")
    {
        var cwdBytes = Encoding.UTF8.GetBytes((cwd ?? "") + "\0");
        var err = new byte[2048];
        var handle = TerminalCreateRaw(parentHwnd, notifyHwnd, controlId, cwdBytes, Encoding.UTF8.GetBytes(profileId + "\0"), err, err.Length);
        error = DecodeZ(err);
        return handle;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_destroy")]
    public static extern void TerminalDestroy(IntPtr term);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_move")]
    public static extern void TerminalMove(IntPtr term, int x, int y, int w, int h);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_set_visible")]
    public static extern void TerminalSetVisible(IntPtr term, int visible);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_write_utf8")]
    private static extern int TerminalWriteUtf8Raw(IntPtr term, byte[] textUtf8);

    public static bool TerminalWrite(IntPtr term, string text)
    {
        var payload = Encoding.UTF8.GetBytes(text + "\0");
        return TerminalWriteUtf8Raw(term, payload) != 0;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_poll")]
    private static extern int TerminalPollRaw(IntPtr term, byte[] buf, int bufLen);

    public static string? TerminalPoll(IntPtr term) =>
        ReadBuf((b, n) => TerminalPollRaw(term, b, n), 64 * 1024);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_resize_pixels")]
    public static extern int TerminalResizePixels(IntPtr term, int w, int h);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "scylla_terminal_running")]
    public static extern int TerminalRunning(IntPtr term);

    private static string? ReadBuf(Func<byte[], int, int> fn, int size = 1024)
    {
        var buf = new byte[size];
        var n = fn(buf, buf.Length);
        if (n <= 0) return null;
        return Encoding.UTF8.GetString(buf, 0, n);
    }

    private static string DecodeZ(byte[] buf)
    {
        var n = Array.IndexOf(buf, (byte)0);
        if (n < 0) n = buf.Length;
        return Encoding.UTF8.GetString(buf, 0, n);
    }
}

internal sealed class SettingsSnapshot
{
    public string ProjectFolder { get; init; } = "";
    public string DefaultProvider { get; init; } = "";
    public string SelectedModel { get; init; } = "";
    public string ReasoningEffort { get; init; } = "";
    public string CodexPath { get; init; } = "";
    public int FilesW { get; init; } = 300;
    public int AgentW { get; init; } = 650;
    public int HistoryW { get; init; } = 300;
    public int FilesMode { get; init; }
    public int HistoryMode { get; init; }
    public int AgentMode { get; init; }
    public bool FocusEditor { get; init; }
    public int TerminalH { get; init; } = 220;
    public bool TerminalVisible { get; init; }
    public int KnowledgeH { get; init; }
    public bool WordWrap { get; init; }
    public bool ShowMinimap { get; init; }
    public bool VerboseAgentProgress { get; init; }
    public bool ShowWhitespace { get; init; }
    public bool EnterSends { get; init; } = true;
    public string AgentTerminalPolicy { get; init; } = "ask";
    public string DefaultTerminalProfileId { get; init; } = "";
    public string PanelSurface { get; init; } = "terminal";

    public static SettingsSnapshot Parse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json)) return new SettingsSnapshot();
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        return new SettingsSnapshot
        {
            ProjectFolder = Str(root, "project_folder"),
            DefaultProvider = Str(root, "default_provider"),
            SelectedModel = Str(root, "selected_model"),
            ReasoningEffort = Str(root, "reasoning_effort"),
            CodexPath = Str(root, "codex_path"),
            FilesW = Int(root, "files_w", 300),
            AgentW = Int(root, "agent_w", 650),
            HistoryW = Int(root, "history_w", 300),
            FilesMode = Int(root, "files_mode", 0),
            HistoryMode = Int(root, "history_mode", 0),
            AgentMode = Int(root, "agent_mode", 0),
            FocusEditor = Bool(root, "focus_editor"),
            TerminalH = Int(root, "terminal_h", 220),
            TerminalVisible = Bool(root, "terminal_visible"),
            KnowledgeH = Int(root, "knowledge_h", 0),
            WordWrap = Bool(root, "word_wrap"),
            ShowMinimap = Bool(root, "show_minimap"),
            VerboseAgentProgress = Bool(root, "verbose_agent_progress"),
            ShowWhitespace = Bool(root, "show_whitespace"),
            EnterSends = !root.TryGetProperty("enter_sends", out var es) || es.ValueKind != JsonValueKind.False,
            AgentTerminalPolicy = Str(root, "agent_terminal_policy"),
            DefaultTerminalProfileId = Str(root, "default_terminal_profile_id"),
            PanelSurface = Str(root, "panel_surface"),
        };
    }

    private static string Str(JsonElement root, string name) =>
        root.TryGetProperty(name, out var e) ? e.GetString() ?? "" : "";

    private static int Int(JsonElement root, string name, int fallback) =>
        root.TryGetProperty(name, out var e) && e.TryGetInt32(out var v) ? v : fallback;

    private static bool Bool(JsonElement root, string name) =>
        root.TryGetProperty(name, out var e) && e.ValueKind == JsonValueKind.True;
}

internal sealed class SessionSnapshot
{
    public string PayloadLog { get; init; } = "";
    public string LastPlanPath { get; init; } = "";
    public bool AccountLoaded { get; init; }
    public string State { get; init; } = "offline";
    public string Status { get; init; } = "";
    public bool RuntimeLive { get; init; }
    public string Activity { get; init; } = "";
    public string Stream { get; init; } = "";
    public string ActiveThreadId { get; init; } = "";
    public string SelectedModel { get; init; } = "";
    public string ReasoningEffort { get; init; } = "";
    public string DefaultProvider { get; init; } = "";
    public string LastError { get; init; } = "";
    public string AccountEmail { get; init; } = "";
    public bool AccountSignedIn { get; init; }
    public string ProjectFolder { get; init; } = "";
    public string GrantLabel { get; init; } = "";
    public List<HistoryRow> History { get; init; } = new();
    public List<ThreadRow> Threads { get; init; } = new();
    public List<string> ProjectRoots { get; init; } = new();
    public List<ModelRow> Models { get; init; } = new();

    public IReadOnlyList<HistoryRow> DisplayHistory(bool verboseAgentProgress)
    {
        // Transient presentation only; completed replies are persisted by the session.
        var generating = string.Equals(State, "generating", StringComparison.OrdinalIgnoreCase)
            || string.Equals(State, "awaiting_action", StringComparison.OrdinalIgnoreCase);
        if (!verboseAgentProgress || !generating || string.IsNullOrEmpty(Stream)) return History;
        return History.Concat(new[] { new HistoryRow { Text = Stream } }).ToList();
    }

    public static SessionSnapshot? Parse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json)) return null;
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        var history = new List<HistoryRow>();
        if (root.TryGetProperty("history", out var hist) && hist.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in hist.EnumerateArray())
            {
                history.Add(new HistoryRow
                {
                    User = item.TryGetProperty("user", out var u) && u.GetBoolean(),
                    Text = item.TryGetProperty("text", out var t) ? t.GetString() ?? "" : "",
                    Attachments = item.TryGetProperty("attachments", out var attachments)
                        ? JsonSerializer.Deserialize<List<ChatAttachment>>(attachments.GetRawText()) ?? new()
                        : new(),
                });
            }
        }
        var threads = new List<ThreadRow>();
        if (root.TryGetProperty("threads", out var th) && th.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in th.EnumerateArray())
            {
                threads.Add(new ThreadRow
                {
                    Id = item.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
                    Name = item.TryGetProperty("name", out var name) ? name.GetString() ?? "" : "",
                    Preview = item.TryGetProperty("preview", out var preview) ? preview.GetString() ?? "" : "",
                    ProjectPaths = Strings(item, "project_paths"),
                    Busy = item.TryGetProperty("busy", out var busy) && busy.ValueKind == JsonValueKind.True,
                    ActivityPhase = Str(item, "activity_phase"),
                    ActiveAgents = item.TryGetProperty("active_agents", out var agents) && agents.TryGetInt32(out var count) ? count : 0,
                    UpdatedAt = item.TryGetProperty("updated_at", out var ua) && ua.TryGetInt64(out var secs)
                        ? secs
                        : 0,
                });
            }
        }
        var models = new List<ModelRow>();
        if (root.TryGetProperty("models", out var md) && md.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in md.EnumerateArray())
            {
                models.Add(new ModelRow
                {
                    Id = item.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
                    Label = item.TryGetProperty("label", out var label) ? label.GetString() ?? "" : "",
                    Specialty = Str(item, "specialty"),
                    DefaultReasoningEffort = Str(item, "default_reasoning_effort"),
                    ReasoningEfforts = Strings(item, "reasoning_efforts"),
                });
            }
        }
        return new SessionSnapshot
        {
            PayloadLog = Str(root, "payload_log"),
            State = Str(root, "state"),
            Status = Str(root, "status"),
            RuntimeLive = root.TryGetProperty("runtime_live", out var rl) && rl.GetBoolean(),
            Activity = Str(root, "activity"),
            Stream = Str(root, "stream"),
            ActiveThreadId = Str(root, "active_thread_id"),
            SelectedModel = Str(root, "selected_model"),
            ReasoningEffort = Str(root, "reasoning_effort"),
            DefaultProvider = Str(root, "default_provider"),
            LastError = Str(root, "last_error"),
            LastPlanPath = Str(root, "last_plan_path"),
            AccountEmail = Str(root, "account_email"),
            AccountSignedIn = root.TryGetProperty("account_signed_in", out var si) && si.GetBoolean(),
            AccountLoaded = root.TryGetProperty("account_loaded", out var al) && al.GetBoolean(),
            ProjectFolder = Str(root, "project_folder"),
            GrantLabel = Str(root, "grant_label"),
            History = history,
            Threads = threads,
            ProjectRoots = Strings(root, "project_roots"),
            Models = models,
        };
    }

    private static List<string> Strings(JsonElement root, string name) =>
        root.TryGetProperty(name, out var values) && values.ValueKind == JsonValueKind.Array
            ? values.EnumerateArray().Where(v => v.ValueKind == JsonValueKind.String)
                .Select(v => v.GetString() ?? "").Where(v => v.Length > 0).ToList() : new();

    private static string Str(JsonElement root, string name) =>
        root.TryGetProperty(name, out var e) ? e.GetString() ?? "" : "";
}

internal sealed class HistoryRow
{
    public List<ChatAttachment> Attachments { get; init; } = new();
    public bool User { get; init; }
    public string Text { get; init; } = "";
}

internal sealed class ThreadRow
{
    public List<string> ProjectPaths { get; init; } = new();
    public bool Busy { get; init; }
    public string ActivityPhase { get; init; } = "";
    public int ActiveAgents { get; init; }
    public string Id { get; init; } = "";
    public string Name { get; init; } = "";
    public string Preview { get; init; } = "";

    /// <summary>Unix seconds from the workspace store; 0 when the thread has never been persisted.</summary>
    public long UpdatedAt { get; init; }

    public DateTime LocalUpdated => UpdatedAt <= 0
        ? DateTime.MinValue
        : DateTimeOffset.FromUnixTimeSeconds(UpdatedAt).ToLocalTime().DateTime;
}

internal sealed class ModelRow
{
    public string Id { get; init; } = "";
    public string Label { get; init; } = "";
    public string Specialty { get; init; } = "";
    public string OptionLabel => Specialty.ToLowerInvariant() switch
    {
        "language" or "lang" => Label + " (lang)",
        "code" or "coding" or "programming" => Label + " (code)",
        _ => Label,
    };
    public bool Enabled { get; init; }
    public string DefaultReasoningEffort { get; init; } = "";
    public List<string> ReasoningEfforts { get; init; } = new();
}

internal sealed class EffortRow
{
    public string Id { get; init; } = "";
    public string Label { get; init; } = "";
}

internal sealed class ProviderCard
{
    public List<ModelRow> Models { get; init; } = new();
    public string Id { get; init; } = "";
    public string DisplayName { get; init; } = "";
    public bool Connected { get; init; }
    public string AuthLabel { get; init; } = "";
    public string Detail { get; init; } = "";
    public string Product { get; init; } = "";
    public bool ClaudeCliPresent { get; init; }

    public static List<ProviderCard> ParseList(string? json)
    {
        var list = new List<ProviderCard>();
        if (string.IsNullOrWhiteSpace(json)) return list;
        using var doc = JsonDocument.Parse(json);
        if (doc.RootElement.ValueKind != JsonValueKind.Array) return list;
        foreach (var item in doc.RootElement.EnumerateArray())
        {
            list.Add(new ProviderCard
            {
                Id = item.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
                DisplayName = item.TryGetProperty("display_name", out var dn) ? dn.GetString() ?? "" : "",
                Models = item.TryGetProperty("models", out var models) && models.ValueKind == JsonValueKind.Array
                    ? models.EnumerateArray().Select(m => new ModelRow
                    {
                        Id = m.GetProperty("id").GetString() ?? "",
                        Label = m.GetProperty("label").GetString() ?? "",
                        Specialty = m.TryGetProperty("specialty", out var specialty) ? specialty.GetString() ?? "" : "",
                        Enabled = m.GetProperty("enabled").GetBoolean(),
                        DefaultReasoningEffort = m.TryGetProperty("default_reasoning_effort", out var dre)
                            ? dre.GetString() ?? "" : "",
                        ReasoningEfforts = m.TryGetProperty("reasoning_efforts", out var efforts) && efforts.ValueKind == JsonValueKind.Array
                            ? efforts.EnumerateArray().Where(v => v.ValueKind == JsonValueKind.String)
                                .Select(v => v.GetString() ?? "").Where(v => v.Length > 0).ToList()
                            : new(),
                    }).ToList() : new(),
                Connected = item.TryGetProperty("connected", out var c) && c.ValueKind == JsonValueKind.True,
                AuthLabel = item.TryGetProperty("auth_label", out var al) ? al.GetString() ?? "" : "",
                Detail = item.TryGetProperty("detail", out var d) ? d.GetString() ?? "" : "",
                Product = item.TryGetProperty("product", out var p) ? p.GetString() ?? "" : "",
                ClaudeCliPresent = item.TryGetProperty("claude_cli_present", out var cli) &&
                                   cli.ValueKind == JsonValueKind.True,
            });
        }
        return list;
    }
}

internal sealed class DirEntry
{
    public string Name { get; init; } = "";
    public string Path { get; init; } = "";
    public bool IsDir { get; init; }

    public static List<DirEntry> ParseList(string? json)
    {
        var list = new List<DirEntry>();
        if (string.IsNullOrWhiteSpace(json)) return list;
        using var doc = JsonDocument.Parse(json);
        if (doc.RootElement.ValueKind != JsonValueKind.Array) return list;
        foreach (var item in doc.RootElement.EnumerateArray())
        {
            list.Add(new DirEntry
            {
                Name = item.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "",
                Path = item.TryGetProperty("path", out var p) ? p.GetString() ?? "" : "",
                IsDir = item.TryGetProperty("is_dir", out var d) && d.GetBoolean(),
            });
        }
        return list.OrderByDescending(e => e.IsDir).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase).ToList();
    }
}

internal sealed class ProjectRootRow
{
    public string Path { get; init; } = "";
    public string Name { get; init; } = "";

    public static List<ProjectRootRow> Parse(string? json)
    {
        var list = new List<ProjectRootRow>();
        if (string.IsNullOrWhiteSpace(json)) return list;
        using var doc = JsonDocument.Parse(json);
        if (!doc.RootElement.TryGetProperty("roots", out var roots) || roots.ValueKind != JsonValueKind.Array)
            return list;
        foreach (var item in roots.EnumerateArray())
        {
            list.Add(new ProjectRootRow
            {
                Path = item.TryGetProperty("path", out var p) ? p.GetString() ?? "" : "",
                Name = item.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "",
            });
        }
        return list;
    }
}

internal sealed class KnowledgeRow
{
    public string Id { get; init; } = "";
    public string Label { get; init; } = "";
    public string Path { get; init; } = "";
    public bool Enabled { get; init; } = true;
    public bool AgentAvailable { get; init; } = true;

    public static List<KnowledgeRow> ParseList(string? json)
    {
        var list = new List<KnowledgeRow>();
        if (string.IsNullOrWhiteSpace(json)) return list;
        using var doc = JsonDocument.Parse(json);
        if (doc.RootElement.ValueKind != JsonValueKind.Array) return list;
        foreach (var item in doc.RootElement.EnumerateArray())
        {
            list.Add(new KnowledgeRow
            {
                Id = item.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
                Label = item.TryGetProperty("label", out var l) ? l.GetString() ?? "" : "",
                Path = item.TryGetProperty("path", out var p) ? p.GetString() ?? "" : "",
                Enabled = !item.TryGetProperty("enabled", out var e) || e.ValueKind != JsonValueKind.False,
                AgentAvailable = !item.TryGetProperty("agent_available", out var a) || a.ValueKind != JsonValueKind.False,
            });
        }
        return list;
    }
}
