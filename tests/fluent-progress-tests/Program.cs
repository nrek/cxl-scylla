using Scylla;

if (SessionSnapshot.Parse("{\"payload_log\":\"12:00:00Z IN tool/call\"}")?.PayloadLog != "12:00:00Z IN tool/call")
    throw new Exception("payload snapshot transport");
if (SessionSnapshot.Parse("{}")?.PayloadLog != "") throw new Exception("legacy payload snapshot");
Console.WriteLine("PASS payload snapshot transport and legacy fallback");

static void Check(bool condition, string name)
{
    if (!condition) throw new Exception(name);
    Console.WriteLine("PASS " + name);
}

Check(!SettingsSnapshot.Parse("{}").VerboseAgentProgress, "older settings default off");
Check(SettingsSnapshot.Parse("{\"reasoning_effort\":\"high\"}").ReasoningEffort == "high", "reasoning effort preference parses");
Check(SettingsSnapshot.Parse("{\"verbose_agent_progress\":true}").VerboseAgentProgress, "enabled preference parses");
Check(!SettingsSnapshot.Parse("{\"verbose_agent_progress\":false}").VerboseAgentProgress, "disabled preference parses");
var depth = SessionSnapshot.Parse("{\"reasoning_effort\":\"medium\",\"models\":[{\"id\":\"gpt-depth\",\"label\":\"GPT Depth\",\"default_reasoning_effort\":\"low\",\"reasoning_efforts\":[\"minimal\",\"low\",\"medium\",\"high\",\"xhigh\"]}]}");
Check(depth?.ReasoningEffort == "medium", "selected reasoning effort parses");
Check(depth?.Models.Single().ReasoningEfforts.SequenceEqual(new[] { "minimal", "low", "medium", "high", "xhigh" }) == true,
    "model-specific reasoning efforts parse in provider order");
var specialties = SessionSnapshot.Parse("{\"models\":[{\"id\":\"sol\",\"label\":\"GPT-5.6-Sol\",\"specialty\":\"\"},{\"id\":\"writer\",\"label\":\"Writer\",\"specialty\":\"language\"},{\"id\":\"coder\",\"label\":\"Coder\",\"specialty\":\"code\"}]}")!.Models;
Check(specialties[0].OptionLabel == "GPT-5.6-Sol", "general model has no purpose suffix");
Check(specialties[1].OptionLabel == "Writer (lang)", "language model label has lang suffix");
Check(specialties[2].OptionLabel == "Coder (code)", "programming model label has code suffix");
var user = new HistoryRow { User = true, Text = "Inspect the project" };
var working = new SessionSnapshot { State = "generating", History = new() { user }, Stream = "Reading files" };
Check(ReferenceEquals(working.DisplayHistory(false), working.History), "off preserves original transcript");
var visible = working.DisplayHistory(true);
Check(visible.Count == 2 && visible[1].Text == "Reading files" && !visible[1].User, "on displays live reply");
Check(working.History.Count == 1 && ReferenceEquals(visible[0], user), "live rendering preserves stored history");
Check(working.DisplayHistory(true).Count == 2, "repeated polls do not duplicate live reply");
Check(working.DisplayHistory(false).Count == 1, "switching off hides live reply");
foreach (var state in new[] { "ready", "failed", "interrupted" }) {
    var completed = new SessionSnapshot { State = state, History = new() { user, new() { Text = "Reading files" } }, Stream = "Reading files" };
    Check(completed.DisplayHistory(true).Count == 2, state + " does not duplicate completed or partial reply");
}
var awaiting = new SessionSnapshot { State = "awaiting_action", Stream = "Waiting for approval" };
Check(awaiting.DisplayHistory(true).Count == 1, "waiting for approval retains progress");
Check(new SessionSnapshot { State = "generating" }.DisplayHistory(true).Count == 0, "empty stream has no placeholder reply");
Check(new SessionSnapshot { ActiveThreadId = "other" }.DisplayHistory(true).Count == 0, "switching threads does not retain previous progress");
