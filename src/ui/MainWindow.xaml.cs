using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Scylla.UI.Models;
using Scylla.UI.ViewModels;

namespace Scylla.UI;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    void OnLoaded(object sender, RoutedEventArgs e)
    {
        SizeToContent = SizeToContent.Height;
        var work = SystemParameters.WorkArea;
        MaxHeight = work.Height;
        if (Top + ActualHeight > work.Bottom)
        {
            Top = Math.Max(work.Top, work.Bottom - ActualHeight);
        }
    }

    void OnClosing(object sender, CancelEventArgs e)
    {
        if (DataContext is MainWindowViewModel vm)
        {
            vm.ShutdownSession();
        }
    }

    void OnAppButtonClick(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn) return;
        var app = btn.Tag as DiscoveredApplication ?? btn.DataContext as DiscoveredApplication;
        if (DataContext is MainWindowViewModel vm)
        {
            vm.LaunchFromIcon(app);
        }
    }

    static bool IsEditingText(IInputElement? el) =>
        el is TextBox or PasswordBox || el is ComboBox || el is ComboBoxItem;

    void OnPreviewTextInput(object sender, TextCompositionEventArgs e)
    {
        if (string.IsNullOrEmpty(e.Text) || IsEditingText(Keyboard.FocusedElement)) return;
        if (DataContext is not MainWindowViewModel vm) return;
        vm.AppFilter += e.Text;
        AppFilterBox.CaretIndex = AppFilterBox.Text.Length;
        AppFilterBox.Focus();
        e.Handled = true;
    }

    void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (IsEditingText(Keyboard.FocusedElement) && !ReferenceEquals(Keyboard.FocusedElement, AppFilterBox))
        {
            return;
        }
        if (DataContext is not MainWindowViewModel vm) return;
        if (e.Key == Key.Back && !string.IsNullOrEmpty(vm.AppFilter) && !IsEditingText(Keyboard.FocusedElement))
        {
            vm.AppFilter = vm.AppFilter[..^1];
            AppFilterBox.CaretIndex = AppFilterBox.Text.Length;
            e.Handled = true;
        }
        else if (e.Key == Key.Escape && !string.IsNullOrEmpty(vm.AppFilter))
        {
            vm.AppFilter = "";
            e.Handled = true;
        }
    }
}
