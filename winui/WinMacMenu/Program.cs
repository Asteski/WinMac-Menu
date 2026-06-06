using Microsoft.UI.Dispatching;
using Microsoft.Windows.ApplicationModel.DynamicDependency;
using Microsoft.Windows.ApplicationModel.WindowsAppRuntime;
using WinMacMenu.Configuration;
using WinMacMenu.Services;

namespace WinMacMenu;

/// <summary>
/// Custom entry point. Enforces single-instance-per-INI and, when a second instance starts,
/// signals the running one to toggle its menu (matching the C app) and exits.
/// </summary>
public static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var configPath = ParseConfigArg(args) ?? ConfigLoader.ResolveDefaultPath();

        var single = SingleInstance.Create(configPath);
        if (!single.IsFirstInstance)
        {
            // Already running for this INI: ask it to toggle, then exit.
            single.SignalToggle();
            single.Dispose();
            return;
        }

        Bootstrap.Initialize(
            Microsoft.WindowsAppSDK.Release.MajorMinor,
            Microsoft.WindowsAppSDK.Release.VersionTag,
            new PackageVersion(Microsoft.WindowsAppSDK.Runtime.Version.UInt64),
            Bootstrap.InitializeOptions.OnNoMatch_ShowUI);

        var deploymentResult = DeploymentManager.Initialize();
        if (deploymentResult.Status != DeploymentStatus.Ok)
        {
            single.Dispose();
            throw new InvalidOperationException($"Windows App Runtime initialization failed with status {deploymentResult.Status} (HRESULT 0x{deploymentResult.ExtendedError.HResult:X8}).");
        }

        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Microsoft.UI.Xaml.Application.Start(p =>
            {
                var context = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
                System.Threading.SynchronizationContext.SetSynchronizationContext(context);
                _ = new App(single, configPath);
            });
        }
        finally
        {
            Bootstrap.Shutdown();
            single.Dispose();
        }
    }

    private static string? ParseConfigArg(string[] args)
    {
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--config", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                return Path.GetFullPath(args[i + 1]);
        }
        return null;
    }
}
