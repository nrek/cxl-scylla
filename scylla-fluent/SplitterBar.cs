using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Scylla;

/// <summary>
/// Pane splitter with resize cursor. Border is sealed and ProtectedCursor is protected,
/// so this Grid subclass owns the hit target + cursor.
/// </summary>
internal sealed class SplitterBar : Grid
{
    public bool Horizontal { get; }

    public SplitterBar(bool horizontal = false)
    {
        Horizontal = horizontal;
        Width = horizontal ? double.NaN : 4;
        Height = horizontal ? 4 : double.NaN;
        MinWidth = horizontal ? 0 : 4;
        MinHeight = horizontal ? 4 : 0;
        Background = ThemeColors.Brush(ThemeColors.Transparent);
        Children.Add(new Border
        {
            Width = horizontal ? double.NaN : 2,
            Height = horizontal ? 2 : double.NaN,
            HorizontalAlignment = horizontal ? HorizontalAlignment.Stretch : HorizontalAlignment.Center,
            VerticalAlignment = horizontal ? VerticalAlignment.Center : VerticalAlignment.Stretch,
            Background = ThemeColors.Brush(ThemeColors.Border),
            IsHitTestVisible = false,
        });
        ProtectedCursor = InputSystemCursor.Create(
            horizontal ? InputSystemCursorShape.SizeNorthSouth : InputSystemCursorShape.SizeWestEast);
    }
}
