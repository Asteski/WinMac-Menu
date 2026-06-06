namespace WinMacMenu.Services;

/// <summary>A lightweight, UI-agnostic description of a generated submenu entry.</summary>
public sealed class DynamicEntry
{
    public string Label { get; init; } = string.Empty;

    /// <summary>Filesystem path used for opening and (optionally) for the shell icon.</summary>
    public string Path { get; init; } = string.Empty;

    public bool IsFolder { get; init; }
    public bool IsSeparator { get; init; }

    /// <summary>True if this entry should show its shell icon.</summary>
    public bool ShowIcon { get; init; }

    /// <summary>Custom click action; when null the entry opens <see cref="Path"/> via the shell.</summary>
    public Action? Action { get; init; }

    /// <summary>When set, the entry is a submenu populated lazily by this factory.</summary>
    public Func<IReadOnlyList<DynamicEntry>>? Children { get; init; }

    public static DynamicEntry Separator { get; } = new() { IsSeparator = true };
}
