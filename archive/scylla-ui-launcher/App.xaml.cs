using System.Windows;
using System.Windows.Threading;

namespace Scylla.UI;

public partial class App : System.Windows.Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        DispatcherUnhandledException += (_, args) =>
        {
            MessageBox.Show(args.Exception.Message, "SCYLLA", MessageBoxButton.OK, MessageBoxImage.Warning);
            args.Handled = true;
        };

        var splash = new SplashWindow();
        splash.Show();

        // Let the splash paint before constructing the main window.
        DoEvents();

        var main = new MainWindow();
        MainWindow = main;
        main.Loaded += (_, _) =>
        {
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(650) };
            timer.Tick += (_, _) =>
            {
                timer.Stop();
                splash.Close();
                main.Activate();
            };
            timer.Start();
        };
        main.Show();

        base.OnStartup(e);
    }

    static void DoEvents()
    {
        var frame = new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.Background, new DispatcherOperationCallback(static f =>
        {
            ((DispatcherFrame)f!).Continue = false;
            return null;
        }), frame);
        Dispatcher.PushFrame(frame);
    }
}
