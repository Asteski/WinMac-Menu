using System.Security.Cryptography;
using System.Text;
using Microsoft.UI.Dispatching;

namespace WinMacMenu.Services;

/// <summary>
/// Single-instance-per-INI guard with a cross-process "toggle" signal, replicating the C app's
/// behaviour where launching the EXE again toggles the menu rather than starting a new process.
/// </summary>
public sealed class SingleInstance : IDisposable
{
    private readonly Mutex _mutex;
    private readonly EventWaitHandle _toggleEvent;
    private Thread? _listener;
    private volatile bool _running;

    public bool IsFirstInstance { get; }

    private SingleInstance(Mutex mutex, bool isFirst, EventWaitHandle toggle)
    {
        _mutex = mutex;
        IsFirstInstance = isFirst;
        _toggleEvent = toggle;
    }

    public static SingleInstance Create(string iniPath)
    {
        var token = HashPath(iniPath);
        var mutex = new Mutex(initiallyOwned: true, $"Local\\WinMacMenu_Mutex_{token}", out bool createdNew);
        var toggle = new EventWaitHandle(false, EventResetMode.AutoReset, $"Local\\WinMacMenu_Toggle_{token}");
        return new SingleInstance(mutex, createdNew, toggle);
    }

    /// <summary>Signals the already-running instance to toggle its menu.</summary>
    public void SignalToggle() => _toggleEvent.Set();

    /// <summary>Starts a background listener that invokes <paramref name="onToggle"/> on the UI thread.</summary>
    public void StartToggleListener(DispatcherQueue dispatcher, Action onToggle)
    {
        _running = true;
        _listener = new Thread(() =>
        {
            while (_running)
            {
                if (_toggleEvent.WaitOne(500) && _running)
                    dispatcher.TryEnqueue(() => onToggle());
            }
        })
        { IsBackground = true, Name = "WinMacMenu.ToggleListener" };
        _listener.Start();
    }

    private static string HashPath(string path)
    {
        var bytes = SHA1.HashData(Encoding.Unicode.GetBytes(path.ToLowerInvariant()));
        return Convert.ToHexString(bytes)[..16];
    }

    public void Dispose()
    {
        _running = false;
        try { _toggleEvent.Set(); } catch { }
        try { _mutex.ReleaseMutex(); } catch { }
        _mutex.Dispose();
        _toggleEvent.Dispose();
    }
}
