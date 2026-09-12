using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Scylla;

// Human-only settings. Values are write-only; snapshots contain names and descriptions.
internal sealed class SecuritySettingsPane : StackPanel
{
    private readonly TextBlock _status = Design.Caption("");
    private readonly StackPanel _body = new() { Spacing = 16 };
    private readonly ContentControl _bodyHost = new() { HorizontalContentAlignment = HorizontalAlignment.Stretch };
    private bool _busy;
    private readonly Window _window;
    private string[] _names = Array.Empty<string>();
    private JsonArray _terminals = new();

    public SecuritySettingsPane(Window window)
    {
        _window = window;
        Spacing = 12;
        _bodyHost.Content = _body;
        Children.Add(_status);
        Children.Add(_bodyHost);
        Loaded += async (_, _) => await Refresh();
    }

    private static StackPanel Field(string label, FrameworkElement control)
    {
        var panel = new StackPanel { Spacing = 6, HorizontalAlignment = HorizontalAlignment.Stretch };
        panel.Children.Add(Design.Caption(label));
        control.HorizontalAlignment = HorizontalAlignment.Stretch;
        panel.Children.Add(control);
        return panel;
    }
    private static TextBox Text(string value = "") => new() { Text = value, MinWidth = 0 };
    private static ComboBox Choice(IEnumerable<string> values, string selected)
    {
        var box = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch };
        foreach (var value in values.Distinct()) box.Items.Add(value);
        if (!box.Items.Contains(selected)) box.Items.Add(selected);
        box.SelectedItem = selected;
        return box;
    }
    private static string Selected(ComboBox control) => control.SelectedItem as string ?? "";
    private ComboBox Reference(string selected) => Choice(new[] { "" }.Concat(_names), selected);
    private static Expander Section(string title, StackPanel content, bool expanded = false) => new() {
        Header = title, Content = content, IsExpanded = expanded,
        HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch
    };

    private async Task Refresh()
    {
        try {
            var raw = await Task.Run(NativeCore.SecurityJson);
            var terminals = await Task.Run(NativeCore.TerminalProfilesJson);
            _terminals = JsonNode.Parse(terminals ?? "{}")?["profiles"]?.AsArray() ?? new();
            var snapshot = JsonNode.Parse(raw ?? throw new InvalidOperationException("Security service unavailable."))!;
            _body.Children.Clear();
            if (!System.IO.File.Exists(System.IO.Path.Combine(AppContext.BaseDirectory, "scylla-broker.exe")))
                _body.Children.Add(Design.Caption("Database and SSH tools are unavailable to the agent: scylla-broker.exe is missing from the application folder. Rebuild/deploy the native runtime and restart Scylla. Saved connections are preserved."));
            var unlocked = snapshot["unlocked"]?.GetValue<bool>() == true;
            var exists = snapshot["exists"]?.GetValue<bool>() == true;
            _names = snapshot["items"]!.AsArray().Select(n => n!["name"]!.GetValue<string>()).ToArray();
            var vault = new StackPanel { Spacing = 10 };
            vault.Children.Add(Design.Caption(unlocked ? "Unlocked for this app session" : exists ? "Keyring locked" : "Create an encrypted keyring"));
            if (unlocked) vault.Children.Add(Design.GhostButton("Lock keyring", () => _ = Run("lock", new())));
            else {
                var passphrase = new PasswordBox();
                var confirm = new PasswordBox();
                vault.Children.Add(Field("App passphrase", passphrase));
                if (!exists) vault.Children.Add(Field("Confirm passphrase", confirm));
                vault.Children.Add(Design.PrimaryButton(exists ? "Unlock" : "Create keyring", () => {
                    if (!exists && passphrase.Password != confirm.Password) { _status.Text = "Passphrases do not match."; return; }
                    var value = passphrase.Password;
                    passphrase.Password = confirm.Password = "";
                    _ = Run(exists ? "unlock" : "create", new(), value);
                }));
            }
            _body.Children.Add(Section("App keyring", vault, true));
            if (unlocked) {
                var items = new StackPanel { Spacing = 12 };
                items.Children.Add(Design.Caption("Store hostnames, usernames, passwords, private keys, passphrases, tokens, and configuration values. Values remain encrypted in the app vault."));
                foreach (var node in snapshot["items"]!.AsArray()) {
                    var item = node!.AsObject();
                    var panel = new StackPanel { Spacing = 8 };
                    panel.Children.Add(Design.Caption(item["description"]?.GetValue<string>() ?? ""));
                    panel.Children.Add(Design.Caption("Scope: " + item["scope"]!.GetValue<string>()));
                    panel.Children.Add(ItemForm(item));
                    var confirmDelete = new CheckBox { Content = "Confirm deletion" };
                    panel.Children.Add(confirmDelete);
                    panel.Children.Add(Design.GhostButton("Delete item", () => {
                        if (confirmDelete.IsChecked != true) { _status.Text = "Confirm deletion first."; return; }
                        _ = Run("remove_item", new JsonObject { ["name"] = item["name"]!.GetValue<string>(),
                            ["scope"] = item["scope"]!.GetValue<string>() });
                    }));
                    items.Children.Add(Section(item["name"]!.GetValue<string>(), panel));
                }
                items.Children.Add(Section("Add encrypted item", ItemForm(null)));
                _body.Children.Add(Section("Variables and credentials", items, true));
            }
            foreach (var kind in new[] { "database", "ssh" }) {
            var connections = new StackPanel { Spacing = 12 };
            connections.Children.Add(Design.Caption("Connections belong to the active project. Credential fields select keyring names, never secret values."));
            if (snapshot["connectionsLoaded"]?.GetValue<bool>() != true)
                connections.Children.Add(Design.Caption("Connections could not be loaded. Saving is blocked to protect existing data."));
            else {
                foreach (var node in snapshot["connections"]!.AsArray()) {
                    var connection = node!.AsObject();
                    if ((connection["kind"]?.GetValue<string>() ?? "database") != kind) continue;
                    connections.Children.Add(Section("@" + connection["alias"]!.GetValue<string>(), ConnectionForm(connection)));
                }
                if (!string.IsNullOrEmpty(snapshot["projectId"]?.GetValue<string>()))
                    connections.Children.Add(Section("Add connection", ConnectionForm(new JsonObject { ["kind"] = kind })));
                else connections.Children.Add(Design.Caption("Open a project to add connections."));
            }
            _body.Children.Add(Section(kind == "ssh" ? "SSH Connections" : "Database connections", connections, true));
            }
        } catch (Exception ex) { _status.Text = "Could not load Security settings: " + ex.Message; }
    }

    private StackPanel ItemForm(JsonObject? current)
    {
        var panel = new StackPanel { Spacing = 8 };
        var name = Text(current?["name"]?.GetValue<string>() ?? "");
        name.IsReadOnly = current != null;
        var description = Text(current?["description"]?.GetValue<string>() ?? "");
        var scope = Choice(new[] { "global", "project" }, current?["scope"]?.GetValue<string>() ?? "project");
        scope.IsEnabled = current == null;
        var value = new PasswordBox();
        string? attachedValue = null;
        var attachmentStatus = Design.Caption("");
        value.PasswordChanged += (_, _) => {
            attachedValue = null;
            attachmentStatus.Text = "";
        };
        panel.Unloaded += (_, _) => { attachedValue = null; value.Password = ""; };
        panel.Children.Add(Field("Reference name (letters, numbers, underscore or dash)", name));
        panel.Children.Add(Field("Label / description", description));
        panel.Children.Add(Field("Scope", scope));
        panel.Children.Add(Field(current == null ? "Value" : "Replacement value (required)", value));
        panel.Children.Add(attachmentStatus);
        panel.Children.Add(Design.GhostButton("Import private key or certificate…", async () => {
            try {
                var picker = new Windows.Storage.Pickers.FileOpenPicker();
                WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(_window));
                picker.FileTypeFilter.Add("*");
                var file = await picker.PickSingleFileAsync();
                if (file == null) return;
                var properties = await file.GetBasicPropertiesAsync();
                if (properties.Size > 1024 * 1024) { attachmentStatus.Text = "Key files must be no larger than 1 MiB."; return; }
                var contents = await Windows.Storage.FileIO.ReadTextAsync(file);
                if (string.IsNullOrWhiteSpace(contents)) { attachmentStatus.Text = "The selected file is empty. Choose a key or certificate file."; return; }
                if (contents.Contains('\0')) { attachmentStatus.Text = "Choose a text private key or PEM certificate; binary files are not supported."; return; }
                value.Password = "";
                attachedValue = contents;
                attachmentStatus.Text = "Attached: " + file.Name + ". Save the encrypted item to import it. Typing a value replaces this attachment.";
            } catch { attachmentStatus.Text = "Could not read the selected file. Choose a readable text key or PEM certificate."; }
        }));
        panel.Children.Add(Design.PrimaryButton(current == null ? "Save encrypted item" : "Replace encrypted item", async () => {
            var secret = attachedValue ?? value.Password;
            await Run("save_item", new JsonObject { ["name"] = name.Text.Trim(), ["description"] = description.Text,
                ["scope"] = Selected(scope) }, secret);
        }));
        return panel;
    }

    private StackPanel ConnectionForm(JsonObject original)
    {
        var data = (JsonObject)original.DeepClone();
        var sshOnly = data["kind"]?.GetValue<string>() == "ssh";
        var panel = new StackPanel { Spacing = 10 };
        var saveFields = new List<Action>();
        JsonObject Group(string key) {
            if (data[key] is not JsonObject) data[key] = new JsonObject();
            return data[key]!.AsObject();
        }
        void Input(JsonObject obj, string key, string label, string fallback = "", bool reference = false) {
            var current = obj[key]?.GetValue<string>() ?? fallback;
            if (reference) {
                var control = Reference(current);
                panel.Children.Add(Field(label + " — keyring reference", control));
                saveFields.Add(() => obj[key] = Selected(control));
            } else {
                var control = Text(current);
                panel.Children.Add(Field(label, control));
                saveFields.Add(() => obj[key] = control.Text.Trim());
            }
        }
        ComboBox Select(JsonObject obj, string key, string label, string[] values, string fallback) {
            var control = Choice(values, obj[key]?.GetValue<string>() ?? fallback);
            panel.Children.Add(Field(label, control));
            saveFields.Add(() => obj[key] = Selected(control));
            return control;
        }
        void Number(JsonObject obj, string key, string label, int fallback, int maximum) {
            var control = new NumberBox { Minimum = 1, Maximum = maximum,
                Value = obj[key]?.GetValue<int>() ?? fallback, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact };
            panel.Children.Add(Field(label, control));
            saveFields.Add(() => {
                if (double.IsNaN(control.Value) || control.Value != Math.Truncate(control.Value) || control.Value < 1 || control.Value > maximum)
                    throw new InvalidOperationException(label + " must be a whole number in range.");
                obj[key] = (int)control.Value;
            });
        }
        void Pair(Action literal, Action reference) {
            var start = panel.Children.Count;
            literal(); reference();
            var left = (FrameworkElement)panel.Children[start];
            var right = (FrameworkElement)panel.Children[start + 1];
            panel.Children.RemoveAt(start + 1);
            panel.Children.RemoveAt(start);
            var grid = new Grid { ColumnSpacing = 12, RowSpacing = 8 };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.Children.Add(left); grid.Children.Add(right);
            void Arrange(double width) {
                var narrow = width < 560;
                Grid.SetColumn(right, narrow ? 0 : 1);
                Grid.SetRow(right, narrow ? 1 : 0);
                Grid.SetColumnSpan(left, narrow ? 2 : 1);
                Grid.SetColumnSpan(right, narrow ? 2 : 1);
            }
            Arrange(0);
            grid.SizeChanged += (_, e) => Arrange(e.NewSize.Width);
            panel.Children.Add(grid);
        }
        Input(data, "name", "Display name");
        Input(data, "alias", sshOnly ? "Agent alias (for example ssh_synq)" : "Agent alias (for example dataconnection)");
        var enabled = new CheckBox { Content = "Enabled", IsChecked = data["enabled"]?.GetValue<bool>() ?? true };
        panel.Children.Add(enabled);
        var route = Select(data, "routeType", "Route", new[] { "remote-execution", "ssh-tunnel", "direct" }, "remote-execution");
        if (sshOnly) {
            panel.Children[panel.Children.Count - 1].Visibility = Visibility.Collapsed;
            var terminal = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch };
            var current = data["terminalProfileId"]?.GetValue<string>() ?? "";
            foreach (var profile in _terminals) {
                if (profile?["enabled"]?.GetValue<bool>() != true ||
                    profile["agent_policy"]?.GetValue<string>() is not ("ask" or "allow") ||
                    !string.Equals(System.IO.Path.GetFileName(profile["executable"]?.GetValue<string>()), "wsl.exe", StringComparison.OrdinalIgnoreCase)) continue;
                var item = new ComboBoxItem { Content = profile["name"]!.GetValue<string>() + " · " + profile["agent_policy"]!.GetValue<string>(), Tag = profile["id"]!.GetValue<string>() };
                terminal.Items.Add(item);
                if ((string)item.Tag == current) terminal.SelectedItem = item;
            }
            panel.Children.Add(Field("Terminal profile", terminal));
            saveFields.Add(() => {
                if (terminal.SelectedItem is not ComboBoxItem item) throw new InvalidOperationException("Select an enabled WSL terminal with Ask or Allow policy.");
                data["terminalProfileId"] = (string)item.Tag;
            });
            panel.Children.Add(Design.Caption("Uses Python 3 and OpenSSH in the selected WSL distribution. Blocked or disabled terminals are excluded. Ask profiles can be saved, but execution requires approval support; Allow profiles can execute under the connection policy."));
            Select(data, "commandAuthority", "Command permission", new[] { "block", "auto" }, "block");
            panel.Children.Add(Design.Caption("Auto permits agent commands on this host. Block prevents execution."));
        } else panel.Children.Add(Design.Caption("Existing execution support: MySQL over SSH with a private key. Other saved routes require an executor before use."));
        var sshRowStart = panel.Children.Count;
        var sshSaveStart = saveFields.Count;
        var ssh = Group("ssh");
        Pair(() => Input(ssh, "host", "SSH hostname"), () => Input(ssh, "hostRef", "SSH hostname", reference: true));
        Pair(() => Number(ssh, "port", "SSH port", 22, 65535), () => Input(ssh, "portRef", "SSH port", reference: true));
        Pair(() => Input(ssh, "username", "SSH username"), () => Input(ssh, "usernameRef", "SSH username", reference: true));
        if (!sshOnly) Input(ssh, "authRef", "SSH password", reference: true);
        Input(ssh, "privateKeyRef", "SSH private key", reference: true);
        Input(ssh, "keyPassphraseRef", "SSH key passphrase", reference: true);
        Input(ssh, "hostKey", "Pinned SSH host PUBLIC key");
        var sshSaveEnd = saveFields.Count;
        var sshRows = panel.Children.Skip(sshRowStart).ToArray();
        bool UsesSsh() => sshOnly || Selected(route) is "remote-execution" or "ssh-tunnel";
        void UpdateSshVisibility() {
            foreach (var row in sshRows) row.Visibility = UsesSsh() ? Visibility.Visible : Visibility.Collapsed;
        }
        route.SelectionChanged += (_, _) => UpdateSshVisibility();
        UpdateSshVisibility();
        if (!sshOnly) {
        var db = Group("database");
        Select(db, "engine", "Database engine", new[] { "mysql", "postgresql", "mongodb", "sql-server" }, "mysql");
        Pair(() => Input(db, "host", "Database hostname"), () => Input(db, "hostRef", "Database hostname", reference: true));
        Pair(() => Number(db, "port", "Database port", 3306, 65535), () => Input(db, "portRef", "Database port", reference: true));
        Input(db, "database", "Database name");
        Input(db, "usernameRef", "Database username", reference: true);
        Input(db, "passwordRef", "Database password", reference: true);
        Input(db, "tlsCaRef", "TLS CA certificate", reference: true);
        var policy = Group("queryPolicy");
        foreach (var (key, label, fallback) in new[] { ("read", "Read queries", "auto"),
            ("dataModification", "Data changes", "ask"), ("schemaModification", "Schema changes", "ask"),
            ("administrative", "Administrative queries", "block") })
            Select(policy, key, label, new[] { "auto", "ask", "block" }, fallback);
        }
        var results = Group("resultPolicy");
        Select(results, "visibility", "Result visibility", new[] { "agent-and-human", "agent-only", "human-only", "metadata-only", "aggregate-only" }, "agent-and-human");
        if (!sshOnly) Number(results, "maxRows", "Maximum rows", 500, 1000000);
        Number(results, "maxBytes", "Maximum result bytes", 2097152, 134217728);
        Number(results, "timeoutSeconds", "Timeout (seconds)", 30, 3600);
        panel.Children.Add(Design.PrimaryButton("Save connection", () => {
            try {
                for (var i = 0; i < saveFields.Count; ++i) {
                    // Hidden SSH inputs must not block saving a direct connection.
                    if (!UsesSsh() && i >= sshSaveStart && i < sshSaveEnd) continue;
                    saveFields[i]();
                }
                data["enabled"] = enabled.IsChecked == true;
                _ = Run("save_connection", data);
            } catch (Exception ex) { _status.Text = ex.Message; }
        }));
        if (original["id"] != null) {
            var confirm = new CheckBox { Content = "Confirm deletion" };
            panel.Children.Add(confirm);
            panel.Children.Add(Design.GhostButton("Delete connection", () => {
                if (confirm.IsChecked != true) { _status.Text = "Confirm deletion first."; return; }
                _ = Run("remove_connection", new JsonObject { ["id"] = original["id"]!.GetValue<string>() });
            }));
        }
        return panel;
    }

    private async Task Run(string action, JsonObject metadata, string value = "")
    {
        if (_busy) return;
        _busy = true;
        _bodyHost.IsEnabled = false;
        _status.Text = "Working…";
        try {
            var result = await Task.Run(() => {
                var ok = NativeCore.SecurityAction(action, metadata.ToJsonString(), value, out var error);
                return (ok, error);
            });
            _status.Text = result.ok ? "Saved." : result.error;
            if (result.ok) await Refresh();
        } catch (Exception ex) { _status.Text = "Security operation failed: " + ex.Message; }
        finally { value = ""; _busy = false; _bodyHost.IsEnabled = true; }
    }
}
