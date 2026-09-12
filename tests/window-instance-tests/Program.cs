using Scylla;

var id = "0123456789abcdef0123456789abcdef";
Expect(WindowInstance.ParseId(["scylla.exe", WindowInstance.ArgumentPrefix + id]) == id,
    "valid id parses");
Expect(WindowInstance.ParseId([WindowInstance.ArgumentPrefix + "..\\escape"]) is null,
    "invalid id rejected");
Expect(WindowInstance.ParseId(["--something-else"]) is null, "ordinary launch remains primary");

var root = WindowInstance.WindowDataRoot(@"C:\state\ScyllaGPT", id);
Expect(root == @"C:\state\ScyllaGPT\windows\0123456789abcdef0123456789abcdef",
    "window state root isolated");

var start = WindowInstance.CreateStartInfo(@"C:\Scylla\scylla.exe", id);
Expect(start.FileName == @"C:\Scylla\scylla.exe", "new process uses Scylla executable");
Expect(start.ArgumentList.Single() == WindowInstance.ArgumentPrefix + id, "new process receives id argument");
Expect(start.Environment[WindowInstance.EnvironmentVariable] == id, "new process environment is explicit");

Console.WriteLine("window instance tests passed");

static void Expect(bool condition, string name)
{
    if (!condition) throw new InvalidOperationException("FAIL " + name);
    Console.WriteLine("ok   " + name);
}
