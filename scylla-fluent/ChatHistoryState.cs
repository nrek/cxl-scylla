namespace Scylla;

// Presentation state survives filtering and switching chats, but never changes workspace grants.
internal sealed class ChatHistoryState
{
    public List<string> Roots { get; private set; } = new();
    public HashSet<string> Selected { get; } = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string> _seen = new();
    private readonly Dictionary<string, string> _pending = new();
    private readonly Dictionary<string, uint> _colors = new(StringComparer.OrdinalIgnoreCase);
    public string Heading => Selected.Count > 1 ? $"CHATS ({Selected.Count})" : "CHATS";
    public bool SetRoots(IEnumerable<string> roots)
    {
        var next = roots.Where(p => !string.IsNullOrWhiteSpace(p)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        if (Roots.SequenceEqual(next, StringComparer.OrdinalIgnoreCase)) return false;
        foreach (var path in next.Except(Roots, StringComparer.OrdinalIgnoreCase)) Selected.Add(path);
        Selected.IntersectWith(next);
        Roots = next;
        foreach (var path in Roots.OrderBy(p => p, StringComparer.OrdinalIgnoreCase)) Color(path);
        return true;
    }
    public IEnumerable<string> Matches(ThreadRow row) => Roots.Where(p => Selected.Contains(p)
        && row.ProjectPaths.Contains(p, StringComparer.OrdinalIgnoreCase));
    public void Observe(IEnumerable<ThreadRow> rows, string active)
    {
        foreach (var row in rows)
        {
            var signature = row.Busy ? "busy" : row.ActivityPhase;
            if (row.Busy) _pending.Remove(row.Id);
            else if (row.ActivityPhase.Length > 0 && (!_seen.TryGetValue(row.Id, out var old) || old != signature))
                _pending[row.Id] = row.ActivityPhase;
            _seen[row.Id] = signature;
        }
        _pending.Remove(active);
    }
    public void Acknowledge(string id) => _pending.Remove(id);
    public string Status(ThreadRow row)
    {
        if (row.Busy)
            return row.ActivityPhase.Contains("approval", StringComparison.OrdinalIgnoreCase) ? "Needs approval"
                : row.ActiveAgents > 0 ? $"Working · {row.ActiveAgents} agents" : "Working";
        if (!_pending.TryGetValue(row.Id, out var phase)) return "";
        return phase.StartsWith("Failed", StringComparison.OrdinalIgnoreCase) ? "Failed"
            : phase == "Completed" ? "Completed" : phase;
    }
    public uint Color(string path)
    {
        if (_colors.TryGetValue(path, out var color)) return color;
        uint hash = 2166136261;
        foreach (var c in path.TrimEnd('\\', '/').ToUpperInvariant()) hash = unchecked((hash ^ c) * 16777619);
        // Bright, readable RGB colors; avoid assigning the same dot to two roots.
        color = 0xFF000000u | (uint)(80 + (hash & 127)) << 16
            | (uint)(80 + ((hash >> 8) & 127)) << 8 | (uint)(80 + ((hash >> 16) & 127));
        while (_colors.Values.Contains(color)) color = 0xFF000000u | ((color + 0x00171329u) & 0xFFFFFFu);
        _colors[path] = color;
        return color;
    }
}
