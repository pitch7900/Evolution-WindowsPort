#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"

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

        /* comptes dans l'ordre d'affichage, avec leur nom */
        std::vector<std::pair<hstring, hstring>> accounts;
        for (auto const& value : m_accounts ? m_accounts : JsonArray{})
        {
            auto obj = value.GetObject();
            hstring uid = obj.GetNamedString(L"uid", L"");
            if (uid.empty() || m_account_folders.find(std::wstring{ uid }) == m_account_folders.end())
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

        TreeView tree;
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

        /* contenu d'un nœud : icône + libellé (estompé si la destination n'est pas permise) */
        auto make_content = [](hstring const& label, FontIcon const& icon, bool dim)
        {
            StackPanel row;
            row.Orientation(Orientation::Horizontal);
            row.Spacing(8);
            icon.FontSize(14);
            TextBlock text;
            text.Text(label);
            text.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(icon);
            row.Children().Append(text);
            if (dim)
                row.Opacity(0.5);
            return row;
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
                node.Content(make_content(name, ::EvolutionWin::Helpers::CreateFolderIcon(type), !allowed));
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
                if (folders_it == m_account_folders.end() || !folders_it->second)
                    continue;

                TreeViewNode account_node;
                bool any = add_folders(folders_it->second, account_node, account, query);
                bool name_match = query.empty() || lower(std::wstring_view{ account_name }).find(query) != std::wstring::npos;
                if (!any && !name_match)
                    continue;

                FontIcon account_icon;
                account_icon.Glyph(L"\xE779");
                bool root_allowed = options.allow_account_root && !(options.blocked && options.blocked(account, L""));
                account_node.Content(make_content(account_name, account_icon, options.allow_account_root && !root_allowed));
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
