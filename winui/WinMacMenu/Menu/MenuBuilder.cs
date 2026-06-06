using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using WinMacMenu.Models;
using WinMacMenu.Services;

namespace WinMacMenu.Menu;

/// <summary>
/// Builds a WinUI <see cref="MenuFlyout"/> from the configuration. The default WinUI menu
/// styling already matches the Windows 11 Win+X power menu (rounded corners, acrylic,
/// cascading submenus), so no custom templates are required.
/// </summary>
public sealed class MenuBuilder
{
    // Guards against pathological recursion into deep/large folder trees.
    private const int DynamicNodeBudget = 2000;

    private readonly Config _cfg;
    private readonly IconResolver _icons;
    private readonly bool _light;
    private int _budget;

    public MenuBuilder(Config cfg, IconResolver icons, bool lightTheme)
    {
        _cfg = cfg;
        _icons = icons;
        _light = lightTheme;
    }

    public MenuFlyout Build()
    {
        var flyout = new MenuFlyout { Placement = FlyoutPlacementMode.BottomEdgeAlignedRight };
        foreach (var item in _cfg.Items)
            AddConfigItem(flyout.Items, item);
        TrimSeparators(flyout.Items);
        return flyout;
    }

    private void AddConfigItem(IList<MenuFlyoutItemBase> target, ConfigItem item)
    {
        switch (item.Type)
        {
            case ConfigItemType.Separator:
                target.Add(new MenuFlyoutSeparator());
                break;

            case ConfigItemType.Category:
                target.Add(new MenuFlyoutItem { Text = item.Label, IsEnabled = false });
                break;

            case ConfigItemType.Uri:
            case ConfigItemType.File:
            case ConfigItemType.Cmd:
            case ConfigItemType.PowerSleep:
            case ConfigItemType.PowerShutdown:
            case ConfigItemType.PowerRestart:
            case ConfigItemType.PowerLock:
            case ConfigItemType.PowerLogoff:
            case ConfigItemType.PowerHibernate:
                target.Add(LeafItem(item.Label, ConfigIcon(item), () => ActionLauncher.Run(item)));
                break;

            case ConfigItemType.Folder:
                AddFolder(target, item);
                break;

            case ConfigItemType.FolderSubmenu:
                AddSubmenu(target, item.Label, ConfigIcon(item),
                    () => FolderProvider.Enumerate(item.Path, _cfg, depth: 1));
                break;

            case ConfigItemType.RecentSubmenu:
                AddSubmenu(target, string.IsNullOrEmpty(item.Label) ? "Recent Items" : item.Label,
                    ConfigIcon(item), () => RecentItemsProvider.GetEntries(_cfg));
                break;

            case ConfigItemType.TaskKill:
                AddSubmenu(target, string.IsNullOrEmpty(item.Label) ? "End task" : item.Label,
                    ConfigIcon(item), () => TaskKillProvider.GetEntries(_cfg));
                break;

            case ConfigItemType.PowerMenu:
                AddPowerMenu(target, item);
                break;

            case ConfigItemType.ThisPc:
                AddSpecial(target, item, () => SpecialFolderProvider.ThisPc(_cfg));
                break;

            case ConfigItemType.Home:
                AddSpecial(target, item, () => SpecialFolderProvider.Home(_cfg));
                break;
        }
    }

    private void AddFolder(IList<MenuFlyoutItemBase> target, ConfigItem item)
    {
        if (item.Submenu)
        {
            AddSubmenu(target, item.Label, ConfigIcon(item),
                () => FolderProvider.Enumerate(item.Path, _cfg, depth: 1));
        }
        else if (item.InlineExpand)
        {
            AddInline(target, item, () => FolderProvider.Enumerate(item.Path, _cfg, depth: 1));
        }
        else
        {
            // Link mode: open the folder directly.
            target.Add(LeafItem(item.Label, ConfigIcon(item) ?? _icons.FromPath(item.Path, isDirectory: true),
                () => ActionLauncher.OpenPath(item.Path)));
        }
    }

    private void AddSpecial(IList<MenuFlyoutItemBase> target, ConfigItem item, Func<IReadOnlyList<DynamicEntry>> provider)
    {
        if (item.Submenu)
            AddSubmenu(target, item.Label, ConfigIcon(item), provider);
        else // inline (the default for This PC / Home)
            AddInline(target, item, provider);
    }

