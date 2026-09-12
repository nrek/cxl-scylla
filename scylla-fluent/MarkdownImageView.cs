using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage;

namespace Scylla;

internal static class MarkdownImageView
{
    public static FrameworkElement Create(MarkdownImage image, string directory)
    {
        var host = new StackPanel { Spacing = 6, Margin = new Thickness(0, 8, 0, 8) };
        var picture = new Image { Stretch = Stretch.Uniform, MaxHeight = 640,
            HorizontalAlignment = HorizontalAlignment.Stretch };
        var status = Design.Caption("Loading image…");
        var label = string.IsNullOrWhiteSpace(image.Alt) ? image.Target : image.Alt;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(picture, label);
        if (image.Title.Length > 0) ToolTipService.SetToolTip(picture, image.Title);
        host.Children.Add(picture);
        host.Children.Add(status);
        void Failed()
        {
            picture.Visibility = Visibility.Collapsed;
            status.Text = "Image unavailable: " + label;
            status.Visibility = Visibility.Visible;
        }
        var started = false;
        host.Loaded += async (_, _) => {
            if (started) return;
            started = true;
            try {
                var resolved = MarkdownLink.Resolve(image.Target, directory);
                var uri = resolved.Web ?? (resolved.File == null ? null : new Uri(resolved.File));
                if (uri == null) { Failed(); return; }
                var svg = uri.AbsolutePath.EndsWith(".svg", StringComparison.OrdinalIgnoreCase);
                if (svg) {
                    var source = new SvgImageSource();
                    source.OpenFailed += (_, _) => Failed();
                    source.Opened += (_, _) => status.Visibility = Visibility.Collapsed;
                    picture.Source = source;
                    if (resolved.File != null) {
                        using var stream = await (await StorageFile.GetFileFromPathAsync(resolved.File)).OpenReadAsync();
                        await source.SetSourceAsync(stream);
                    } else source.UriSource = uri;
                } else {
                    var source = new BitmapImage { DecodePixelWidth = 1600 };
                    source.ImageFailed += (_, _) => Failed();
                    source.ImageOpened += (_, _) => status.Visibility = Visibility.Collapsed;
                    picture.Source = source;
                    if (resolved.File != null) {
                        using var stream = await (await StorageFile.GetFileFromPathAsync(resolved.File)).OpenReadAsync();
                        await source.SetSourceAsync(stream);
                    } else source.UriSource = uri;
                }
            } catch { Failed(); }
        };
        return host;
    }
}
