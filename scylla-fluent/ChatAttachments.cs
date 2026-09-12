using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics.Imaging;
using Windows.Storage;

namespace Scylla;

internal sealed class ChatSendRequest : EventArgs
{
    public string Payload { get; init; } = "";
    public string Mode { get; init; } = "Execute";
    public bool Accepted { get; set; }
}

internal static class ChatAttachments
{
    private static string AttachmentDirectory => System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ScyllaGPT", "attachments");

    private static readonly HashSet<string> ImageExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".bmp", ".gif", ".heic", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp",
    };

    // Keep pasted images outside temporary storage so restored chats retain them.
    public static async Task<ChatAttachment> SaveBitmapAsync(DataPackageView clipboard)
    {
        var directory = AttachmentDirectory;
        System.IO.Directory.CreateDirectory(directory);
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync($"paste-{Guid.NewGuid():N}.png", CreationCollisionOption.FailIfExists);
        try
        {
            var reference = await clipboard.GetBitmapAsync();
            using var source = await reference.OpenReadAsync();
            var decoder = await BitmapDecoder.CreateAsync(source);
            using var bitmap = await decoder.GetSoftwareBitmapAsync();
            using var destination = await file.OpenAsync(FileAccessMode.ReadWrite);
            var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, destination);
            encoder.SetSoftwareBitmap(bitmap);
            await encoder.FlushAsync();
            return new ChatAttachment { Path = file.Path, Label = "Pasted image" };
        }
        catch
        {
            await file.DeleteAsync();
            throw;
        }
    }

    public static async Task<ChatAttachment> SaveFileAsync(StorageFile source)
    {
        System.IO.Directory.CreateDirectory(AttachmentDirectory);
        var folder = await StorageFolder.GetFolderFromPathAsync(AttachmentDirectory);
        var extension = System.IO.Path.GetExtension(source.Name);
        var copy = await source.CopyAsync(folder, $"file-{Guid.NewGuid():N}{extension}",
            NameCollisionOption.FailIfExists);
        return new ChatAttachment
        {
            Path = copy.Path,
            Label = source.Name,
            Image = ImageExtensions.Contains(extension),
        };
    }

    public static string Payload(string text, IReadOnlyList<ChatAttachment> attachments)
    {
        if (attachments.Count == 0) return text;
        var display = JsonSerializer.Serialize(new { text, attachments });
        var files = string.Join("\n", attachments.Select(a => $"- {a.Label}: {a.Path}"));
        return "\n<scylla-display-json>" + display + "</scylla-display-json>\n"
            + (text.Length == 0 ? "Please inspect the attached files." : text)
            + "\n\nUser-attached files (staged for this chat session):\n" + files;
    }

    public static Button Thumbnail(ChatAttachment attachment, FrameworkElement owner)
    {
        var button = new Button { Width = 112, Height = 88, Padding = new Thickness(4) };
        ToolTipService.SetToolTip(button, attachment.Label);
        if (attachment.Image && System.IO.File.Exists(attachment.Path))
        {
            var source = new BitmapImage(new Uri(attachment.Path)) { DecodePixelWidth = 224 };
            source.ImageFailed += (_, _) => button.Content = Design.Caption("Image unavailable");
            button.Content = new Image { Source = source, Stretch = Stretch.Uniform };
            button.Click += async (_, _) =>
            {
                var preview = new Image
                {
                    Source = new BitmapImage(new Uri(attachment.Path)),
                    Stretch = Stretch.Uniform,
                    MaxWidth = Math.Max(240, owner.XamlRoot.Size.Width - 120),
                    MaxHeight = Math.Max(180, owner.XamlRoot.Size.Height - 180),
                };
                var dialog = new ContentDialog
                {
                    XamlRoot = owner.XamlRoot,
                    Title = attachment.Label,
                    Content = preview,
                    CloseButtonText = "Close",
                    FullSizeDesired = true,
                };
                dialog.Resources["ContentDialogMaxWidth"] = Math.Max(320, owner.XamlRoot.Size.Width - 80);
                preview.ImageFailed += (_, _) => dialog.Content = Design.Caption("This image could not be loaded.");
                try { await dialog.ShowAsync(); }
                catch (System.Runtime.InteropServices.COMException) { /* Another dialog is already open. */ }
            };
        }
        else
        {
            button.Content = Design.Caption(attachment.Image ? "Image unavailable" : attachment.Label);
            button.IsEnabled = false;
        }
        return button;
    }
}
