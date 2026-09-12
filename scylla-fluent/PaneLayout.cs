namespace Scylla;

/// <summary>Port of scyllagpt::compute_panes — DIP breakpoints and mins must stay aligned.</summary>
internal sealed class PaneLayout
{
    public int Files { get; set; }
    public int Editor { get; set; }
    public int Agent { get; set; }
    public int History { get; set; }
    public int Splitter { get; set; } = 4;
    public bool ShowFiles { get; set; }
    public bool ShowEditor { get; set; } = true;
    public bool ShowAgent { get; set; } = true;
    public bool ShowHistory { get; set; }
    public bool NarrowTabs { get; set; }

    public static PaneLayout Compute(
        int clientW,
        int filesPref,
        int agentPref,
        int historyPref,
        bool focusEditor,
        int filesMode,
        int historyMode,
        int agentMode,
        int narrowTab)
    {
        var L = new PaneLayout();
        const int minF = 180, maxF = 600, minE = 400, minA = 300, minH = 180, maxH = 600, split = 4;

        L.NarrowTabs = !focusEditor && clientW < 860;
        L.ShowEditor = true;
        L.ShowAgent = true;
        L.ShowFiles = false;
        L.ShowHistory = false;

        if (focusEditor)
        {
            L.ShowFiles = false;
            L.ShowHistory = false;
            L.ShowAgent = true;
            L.ShowEditor = true;
        }
        else if (clientW >= 1280)
        {
            L.ShowFiles = filesMode != 2;
            L.ShowHistory = historyMode != 2;
        }
        else if (clientW >= 1080)
        {
            L.ShowFiles = filesMode != 2;
            L.ShowHistory = historyMode == 1;
        }
        else if (clientW >= 860)
        {
            L.ShowFiles = filesMode == 1;
            L.ShowHistory = historyMode == 1;
        }
        else
        {
            L.ShowFiles = filesMode == 1;
            L.ShowHistory = historyMode == 1;
            if (narrowTab == 1) L.ShowEditor = false;
            else L.ShowAgent = false;
        }

        // View → Chats: hide/show the chat log (agent pane) independently of Chat History.
        if (agentMode == 2) L.ShowAgent = false;
        else if (agentMode == 1) L.ShowAgent = true;

        int n = 0;
        if (L.ShowFiles) ++n;
        if (L.ShowEditor) ++n;
        if (L.ShowAgent) ++n;
        if (L.ShowHistory) ++n;
        var splits = Math.Max(0, n - 1);
        L.Splitter = split;
        var avail = Math.Max(1, clientW - splits * split);

        static int Clamp(int v, int lo, int hi) => v < lo ? lo : (v > hi ? hi : v);
        int Take(bool on, int pref, int mn, int mx = int.MaxValue) =>
            !on ? 0 : Clamp(pref, mn, Math.Min(mx, Math.Max(mn, avail)));

        L.Files = Take(L.ShowFiles, filesPref > 0 ? filesPref : 300, minF, maxF);
        L.Agent = Take(L.ShowAgent, agentPref > 0 ? agentPref : 650, minA);
        L.History = Take(L.ShowHistory, historyPref > 0 ? historyPref : 300, minH, maxH);

        var used = (L.ShowFiles ? L.Files : 0) + (L.ShowAgent ? L.Agent : 0) + (L.ShowHistory ? L.History : 0);
        var remain = avail - used;
        if (L.ShowEditor)
        {
            if (remain < minE)
            {
                var deficit = minE - remain;
                void Shrink(ref int col, bool on, int mn)
                {
                    if (deficit <= 0 || !on) return;
                    var can = col - mn;
                    var cut = Math.Min(can, deficit);
                    col -= cut;
                    deficit -= cut;
                }
                var hist = L.History;
                var files = L.Files;
                var agent = L.Agent;
                Shrink(ref hist, L.ShowHistory, minH);
                Shrink(ref files, L.ShowFiles, minF);
                Shrink(ref agent, L.ShowAgent, minA);
                L.History = hist;
                L.Files = files;
                L.Agent = agent;
                used = (L.ShowFiles ? L.Files : 0) + (L.ShowAgent ? L.Agent : 0) + (L.ShowHistory ? L.History : 0);
                remain = avail - used;
            }
            L.Editor = Math.Max(remain, L.ShowEditor ? minE : 0);
            if (!L.ShowEditor) L.Editor = 0;
        }
        else
        {
            L.Editor = 0;
            if (L.ShowAgent) L.Agent = Math.Max(L.Agent, remain + L.Agent);
        }

        var total = (L.ShowFiles ? L.Files : 0) + (L.ShowEditor ? L.Editor : 0) + (L.ShowAgent ? L.Agent : 0) +
                    (L.ShowHistory ? L.History : 0) + splits * split;
        if (total > clientW && L.ShowHistory)
            return Compute(clientW, filesPref, agentPref, 0, focusEditor, filesMode, 2, agentMode, narrowTab);
        if (total > clientW && L.ShowFiles)
            return Compute(clientW, filesPref, agentPref, historyPref, focusEditor, 2, historyMode, agentMode, narrowTab);
        return L;
    }
}
