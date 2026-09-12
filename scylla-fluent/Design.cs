using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Scylla;

/// <summary>
/// Shared spacing / shape / typography scale so every pane breathes the same.
/// Accent (#e69405) is reserved for state and the primary send action.
/// </summary>
internal static class Design
{
    // 4px grid, mirrors ui_space in the Win32 shell.
    public const double GapXs = 4;
    public const double GapSm = 8;
    public const double Gap = 12;
    public const double GapLg = 16;
    public const double GapXl = 24;

    public const double RowH = 32;
    public const double RowCompact = 28;

    public static readonly CornerRadius RadiusSm = new(6);
    public static readonly CornerRadius Radius = new(8);
    public static readonly CornerRadius RadiusLg = new(12);

    public static readonly Thickness PanePad = new(Gap);
    public static readonly Thickness CardPad = new(GapLg);

    /// <summary>Segoe Fluent Icons glyphs — no XamlReader path geometry (crashes WinUI).</summary>
    public static FontIcon Icon(string name) => new()
    {
        FontFamily = new FontFamily("Segoe Fluent Icons"),
        FontSize = 14,
        Glyph = name switch
        {
            "stop" => "\uE71A",   // Stop
            "plus" => "\uE710",   // Add
            "chevron-down" => "\uE70D",
            "more" => "\uE712",
            "delete" => "\uE74D",
            "search" => "\uE721", // Search
            "filter" => "\uE71C", // Filter
            "cog" => "\uE713",    // Settings
            "arrow" => "\uE724",  // Send
            _ => "\uE76C",        // Up
        },
    };

    public static Button IconButton(string icon, string label, string tooltip, Action click)
    {
        var button = GhostButton(label, click);
        button.BorderThickness = new Thickness(0);
        var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        content.Children.Add(Icon(icon));
        if (label.Length > 0) content.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
        button.Content = content;
        ToolTipService.SetToolTip(button, tooltip);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, tooltip);
        return button;
    }

    public static Grid IconField(TextBox field, string icon)
    {
        field.Padding = new Thickness(30, 4, 8, 4);
        var glyph = Icon(icon);
        glyph.HorizontalAlignment = HorizontalAlignment.Left;
        glyph.Margin = new Thickness(9, 0, 0, 0);
        glyph.IsHitTestVisible = false;
        return new Grid { Children = { field, glyph } };
    }
    public static TextBlock PaneTitle(string text) => new()
    {
        Text = text,
        FontSize = 12,
        FontWeight = FontWeights.SemiBold,
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        CharacterSpacing = 60,
    };

    public static TextBlock Heading(string text) => new()
    {
        Text = text,
        FontSize = 20,
        FontWeight = FontWeights.SemiBold,
        Foreground = ThemeColors.Brush(ThemeColors.Text),
    };

    public static TextBlock Body(string text) => new()
    {
        Text = text,
        Foreground = ThemeColors.Brush(ThemeColors.Secondary),
        TextWrapping = TextWrapping.WrapWholeWords,
    };

    public static TextBlock Caption(string text) => new()
    {
        Text = text,
        FontSize = 12,
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        TextWrapping = TextWrapping.WrapWholeWords,
    };

    /// <summary>Rounded, bordered container — the only "box" primitive in the shell.</summary>
    public static Border Card(UIElement? child = null, Thickness? padding = null) => new()
    {
        Background = ThemeColors.Brush(ThemeColors.Surface),
        BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
        BorderThickness = new Thickness(1),
        CornerRadius = Radius,
        Padding = padding ?? CardPad,
        Child = child,
    };

    /// <summary>Hairline divider for grouping without adding boxes.</summary>
    public static Border Divider(Thickness? margin = null) => new()
    {
        Height = 1,
        Background = ThemeColors.Brush(ThemeColors.BorderSubtle),
        Margin = margin ?? new Thickness(0, GapSm, 0, GapSm),
        HorizontalAlignment = HorizontalAlignment.Stretch,
    };

    /// <summary>Quiet secondary button — no fills, border only.</summary>
    public static Button GhostButton(string label, Action click, double height = RowCompact)
    {
        var b = new Button
        {
            Content = label,
            Height = height,
            Padding = new Thickness(Gap, 0, Gap, 0),
            Background = ThemeColors.Brush(ThemeColors.Transparent),
            Foreground = ThemeColors.Brush(ThemeColors.Secondary),
            BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
            BorderThickness = new Thickness(1),
            CornerRadius = RadiusSm,
            FontSize = 12,
        };
        b.Click += (_, _) => click();
        return b;
    }

    /// <summary>Accent action for Settings forms.</summary>
    public static Button PrimaryButton(string label, Action click, double height = RowCompact)
    {
        var b = new Button
        {
            Content = label,
            Height = height,
            Padding = new Thickness(Gap, 0, Gap, 0),
            Background = ThemeColors.Brush(ThemeColors.Amber),
            Foreground = new SolidColorBrush(Microsoft.UI.Colors.White),
            BorderThickness = new Thickness(0),
            CornerRadius = RadiusSm,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
        };
        b.Click += (_, _) => click();
        return b;
    }

    /// <summary>Search / filter field styled flat against the pane.</summary>
    public static TextBox Field(string placeholder) => new()
    {
        PlaceholderText = placeholder,
        Height = RowH,
        CornerRadius = RadiusSm,
        Background = ThemeColors.Brush(ThemeColors.Input),
        BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
        BorderThickness = new Thickness(1),
        FontSize = 13,
    };

    /// <summary>Strips the stock focus/hover fills so an embedded TextBox reads as plain text.</summary>
    public static void MakeTransparent(TextBox box)
    {
        var clear = ThemeColors.Brush(ThemeColors.Transparent);
        box.Background = clear;
        box.BorderThickness = new Thickness(0);
        box.Resources["TextControlBackground"] = clear;
        box.Resources["TextControlBackgroundPointerOver"] = clear;
        box.Resources["TextControlBackgroundFocused"] = clear;
        box.Resources["TextControlBorderBrush"] = clear;
        box.Resources["TextControlBorderBrushPointerOver"] = clear;
        box.Resources["TextControlBorderBrushFocused"] = clear;
    }
}
