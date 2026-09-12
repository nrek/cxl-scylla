using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;

namespace Scylla;

internal sealed partial class BottomPanelView
{
    private string _payloadDisplay = "";

    private void InitializePayloadMenu()
    {
        var menu = new MenuFlyout();
        var color = new ToggleMenuFlyoutItem { Text = "Color coding", IsChecked = PanelPreferences.Current.PayloadColorCoding };
        color.Click += (_, _) =>
        {
            PanelPreferences.Current.PayloadColorCoding = color.IsChecked;
            RenderPayload();
            try { PanelPreferences.Current.Save(); }
            catch (Exception ex) { ShowError("Could not save payload preferences: " + ex.Message); }
        };
        menu.Items.Add(color); // First option, ahead of the standard copy actions.
        menu.Items.Add(new MenuFlyoutSeparator());
        var copy = ActionItem("Copy", () => CopyPayloadText(_output.SelectedText));
        menu.Items.Add(copy);
        menu.Items.Add(ActionItem("Copy all", () => CopyPayloadText(_payloadDisplay)));
        menu.Opening += (_, _) =>
        {
            copy.IsEnabled = !string.IsNullOrEmpty(_output.SelectedText);
            color.IsChecked = PanelPreferences.Current.PayloadColorCoding;
        };
        _output.ContextFlyout = menu;
    }

    private void CopyPayloadText(string text)
    {
        try
        {
            var data = new DataPackage();
            data.SetText(text);
            Clipboard.SetContent(data);
        }
        catch (Exception ex) { ShowError("Could not copy payload: " + ex.Message); }
    }

    private void RenderPayload()
    {
        _output.Inlines.Clear();
        if (!PanelPreferences.Current.PayloadColorCoding) { _output.Text = _payloadDisplay; return; }
        _output.Text = "";
        foreach (var token in PayloadSyntax.Highlight(_payloadDisplay))
        {
            var color = token.Kind switch
            {
                PayloadTokenKind.Timestamp => Microsoft.UI.Colors.LightSlateGray,
                PayloadTokenKind.Key => Microsoft.UI.Colors.LightSkyBlue,
                PayloadTokenKind.String => Microsoft.UI.Colors.LightGreen,
                PayloadTokenKind.Number => Microsoft.UI.Colors.SandyBrown,
                PayloadTokenKind.Literal => Microsoft.UI.Colors.Plum,
                PayloadTokenKind.Direction => Microsoft.UI.Colors.Salmon,
                PayloadTokenKind.Punctuation => Microsoft.UI.Colors.Goldenrod,
                _ => ThemeColors.Secondary,
            };
            _output.Inlines.Add(new Run { Text = token.Text, Foreground = new SolidColorBrush(color) });
        }
    }
}
