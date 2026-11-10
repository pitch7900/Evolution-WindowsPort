#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Ouvre la fenêtre de choix d'un dossier de destination (déplacer / copier ici).
     *
     * Les dossiers sont présentés par compte dans une arborescence repliable, avec une
     * zone de recherche (les dossiers qui correspondent restent visibles avec leurs parents,
     * dépliés). Deux boutons : « <confirm_label> » (déplacer ici / copier ici) et « Annuler ».
     * Les destinations interdites (options.blocked) sont affichées en grisé et non sélectionnables.
     *
     * @param options Titre, libellé du bouton de validation, compte imposé éventuel,
     *                autorisation de choisir la racine d'un compte, prédicat des destinations interdites.
     * @param on_pick Appelé avec (compte, dossier) à la validation ; dossier vide = racine du compte.
     */
    fire_and_forget MainWindow::PickFolderAsync(
        FolderPickOptions options,
        std::function<void(hstring const&, hstring const&)> on_pick)
    {
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;
        auto strong = get_strong();

        struct Target
        {
            hstring account;
            hstring folder;
            bool selectable{ false };
        };

        /* clé d'identité d'un nœud (même pointeur quelle que soit l'interface interrogée) */
        auto node_key = [](IInspectable const& object) -> void*
        {
            return winrt::get_abi(object.as<IInspectable>());
        };

        std::map<void*, Target> targets;
        std::optional<Target> chosen;
        bool double_clicked = false;

        /* comptes dans le même ordre que l'arborescence principale : compte par défaut, distants A-Z, local en dernier */
        std::vector<JsonObject> sorted_accounts;
        for (auto const& value : m_accounts ? m_accounts : JsonArray{})
            sorted_accounts.push_back(value.GetObject());
        std::stable_sort(sorted_accounts.begin(), sorted_accounts.end(), [](JsonObject const& a, JsonObject const& b)
        {
            bool def_a = a.GetNamedBoolean(L"default", false);
            bool def_b = b.GetNamedBoolean(L"default", false);
            if (def_a != def_b)
                return def_a > def_b;
            bool loc_a = a.GetNamedBoolean(L"local", false);
            bool loc_b = b.GetNamedBoolean(L"local", false);
            if (loc_a != loc_b)
                return loc_a < loc_b;
            hstring na = a.GetNamedString(L"name", L"");
            hstring nb = b.GetNamedString(L"name", L"");
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                na.c_str(), static_cast<int>(na.size()), nb.c_str(), static_cast<int>(nb.size()),
                nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });

        /* tous les comptes sont listés, même si leurs dossiers ne sont pas (encore) chargés */
        std::vector<std::pair<hstring, hstring>> accounts;
        for (auto const& obj : sorted_accounts)
        {
            hstring uid = obj.GetNamedString(L"uid", L"");
            if (uid.empty())
                continue;
            accounts.emplace_back(uid, obj.GetNamedString(L"name", obj.GetNamedString(L"display_name", uid)));
        }
        for (auto const& [key, ignored] : m_account_folders)
        {
            bool known = std::any_of(accounts.begin(), accounts.end(),
                [&key](auto const& entry) { return std::wstring{ entry.first } == key; });
            if (!known)
                accounts.emplace_back(hstring{ key }, hstring{ key });
        }
        if (!options.only_account.empty())
        {
            accounts.erase(std::remove_if(accounts.begin(), accounts.end(),
                [&options](auto const& entry) { return entry.first != options.only_account; }), accounts.end());
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(options.title));
        dialog.PrimaryButtonText(options.confirm_label);
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(false);

        StackPanel panel;
        panel.Spacing(12);
        panel.Width(440);

        TextBox search_box;
        search_box.PlaceholderText(Tr(L"Rechercher un dossier..."));
        panel.Children().Append(search_box);

        if (!options.note.empty())
        {
            TextBlock note;
            note.Text(options.note);
            note.Opacity(0.7);
            note.TextWrapping(TextWrapping::Wrap);
            panel.Children().Append(note);
        }

        TreeView tree;
        /* même gabarit que l'arborescence principale : le contenu d'un nœud est un FolderItem (icône + nom) */
        tree.ItemTemplate(Microsoft::UI::Xaml::Markup::XamlReader::Load(
            L"<DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>"
            L"<TreeViewItem><Grid ColumnSpacing='8'>"
            L"<Grid.ColumnDefinitions><ColumnDefinition Width='Auto'/><ColumnDefinition Width='*'/></Grid.ColumnDefinitions>"
            L"<FontIcon Glyph='{Binding Content.Glyph}' FontFamily='{Binding Content.GlyphFontFamily}' FontSize='14'/>"
            L"<TextBlock Grid.Column='1' Text='{Binding Content.Name}' TextTrimming='CharacterEllipsis'/>"
            L"</Grid></TreeViewItem></DataTemplate>").as<DataTemplate>());
        tree.SelectionMode(TreeViewSelectionMode::Single);
        tree.Height(340);
        panel.Children().Append(tree);

        TextBlock empty_text;
        empty_text.Text(Tr(L"Aucun dossier ne correspond."));
        empty_text.Opacity(0.7);
        empty_text.Visibility(Visibility::Collapsed);
        panel.Children().Append(empty_text);

        auto lower = [](std::wstring_view text)
        {
            std::wstring result{ text };
            if (!result.empty())
                ::CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
            return result;
        };

        /* ajoute récursivement les dossiers d'un compte ; renvoie vrai si au moins un nœud est ajouté */
        std::function<bool(JsonArray const&, TreeViewNode const&, hstring const&, std::wstring const&)> add_folders;
        add_folders = [&](JsonArray const& folders, TreeViewNode const& parent, hstring const& account, std::wstring const& query)
        {
            bool any = false;
            for (auto const& value : folders)
            {
                auto folder = value.GetObject();
                hstring full_name = folder.GetNamedString(L"full_name", L"");
                hstring name = folder.GetNamedString(L"name", full_name);
                hstring type = folder.GetNamedString(L"type", L"");
                bool selectable = folder.GetNamedBoolean(L"selectable", true);

                TreeViewNode node;
                bool children_match = add_folders(folder.GetNamedArray(L"children", JsonArray{}), node, account, query);
                bool self_match = query.empty() || lower(std::wstring_view{ name }).find(query) != std::wstring::npos;
                if (!self_match && !children_match)
                    continue;

                bool allowed = selectable && !(options.blocked && options.blocked(account, full_name));
                node.Content(EvolutionWin::FolderItem(name, FolderGlyph(type), 0, account, full_name, false));
                node.IsExpanded(!query.empty() && node.Children().Size() > 0);
                targets[node_key(node)] = Target{ account, full_name, allowed };
                parent.Children().Append(node);
                any = true;
            }
            return any;
        };

        /* (re)construit l'arborescence selon la recherche */
        auto rebuild = [&](std::wstring const& query)
        {
            tree.RootNodes().Clear();
            targets.clear();
            chosen.reset();
            dialog.IsPrimaryButtonEnabled(false);

            for (auto const& [account, account_name] : accounts)
            {
                auto folders_it = m_account_folders.find(std::wstring{ account });

                TreeViewNode account_node;
                bool any = folders_it != m_account_folders.end() && folders_it->second &&
                    add_folders(folders_it->second, account_node, account, query);
                bool name_match = query.empty() || lower(std::wstring_view{ account_name }).find(query) != std::wstring::npos;
                if (!any && !name_match)
                    continue;

                bool root_allowed = options.allow_account_root && !(options.blocked && options.blocked(account, L""));
                account_node.Content(EvolutionWin::FolderItem(account_name, L"\xE715", 0, account, L"", true));
                account_node.IsExpanded(true);
                targets[node_key(account_node)] = Target{ account, L"", root_allowed };
                tree.RootNodes().Append(account_node);
            }
            empty_text.Visibility(tree.RootNodes().Size() == 0 ? Visibility::Visible : Visibility::Collapsed);
        };

        rebuild(L"");

        search_box.TextChanged([&](IInspectable const&, TextChangedEventArgs const&)
        {
            rebuild(lower(std::wstring_view{ search_box.Text() }));
        });

        /* clic sur un dossier : mémorise la destination si elle est permise */
        tree.ItemInvoked([&](TreeView const&, TreeViewItemInvokedEventArgs const& args)
        {
            auto found = targets.find(node_key(args.InvokedItem()));
            if (found != targets.end() && found->second.selectable)
            {
                chosen = found->second;
                dialog.IsPrimaryButtonEnabled(true);
            }
            else
            {
                chosen.reset();
                dialog.IsPrimaryButtonEnabled(false);
            }
        });

        /* double-clic sur une destination permise : validation immédiate */
        tree.DoubleTapped([&](IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
        {
            if (chosen)
            {
                double_clicked = true;
                dialog.Hide();
            }
        });

        dialog.Content(panel);
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if ((result == ContentDialogResult::Primary || double_clicked) && chosen)
            on_pick(chosen->account, chosen->folder);
    }
}
