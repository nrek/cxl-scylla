using Scylla;

static void Check(bool value, string name) { if (!value) throw new Exception(name); Console.WriteLine("PASS " + name); }
var state = new ChatHistoryState();
const string a = @"D:\projects\cxl-scylla", b = @"D:\projects\cxl-sentinel", c = @"D:\projects\cxl-spore";
state.SetRoots(new[] { a, b, c, a.ToUpperInvariant() });
Check(state.Roots.Count == 3 && state.Heading == "CHATS (3)", "deduplicate roots and select initial folders");
var shared = new ThreadRow { Id = "shared", ProjectPaths = new() { a, b } };
Check(state.Matches(shared).Count() == 2, "shared chat has two matching dots");
state.Selected.Remove(b);
Check(state.Matches(shared).SequenceEqual(new[] { a }), "only selected matching project has a dot");
state.Selected.Clear();
Check(state.Heading == "CHATS" && !state.Matches(shared).Any(), "zero selection shows no chats");
state.Selected.Add(a);
Check(state.Heading == "CHATS", "one selection has no count");
Check(!state.SetRoots(new[] { a, b, c }) && state.Selected.Count == 1, "poll preserves deliberate selection");
var colors = state.Roots.Select(state.Color).ToArray();
Check(colors.Distinct().Count() == 3 && state.Color(a.ToUpperInvariant()) == colors[0], "unique case-insensitive project colors");
state.SetRoots(new[] { c, a });
Check(state.Color(a) == colors[0] && state.Color(c) == colors[2], "reordering roots preserves colors");
Check(state.Selected.SetEquals(new[] { a }), "removing roots preserves remaining selection");
Check(!state.Matches(new ThreadRow { ProjectPaths = new() { @"D:\projects\closed" } }).Any(), "closed roots excluded");
var busy = new ThreadRow { Id = "job", Busy = true, ActiveAgents = 2 };
state.Observe(new[] { busy }, "other");
Check(state.Status(busy) == "Working · 2 agents", "background subagent work visible");
var done = new ThreadRow { Id = "job", ActivityPhase = "Completed" };
state.Observe(new[] { done }, "other");
state.Observe(new[] { done }, "other");
Check(state.Status(done) == "Completed", "completion survives repeated polls and filtering");
state.Observe(new[] { done }, "job");
state.Observe(new[] { done }, "other");
Check(state.Status(done) == "", "visiting acknowledges completion without reappearing");
state.Observe(new[] { busy }, "other");
var failed = new ThreadRow { Id = "job", ActivityPhase = "Failed: network" };
state.Observe(new[] { failed }, "other");
Check(state.Status(failed) == "Failed", "failure is not successful completion");
Check(state.Status(new ThreadRow { Busy = true, ActivityPhase = "Waiting for approval" }) == "Needs approval", "approval label");
var snap = SessionSnapshot.Parse("""
{"project_roots":["root"],"threads":[{"id":"t","project_paths":["root"],"busy":true,"activity_phase":"Working","active_agents":3}]}
""")!;
Check(snap.ProjectRoots.Single() == "root" && snap.Threads[0].ProjectPaths.Single() == "root"
    && snap.Threads[0].Busy && snap.Threads[0].ActiveAgents == 3, "native snapshot contract");
Check(SessionSnapshot.Parse("{\"threads\":[{\"id\":\"old\"}]}")!.Threads[0].ProjectPaths.Count == 0, "legacy snapshot has safe defaults");
