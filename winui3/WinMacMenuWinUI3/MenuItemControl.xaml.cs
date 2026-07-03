using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3;

public sealed partial class MenuItemControl : UserControl
{
    public static readonly DependencyProperty ItemProperty =
        DependencyProperty.Register(nameof(Item), typeof(ConfigItem), typeof(MenuItemControl),
            new PropertyMetadata(null, OnItemChanged));

    public ConfigItem? Item
    {
        get => (ConfigItem?)GetValue(ItemProperty);
        set => SetValue(ItemProperty, value);
    }

    public MenuItemControl()
    {
        InitializeComponent();
    }

    private static void OnItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((MenuItemControl)d).Refresh();
    }

    private void Refresh()
    {
        var item = Item;
        if (item == null) return;

        // Hide all variants first
        SeparatorLine.Visibility  = Visibility.Collapsed;
        CategoryLabel.Visibility  = Visibility.Collapsed;
        ItemButton.Visibility     = Visibility.Collapsed;

        if (item.IsSeparator)
        {
            SeparatorLine.Visibility = Visibility.Visible;
            return;
        }

        if (item.IsCategory)
        {
            CategoryLabel.Text       = item.Label;
            CategoryLabel.Visibility = Visibility.Visible;
            return;
        }

        // Regular item
        ItemLabel.Text       = item.Label;
        ItemButton.Visibility = Visibility.Visible;

        // Show submenu arrow for items that expand
        var hasSubmenu = item.Type is ConfigItemType.Folder or ConfigItemType.FolderSubmenu
                      or ConfigItemType.PowerMenu or ConfigItemType.TaskKill
                      or ConfigItemType.RecentSubmenu or ConfigItemType.Recent;
        SubArrow.Visibility = hasSubmenu ? Visibility.Visible : Visibility.Collapsed;

        // Load icon if path provided
        LoadIcon(item);
    }

    private void LoadIcon(ConfigItem item)
    {
        var iconPath = item.IconPath;
        if (string.IsNullOrEmpty(iconPath))
        {
            // Use built-in Segoe Fluent Icons for well-known types
            ItemIcon.Visibility = Visibility.Collapsed;
            return;
        }

        // For DLL resource icons (e.g. "shell32.dll,-271") we skip loading on this pass
        // and rely on the shell icon service (not yet implemented)
        if (iconPath.Contains(".dll,") || iconPath.Contains(".exe,"))
        {
            ItemIcon.Visibility = Visibility.Collapsed;
            return;
        }

        try
        {
            var bmp = new Microsoft.UI.Xaml.Media.Imaging.BitmapImage(new Uri(iconPath));
            ItemIcon.Source  = bmp;
            ItemIcon.Visibility = Visibility.Visible;
        }
        catch
        {
            ItemIcon.Visibility = Visibility.Collapsed;
        }
    }

    // Raised when the user clicks this item; MenuWindow subscribes via ItemsRepeater
    public event EventHandler<ConfigItem>? ItemClicked;

    private void ItemButton_Click(object sender, RoutedEventArgs e)
    {
        if (Item != null)
            ItemClicked?.Invoke(this, Item);
    }
}
