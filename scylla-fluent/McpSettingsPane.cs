using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using System.Text.Json.Nodes;

namespace Scylla;

internal sealed class McpSettingsPane : StackPanel
{
    private readonly TextBlock _status = Design.Caption("");
    private readonly StackPanel _cards = new() { Spacing = 12 };
    private readonly ContentControl _host = new() { HorizontalContentAlignment = HorizontalAlignment.Stretch };
    private readonly TextBox _search = Design.Field("Search MCP services and connections");
    private bool _busy;

    public McpSettingsPane()
    {
        Spacing = 12;
        Children.Add(Design.Body("MCP services"));
        Children.Add(_search);
        Children.Add(_status);
        _host.Content = _cards;
        Children.Add(_host);
        Reload();
    }

    private static string Value(JsonNode? node, string key, string fallback = "") => node?[key]?.GetValue<string>() ?? fallback;
    private static Expander Expand(string title, UIElement content, bool open = false) => new() {
        Header = title, Content = content, IsExpanded = open,
        HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch
    };

    private void Reload()
    {
        _cards.Children.Clear();
        try {
            var catalog = JsonNode.Parse(NativeCore.McpCatalogJson() ?? "[]")!.AsArray();
            var saved = JsonNode.Parse(NativeCore.McpJson() ?? "[]")!.AsArray();
            var used = new HashSet<string>();
            foreach (var template in catalog) {
                var service = Value(template, "id");
                var matches = saved.Where(c => Value(c, "service_id") == service ||
                    (service == "linear" && Value(c, "service_id") == "linear-readonly")).ToList();
                foreach (var connection in matches) {
                    used.Add(Value(connection, "id"));
                    AddCard(template, connection);
                }
                if (matches.Count == 0) AddCard(template, null);
                else {
                    var another = Expand("Add another " + Value(template, "name") + " connection", Connection(template, null));
                    _cards.Children.Add(another);
                }
            }
            foreach (var connection in saved.Where(c => !used.Contains(Value(c, "id")) && Value(c, "service_id") != "workspace-knowledge"))
                AddCard(null, connection);
            _cards.Children.Add(Design.Card(Expand("Advanced — Add Custom MCP", Connection(null, null))));
            Filter();
        } catch (Exception ex) { _status.Text = "Could not load MCP settings: " + ex.Message; }
    }

    private void Filter()
    {
        foreach (var child in _cards.Children.OfType<FrameworkElement>())
            child.Visibility = child.Tag is not string title || title.Contains(_search.Text, StringComparison.OrdinalIgnoreCase)
                ? Visibility.Visible : Visibility.Collapsed;
    }

    private void AddCard(JsonNode? template, JsonNode? connection)
    {
        var card = Design.Card(Connection(template, connection), new Thickness(16));
        card.Tag = Value(template, "name", Value(connection, "display_name", "Custom MCP")) + " " + Value(connection, "connection_name");
        _cards.Children.Add(card);
    }

