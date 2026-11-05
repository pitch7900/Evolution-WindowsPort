#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "CategoryStore.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

/* Catégories unifiées : éditeur de palette (Paramètres), menus d'affectation
 * et pastilles de couleur communs aux courriels, contacts, tâches et événements.
 * La logique pure (registre, sérialisation, filtres) est dans Categories.h. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    using ::EvolutionWin::CategoryStore;
    namespace Cat = ::EvolutionWin::Categories;

    /**
     * @brief Crée une pastille colorée portant le nom d'une catégorie.
     * @param name Nom de la catégorie.
     * @return Bordure colorée contenant le libellé.
     */
    Border MakeCategoryBadge(std::wstring const& name)
    {
        Border badge;
        badge.Background(SolidColorBrush{ CategoryStore::ColorOf(name) });
        badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
        badge.Padding(ThicknessHelper::FromLengths(4, 1, 4, 1));
        badge.VerticalAlignment(VerticalAlignment::Center);

        TextBlock text;
        text.Text(hstring{ name });
        text.FontSize(10);
        text.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        text.Foreground(SolidColorBrush{ Microsoft::UI::Colors::White() });
        badge.Child(text);

        Controls::ToolTipService::SetToolTip(badge, box_value(hstring{ name }));
        return badge;
    }

    /**
     * @brief Crée une icône ronde colorée (pastille) pour une catégorie.
     * @param color Couleur de la pastille.
     * @return Élément graphique de 16 pixels.
     */
    FontIcon MakeColorIcon(Windows::UI::Color const& color)
    {
        FontIcon icon;
        icon.Glyph(L"\xF127");
        icon.Foreground(SolidColorBrush{ color });
        return icon;
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Enregistre le registre des catégories puis rafraîchit les listes déjà chargées
     *        pour que les couleurs et les noms modifiés apparaissent partout.
     */
    void MainWindow::OnCategoriesRegistryChanged()
    {
        ::EvolutionWin::CategoryStore::Save();

        if (m_all_messages)
            ShowMessages(m_all_messages);
        if (m_all_tasks)
            ShowTasks();
        if (m_all_calendar_events)
            FilterAndRenderCalendarEvents();
        if (m_all_contacts && !m_gal_mode)
            ShowContacts();
    }

    /**
     * @brief Construit l'éditeur de palette dans Paramètres > Catégories.
     *
     * Une ligne par catégorie : pastille de couleur (sélecteur de couleur), nom
     * modifiable, raccourci indicatif et bouton de suppression ; puis ajout d'une
     * catégorie et rétablissement de la palette standard.
     */
    void MainWindow::RefreshCategoriesView()
    {
        auto host = CategoriesEditorHost();
        if (!host)
            return;
        host.Children().Clear();

        auto resources = Application::Current().Resources();
        auto caption_style = resources.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        auto secondary_brush = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>();
        auto card_brush = resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>();
        auto stroke_brush = resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>();

        auto& registry = ::EvolutionWin::CategoryStore::Registry();
        size_t index = 0;
        for (auto const& cat : registry.Items())
        {
            /* nom courant partagé entre les gestionnaires (change au renommage) */
            auto current = std::make_shared<std::wstring>(cat.name);

            Border card;
            card.Background(card_brush);
            card.BorderBrush(stroke_brush);
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            card.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));

            Grid row;
            row.ColumnSpacing(12);
            ColumnDefinition swatch_col;
            swatch_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(swatch_col);
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition shortcut_col;
            shortcut_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(shortcut_col);
            ColumnDefinition delete_col;
            delete_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(delete_col);

            /* pastille : ouvre un sélecteur de couleur */
            Button swatch;
            swatch.Width(36);
            swatch.Height(32);
            swatch.Padding(ThicknessHelper::FromUniformLength(0));
            swatch.Background(SolidColorBrush{ Windows::UI::Color{ 255, cat.r, cat.g, cat.b } });
            Controls::ToolTipService::SetToolTip(swatch, box_value(Tr(L"Choisir la couleur", L"Choose the colour")));

            ColorPicker picker;
            picker.IsAlphaEnabled(false);
            picker.IsHexInputVisible(true);
            picker.Color(Windows::UI::Color{ 255, cat.r, cat.g, cat.b });
            picker.ColorChanged([current, swatch](ColorPicker const&, ColorChangedEventArgs const& args)
            {
                auto color = args.NewColor();
                swatch.Background(SolidColorBrush{ color });
                ::EvolutionWin::CategoryStore::Registry().SetColor(*current, color.R, color.G, color.B);
            });
            Flyout picker_flyout;
            picker_flyout.Content(picker);
            picker_flyout.Closed([this](IInspectable const&, IInspectable const&)
            {
                OnCategoriesRegistryChanged();
            });
            swatch.Flyout(picker_flyout);
            row.Children().Append(swatch);

            /* nom modifiable : validé en quittant le champ ou par Entrée */
            TextBox name_box;
            name_box.Text(hstring{ cat.name });
            name_box.MaxLength(100);
            name_box.VerticalAlignment(VerticalAlignment::Center);
            name_box.LostFocus([this, current](IInspectable const& sender, RoutedEventArgs const&)
            {
                auto box = sender.as<TextBox>();
                std::wstring wanted = Cat::TrimName(std::wstring_view{ box.Text() });
                if (wanted == *current)
                    return;
                if (!::EvolutionWin::CategoryStore::Registry().Rename(*current, wanted))
                {
                    ShowStatus(Tr(L"Nom de catégorie vide, contenant une virgule ou déjà utilisé.", L"Category name is empty, contains a comma or is already used."), InfoBarSeverity::Warning);
                    box.Text(hstring{ *current });
                    return;
                }
                *current = wanted;
                box.Text(hstring{ wanted });
                OnCategoriesRegistryChanged();
                ShowStatus(Tr(L"Catégorie renommée. Les éléments déjà classés gardent l'ancien nom.", L"Category renamed. Items already categorised keep the old name."), InfoBarSeverity::Informational);
            });
            name_box.KeyDown([swatch](IInspectable const&, Input::KeyRoutedEventArgs const& args)
            {
                if (args.Key() == Windows::System::VirtualKey::Enter)
                    swatch.Focus(FocusState::Programmatic);
            });
            Grid::SetColumn(name_box, 1);
            row.Children().Append(name_box);

            std::wstring shortcut = Cat::CategoryRegistry::ShortcutForIndex(index);
            TextBlock shortcut_text;
            shortcut_text.Text(hstring{ shortcut });
            shortcut_text.Style(caption_style);
            shortcut_text.Foreground(secondary_brush);
            shortcut_text.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(shortcut_text, 2);
            row.Children().Append(shortcut_text);

            Button delete_button;
            FontIcon delete_icon;
            delete_icon.Glyph(L"\xE74D");
            delete_icon.FontSize(14);
            delete_button.Content(delete_icon);
            Controls::ToolTipService::SetToolTip(delete_button, box_value(Tr(L"Supprimer la catégorie", L"Delete category")));
            delete_button.Click([this, current](IInspectable const&, RoutedEventArgs const&)
            {
                if (::EvolutionWin::CategoryStore::Registry().Remove(*current))
                {
                    OnCategoriesRegistryChanged();
                    RefreshCategoriesView();
                    ShowStatus(Tr(L"Catégorie supprimée de la palette. Les éléments déjà classés la conservent.", L"Category removed from the palette. Items already categorised keep it."), InfoBarSeverity::Informational);
                }
            });
            Grid::SetColumn(delete_button, 3);
            row.Children().Append(delete_button);

            card.Child(row);
            host.Children().Append(card);
            ++index;
        }

        /* ajout d'une catégorie */
        Grid add_row;
        add_row.ColumnSpacing(8);
        add_row.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        add_row.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition add_col;
        add_col.Width(GridLengthHelper::Auto());
        add_row.ColumnDefinitions().Append(add_col);

        TextBox new_name;
        new_name.PlaceholderText(Tr(L"Nom de la nouvelle catégorie", L"New category name"));
        new_name.MaxLength(100);
        add_row.Children().Append(new_name);

        Button add_button;
        add_button.Content(box_value(Tr(L"Ajouter une catégorie", L"Add a category")));
        auto add_action = [this, new_name]()
        {
            std::wstring name = Cat::TrimName(std::wstring_view{ new_name.Text() });
            uint8_t r = 0, g = 0, b = 0;
            Cat::FallbackColor(name, r, g, b);
            if (!::EvolutionWin::CategoryStore::Registry().Add(name, r, g, b))
            {
                ShowStatus(Tr(L"Nom de catégorie vide, contenant une virgule ou déjà utilisé.", L"Category name is empty, contains a comma or is already used."), InfoBarSeverity::Warning);
                return;
            }
            OnCategoriesRegistryChanged();
            RefreshCategoriesView();
        };
        add_button.Click([add_action](IInspectable const&, RoutedEventArgs const&) { add_action(); });
        new_name.KeyDown([add_action](IInspectable const&, Input::KeyRoutedEventArgs const& args)
        {
            if (args.Key() == Windows::System::VirtualKey::Enter)
                add_action();
        });
        Grid::SetColumn(add_button, 1);
        add_row.Children().Append(add_button);
        host.Children().Append(add_row);

        /* palette standard */
        Button reset_button;
        reset_button.Content(box_value(Tr(L"Rétablir la palette standard", L"Restore the standard palette")));
        reset_button.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        reset_button.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            ::EvolutionWin::CategoryStore::Registry() = ::EvolutionWin::Categories::CategoryRegistry{};
            OnCategoriesRegistryChanged();
            RefreshCategoriesView();
            ShowStatus(Tr(L"Palette de catégories rétablie.", L"Category palette restored."), InfoBarSeverity::Informational);
        });
        host.Children().Append(reset_button);

        TextBlock note;
        note.Text(Tr(L"Supprimer ou renommer une catégorie ne modifie pas les éléments déjà classés : ils gardent leur ancien nom, avec une couleur calculée à partir de ce nom.", L"Deleting or renaming a category does not change items already categorised: they keep their old name, with a colour derived from that name."));
        note.TextWrapping(TextWrapping::Wrap);
        note.Style(caption_style);
        note.Foreground(secondary_brush);
        host.Children().Append(note);
    }

    /**
     * @brief Remplit un menu avec les catégories du registre, cochées selon l'élément ciblé.
     *
     * Les catégories portées par l'élément mais absentes du registre sont aussi
     * proposées (cochées) pour pouvoir les retirer.
     *
     * @param items Éléments du menu à peupler (vidés au préalable).
     * @param current Catégories actuelles de l'élément.
     * @param apply Rappel appelé avec la nouvelle liste complète de catégories de l'élément.
     */
    void MainWindow::PopulateItemCategoryMenu(
        Windows::Foundation::Collections::IVector<MenuFlyoutItemBase> const& items,
        std::vector<std::wstring> const& current,
        std::function<void(std::vector<std::wstring> const&)> apply)
    {
        items.Clear();

        auto add_toggle = [&](std::wstring const& name, Windows::UI::Color const& color, std::wstring const& shortcut)
        {
            bool present = std::any_of(current.begin(), current.end(),
                [&](std::wstring const& have) { return Cat::NamesEqual(have, name); });

            ToggleMenuFlyoutItem item;
            item.Text(hstring{ name });
            if (!shortcut.empty())
                item.KeyboardAcceleratorTextOverride(hstring{ shortcut });
            item.Icon(MakeColorIcon(color));
            item.IsChecked(present);
            item.Click([current, name, present, apply](IInspectable const&, RoutedEventArgs const&)
            {
                auto next = current;
                Cat::SetCategoryPresent(next, name, !present);
                apply(next);
            });
            items.Append(item);
        };

        auto const& registry = ::EvolutionWin::CategoryStore::Registry();
        size_t index = 0;
        for (auto const& cat : registry.Items())
            add_toggle(cat.name, Windows::UI::Color{ 255, cat.r, cat.g, cat.b }, Cat::CategoryRegistry::ShortcutForIndex(index++));

        for (auto const& name : current)
        {
            if (!registry.Contains(name))
                add_toggle(name, ::EvolutionWin::CategoryStore::ColorOf(name), L"");
        }

        items.Append(MenuFlyoutSeparator{});

        MenuFlyoutItem clear_item;
        clear_item.Text(Tr(L"Effacer toutes les catégories", L"Clear all categories"));
        FontIcon clear_icon;
        clear_icon.Glyph(L"\xE74D");
        clear_item.Icon(clear_icon);
        clear_item.IsEnabled(!current.empty());
        clear_item.Click([apply](IInspectable const&, RoutedEventArgs const&)
        {
            apply(std::vector<std::wstring>{});
        });
        items.Append(clear_item);

        MenuFlyoutItem manage_item;
        manage_item.Text(Tr(L"Gérer les catégories…", L"Manage categories..."));
        FontIcon manage_icon;
        manage_icon.Glyph(L"\xE713");
        manage_item.Icon(manage_icon);
        manage_item.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            Navigation().SelectedItem(Navigation().SettingsItem());
            SelectSettingsCategory(L"categories");
        });
        items.Append(manage_item);
    }

    /**
     * @brief Associe à un élément graphique un menu contextuel « Catégoriser ».
     * @param element Élément (ligne de liste, bloc d'événement…) qui reçoit le clic droit.
     * @param current Catégories actuelles de l'élément métier représenté.
     * @param apply Rappel appelé avec la nouvelle liste de catégories.
     */
    void MainWindow::AttachCategoryContextMenu(
        UIElement const& element,
        std::vector<std::wstring> const& current,
        std::function<void(std::vector<std::wstring> const&)> apply)
    {
        MenuFlyout flyout;
        MenuFlyoutSubItem sub;
        sub.Text(Tr(L"Catégoriser", L"Categorise"));
        FontIcon icon;
        icon.Glyph(L"\xE8EC");
        sub.Icon(icon);
        PopulateItemCategoryMenu(sub.Items(), current, std::move(apply));
        flyout.Items().Append(sub);
        element.ContextFlyout(flyout);
    }

    /**
     * @brief Construit une rangée de pastilles colorées pour une liste de catégories.
     * @param names Noms de catégories.
     * @return Panneau horizontal (vide si aucune catégorie).
     */
    UIElement MainWindow::BuildCategoryChips(std::vector<std::wstring> const& names)
    {
        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Spacing(4);
        for (auto const& name : names)
            panel.Children().Append(MakeCategoryBadge(name));
        panel.Visibility(names.empty() ? Visibility::Collapsed : Visibility::Visible);
        return panel;
    }

    /**
     * @brief Remplace les catégories d'une tâche (propriété iCalendar CATEGORIES) puis recharge la liste.
     * @param task Tâche au format JSON du moteur (list_uid, uid).
     * @param names Nouvelle liste de catégories.
     */
    void MainWindow::SetTaskCategories(JsonObject const& task, std::vector<std::wstring> const& names)
    {
        JsonObject request;
        request.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(task.GetNamedString(L"list_uid", L"")));
        request.SetNamedValue(L"uid", JsonValue::CreateStringValue(task.GetNamedString(L"uid", L"")));
        request.SetNamedValue(L"categories", ::EvolutionWin::CategoryStore::ToJsonArray(names));

        m_core->ModifyTask(request.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
                ShowStatus(Tr(L"Catégories de la tâche : ", L"Task categories: ") + error, InfoBarSeverity::Error);
            LoadTasks();
        });
    }

    /**
     * @brief Remplace les catégories d'un événement (propriété iCalendar CATEGORIES) puis recharge le calendrier.
     * @param event Événement au format JSON du moteur (calendar_uid, uid).
     * @param names Nouvelle liste de catégories.
     */
    void MainWindow::SetEventCategories(JsonObject const& event, std::vector<std::wstring> const& names)
    {
        JsonObject request;
        request.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(event.GetNamedString(L"calendar_uid", L"")));
        request.SetNamedValue(L"uid", JsonValue::CreateStringValue(event.GetNamedString(L"uid", L"")));
        request.SetNamedValue(L"categories", ::EvolutionWin::CategoryStore::ToJsonArray(names));

        m_core->ModifyEvent(request.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
                ShowStatus(Tr(L"Catégories de l'événement : ", L"Event categories: ") + error, InfoBarSeverity::Error);
            LoadEvents();
        });
    }

    /**
     * @brief Remplace les catégories d'un contact (champ vCard CATEGORIES) puis recharge la liste.
     * @param contact Contact au format JSON du moteur (book_uid, uid).
     * @param names Nouvelle liste de catégories.
     */
    void MainWindow::SetContactCategories(JsonObject const& contact, std::vector<std::wstring> const& names)
    {
        m_core->SetContactCategories(
            contact.GetNamedString(L"book_uid", L""),
            contact.GetNamedString(L"uid", L""),
            ::EvolutionWin::CategoryStore::ToJsonArray(names).Stringify(),
            [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
                ShowStatus(Tr(L"Catégories du contact : ", L"Contact categories: ") + error, InfoBarSeverity::Error);
            LoadContacts(m_contact_query);
        });
    }
    /**
     * @brief Ouvre le menu de filtrage par catégorie d'une liste.
     *
     * Une entrée cochable par catégorie du registre (logique « ou » entre les
     * catégories cochées), une entrée « Sans catégorie » et « Effacer le filtre ».
     *
     * @param anchor Bouton sous lequel s'affiche le menu.
     * @param filter Critère de filtrage de la liste (modifié par le menu).
     * @param changed Rappel appelé après chaque modification pour réafficher la liste.
     */
    void MainWindow::ShowCategoryFilterMenu(
        FrameworkElement const& anchor,
        Cat::CategoryFilter& filter,
        std::function<void()> changed)
    {
        MenuFlyout flyout;
        Cat::CategoryFilter* target = &filter;

        for (auto const& cat : ::EvolutionWin::CategoryStore::Registry().Items())
        {
            std::wstring name = cat.name;
            ToggleMenuFlyoutItem item;
            item.Text(hstring{ name });
            item.Icon(MakeColorIcon(Windows::UI::Color{ 255, cat.r, cat.g, cat.b }));
            item.IsChecked(std::any_of(filter.selected.begin(), filter.selected.end(),
                [&](std::wstring const& n) { return Cat::NamesEqual(n, name); }));
            item.Click([this, target, name, changed](IInspectable const&, RoutedEventArgs const&)
            {
                Cat::ToggleFilterCategory(*target, name);
                UpdateCategoryFilterButtons();
                changed();
            });
            flyout.Items().Append(item);
        }

        flyout.Items().Append(MenuFlyoutSeparator{});

        ToggleMenuFlyoutItem none_item;
        none_item.Text(Tr(L"Sans catégorie", L"No category"));
        none_item.IsChecked(filter.include_uncategorized);
        none_item.Click([this, target, changed](IInspectable const&, RoutedEventArgs const&)
        {
            target->include_uncategorized = !target->include_uncategorized;
            UpdateCategoryFilterButtons();
            changed();
        });
        flyout.Items().Append(none_item);

        MenuFlyoutItem clear_item;
        clear_item.Text(Tr(L"Effacer le filtre", L"Clear filter"));
        clear_item.IsEnabled(!filter.IsEmpty());
        clear_item.Click([this, target, changed](IInspectable const&, RoutedEventArgs const&)
        {
            *target = Cat::CategoryFilter{};
            UpdateCategoryFilterButtons();
            changed();
        });
        flyout.Items().Append(clear_item);

        flyout.ShowAt(anchor);
    }

    /**
     * @brief Met à jour l'aspect des quatre boutons de filtre par catégorie
     *        (icône en couleur d'accent et infobulle détaillée quand un filtre est actif).
     */
    void MainWindow::UpdateCategoryFilterButtons()
    {
        auto update = [](Button const& button, Cat::CategoryFilter const& filter)
        {
            if (!button)
                return;

            FontIcon icon;
            icon.Glyph(L"\xE8EC");
            icon.FontSize(13);
            if (!filter.IsEmpty())
                icon.Foreground(Application::Current().Resources().Lookup(box_value(L"AccentTextFillColorPrimaryBrush")).as<Brush>());

            std::wstring tip{ Tr(L"Filtrer par catégorie", L"Filter by category") };
            if (!filter.IsEmpty())
            {
                std::vector<std::wstring> shown = filter.selected;
                if (filter.include_uncategorized)
                    shown.push_back(std::wstring{ Tr(L"Sans catégorie", L"No category") });
                tip += L" : " + Cat::JoinCategories(shown);
            }
            button.Content(icon);
            Controls::ToolTipService::SetToolTip(button, box_value(hstring{ tip }));
        };

        update(MailCategoryFilterButton(), m_mail_category_filter);
        update(TaskCategoryFilterButton(), m_task_category_filter);
        update(ContactCategoryFilterButton(), m_contact_category_filter);
        update(EventCategoryFilterButton(), m_event_category_filter);
    }

    /** @brief Bouton de filtre par catégorie de la liste des courriels. */
    void MainWindow::OnMailCategoryFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        ShowCategoryFilterMenu(sender.as<FrameworkElement>(), m_mail_category_filter, [this]() { ReapplyMessageFilters(); });
    }

    /** @brief Bouton de filtre par catégorie de la liste des tâches. */
    void MainWindow::OnTaskCategoryFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        ShowCategoryFilterMenu(sender.as<FrameworkElement>(), m_task_category_filter, [this]() { ShowTasks(); });
    }

    /** @brief Bouton de filtre par catégorie de la liste des contacts. */
    void MainWindow::OnContactCategoryFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        ShowCategoryFilterMenu(sender.as<FrameworkElement>(), m_contact_category_filter, [this]() { if (!m_gal_mode) ShowContacts(); });
    }

    /** @brief Bouton de filtre par catégorie du calendrier (liste et grilles). */
    void MainWindow::OnEventCategoryFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        ShowCategoryFilterMenu(sender.as<FrameworkElement>(), m_event_category_filter, [this]() { FilterAndRenderCalendarEvents(); });
    }
}
