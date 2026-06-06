namespace WinMacMenu.Configuration;

/// <summary>Environment-variable expansion equivalent to the Win32 app's <c>expand_env</c>.</summary>
public static class Env
{
    public static string Expand(string? value)
    {
        if (string.IsNullOrEmpty(value))
            return string.Empty;
        return Environment.ExpandEnvironmentVariables(value);
    }
}