    private void AddInline(IList<MenuFlyoutItemBase> target, ConfigItem item, Func<IReadOnlyList<DynamicEntry>> provider)
    {
        if (!item.InlineNoHeader && !string.IsNullOrEmpty(item.Label))
        {
            if (item.InlineOpen)
                target.Add(LeafItem(item.Label, ConfigIcon(item), () => ActionLauncher.OpenPath(item.Path)));
            else
                target.Add(new MenuFlyoutItem { Text = item.Label, IsEnabled = false });
        }
        AddEntries(target, provider());
    }

    private void AddPowerMenu(IList<MenuFlyoutItemBase> target, ConfigItem item)
    {
        var sub = new MenuFlyoutSubItem { Text = string.IsNullOrEmpty(item.Label) ? "Power" : item.Label };
        if (ConfigIcon(item) is { } icon) sub.Icon = icon;

        void Add(bool excluded, string text, ConfigItemType type)
        {
            if (excluded) return;
            sub.Items.Add(LeafItem(text, null, () => ActionLauncher.Run(new ConfigItem { Type = type })));
        }

        Add(_cfg.ExcludeSleep, "Sleep", ConfigItemType.PowerSleep);
        Add(_cfg.ExcludeHibernate, "Hibernate", ConfigItemType.PowerHibernate);
        Add(_cfg.ExcludeRestart, "Restart", ConfigItemType.PowerRestart);
        Add(_cfg.ExcludeShutdown, "Shut down", ConfigItemType.PowerShutdown);
        Add(_cfg.ExcludeLock, "Lock", ConfigItemType.PowerLock);
        Add(_cfg.ExcludeLogoff, "Sign out", ConfigItemType.PowerLogoff);

        if (sub.Items.Count > 0)
            target.Add(sub);
    }

    private void AddSubmenu(IList<MenuFlyoutItemBase> target, string label, IconElement? icon,
        Func<IReadOnlyList<DynamicEntry>> provider)
    {
        var sub = new MenuFlyoutSubItem { Text = label };
        if (icon is not null) sub.Icon = icon;
        AddEntries(sub.Items, provider());
        target.Add(sub);
    }

    private void AddEntries(IList<MenuFlyoutItemBase> target, IReadOnlyList<DynamicEntry> entries)
    {
        foreach (var e in entries)
        {
            if (_budget >= DynamicNodeBudget) break;
            _budget++;

            if (e.IsSeparator)
            {
                target.Add(new MenuFlyoutSeparator());
                continue;
            }

            IconElement? icon = e.ShowIcon ? _icons.FromPath(e.Path, e.IsFolder) : null;

            if (e.Children is not null)
            {
                var sub = new MenuFlyoutSubItem { Text = e.Label };
                if (icon is not null) sub.Icon = icon;
                AddEntries(sub.Items, e.Children());
                target.Add(sub);
            }
            else
            {
                var path = e.Path;
                var action = e.Action ?? (() => ActionLauncher.OpenPath(path));
                target.Add(LeafItem(e.Label, icon, action));
            }
        }
    }

    private static MenuFlyoutItem LeafItem(string text, IconElement? icon, Action onClick)
    {
        var mi = new MenuFlyoutItem { Text = text };
        if (icon is not null) mi.Icon = icon;
        mi.Click += (_, _) => onClick();
        return mi;
    }

    /// <summary>Resolves a config item's themed icon spec, honouring the ShowIcons toggle.</summary>
    private IconElement? ConfigIcon(ConfigItem item)
    {
        if (_cfg.ShowIcons == 0)
            return null;
        var spec = _light ? item.IconPathLight : item.IconPathDark;
        if (string.IsNullOrEmpty(spec)) spec = item.IconPath;
        if (string.IsNullOrEmpty(spec)) spec = _light ? _cfg.DefaultIconPathLight : _cfg.DefaultIconPathDark;
        if (string.IsNullOrEmpty(spec)) spec = _cfg.DefaultIconPath;
        return _icons.FromSpec(spec);
    }

    /// <summary>Removes leading/trailing/duplicate separators left by hidden items.</summary>
    private static void TrimSeparators(IList<MenuFlyoutItemBase> items)
    {
        while (items.Count > 0 && items[0] is MenuFlyoutSeparator) items.RemoveAt(0);
        while (items.Count > 0 && items[^1] is MenuFlyoutSeparator) items.RemoveAt(items.Count - 1);
        for (int i = items.Count - 1; i > 0; i--)
            if (items[i] is MenuFlyoutSeparator && items[i - 1] is MenuFlyoutSeparator)
                items.RemoveAt(i);
    }
}
