using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace WinMacMenuWinUI3;

public class MenuItemTemplateSelector : DataTemplateSelector
{
    public DataTemplate? SeparatorTemplate { get; set; }
    public DataTemplate? CategoryTemplate  { get; set; }
    public DataTemplate? NormalTemplate    { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item)
    {
        if (item is MenuItemViewModel vm)
        {
            if (vm.IsSeparator) return SeparatorTemplate;
            if (vm.IsCategory)  return CategoryTemplate;
        }
        return NormalTemplate;
    }
}
