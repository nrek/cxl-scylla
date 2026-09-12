using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Media.Core;
using Windows.Media.Playback;
using Windows.Storage;
using Windows.Storage.FileProperties;

namespace Scylla;

internal sealed class MediaPreview : Grid, IDisposable
{
    private static readonly HashSet<string> Images = new(StringComparer.OrdinalIgnoreCase)
        { ".png", ".jpg", ".jpeg", ".jfif", ".bmp", ".dib", ".gif", ".tif", ".tiff", ".webp",
          ".ico", ".cur", ".svg", ".avif", ".heic", ".heif", ".jxr", ".wdp" };
    private static readonly HashSet<string> Videos = new(StringComparer.OrdinalIgnoreCase)
        { ".mp4", ".m4v", ".mov", ".webm", ".mkv", ".avi", ".wmv", ".mpg", ".mpeg", ".m2ts", ".mts", ".3gp" };
    public static bool Supports(string path) => Images.Contains(Path.GetExtension(path)) || Videos.Contains(Path.GetExtension(path));
    private readonly string _path;
    private readonly Grid _surface = new();
    private readonly TextBlock _status = Design.Caption("Loading preview…");
    private MediaPlayer? _player;
    private MediaPlayerElement? _video;
    private bool _disposed;
    private bool _started;

    public MediaPreview(string path)
    {
        _path = path;
        Padding = new Thickness(16);
        RowSpacing = 8;
        RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Children.Add(_surface);
        Grid.SetRow(_status, 1);
        Children.Add(_status);
        var external = Design.GhostButton("Open in default app", async () =>
        {
            try { await Windows.System.Launcher.LaunchFileAsync(await StorageFile.GetFileFromPathAsync(_path)); }
            catch (Exception ex) { _status.Text = ex.Message; }
        });
        external.HorizontalAlignment = HorizontalAlignment.Center;
        Grid.SetRow(external, 2);
        Children.Add(external);
        Loaded += async (_, _) => { if (!_started) { _started = true; await LoadAsync(); } };
    }

    private async Task LoadAsync()
    {
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(_path);
            if (_disposed) return;
            var extension = Path.GetExtension(_path);
            if (Videos.Contains(extension))
            {
                _player = new MediaPlayer { AutoPlay = false };
                _player.MediaFailed += (_, args) => DispatcherQueue.TryEnqueue(() =>
                {
                    if (!_disposed) _status.Text = "Video unavailable: " + args.ErrorMessage + " Try opening it in the default app.";
                });
                _video = new MediaPlayerElement { AreTransportControlsEnabled = true, Stretch = Stretch.Uniform };
                _video.SetMediaPlayer(_player);
                _surface.Children.Add(_video);
                _player.Source = MediaSource.CreateFromStorageFile(file);
            }
            else
            {
                var picture = new Image { Stretch = Stretch.Uniform };
                _surface.Children.Add(picture);
                if (extension.Equals(".svg", StringComparison.OrdinalIgnoreCase))
                {
                    var svg = new SvgImageSource();
                    svg.OpenFailed += (_, _) => { if (!_disposed) _status.Text = "SVG preview unavailable. Open in the default app."; };
                    picture.Source = svg;
                    using var stream = await file.OpenReadAsync();
                    if (_disposed) return;
                    await svg.SetSourceAsync(stream);
                }
                else
                {
                    var bitmap = new BitmapImage();
                    picture.Source = bitmap;
                    if (extension.Equals(".ico", StringComparison.OrdinalIgnoreCase) || extension.Equals(".cur", StringComparison.OrdinalIgnoreCase))
                    {
                        using var thumbnail = await file.GetThumbnailAsync(ThumbnailMode.SingleItem, 256, ThumbnailOptions.UseCurrentScale);
                        if (_disposed) return;
                        if (thumbnail is null) throw new InvalidOperationException("No icon preview available.");
                        await bitmap.SetSourceAsync(thumbnail);
                    }
                    else
                    {
                        using var stream = await file.OpenReadAsync();
                        if (_disposed) return;
                        await bitmap.SetSourceAsync(stream);
                    }
                    if (_disposed) return;
                    _status.Text = $"{bitmap.PixelWidth} × {bitmap.PixelHeight}";
                    return;
                }
            }
            if (!_disposed) _status.Text = Path.GetFileName(_path);
        }
        catch (Exception ex)
        {
            if (!_disposed) _status.Text = "Preview unavailable: " + ex.Message + " Open in the default app; some formats require a Windows codec.";
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _video?.SetMediaPlayer(null);
        _player?.Dispose();
        _player = null;
        _surface.Children.Clear();
    }
}
