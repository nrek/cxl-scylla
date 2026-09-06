using System.Windows.Input;

namespace Scylla.UI.ViewModels;

public sealed class RelayCommand : ICommand
{
    readonly Action<object?> _exec;
    readonly Func<object?, bool>? _can;

    public RelayCommand(Action exec, Func<bool>? can = null)
        : this(_ => exec(), can is null ? null : _ => can())
    {
    }

    public RelayCommand(Action<object?> exec, Func<object?, bool>? can = null)
    {
        _exec = exec;
        _can = can;
    }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public bool CanExecute(object? parameter) => _can?.Invoke(parameter) ?? true;

    public void Execute(object? parameter) => _exec(parameter);

    public void RaiseCanExecuteChanged() => CommandManager.InvalidateRequerySuggested();
}
