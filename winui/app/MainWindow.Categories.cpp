#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "CategoryStore.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
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
     * @brief Répercute une modification de catégorie sur les comptes auxquels elle est rattachée.
     *
     * Les échecs (droits manquants, réseau) sont signalés dans la barre d'état ; la
     * modification locale reste valable.
     *
     * @param action "create", "rename", "recolor" ou "delete".
     * @param name Nom de la catégorie (ancien nom pour "rename").
     * @param new_name Nouveau nom ("rename"), sinon vide.
     * @param color_source Nom sous lequel lire la couleur dans le registre (vide : pas de couleur).
     * @param accounts Comptes visés ; nullptr pour lire ceux de la catégorie dans le registre.
     */
    void MainWindow::PushCategoryChange(
        std::wstring const& action,
        std::wstring const& name,
        std::wstring const& new_name,
        std::wstring const& color_source,
        std::vector<std::wstring> const* accounts)
    {
        auto const* found = ::EvolutionWin::CategoryStore::Registry().Find(color_source);
        std::vector<std::wstring> targets = accounts ? *accounts : (found ? found->accounts : std::vector<std::wstring>{});
        if (targets.empty())
            return;

        std::wstring color;
        if (found)
            color = Cat::FormatHexColor(found->r, found->g, found->b);

        for (auto const& uid : targets)
        {
            m_core->EditMailCategory(hstring{ uid }, hstring{ action }, hstring{ name }, hstring{ new_name }, hstring{ color },
                [this, uid](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(Fmt(Tr(L"Catégorie non mise à jour sur le compte {0} : {1}"), { hstring{ uid }, error }), InfoBarSeverity::Warning);
            });
        }
    }

    /**
     * @brief Regroupe les catégories du registre par compte pour l'affichage.
     *
     * Les catégories locales viennent en premier, puis un groupe par compte
     * (dans l'ordre de la liste des comptes). Une catégorie présente sur plusieurs
     * comptes figure dans chaque groupe.
     *
     * @return Paires (titre du groupe, positions dans le registre) ; vide pour un affichage à plat
     *         (aucune catégorie rattachée à un compte).
     */
    std::vector<std::pair<hstring, std::vector<size_t>>> MainWindow::CategoryGroupsForDisplay()
    {
        std::vector<std::pair<hstring, std::vector<size_t>>> result;
        if (!m_accounts)
            return result;

        std::vector<std::wstring> uids;
        std::unordered_map<std::wstring, hstring> titles;
        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            if (account.GetNamedBoolean(L"local", false))
                continue;
            hstring uid = account.GetNamedString(L"uid", L"");
            if (uid.empty())
                continue;
            hstring name = account.GetNamedString(L"name", uid);
            hstring address = account.GetNamedString(L"address", L"");
            uids.emplace_back(uid);
            titles[std::wstring{ uid }] = (address.empty() || address == name) ? name : name + L" — " + address;
        }

        for (auto& group : Cat::GroupByAccount(CategoryStore::Registry().Items(), uids))
        {
            hstring title = group.account.empty() ? Tr(L"Catégories locales") : titles[group.account];
            result.emplace_back(title, std::move(group.indices));
        }
        return result;
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
        /* une carte d'édition par catégorie ; index = position dans le registre (raccourci) */
        auto make_card = [&](size_t index) -> Border
        {
            auto const cat = registry.Items()[index];
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
            picker_flyout.Closed([this, current](IInspectable const&, IInspectable const&)
            {
                OnCategoriesRegistryChanged();
                PushCategoryChange(L"recolor", *current, L"", *current);
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
                std::wstring previous = *current;
                *current = wanted;
                box.Text(hstring{ wanted });
                OnCategoriesRegistryChanged();
                PushCategoryChange(L"rename", previous, wanted, wanted);
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
                auto do_delete = [this, current]()
                {
                    auto const* found = ::EvolutionWin::CategoryStore::Registry().Find(*current);
                    auto linked_accounts = found ? found->accounts : std::vector<std::wstring>{};
                    std::wstring name = *current;
                    if (::EvolutionWin::CategoryStore::Registry().Remove(name))
                    {
                        PushCategoryChange(L"delete", name, L"", name, &linked_accounts);
                        OnCategoriesRegistryChanged();
                        RefreshCategoriesView();
                        ShowStatus(Tr(L"Catégorie supprimée de la palette. Les éléments déjà classés la conservent.", L"Category removed from the palette. Items already categorised keep it."), InfoBarSeverity::Informational);
                    }
                };

                auto const* found = ::EvolutionWin::CategoryStore::Registry().Find(*current);
                if (!found || found->accounts.empty() || !Content().XamlRoot())
                {
                    do_delete();
                    return;
                }

                /* la suppression se répercute sur le serveur : confirmation */
                ContentDialog dialog;
                dialog.XamlRoot(Content().XamlRoot());
                dialog.Title(box_value(Tr(L"Supprimer la catégorie des comptes ?")));
                dialog.Content(box_value(Fmt(Tr(L"La catégorie « {0} » sera aussi supprimée de la liste des catégories de ses comptes (Microsoft 365, Gmail). Les messages déjà classés la conservent."), { hstring{ *current } })));
                dialog.PrimaryButtonText(Tr(L"Supprimer"));
                dialog.CloseButtonText(Tr(L"Annuler"));
                dialog.DefaultButton(ContentDialogButton::Close);
                dialog.ShowAsync().Completed([do_delete](IAsyncOperation<ContentDialogResult> const& operation, AsyncStatus)
                {
                    if (operation.GetResults() == ContentDialogResult::Primary)
                        do_delete();
                });
            });
            Grid::SetColumn(delete_button, 3);
            row.Children().Append(delete_button);

            card.Child(row);
            return card;
        };

        auto groups = CategoryGroupsForDisplay();
        if (groups.empty())
        {
            for (size_t i = 0; i < registry.Items().size(); ++i)
                host.Children().Append(make_card(i));
        }
        else
        {
            /* regroupement par comptes : un groupe repliable par compte */
            for (auto const& group : groups)
            {
                StackPanel content;
                content.Spacing(8);
                for (size_t i : group.second)
                    content.Children().Append(make_card(i));

                Expander expander;
                expander.IsExpanded(true);
                expander.HorizontalAlignment(HorizontalAlignment::Stretch);
                expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                TextBlock header;
                header.Text(Fmt(Tr(L"{0} ({1})"), { group.first, to_hstring(group.second.size()) }));
                header.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                expander.Header(header);
                expander.Content(content);
                host.Children().Append(expander);
            }
        }

        /* ajout d'une catégorie */
        Grid add_row;
        add_row.ColumnSpacing(8);
        add_row.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        add_row.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition target_col;
        target_col.Width(GridLengthHelper::Auto());
        add_row.ColumnDefinitions().Append(target_col);
        ColumnDefinition add_col;
        add_col.Width(GridLengthHelper::Auto());
        add_row.ColumnDefinitions().Append(add_col);

        TextBox new_name;
        new_name.PlaceholderText(Tr(L"Nom de la nouvelle catégorie", L"New category name"));
        new_name.MaxLength(100);
        add_row.Children().Append(new_name);

        /* destination : locale, ou un compte (la catégorie y est créée aussi) */
        ComboBox target_box;
        target_box.Width(220);
        target_box.Items().Append(box_value(Tr(L"Locale (cet ordinateur)")));
        auto target_uids = std::make_shared<std::vector<std::wstring>>();
        target_uids->push_back(L"");
        if (m_accounts)
        {
            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                hstring uid = account.GetNamedString(L"uid", L"");
                if (account.GetNamedBoolean(L"local", false) || uid.empty())
                    continue;
                target_box.Items().Append(box_value(account.GetNamedString(L"name", uid)));
                target_uids->push_back(std::wstring{ uid });
            }
        }
        target_box.SelectedIndex(0);
        Grid::SetColumn(target_box, 1);
        add_row.Children().Append(target_box);

        Button add_button;
        add_button.Content(box_value(Tr(L"Ajouter une catégorie", L"Add a category")));
        auto add_action = [this, new_name, target_box, target_uids]()
        {
            std::wstring name = Cat::TrimName(std::wstring_view{ new_name.Text() });
            uint8_t r = 0, g = 0, b = 0;
            Cat::FallbackColor(name, r, g, b);
            if (!::EvolutionWin::CategoryStore::Registry().Add(name, r, g, b))
            {
                ShowStatus(Tr(L"Nom de catégorie vide, contenant une virgule ou déjà utilisé.", L"Category name is empty, contains a comma or is already used."), InfoBarSeverity::Warning);
                return;
            }
            int target = target_box.SelectedIndex();
            if (target > 0 && static_cast<size_t>(target) < target_uids->size())
            {
                ::EvolutionWin::CategoryStore::Registry().AddAccount(name, (*target_uids)[target]);
                std::vector<std::wstring> accounts{ (*target_uids)[target] };
                PushCategoryChange(L"create", name, L"", name, &accounts);
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
        Grid::SetColumn(add_button, 2);
        add_row.Children().Append(add_button);
        host.Children().Append(add_row);

        /* reprise des catégories existantes des comptes Microsoft 365 / Exchange Online */
        Button import_button;
        import_button.Content(box_value(Tr(L"Reprendre les catégories des comptes")));
        import_button.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        Controls::ToolTipService::SetToolTip(import_button, box_value(Tr(L"Importe la liste des catégories (noms et couleurs) des comptes Microsoft 365 / Exchange Online (liste de catégories) et Gmail (libellés) ; les catégories déjà présentes sont conservées.")));
        import_button.Click([this](IInspectable const& sender, RoutedEventArgs const&)
        {
            ImportAccountCategories(sender.as<Button>());
        });
        host.Children().Append(import_button);

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

        RefreshFormatRulesView();

        /* reprise automatique, une fois par session, des catégories des comptes */
        if (!m_categories_auto_imported && m_accounts && m_accounts.Size() > 0)
        {
            m_categories_auto_imported = true;
            ImportAccountCategories(nullptr);
        }
    }

    /**
     * @brief Enregistre les règles de mise en forme conditionnelle et rafraîchit les listes
     *        déjà chargées (messages, calendrier).
     */
    void MainWindow::OnFormatRulesChanged()
    {
        ::EvolutionWin::CategoryStore::SaveRules();
        if (m_all_messages)
            ShowMessages(m_all_messages);
        if (m_all_calendar_events)
            FilterAndRenderCalendarEvents();
    }

    /**
     * @brief Construit l'éditeur des règles de mise en forme conditionnelle (Paramètres > Catégories).
     *
     * Une ligne par règle : activation, critère, valeur (texte ou importance), couleur,
     * montée en priorité et suppression ; puis le bouton d'ajout.
     */
    void MainWindow::RefreshFormatRulesView()
    {
        namespace CF = ::EvolutionWin::ConditionalFormat;
        auto host = FormatRulesHost();
        if (!host)
            return;
        host.Children().Clear();

        auto resources = Application::Current().Resources();
        auto card_brush = resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>();
        auto stroke_brush = resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>();

        auto& rules = ::EvolutionWin::CategoryStore::Rules();
        for (size_t index = 0; index < rules.size(); ++index)
        {
            auto const rule = rules[index];

            Border card;
            card.Background(card_brush);
            card.BorderBrush(stroke_brush);
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            card.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));

            Grid row;
            row.ColumnSpacing(8);
            for (int i = 0; i < 6; ++i)
            {
                ColumnDefinition column;
                if (i == 2)
                    column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                else
                    column.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(column);
            }

            CheckBox enabled;
            enabled.IsChecked(rule.enabled);
            enabled.MinWidth(0);
            enabled.VerticalAlignment(VerticalAlignment::Center);
            Controls::ToolTipService::SetToolTip(enabled, box_value(Tr(L"Règle active")));
            enabled.Click([this, index](IInspectable const& sender, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::CategoryStore::Rules();
                if (index < all.size())
                    all[index].enabled = sender.as<CheckBox>().IsChecked().Value();
                OnFormatRulesChanged();
            });
            row.Children().Append(enabled);

            ComboBox field_box;
            field_box.Width(240);
            field_box.VerticalAlignment(VerticalAlignment::Center);
            for (auto const& label : {
                     Tr(L"L'expéditeur contient"), Tr(L"Le domaine de l'expéditeur est"), Tr(L"L'objet ou le titre contient"),
                     Tr(L"L'importance est"), Tr(L"L'organisateur contient") })
                field_box.Items().Append(box_value(label));
            field_box.SelectedIndex(static_cast<int>(rule.field));
            field_box.SelectionChanged([this, index](IInspectable const& sender, SelectionChangedEventArgs const&)
            {
                auto& all = ::EvolutionWin::CategoryStore::Rules();
                int selected = sender.as<ComboBox>().SelectedIndex();
                if (index >= all.size() || selected < 0 || selected == static_cast<int>(all[index].field))
                    return;
                all[index].field = static_cast<CF::RuleField>(selected);
                if (all[index].field == CF::RuleField::Importance && all[index].text != L"high" && all[index].text != L"low")
                    all[index].text = L"high";
                OnFormatRulesChanged();
                /* la zone de valeur change selon le critère : on reconstruit */
                DispatcherQueue().TryEnqueue([this]() { RefreshFormatRulesView(); });
            });
            Grid::SetColumn(field_box, 1);
            row.Children().Append(field_box);

            if (rule.field == CF::RuleField::Importance)
            {
                ComboBox level_box;
                level_box.HorizontalAlignment(HorizontalAlignment::Stretch);
                level_box.VerticalAlignment(VerticalAlignment::Center);
                level_box.Items().Append(box_value(Tr(L"Haute")));
                level_box.Items().Append(box_value(Tr(L"Basse")));
                level_box.SelectedIndex(rule.text == L"low" ? 1 : 0);
                level_box.SelectionChanged([this, index](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    auto& all = ::EvolutionWin::CategoryStore::Rules();
                    if (index < all.size())
                        all[index].text = sender.as<ComboBox>().SelectedIndex() == 1 ? L"low" : L"high";
                    OnFormatRulesChanged();
                });
                Grid::SetColumn(level_box, 2);
                row.Children().Append(level_box);
            }
            else
            {
                TextBox value_box;
                value_box.Text(hstring{ rule.text });
                value_box.MaxLength(200);
                value_box.VerticalAlignment(VerticalAlignment::Center);
                value_box.PlaceholderText(rule.field == CF::RuleField::SenderDomain ? Tr(L"exemple.fr") : Tr(L"Texte à rechercher"));
                value_box.LostFocus([this, index](IInspectable const& sender, RoutedEventArgs const&)
                {
                    auto& all = ::EvolutionWin::CategoryStore::Rules();
                    std::wstring text{ sender.as<TextBox>().Text() };
                    if (index < all.size() && all[index].text != text)
                    {
                        all[index].text = text;
                        OnFormatRulesChanged();
                    }
                });
                Grid::SetColumn(value_box, 2);
                row.Children().Append(value_box);
            }

            Button swatch;
            swatch.Width(36);
            swatch.Height(32);
            swatch.Padding(ThicknessHelper::FromUniformLength(0));
            swatch.Background(SolidColorBrush{ Windows::UI::Color{ 255, rule.r, rule.g, rule.b } });
            Controls::ToolTipService::SetToolTip(swatch, box_value(Tr(L"Choisir la couleur")));
            ColorPicker picker;
            picker.IsAlphaEnabled(false);
            picker.IsHexInputVisible(true);
            picker.Color(Windows::UI::Color{ 255, rule.r, rule.g, rule.b });
            picker.ColorChanged([index, swatch](ColorPicker const&, ColorChangedEventArgs const& args)
            {
                auto color = args.NewColor();
                swatch.Background(SolidColorBrush{ color });
                auto& all = ::EvolutionWin::CategoryStore::Rules();
                if (index < all.size())
                {
                    all[index].r = color.R;
                    all[index].g = color.G;
                    all[index].b = color.B;
                }
            });
            Flyout picker_flyout;
            picker_flyout.Content(picker);
            picker_flyout.Closed([this](IInspectable const&, IInspectable const&) { OnFormatRulesChanged(); });
            swatch.Flyout(picker_flyout);
            Grid::SetColumn(swatch, 3);
            row.Children().Append(swatch);

            Button up_button;
            FontIcon up_icon;
            up_icon.Glyph(L"\xE70E");
            up_icon.FontSize(14);
            up_button.Content(up_icon);
            up_button.IsEnabled(index > 0);
            Controls::ToolTipService::SetToolTip(up_button, box_value(Tr(L"Donner plus de priorité")));
            up_button.Click([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::CategoryStore::Rules();
                if (index > 0 && index < all.size())
                {
                    std::swap(all[index], all[index - 1]);
                    OnFormatRulesChanged();
                    RefreshFormatRulesView();
                }
            });
            Grid::SetColumn(up_button, 4);
            row.Children().Append(up_button);

            Button delete_button;
            FontIcon delete_icon;
            delete_icon.Glyph(L"\xE74D");
            delete_icon.FontSize(14);
            delete_button.Content(delete_icon);
            Controls::ToolTipService::SetToolTip(delete_button, box_value(Tr(L"Supprimer la règle")));
            delete_button.Click([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::CategoryStore::Rules();
                if (index < all.size())
                {
                    all.erase(all.begin() + index);
                    OnFormatRulesChanged();
                    RefreshFormatRulesView();
                }
            });
            Grid::SetColumn(delete_button, 5);
            row.Children().Append(delete_button);

            card.Child(row);
            host.Children().Append(card);
        }

        Button add_button;
        add_button.Content(box_value(Tr(L"Ajouter une règle")));
        add_button.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            ::EvolutionWin::CategoryStore::Rules().push_back(CF::FormatRule{});
            OnFormatRulesChanged();
            RefreshFormatRulesView();
        });
        host.Children().Append(add_button);
    }

    /**
     * @brief Reprend dans le registre local les catégories des comptes Microsoft 365 et les libellés des comptes Gmail.
     *
     * Interroge chaque compte non local ; les comptes d'un autre type répondent
     * « non pris en charge » et sont ignorés. Les catégories déjà présentes (même nom,
     * sans tenir compte de la casse) gardent leur couleur locale.
     *
     * @param button Bouton déclencheur, désactivé pendant la lecture.
     */
    void MainWindow::ImportAccountCategories(Button const& button)
    {
        struct ImportState
        {
            size_t pending{ 0 };
            size_t accounts_ok{ 0 };
            size_t added{ 0 };
            size_t linked{ 0 };
            std::wstring last_error;
        };
        auto state = std::make_shared<ImportState>();

        std::vector<hstring> uids;
        if (m_accounts)
        {
            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                if (account.GetNamedBoolean(L"local", false))
                    continue;
                hstring uid = account.GetNamedString(L"uid", L"");
                if (!uid.empty())
                    uids.push_back(uid);
            }
        }
        if (uids.empty())
        {
            ShowStatus(Tr(L"Aucun compte à interroger."), InfoBarSeverity::Warning);
            return;
        }

        if (button)
            button.IsEnabled(false);
        state->pending = uids.size();
        for (auto const& uid : uids)
        {
            m_core->GetMailCategories(uid, [this, state, button, uid](hstring const& json, hstring const& error)
            {
                if (!error.empty() || json.empty())
                {
                    state->last_error = std::wstring{ error };
                }
                else
                {
                    JsonObject root;
                    if (JsonObject::TryParse(json, root) && root.HasKey(L"categories"))
                    {
                        ++state->accounts_ok;
                        auto& registry = ::EvolutionWin::CategoryStore::Registry();
                        for (auto const& value : root.GetNamedArray(L"categories"))
                        {
                            auto entry = value.GetObject();
                            std::wstring name{ entry.GetNamedString(L"name", L"") };
                            /* la virgule sépare les catégories d'un courriel : on l'évite */
                            std::replace(name.begin(), name.end(), L',', L' ');
                            name = Cat::TrimName(std::wstring_view{ name });
                            if (name.empty())
                                continue;
                            if (registry.Contains(name))
                            {
                                /* déjà connue : on garde sa couleur, on la rattache au compte */
                                if (registry.AddAccount(name, std::wstring_view{ uid }))
                                    ++state->linked;
                                continue;
                            }
                            uint8_t r = 0, g = 0, b = 0;
                            if (!Cat::ParseHexColor(std::wstring_view{ entry.GetNamedString(L"color", L"") }, r, g, b))
                                Cat::FallbackColor(name, r, g, b);
                            if (registry.Add(name, r, g, b))
                            {
                                registry.AddAccount(name, std::wstring_view{ uid });
                                ++state->added;
                            }
                        }
                    }
                }

                if (--state->pending > 0)
                    return;

                if (button)
                    button.IsEnabled(true);
                if (state->accounts_ok == 0)
                {
                    if (!button)
                        return;
                    ShowStatus(Fmt(Tr(L"Aucune catégorie reprise (comptes Microsoft 365 / Exchange Online et Gmail uniquement) : {0}"), { hstring{ state->last_error } }), InfoBarSeverity::Warning);
                    return;
                }
                if (state->added > 0 || state->linked > 0)
                {
                    OnCategoriesRegistryChanged();
                    RefreshCategoriesView();
                }
                if (button || state->added > 0)
                    ShowStatus(Fmt(Tr(L"{0} catégorie(s) reprise(s) depuis {1} compte(s)."), { to_hstring(state->added), to_hstring(state->accounts_ok) }), InfoBarSeverity::Success);
            });
        }
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
        auto add_by_index = [&](size_t index)
        {
            auto const& cat = registry.Items()[index];
            add_toggle(cat.name, Windows::UI::Color{ 255, cat.r, cat.g, cat.b }, Cat::CategoryRegistry::ShortcutForIndex(index));
        };
        auto groups = CategoryGroupsForDisplay();
        if (groups.empty())
        {
            for (size_t i = 0; i < registry.Items().size(); ++i)
                add_by_index(i);
        }
        else
        {
            /* regroupement par comptes : en-tête (inactif) puis catégories du compte */
            for (auto const& group : groups)
            {
                MenuFlyoutItem header;
                header.Text(group.first);
                header.IsEnabled(false);
                items.Append(header);
                for (size_t i : group.second)
                    add_by_index(i);
            }
        }

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
                ShowStatus(Fmt(Tr(L"Catégories de la tâche : {0}", L"Task categories: {0}"), { error }), InfoBarSeverity::Error);
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
                ShowStatus(Fmt(Tr(L"Catégories de l'événement : {0}", L"Event categories: {0}"), { error }), InfoBarSeverity::Error);
            LoadEvents();
        });
    }


    /**
     * @brief Modifie quelques champs d'un événement (sans ouvrir sa fenêtre) puis recharge le calendrier.
     * @param event Événement au format JSON du moteur (calendar_uid, uid).
     * @param changes Champs à modifier (show_as, private, importance, categories…).
     */
    void MainWindow::PatchEvent(JsonObject const& event, JsonObject const& changes)
    {
        JsonObject request = JsonObject::Parse(changes.Stringify());
        request.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(event.GetNamedString(L"calendar_uid", L"")));
        request.SetNamedValue(L"uid", JsonValue::CreateStringValue(event.GetNamedString(L"uid", L"")));
        m_core->ModifyEvent(request.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
                ShowStatus(Fmt(Tr(L"Modification de l'événement : {0}", L"Event update: {0}"), { error }), InfoBarSeverity::Error);
            LoadEvents();
        });
    }

    /**
     * @brief Menu contextuel d'un événement du calendrier : Ouvrir, Classer (catégories), Afficher comme,
     *        Privé et Importance, comme le bandeau de la fenêtre de l'événement.
     * @param element Élément qui reçoit le clic droit (bloc de la grille, ligne de liste).
     * @param event Événement au format JSON de la liste du moteur.
     */
    void MainWindow::AttachEventContextMenu(UIElement const& element, JsonObject const& event)
    {
        MenuFlyout flyout;
        hstring calendar_uid = event.GetNamedString(L"calendar_uid", L"");
        hstring uid = event.GetNamedString(L"uid", L"");

        MenuFlyoutItem open_item;
        open_item.Text(Tr(L"Ouvrir", L"Open"));
        FontIcon open_icon;
        open_icon.Glyph(L"\xE8A7");
        open_item.Icon(open_icon);
        open_item.Click([this, calendar_uid, uid](IInspectable const&, RoutedEventArgs const&) { OpenCalendarEvent(calendar_uid, uid); });
        flyout.Items().Append(open_item);
        flyout.Items().Append(MenuFlyoutSeparator{});

        /* Classer : catégories */
        MenuFlyoutSubItem classify;
        classify.Text(Tr(L"Classer", L"Categorise"));
        FontIcon classify_icon;
        classify_icon.Glyph(L"\xE8EC");
        classify.Icon(classify_icon);
        PopulateItemCategoryMenu(classify.Items(), ::EvolutionWin::CategoryStore::CategoriesOf(event),
            [this, event](std::vector<std::wstring> const& names) { SetEventCategories(event, names); });
        flyout.Items().Append(classify);

        /* Afficher comme : les états d'Outlook */
        MenuFlyoutSubItem show_as;
        show_as.Text(Tr(L"Afficher comme", L"Show as"));
        FontIcon show_as_icon;
        show_as_icon.Glyph(L"\xE787");
        show_as.Icon(show_as_icon);
        {
            static const wchar_t* const values[] = { L"free", L"working", L"tentative", L"busy", L"oof" };
            hstring current = event.GetNamedString(L"show_as", L"busy");
            std::vector<hstring> labels{ Tr(L"Disponible", L"Free"), Tr(L"Travaille ailleurs", L"Working elsewhere"),
                Tr(L"Provisoire", L"Tentative"), Tr(L"Occupé(e)", L"Busy"), Tr(L"Absent(e)", L"Out of office") };
            for (int i = 0; i < 5; ++i)
            {
                RadioMenuFlyoutItem item;
                item.Text(labels[i]);
                item.GroupName(L"show_as");
                item.IsChecked(current == values[i]);
                hstring value = values[i];
                item.Click([this, event, value](IInspectable const&, RoutedEventArgs const&)
                {
                    JsonObject changes;
                    changes.SetNamedValue(L"show_as", JsonValue::CreateStringValue(value));
                    PatchEvent(event, changes);
                });
                show_as.Items().Append(item);
            }
        }
        flyout.Items().Append(show_as);

        /* Privé */
        ToggleMenuFlyoutItem private_item;
        private_item.Text(Tr(L"Privé", L"Private"));
        FontIcon private_icon;
        private_icon.Glyph(L"\xE72E");
        private_item.Icon(private_icon);
        bool is_private = event.GetNamedBoolean(L"private", false);
        private_item.IsChecked(is_private);
        private_item.Click([this, event, is_private](IInspectable const&, RoutedEventArgs const&)
        {
            JsonObject changes;
            changes.SetNamedValue(L"private", JsonValue::CreateBooleanValue(!is_private));
            PatchEvent(event, changes);
        });
        flyout.Items().Append(private_item);

        /* Importance */
        MenuFlyoutSubItem importance;
        importance.Text(Tr(L"Importance", L"Importance"));
        FontIcon importance_icon;
        importance_icon.Glyph(L"\xE783");
        importance.Icon(importance_icon);
        {
            static const wchar_t* const values[] = { L"high", L"normal", L"low" };
            hstring current = event.GetNamedString(L"importance", L"normal");
            std::vector<hstring> labels{ Tr(L"Haute", L"High"), Tr(L"Normale", L"Normal"), Tr(L"Faible", L"Low") };
            for (int i = 0; i < 3; ++i)
            {
                RadioMenuFlyoutItem item;
                item.Text(labels[i]);
                item.GroupName(L"importance");
                item.IsChecked(current == values[i]);
                hstring value = values[i];
                item.Click([this, event, value](IInspectable const&, RoutedEventArgs const&)
                {
                    JsonObject changes;
                    changes.SetNamedValue(L"importance", JsonValue::CreateStringValue(value));
                    PatchEvent(event, changes);
                });
                importance.Items().Append(item);
            }
        }
        flyout.Items().Append(importance);

        element.ContextFlyout(flyout);
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
                ShowStatus(Fmt(Tr(L"Catégories du contact : {0}", L"Contact categories: {0}"), { error }), InfoBarSeverity::Error);
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

            hstring tip = Tr(L"Filtrer par catégorie", L"Filter by category");
            if (!filter.IsEmpty())
            {
                std::vector<std::wstring> shown = filter.selected;
                if (filter.include_uncategorized)
                    shown.push_back(std::wstring{ Tr(L"Sans catégorie", L"No category") });
                tip = Fmt(Tr(L"Filtrer par catégorie : {0}", L"Filter by category: {0}"), { hstring{ Cat::JoinCategories(shown) } });
            }
            button.Content(icon);
            Controls::ToolTipService::SetToolTip(button, box_value(tip));
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
