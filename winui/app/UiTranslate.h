#pragma once

/**
 * @file UiTranslate.h
 * @brief Traduction à l'exécution des textes statiques déclarés en XAML.
 *
 * Les fragments XAML contiennent les libellés en français. Après le chargement de la
 * fenêtre, ApplyToElement() parcourt l'arbre logique (puis visuel) et remplace chaque
 * texte français par sa traduction dans la langue active, retrouvée par Tr() grâce à
 * la clé déduite du libellé (cf. I18n::KeyFor). Un texte absent des catalogues reste tel
 * quel. En français, rien n'est parcouru.
 */

#include "MainWindow.Helpers.h"
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace EvolutionWin::UiTranslate
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    namespace Docs = winrt::Microsoft::UI::Xaml::Documents;

    /**
     * @brief Traduit un libellé français (retourne le libellé inchangé s'il est inconnu).
     * @param text Libellé source.
     * @return Libellé traduit.
     */
    inline hstring T(hstring const& text)
    {
        if (text.empty())
            return text;
        std::wstring_view view{ text };
        if (view.find_first_not_of(L" \t\r\n") == std::wstring_view::npos)
            return text;
        return ::EvolutionWin::Helpers::Tr(text.c_str());
    }

    /**
     * @brief Traduit un objet de contenu s'il s'agit d'une chaîne.
     * @param value Objet (souvent une chaîne boxée).
     * @param out Reçoit le nouvel objet si une traduction s'applique.
     * @return true si la valeur est une chaîne dont la traduction diffère.
     */
    inline bool TranslateBoxed(Windows::Foundation::IInspectable const& value, Windows::Foundation::IInspectable& out)
    {
        if (!value)
            return false;
        auto property = value.try_as<Windows::Foundation::IPropertyValue>();
        if (!property || property.Type() != Windows::Foundation::PropertyType::String)
            return false;
        hstring source = property.GetString();
        hstring translated = T(source);
        if (translated == source)
            return false;
        out = box_value(translated);
        return true;
    }

    inline void ApplyToElement(DependencyObject const& element, bool visual);

    /** @brief Traduit les éléments d'un menu déroulant (MenuFlyout) et de ses sous-menus. */
    template <typename Items>
    inline void ApplyToMenuItems(Items const& items, bool visual)
    {
        for (auto const& base : items)
        {
            if (auto sub = base.template try_as<MenuFlyoutSubItem>())
            {
                sub.Text(T(sub.Text()));
                ApplyToMenuItems(sub.Items(), visual);
            }
            else if (auto item = base.template try_as<MenuFlyoutItem>())
            {
                item.Text(T(item.Text()));
            }
        }
    }

    /** @brief Traduit le contenu d'une infobulle ou d'un menu rattaché (Flyout). */
    inline void ApplyToFlyout(Primitives::FlyoutBase const& flyout, bool visual)
    {
        if (!flyout)
            return;
        if (auto menu = flyout.try_as<MenuFlyout>())
        {
            ApplyToMenuItems(menu.Items(), visual);
        }
        else if (auto plain = flyout.try_as<Flyout>())
        {
            if (auto content = plain.Content())
                ApplyToElement(content, visual);
        }
        else if (auto commands = flyout.try_as<CommandBarFlyout>())
        {
            for (auto const& command : commands.PrimaryCommands())
                ApplyToElement(command.try_as<DependencyObject>(), visual);
            for (auto const& command : commands.SecondaryCommands())
                ApplyToElement(command.try_as<DependencyObject>(), visual);
        }
    }

    /** @brief Traduit les en-têtes de type objet (Header) et les contenus textuels simples. */
    inline void ApplyHeader(Windows::Foundation::IInspectable const& header, std::function<void(Windows::Foundation::IInspectable const&)> const& set, bool visual)
    {
        Windows::Foundation::IInspectable translated{ nullptr };
        if (TranslateBoxed(header, translated))
            set(translated);
        else if (auto child = header.try_as<DependencyObject>())
            ApplyToElement(child, visual);
    }

    /**
     * @brief Traduit un élément et, récursivement, ses enfants logiques (et visuels si demandé).
     * @param element Élément XAML de départ (peut être nul).
     * @param visual true pour parcourir aussi l'arbre visuel (éléments déjà chargés).
     */
    inline void ApplyToElement(DependencyObject const& element, bool visual)
    {
        if (!element)
            return;

        // Texte simple
        if (auto text = element.try_as<TextBlock>())
        {
            auto inlines = text.Inlines();
            if (inlines.Size() == 0)
            {
                text.Text(T(text.Text()));
            }
            else
            {
                std::function<void(Docs::InlineCollection const&)> walk = [&](Docs::InlineCollection const& collection)
                {
                    for (auto const& inl : collection)
                    {
                        if (auto run = inl.try_as<Docs::Run>())
                            run.Text(T(run.Text()));
                        else if (auto span = inl.try_as<Docs::Span>())
                            walk(span.Inlines());
                    }
                };
                walk(inlines);
            }
        }

        // Saisie : en-têtes et texte indicatif
        if (auto box = element.try_as<TextBox>())
        {
            box.PlaceholderText(T(box.PlaceholderText()));
            ApplyHeader(box.Header(), [&](auto const& v) { box.Header(v); }, visual);
        }
        if (auto password = element.try_as<PasswordBox>())
        {
            password.PlaceholderText(T(password.PlaceholderText()));
            ApplyHeader(password.Header(), [&](auto const& v) { password.Header(v); }, visual);
        }
        if (auto suggest = element.try_as<AutoSuggestBox>())
        {
            suggest.PlaceholderText(T(suggest.PlaceholderText()));
            ApplyHeader(suggest.Header(), [&](auto const& v) { suggest.Header(v); }, visual);
        }
        if (auto number = element.try_as<NumberBox>())
        {
            number.PlaceholderText(T(number.PlaceholderText()));
            ApplyHeader(number.Header(), [&](auto const& v) { number.Header(v); }, visual);
        }
        if (auto combo = element.try_as<ComboBox>())
        {
            combo.PlaceholderText(T(combo.PlaceholderText()));
            ApplyHeader(combo.Header(), [&](auto const& v) { combo.Header(v); }, visual);
        }
        if (auto date = element.try_as<DatePicker>())
            ApplyHeader(date.Header(), [&](auto const& v) { date.Header(v); }, visual);
        if (auto time = element.try_as<TimePicker>())
            ApplyHeader(time.Header(), [&](auto const& v) { time.Header(v); }, visual);
        if (auto calendar = element.try_as<CalendarDatePicker>())
        {
            calendar.PlaceholderText(T(calendar.PlaceholderText()));
            ApplyHeader(calendar.Header(), [&](auto const& v) { calendar.Header(v); }, visual);
        }
        if (auto slider = element.try_as<Slider>())
            ApplyHeader(slider.Header(), [&](auto const& v) { slider.Header(v); }, visual);
        if (auto radios = element.try_as<RadioButtons>())
            ApplyHeader(radios.Header(), [&](auto const& v) { radios.Header(v); }, visual);
        if (auto toggle = element.try_as<ToggleSwitch>())
        {
            ApplyHeader(toggle.Header(), [&](auto const& v) { toggle.Header(v); }, visual);
            ApplyHeader(toggle.OnContent(), [&](auto const& v) { toggle.OnContent(v); }, visual);
            ApplyHeader(toggle.OffContent(), [&](auto const& v) { toggle.OffContent(v); }, visual);
        }

        // Barres de commandes
        if (auto button = element.try_as<AppBarButton>())
        {
            button.Label(T(button.Label()));
            ApplyToFlyout(button.Flyout(), visual);
        }
        else if (auto toggle_button = element.try_as<AppBarToggleButton>())
        {
            toggle_button.Label(T(toggle_button.Label()));
        }
        if (auto bar = element.try_as<CommandBar>())
        {
            for (auto const& command : bar.PrimaryCommands())
                ApplyToElement(command.try_as<DependencyObject>(), visual);
            for (auto const& command : bar.SecondaryCommands())
                ApplyToElement(command.try_as<DependencyObject>(), visual);
        }

        // Boutons à menu
        if (auto split = element.try_as<SplitButton>())
            ApplyToFlyout(split.Flyout(), visual);
        if (auto toggle_split = element.try_as<ToggleSplitButton>())
            ApplyToFlyout(toggle_split.Flyout(), visual);
        if (auto plain_button = element.try_as<Button>())
            ApplyToFlyout(plain_button.Flyout(), visual);
        if (auto ui = element.try_as<UIElement>())
            ApplyToFlyout(ui.ContextFlyout(), visual);

        // Informations
        if (auto info = element.try_as<InfoBar>())
        {
            info.Title(T(info.Title()));
            info.Message(T(info.Message()));
        }
        if (auto tip = element.try_as<TeachingTip>())
        {
            tip.Title(T(tip.Title()));
            tip.Subtitle(T(tip.Subtitle()));
        }
        if (auto menu_bar_item = element.try_as<MenuBarItem>())
        {
            menu_bar_item.Title(T(menu_bar_item.Title()));
            ApplyToMenuItems(menu_bar_item.Items(), visual);
        }

        // Navigation et conteneurs avec en-tête
        if (auto nav = element.try_as<NavigationView>())
        {
            for (auto const& item : nav.MenuItems())
                ApplyToElement(item.try_as<DependencyObject>(), visual);
            for (auto const& item : nav.FooterMenuItems())
                ApplyToElement(item.try_as<DependencyObject>(), visual);
            ApplyHeader(nav.Header(), [&](auto const& v) { nav.Header(v); }, visual);
            nav.PaneTitle(T(nav.PaneTitle()));
        }
        if (auto expander = element.try_as<Expander>())
            ApplyHeader(expander.Header(), [&](auto const& v) { expander.Header(v); }, visual);
        if (auto tab_view = element.try_as<TabView>())
        {
            for (auto const& item : tab_view.TabItems())
                ApplyToElement(item.try_as<DependencyObject>(), visual);
        }
        if (auto tab = element.try_as<TabViewItem>())
            ApplyHeader(tab.Header(), [&](auto const& v) { tab.Header(v); }, visual);
        if (auto split_view = element.try_as<SplitView>())
        {
            ApplyToElement(split_view.Pane(), visual);
            ApplyToElement(split_view.Content(), visual);
        }

        // Listes : éléments en ligne (chaînes ou conteneurs)
        if (auto items_control = element.try_as<ItemsControl>())
        {
            auto items = items_control.Items();
            auto selector = element.try_as<Primitives::Selector>();
            int32_t selected = selector ? selector.SelectedIndex() : -1;
            bool replaced = false;
            for (uint32_t i = 0; i < items.Size(); ++i)
            {
                auto item = items.GetAt(i);
                Windows::Foundation::IInspectable translated{ nullptr };
                if (TranslateBoxed(item, translated))
                {
                    items.SetAt(i, translated);
                    replaced = true;
                }
                else if (auto child = item.try_as<DependencyObject>())
                {
                    ApplyToElement(child, visual);
                }
            }
            if (replaced && selector && selector.SelectedIndex() != selected)
                selector.SelectedIndex(selected);
        }

        // Contenu (boutons, cases à cocher, éléments de liste, conteneurs de défilement…)
        if (auto content_control = element.try_as<ContentControl>())
        {
            Windows::Foundation::IInspectable translated{ nullptr };
            auto content = content_control.Content();
            if (TranslateBoxed(content, translated))
                content_control.Content(translated);
            else if (auto child = content.try_as<DependencyObject>())
                ApplyToElement(child, visual);
        }
        if (auto presenter = element.try_as<ContentPresenter>())
        {
            Windows::Foundation::IInspectable translated{ nullptr };
            auto content = presenter.Content();
            if (TranslateBoxed(content, translated))
                presenter.Content(translated);
            else if (auto child = content.try_as<DependencyObject>())
                ApplyToElement(child, visual);
        }
        if (auto border = element.try_as<Border>())
            ApplyToElement(border.Child(), visual);
        if (auto viewbox = element.try_as<Viewbox>())
            ApplyToElement(viewbox.Child(), visual);
        if (auto panel = element.try_as<Panel>())
        {
            for (auto const& child : panel.Children())
                ApplyToElement(child, visual);
        }

        // Infobulle et nom d'accessibilité
        if (auto ui = element.try_as<UIElement>())
        {
            auto tip = ToolTipService::GetToolTip(ui);
            Windows::Foundation::IInspectable translated{ nullptr };
            if (TranslateBoxed(tip, translated))
                ToolTipService::SetToolTip(ui, translated);
            else if (auto tool_tip = tip.try_as<ToolTip>())
            {
                if (TranslateBoxed(tool_tip.Content(), translated))
                    tool_tip.Content(translated);
            }
            hstring name = Automation::AutomationProperties::GetName(ui);
            if (!name.empty())
                Automation::AutomationProperties::SetName(ui, T(name));
        }

        // Arbre visuel (éléments créés par les modèles de contrôle)
        if (visual)
        {
            int count = Media::VisualTreeHelper::GetChildrenCount(element);
            for (int i = 0; i < count; ++i)
                ApplyToElement(Media::VisualTreeHelper::GetChild(element, i), true);
        }
    }

    /**
     * @brief Traduit tout le contenu d'une fenêtre (titre compris) dans la langue active.
     * À appeler après InitializeComponent(), puis à nouveau à l'événement Loaded du contenu.
     * @param window Fenêtre XAML.
     * @param visual true pour inclure l'arbre visuel (à passer à true une fois chargée).
     */
    inline void ApplyToWindow(Window const& window, bool visual = false)
    {
        try
        {
            std::wstring lang = ::EvolutionWin::Helpers::GetEffectiveUiLanguage();
            if (lang.size() >= 2 && (lang[0] == L'f' || lang[0] == L'F') && (lang[1] == L'r' || lang[1] == L'R'))
                return;
            window.Title(T(window.Title()));
            ApplyToElement(window.Content(), visual);
        }
        catch (...) {}
    }
}
