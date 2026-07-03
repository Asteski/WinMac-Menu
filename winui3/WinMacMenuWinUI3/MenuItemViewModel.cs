using Microsoft.UI.Xaml;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3;

public class MenuItemViewModel
{
    public ConfigItem Item { get; }
    private readonly Action<ConfigItem> _onClick;

    public string Label       => Item.Label;
    public bool   IsSeparator => Item.IsSeparator;
    public bool   IsCategory  => Item.IsCategory;

    public bool HasSubmenu => Item.Type is
        ConfigItemType.Folder or ConfigItemType.FolderSubmenu or
        ConfigItemType.PowerMenu or ConfigItemType.TaskKill or
        ConfigItemType.Recent or ConfigItemType.RecentSubmenu;

    public Visibility ChevronVisibility =>
        HasSubmenu ? Visibility.Visible : Visibility.Collapsed;

    public MenuItemViewModel(ConfigItem item, Action<ConfigItem> onClick)
    {
        Item = item;
        _onClick = onClick;
    }

    public void Click() => _onClick(Item);
}