    private FrameworkElement Connection(JsonNode? template, JsonNode? original)
    {
        _search.TextChanged -= SearchChanged;
        _search.TextChanged += SearchChanged;
        var panel = new StackPanel { Spacing = 10 };
        var service = Value(template, "id", Value(original, "service_id", "custom"));
        var title = Value(template, "name", Value(original, "display_name", "Custom MCP"));
        var id = Value(original, "id", Guid.NewGuid().ToString());
        var exists = original != null;
        var state = Value(original, "auth_state", "unknown");
        var healthy = state == "healthy";
        var hasAuthenticated = original?["has_authenticated"]?.GetValue<bool>() == true || healthy;
        var stale = exists && hasAuthenticated && !healthy;
        var indicator = new Ellipse { Width = 9, Height = 9, VerticalAlignment = VerticalAlignment.Center,
            Fill = new SolidColorBrush(healthy ? Microsoft.UI.Colors.LimeGreen : stale ? Microsoft.UI.Colors.IndianRed : Microsoft.UI.Colors.Gray) };
        var stateText = healthy ? "Authenticated" : stale ? "Stale — reauthentication required" : "Not authenticated";
        ToolTipService.SetToolTip(indicator, stateText);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(indicator, stateText);
        panel.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8,
            Children = { indicator, Design.Body(title), Design.Caption(stateText) } });

        var nickname = Design.Field("Nickname (optional)");
        nickname.Text = Value(original, "connection_name");
        var endpoint = Design.Field("https://service.example/mcp");
        endpoint.Text = Value(original, "endpoint_or_cmd", Value(template, "endpoint"));
        if (service == "sentry" && endpoint.Text.TrimEnd('/') == "https://mcp.sentry.dev") endpoint.Text = "https://mcp.sentry.dev/mcp";
        var aliasBox = Design.Field("Optional agent @alias");
        aliasBox.Text = Value(original, "agent_alias");
        var enabled = new CheckBox { Content = "Connection enabled", IsChecked = original?["enabled"]?.GetValue<bool>() ?? true };
        var status = Design.Caption(Value(original, "last_error"));
        var authorize = Design.PrimaryButton(hasAuthenticated ? "Reauthenticate" : "Authenticate", () => { });
        var remove = Design.GhostButton("Remove", () => { });
        remove.Visibility = exists ? Visibility.Visible : Visibility.Collapsed;
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { authorize, remove } };
        var top = new Grid { ColumnSpacing = 12, RowSpacing = 8 };
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        top.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        top.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetColumn(actions, 1);
        top.Children.Add(nickname); top.Children.Add(actions);
        top.SizeChanged += (_, e) => {
            bool narrow = e.NewSize.Width < 540;
            Grid.SetRow(actions, narrow ? 1 : 0); Grid.SetColumn(actions, narrow ? 0 : 1);
            Grid.SetColumnSpan(nickname, narrow ? 2 : 1);
        };
        panel.Children.Add(top);
        if (exists) {
            var info = new Grid();
            info.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            info.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            info.Children.Add(Design.Caption(endpoint.Text));
            // Agent aliases and legacy account labels are not verified provider usernames.
            var alias = Value(original, "agent_alias");
            if (alias.Length > 0) {
                var label = Design.Caption("Agent alias: @" + alias); Grid.SetColumn(label, 1); info.Children.Add(label);
            }
            panel.Children.Add(info);
        }

        var scopesPanel = new StackPanel { Spacing = 8 };
        var checklist = new StackPanel { Spacing = 4 };
        var selected = original?["oauth_scopes"]?.AsArray().Select(n => n!.GetValue<string>()).ToHashSet() ?? new HashSet<string>();
        var boxes = new Dictionary<string, CheckBox>();
        var providerConsent = new CheckBox { Content = "Choose permissions at provider consent (or in the token settings)",
            // A fresh catalog card must have a valid, least-surprising authentication path.
            // The provider's consent screen remains the authority; users can instead select
            // individual advertised scopes before authenticating.
            IsChecked = original == null || original?["scopes_selected"]?.GetValue<bool>() == true && selected.Count == 0 };
        var save = Design.PrimaryButton("Save", () => { });
        save.Visibility = Visibility.Collapsed;
        string Initial() => nickname.Text + "\n" + aliasBox.Text + "\n" + enabled.IsChecked + "\n" + endpoint.Text + "\n" + string.Join(" ", boxes.Where(p => p.Value.IsChecked == true).Select(p => p.Key).Order()) + "\n" + providerConsent.IsChecked;
        string initial = "";
        void Dirty() { save.Visibility = exists && Initial() != initial ? Visibility.Visible : Visibility.Collapsed; }
        void Scope(string value, bool check) {
            if (boxes.ContainsKey(value)) return;
            var box = new CheckBox { Content = value, IsChecked = check };
            boxes.Add(value, box); checklist.Children.Add(box);
            box.Checked += (_, _) => { providerConsent.IsChecked = false; Dirty(); };
            box.Unchecked += (_, _) => Dirty();
        }
        foreach (var scope in selected.Order()) Scope(scope, true);
        foreach (var scope in template?["scopes"]?.AsArray() ?? new JsonArray()) Scope(scope!.GetValue<string>(), false);
        providerConsent.Checked += (_, _) => { foreach (var box in boxes.Values) box.IsChecked = false; Dirty(); };
        providerConsent.Unchecked += (_, _) => Dirty();
        scopesPanel.Children.Add(checklist);
        scopesPanel.Children.Add(providerConsent);
        var discover = Design.GhostButton("Load available scopes", () => { });
        discover.Click += async (_, _) => {
            var requestedEndpoint = endpoint.Text.Trim();
            discover.IsEnabled = false; status.Text = "Loading provider scope metadata…";
            try {
                var json = await Task.Run(() => NativeCore.McpScopesJson(requestedEndpoint));
                if (requestedEndpoint != endpoint.Text.Trim()) return;
                var result = JsonNode.Parse(json ?? "{}");
                foreach (var scope in result?["scopes"]?.AsArray() ?? new JsonArray()) Scope(scope!.GetValue<string>(), false);
                status.Text = Value(result, "message");
            } catch (Exception ex) { status.Text = ex.Message; }
            finally { discover.IsEnabled = true; }
        };
        scopesPanel.Children.Add(discover);
        var manual = Design.Field("Additional OAuth scope");
        scopesPanel.Children.Add(manual);
        scopesPanel.Children.Add(Design.GhostButton("Add scope", () => {
            foreach (var value in manual.Text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)) Scope(value, true);
            if (!string.IsNullOrWhiteSpace(manual.Text)) providerConsent.IsChecked = false;
            manual.Text = ""; Dirty();
        }));
        scopesPanel.Children.Add(Design.Caption("OAuth scopes request access from the provider. Token permissions must be selected when creating the token. Tools still require project approval."));
        scopesPanel.Children.Add(endpoint);
        scopesPanel.Children.Add(aliasBox);
        scopesPanel.Children.Add(enabled);
        if (service == "supabase") {
            void Query(string key, string value) {
                if (!Uri.TryCreate(endpoint.Text, UriKind.Absolute, out var uri)) return;
                var parts = uri.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries)
                    .Where(p => !p.StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)).ToList();
                if (value.Length > 0) parts.Add(key + "=" + Uri.EscapeDataString(value));
                endpoint.Text = new UriBuilder(uri) { Query = string.Join("&", parts) }.Uri.AbsoluteUri;
            }
            string QueryValue(string key) => Uri.TryCreate(endpoint.Text, UriKind.Absolute, out var uri)
                ? Uri.UnescapeDataString(uri.Query.TrimStart('?').Split('&').FirstOrDefault(p => p.StartsWith(key + "="))?[(key.Length + 1)..] ?? "") : "";
            var project = Design.Field("Supabase project reference (optional)");
            project.Text = QueryValue("project_ref");
            project.TextChanged += (_, _) => Query("project_ref", project.Text.Trim());
            scopesPanel.Children.Add(project);
            var groups = new[] { "docs", "account", "database", "debugging", "development", "functions", "branching", "storage" };
            var configured = QueryValue("features");
            var features = groups.ToDictionary(g => g, g => new CheckBox { Content = g,
                IsChecked = configured.Length == 0 ? g != "storage" : configured.Split(',').Contains(g) });
            var featurePanel = new StackPanel { Spacing = 4 };
            featurePanel.Children.Add(Design.Caption("Supabase tool groups"));
            foreach (var pair in features) {
                featurePanel.Children.Add(pair.Value);
                pair.Value.Click += (_, _) => {
                    var values = features.Where(p => p.Value.IsChecked == true).Select(p => p.Key).ToArray();
                    if (values.Length == 0) { pair.Value.IsChecked = true; status.Text = "Keep at least one tool group selected."; return; }
                    Query("features", string.Join(",", values));
                };
            }
            scopesPanel.Children.Add(Expand("Tool groups", featurePanel));
            var readOnly = new CheckBox { Content = "Read-only database access", IsChecked = endpoint.Text.Contains("read_only=true") };
            readOnly.Click += (_, _) => {
                if (!Uri.TryCreate(endpoint.Text, UriKind.Absolute, out var uri)) return;
                var parts = uri.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries)
                    .Where(p => !p.StartsWith("read_only=", StringComparison.OrdinalIgnoreCase)).ToList();
                if (readOnly.IsChecked == true) parts.Add("read_only=true");
                endpoint.Text = new UriBuilder(uri) { Query = string.Join("&", parts) }.Uri.AbsoluteUri;
            };
            scopesPanel.Children.Add(readOnly);
        }
        if (service is "linear" or "linear-readonly") {
            var readOnly = new CheckBox { Content = "Read-only Linear endpoint", IsChecked = endpoint.Text.EndsWith("/readonly") };
            readOnly.Click += (_, _) => endpoint.Text = readOnly.IsChecked == true
                ? "https://mcp.linear.app/mcp/readonly" : "https://mcp.linear.app/mcp";
            scopesPanel.Children.Add(readOnly);
        }
        if (service == "github") {
            var readOnly = new CheckBox { Content = "Expose only read-only GitHub tools", IsChecked = endpoint.Text.TrimEnd('/').EndsWith("/readonly") };
            readOnly.Click += (_, _) => {
                var value = endpoint.Text.TrimEnd('/');
                if (value.EndsWith("/readonly")) value = value[..^9];
                endpoint.Text = readOnly.IsChecked == true ? value + "/readonly" : value + "/";
            };
            scopesPanel.Children.Add(readOnly);
        }
        scopesPanel.Children.Add(save);
        panel.Children.Add(hasAuthenticated ? Expand("Edit Scopes", scopesPanel) : scopesPanel);
        var token = new PasswordBox { PlaceholderText = "Access token", HorizontalAlignment = HorizontalAlignment.Stretch };
        var tokenMode = new CheckBox { Content = "Use an access token instead of browser sign-in", IsChecked = service == "github" };
        void SyncAuthenticationMode() {
            token.Visibility = tokenMode.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            authorize.Content = tokenMode.IsChecked == true
                ? (hasAuthenticated ? "Validate new token" : "Validate token")
                : (hasAuthenticated ? "Reauthenticate" : "Authenticate in browser");
        }
        tokenMode.Checked += (_, _) => SyncAuthenticationMode();
        tokenMode.Unchecked += (_, _) => SyncAuthenticationMode();
        SyncAuthenticationMode();
        var credentials = new StackPanel { Spacing = 8, Children = { tokenMode, token,
            Design.Caption("Use this for GitHub or providers without automatic OAuth client registration. Stored in Windows Credential Manager.") } };
        if (service is "notion" or "gitlab") scopesPanel.Children.Add(Design.Caption(
            service == "notion" ? "Notion advertises one default scope. Content access is chosen during Notion authorization."
                : "GitLab advertises one mcp scope. GitLab account permissions determine access within it."));
        if (service == "github") credentials.Children.Add(new HyperlinkButton {
            Content = "Create a GitHub token and select its permissions", NavigateUri = new Uri("https://github.com/settings/personal-access-tokens/new") });
        panel.Children.Add(Expand("Authentication options", credentials, service == "github"));
        panel.Children.Add(status);
        initial = Initial();
        nickname.TextChanged += (_, _) => Dirty(); endpoint.TextChanged += (_, _) => Dirty();
        aliasBox.TextChanged += (_, _) => Dirty();
        enabled.Checked += (_, _) => Dirty(); enabled.Unchecked += (_, _) => Dirty();
        JsonObject Payload() => new() {
            ["id"] = id, ["service_id"] = service, ["display_name"] = title, ["connection_name"] = nickname.Text.Trim(),
            ["endpoint_or_cmd"] = endpoint.Text.Trim(), ["transport"] = "http",
            ["agent_alias"] = aliasBox.Text.Trim(), ["enabled"] = enabled.IsChecked == true,
            ["oauth_scopes"] = new JsonArray(boxes.Where(p => p.Value.IsChecked == true).Select(p => (JsonNode?)JsonValue.Create(p.Key)).ToArray()),
            ["scopes_selected"] = providerConsent.IsChecked == true || boxes.Any(p => p.Value.IsChecked == true)
        };
        bool Persist() {
            if (!Uri.TryCreate(endpoint.Text.Trim(), UriKind.Absolute, out var uri) || uri.Scheme != "https") {
                status.Text = "Enter an HTTPS MCP endpoint."; return false;
            }
            var data = Payload();
            if (data["scopes_selected"]?.GetValue<bool>() != true) { status.Text = "Select scopes or choose provider consent."; return false; }
            if (!NativeCore.McpAction(exists ? "update" : template == null ? "add" : "add_catalog", data.ToJsonString(), out var error)) {
                status.Text = error; return false;
            }
            exists = true; initial = Initial(); save.Visibility = Visibility.Collapsed;
            remove.Visibility = Visibility.Visible;
            return true;
        }
        save.Click += (_, _) => { if (Persist()) { _status.Text = "Saved. Reauthenticate to apply changed permissions."; Reload(); } };
        remove.Click += (_, _) => {
            if (NativeCore.McpAction("remove", new JsonObject { ["id"] = id }.ToJsonString(), out var error)) Reload();
            else status.Text = error;
        };
        authorize.Click += async (_, _) => {
            if (_busy) return;
            if (tokenMode.IsChecked == true && string.IsNullOrWhiteSpace(token.Password)) { status.Text = "Enter an access token."; return; }
            if (!Persist()) return;
            _busy = true; _host.IsEnabled = false;
            status.Text = tokenMode.IsChecked == true ? "Validating access token…" : "Waiting for browser authorization…";
            var useToken = tokenMode.IsChecked == true;
            var data = new JsonObject { ["id"] = id };
            if (useToken) data["token"] = token.Password;
            token.Password = "";
            try {
                var result = await Task.Run(() => {
                    var ok = NativeCore.McpAction(useToken ? "token" : "authorize", data.ToJsonString(), out var error);
                    return (ok, error);
                });
                _status.Text = result.ok ? "Authenticated." : result.error;
            } catch (Exception ex) { _status.Text = ex.Message; }
            finally { data.Remove("token"); _busy = false; _host.IsEnabled = true; Reload(); }
        };
        return panel;
    }

    private void SearchChanged(object sender, TextChangedEventArgs e) => Filter();
}
