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

    public event EventHandler<ConfigItem>? ItemClicked;

    public MenuItemControl()
    {
        InitializeComponent();
        // Refresh after layout is ready — guards against the property being set
        // before InitializeComponent has wired up the named elements.
        Loaded += (_, _) => Refresh();
    }

    private static void OnItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((MenuItemControl)d).Refresh();

    private void Refresh()
    {
        var item = Item;
        if (item == null) return;

        SeparatorRoot.Visibility = Visibility.Collapsed;
        CategoryRoot.Visibility  = Visibility.Collapsed;
        ItemRoot.Visibility      = Visibility.Collapsed;

        if (item.IsSeparator)
        {
            SeparatorRoot.Visibility = Visibility.Visible;
            return;
        }

        if (item.IsCategory)
        {
            CategoryRoot.Text       = item.Label;
            CategoryRoot.Visibility = Visibility.Visible;
            return;
        }

        ItemLabel.Text      = item.Label;
        ItemRoot.Visibility = Visibility.Visible;

        bool hasSubmenu = item.Type is
            ConfigItemType.Folder or ConfigItemType.FolderSubmenu or
            ConfigItemType.PowerMenu or ConfigItemType.TaskKill or
            ConfigItemType.Recent or ConfigItemType.RecentSubmenu;
        SubArrow.Visibility = hasSubmenu ? Visibility.Visible : Visibility.Collapsed;

        LoadIcon(item);
    }

    private void LoadIcon(ConfigItem item)
    {
        var path = item.IconPath;

        // DLL/EXE resource icons (e.g. "shell32.dll,-271") — not yet supported
        if (string.IsNullOrEmpty(path) || path.Contains(".dll,") || path.Contains(".exe,"))
        {
            ItemIcon.Visibility = Visibility.Collapsed;
            return;
        }

        try
        {
            ItemIcon.Source = new Microsoft.UI.Xaml.Media.Imaging.BitmapImage(new Uri(path));
            ItemIcon.Visibility = Visibility.Visible;
        }
        catch
        {
            ItemIcon.Visibility = Visibility.Collapsed;
        }
    }

    private void ItemRoot_Click(object sender, RoutedEventArgs e)
    {
        if (Item != null)
            ItemClicked?.Invoke(this, Item);
    }
}
