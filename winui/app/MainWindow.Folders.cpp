#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"

#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <vector>

/* Comptes et arbre des dossiers : chargement, compteurs, menu contextuel et
 * gestion des dossiers (création, renommage, déplacement, suppression). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    std::wstring MainWindow::NodeKey(hstring const& account, hstring const& folder)
    {
        return std::wstring{ account } + L'\x1F' + std::wstring{ folder };
    }

    void MainWindow::LoadAccounts()
    {
        m_core->ListAccounts([this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Comptes : " + error, InfoBarSeverity::Error);
                return;
            }

            FolderTree().RootNodes().Clear();
            m_nodes.clear();
            m_account_folders.clear();
            m_accounts = JsonArray::Parse(json);
            RefreshAccountsView();

            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                hstring uid = account.GetNamedString(L"uid");
                bool local = account.GetNamedBoolean(L"local", false);

                TreeViewNode node;
                node.Content(EvolutionWin::FolderItem(account.GetNamedString(L"name", uid),
                    local ? L"\xE7F8" : L"\xE715", 0, uid, L"", true));
                node.IsExpanded(true);
                FolderTree().RootNodes().Append(node);
                m_nodes[NodeKey(uid, L"")] = node;

                m_core->ListFolders(uid, [this, node, uid](hstring const& folders, hstring const& folders_error)
                {
                    if (!folders_error.empty())
                    {
                        ShowStatus(L"Dossiers : " + folders_error, InfoBarSeverity::Warning);
                        return;
                    }

                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ uid }] = parsed;
                    AddFolders(node, parsed, uid);
                    UpdateMailBadge();
                });
            }
        });
    }

    /* Rang d'un dossier spécial dans l'arborescence (ordre d'Outlook), les
     * autres dossiers après, par ordre alphabétique. */
    static int SpecialFolderRank(hstring const& type)
    {
        static wchar_t const* const order[] = { L"inbox", L"drafts", L"sent", L"trash", L"archive", L"junk", L"outbox" };
        for (int ii = 0; ii < static_cast<int>(std::size(order)); ii++)
            if (type == order[ii])
                return ii;
        return static_cast<int>(std::size(order));
    }

    /* Nom affiché d'un dossier spécial, quelle que soit la langue du serveur
     * (« INBOX », « Sent Items »…) ; vide pour un dossier ordinaire. */
    static hstring SpecialFolderName(hstring const& type)
    {
        if (type == L"inbox") return L"Boîte de réception";
        if (type == L"drafts") return L"Brouillons";
        if (type == L"sent") return L"Éléments envoyés";
        if (type == L"trash") return L"Éléments supprimés";
        if (type == L"archive") return L"Archive";
        if (type == L"junk") return L"Courrier indésirable";
        if (type == L"outbox") return L"Boîte d'envoi";
        return {};
    }

    void MainWindow::AddFolders(TreeViewNode const& parent, JsonArray const& folders, hstring const& account)
    {
        /* dossiers spéciaux d'abord, puis ordre alphabétique (règles de la langue) */
        std::vector<JsonObject> sorted;
        for (auto const& value : folders)
            sorted.push_back(value.GetObject());

        std::stable_sort(sorted.begin(), sorted.end(), [](JsonObject const& a, JsonObject const& b)
        {
            int ra = SpecialFolderRank(a.GetNamedString(L"type", L"")), rb = SpecialFolderRank(b.GetNamedString(L"type", L""));
            if (ra != rb)
                return ra < rb;
            hstring na = a.GetNamedString(L"name", L""), nb = b.GetNamedString(L"name", L"");
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                na.c_str(), static_cast<int>(na.size()), nb.c_str(), static_cast<int>(nb.size()),
                nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });

        for (auto const& folder : sorted)
        {
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring type = folder.GetNamedString(L"type", L"");
            hstring name = SpecialFolderName(type);
            if (name.empty())
                name = folder.GetNamedString(L"name", full_name);

            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(name, FolderGlyph(type),
                static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)), account,
                folder.GetNamedBoolean(L"selectable", true) ? full_name : hstring{}, false));
            node.IsExpanded(type == L"inbox");
            parent.Children().Append(node);
            m_nodes[NodeKey(account, full_name)] = node;

            AddFolders(node, folder.GetNamedArray(L"children", JsonArray{}), account);
        }
    }

    void MainWindow::UpdateFolderCounts(hstring const& account, JsonArray const& folders)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");

            if (auto node = m_nodes.find(NodeKey(account, full_name)); node != m_nodes.end())
            {
                if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                    item.Unread(static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)));
            }

            UpdateFolderCounts(account, folder.GetNamedArray(L"children", JsonArray{}));
        }
    }

    /* Menu contextuel d'un dossier de l'arborescence : Marquer tout comme lu,
     * Vider la Corbeille / les Indésirables, Vider le dossier, puis la gestion
     * du dossier (Nouveau dossier, Renommer, Déplacer, Copier, Supprimer).
     * Sur un compte : Nouveau dossier (premier niveau). */
    void MainWindow::ShowFolderContextMenu(UIElement const&, Input::ContextRequestedEventArgs const& args)
    {
        /* élément de l'arborescence sous le pointeur (ou ayant le focus) */
        DependencyObject element = args.OriginalSource().try_as<DependencyObject>();
        while (element && !element.try_as<TreeViewItem>())
            element = VisualTreeHelper::GetParent(element);
        auto container = element ? element.try_as<TreeViewItem>() : nullptr;
        auto node = container ? FolderTree().NodeFromContainer(container) : nullptr;
        auto item = node ? node.Content().try_as<EvolutionWin::FolderItem>() : nullptr;

        if (!item)
            return;
        args.Handled(true);

        bool trash = item.Glyph() == FolderGlyph(L"trash");
        bool junk = item.Glyph() == FolderGlyph(L"junk");

        MenuFlyout menu;
        Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
        Point position;
        if (args.TryGetPosition(container, position))
            options.Position(position);

        /* compte ou dossier non sélectionnable : gestion des dossiers seule */
        if (item.IsAccount() || item.Folder().empty())
        {
            AppendFolderManagementItems(menu, item, NodeFolderName(node));
            if (menu.Items().Size() > 0)
                menu.ShowAt(container, options);
            return;
        }

        MenuFlyoutItem mark_read;
        mark_read.Text(L"Marquer tout comme lu");
        mark_read.Icon(FontIcon{});
        mark_read.Icon().as<FontIcon>().Glyph(L"\xE8C3");
        mark_read.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"mark-all-read");
        });
        menu.Items().Append(mark_read);
        menu.Items().Append(MenuFlyoutSeparator{});

        MenuFlyoutItem empty;
        empty.Text(trash ? L"Vider la Corbeille" : junk ? L"Vider les Indésirables" : L"Vider le dossier");
        empty.Icon(FontIcon{});
        empty.Icon().as<FontIcon>().Glyph(L"\xE74D");
        empty.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"empty");
        });
        menu.Items().Append(empty);

        menu.Items().Append(MenuFlyoutSeparator{});
        AppendFolderManagementItems(menu, item, NodeFolderName(node));

        menu.ShowAt(container, options);
    }

    /* Exécute une action sur tout un dossier, avec confirmation pour
     * « empty » (définitif pour la Corbeille et les Indésirables).
     * Paramètres :
     *   item   : dossier visé
     *   action : "mark-all-read" ou "empty" */
    fire_and_forget MainWindow::RunFolderActionAsync(EvolutionWin::FolderItem item, hstring action)
    {
        auto strong = get_strong();

        if (action == L"empty")
        {
            if (m_dialog_open || !Content() || !Content().XamlRoot())
                co_return;
            bool permanent = item.Glyph() == FolderGlyph(L"trash") || item.Glyph() == FolderGlyph(L"junk");

            ContentDialog confirm;
            confirm.XamlRoot(Content().XamlRoot());
            confirm.Title(box_value(permanent ? L"Supprimer définitivement ?" : L"Vider le dossier ?"));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            text.Text(permanent
                ? L"Tous les messages de « " + item.Name() + L" » seront supprimés définitivement. Cette action est irréversible."
                : L"Tous les messages de « " + item.Name() + L" » seront déplacés vers la Corbeille.");
            confirm.Content(text);
            confirm.PrimaryButtonText(L"Supprimer");
            confirm.CloseButtonText(L"Annuler");
            confirm.DefaultButton(ContentDialogButton::Close);

            m_dialog_open = true;
            auto result = co_await confirm.ShowAsync();
            m_dialog_open = false;
            if (result != ContentDialogResult::Primary)
                co_return;
        }

        hstring account = item.Account();
        hstring folder = item.Folder();
        hstring name = item.Name();
        ShowStatus((action == L"empty" ? L"Suppression des messages de « " : L"Marquage comme lus dans « ") + name + L" »…",
            InfoBarSeverity::Informational);

        m_core->FolderAction(account, folder, action, [this, account, folder, name, action](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"« " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            uint32_t count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0;
            ShowStatus(action == L"empty"
                ? hstring{ std::to_wstring(count) + L" message(s) supprimé(s) de « " } + name + L" »."
                : hstring{ std::to_wstring(count) + L" message(s) marqué(s) comme lu(s) dans « " } + name + L" ».",
                InfoBarSeverity::Success);

            if (account == m_account && folder == m_folder)
                LoadMessages(false);
        });
    }

    /* Vrai si « full_name » est le dossier « root » ou l'un de ses
     * sous-dossiers (chemins complets, séparateur « / »). */
    static bool IsFolderOrSubfolder(std::wstring_view full_name, std::wstring_view root)
    {
        return !root.empty() && full_name.substr(0, root.size()) == root &&
            (full_name.size() == root.size() || full_name[root.size()] == L'/');
    }

    /* Chemin complet du dossier d'un nœud de l'arborescence (y compris un
     * dossier non sélectionnable, dont FolderItem::Folder() est vide).
     * Paramètres :
     *   node : nœud de l'arborescence
     * Retourne : le chemin complet, vide pour un compte ou un nœud inconnu. */
    hstring MainWindow::NodeFolderName(TreeViewNode const& node)
    {
        for (auto const& [key, value] : m_nodes)
        {
            if (value == node)
            {
                auto separator = key.find(L'\x1F');
                return separator == std::wstring::npos ? hstring{} : hstring{ key.substr(separator + 1) };
            }
        }
        return {};
    }

    /* Recherche récursivement un dossier dans l'arborescence JSON d'un compte.
     * Paramètres :
     *   folders   : tableau JSON des dossiers (evo_core_list_folders)
     *   full_name : chemin complet recherché
     * Retourne : l'objet JSON du dossier, ou nullptr. */
    JsonObject MainWindow::FindFolderJson(JsonArray const& folders, hstring const& full_name)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            if (folder.GetNamedString(L"full_name", L"") == full_name)
                return folder;
            if (auto found = FindFolderJson(folder.GetNamedArray(L"children", JsonArray{}), full_name))
                return found;
        }
        return nullptr;
    }

    /* Ajoute récursivement les dossiers d'un compte comme destinations d'un
     * déplacement ou d'une copie de dossier.
     * Paramètres :
     *   items           : collection d'éléments du sous-menu à remplir
     *   folders         : tableau JSON des dossiers à parcourir
     *   prefix          : préfixe d'affichage des sous-dossiers
     *   excluded        : dossier exclu avec ses sous-dossiers (vide : aucun)
     *   skipped         : dossier non proposé, ses sous-dossiers restant proposés
     *   selectable_only : n'ajouter que les dossiers sélectionnables
     *   on_pick         : action recevant le chemin complet choisi */
    void MainWindow::AppendFolderTargetItems(
        Collections::IVector<MenuFlyoutItemBase> const& items,
        JsonArray const& folders,
        hstring const& prefix,
        hstring const& excluded,
        hstring const& skipped,
        bool selectable_only,
        std::function<void(hstring const&)> const& on_pick)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring name = folder.GetNamedString(L"name", full_name);
            hstring label = prefix.empty() ? name : prefix + L" / " + name;

            if (IsFolderOrSubfolder(full_name, excluded))
                continue;

            if (full_name != skipped && (!selectable_only || folder.GetNamedBoolean(L"selectable", true)))
            {
                MenuFlyoutItem item;
                item.Text(label);
                item.Icon(FontIcon{});
                item.Icon().as<FontIcon>().Glyph(FolderGlyph(folder.GetNamedString(L"type", L"")));
                item.Click([on_pick, full_name](IInspectable const&, RoutedEventArgs const&)
                {
                    on_pick(full_name);
                });
                items.Append(item);
            }

            AppendFolderTargetItems(items, folder.GetNamedArray(L"children", JsonArray{}), label, excluded,
                skipped, selectable_only, on_pick);
        }
    }

    /* Ajoute au menu contextuel de l'arborescence les entrées de gestion du
     * dossier : Nouveau dossier…, Renommer…, Déplacer vers, Copier vers,
     * Supprimer le dossier… (Nouveau dossier… seul sur un compte). Les
     * dossiers spéciaux ne peuvent être ni renommés, ni déplacés, ni
     * supprimés ; un dossier qui a des sous-dossiers n'est pas supprimable.
     * Paramètres :
     *   menu      : menu contextuel à compléter
     *   item      : élément du nœud (dossier ou compte)
     *   full_name : chemin complet du dossier (vide pour un compte) */
    void MainWindow::AppendFolderManagementItems(MenuFlyout const& menu, EvolutionWin::FolderItem const& item,
                                                 hstring const& full_name)
    {
        hstring account = item.Account();
        hstring name = item.Name();
        bool is_account = item.IsAccount();

        auto folders_it = m_account_folders.find(std::wstring{ account });
        JsonArray folders = folders_it != m_account_folders.end() ? folders_it->second : nullptr;
        if (!folders)
            return;

        JsonObject folder = is_account ? nullptr : FindFolderJson(folders, full_name);
        if (!is_account && !folder)
            return;

        hstring type = folder ? folder.GetNamedString(L"type", L"") : hstring{};
        bool selectable = !folder || folder.GetNamedBoolean(L"selectable", true);
        bool special = !type.empty() && type != L"archive";
        bool has_children = folder && folder.GetNamedArray(L"children", JsonArray{}).Size() > 0;

        auto add_item = [&menu](hstring const& text, hstring const& glyph, bool enabled, auto handler)
        {
            MenuFlyoutItem entry;
            entry.Text(text);
            entry.Icon(FontIcon{});
            entry.Icon().as<FontIcon>().Glyph(glyph);
            entry.IsEnabled(enabled);
            entry.Click(handler);
            menu.Items().Append(entry);
        };

        add_item(L"Nouveau dossier…", L"\xE8F4", selectable,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                CreateFolderAsync(account, full_name, name);
            });

        if (is_account)
            return;

        add_item(L"Renommer…", L"\xE8AC", !special,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                RenameFolderAsync(account, full_name, name);
            });

        /* destinations : tout le compte sauf le parent actuel (déplacement
         * sans effet, copie déjà présente), et pour un déplacement le
         * dossier lui-même et ses sous-dossiers */
        std::wstring path{ full_name };
        auto slash = path.rfind(L'/');
        hstring parent{ slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash) };

        for (bool copy : { false, true })
        {
            MenuFlyoutSubItem sub;
            sub.Text(copy ? L"Copier vers" : L"Déplacer vers");
            sub.Icon(FontIcon{});
            sub.Icon().as<FontIcon>().Glyph(copy ? L"\xE8C8" : L"\xE8DE");
            sub.IsEnabled(copy ? selectable : !special);

            auto on_pick = [this, account, full_name, name, copy](hstring const& target)
            {
                MoveOrCopyFolder(account, full_name, name, target, copy);
            };

            if (!parent.empty())
            {
                MenuFlyoutItem top;
                top.Text(L"Premier niveau du compte");
                top.Icon(FontIcon{});
                top.Icon().as<FontIcon>().Glyph(L"\xE715");
                top.Click([on_pick](IInspectable const&, RoutedEventArgs const&) { on_pick(L""); });
                sub.Items().Append(top);
            }

            AppendFolderTargetItems(sub.Items(), folders, L"", copy ? hstring{} : full_name, parent, copy, on_pick);

            if (sub.Items().Size() == 0)
            {
                MenuFlyoutItem none;
                none.Text(L"Aucun dossier disponible");
                none.IsEnabled(false);
                sub.Items().Append(none);
            }
            menu.Items().Append(sub);
        }

        add_item(L"Supprimer le dossier…", L"\xE74D", !special && !has_children,
            [this, account, full_name, name, folder](IInspectable const&, RoutedEventArgs const&)
            {
                DeleteFolderAsync(account, full_name, name, static_cast<int32_t>(folder.GetNamedNumber(L"total", 0)));
            });
    }

    /* Demande un nom de dossier (ContentDialog avec une zone de texte).
     * Paramètres :
     *   title   : titre de la boîte de dialogue
     *   label   : libellé de la zone de texte
     *   initial : texte initial (nom actuel pour un renommage)
     * Retourne : le nom saisi sans espaces de début et de fin, vide si
     *            l'utilisateur annule. */
    IAsyncOperation<hstring> MainWindow::PromptFolderNameAsync(hstring title, hstring label, hstring initial)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return hstring{};

        auto trimmed = [](hstring const& text)
        {
            std::wstring value{ text };
            auto first = value.find_first_not_of(L" \t\r\n");
            auto last = value.find_last_not_of(L" \t\r\n");
            return first == std::wstring::npos ? std::wstring{} : value.substr(first, last - first + 1);
        };

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(title));
        TextBox box;
        box.Header(box_value(label));
        box.Text(initial);
        box.SelectAll();
        box.MinWidth(320);
        dialog.Content(box);
        dialog.PrimaryButtonText(L"OK");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(!trimmed(initial).empty());
        box.TextChanged([dialog, trimmed](IInspectable const& sender, TextChangedEventArgs const&)
        {
            auto value = trimmed(sender.as<TextBox>().Text());
            dialog.IsPrimaryButtonEnabled(!value.empty() && value.find(L'/') == std::wstring::npos);
        });

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return hstring{};
        co_return hstring{ trimmed(box.Text()) };
    }

    /* Nouveau dossier sous un dossier ou au premier niveau d'un compte.
     * Paramètres :
     *   account     : identifiant du compte
     *   parent      : chemin complet du parent ("" : premier niveau)
     *   parent_name : nom affiché du parent ou du compte */
    fire_and_forget MainWindow::CreateFolderAsync(hstring account, hstring parent, hstring parent_name)
    {
        auto strong = get_strong();
        hstring name = co_await PromptFolderNameAsync(L"Nouveau dossier",
            (parent.empty() ? L"Nom du nouveau dossier au premier niveau de « " : L"Nom du nouveau sous-dossier de « ") +
                parent_name + L" »",
            L"");
        if (name.empty())
            co_return;

        ShowStatus(L"Création du dossier « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->CreateFolder(account, parent, name, [this, account, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Nouveau dossier « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring full_name = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(L"Dossier « " + name + L" » créé.", InfoBarSeverity::Success);
            ReloadAccountFolders(account, L"", full_name);
        });
    }

    /* Renomme un dossier (il garde son parent).
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché actuel */
    fire_and_forget MainWindow::RenameFolderAsync(hstring account, hstring full_name, hstring name)
    {
        auto strong = get_strong();
        hstring new_name = co_await PromptFolderNameAsync(L"Renommer le dossier", L"Nouveau nom de « " + name + L" »", name);
        if (new_name.empty() || new_name == name)
            co_return;

        ShowStatus(L"Renommage de « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->RenameFolder(account, full_name, new_name,
            [this, account, full_name, name, new_name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Renommer « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring renamed = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(L"Dossier « " + name + L" » renommé en « " + new_name + L" ».", InfoBarSeverity::Success);
            ReloadAccountFolders(account, full_name, renamed);
        });
    }

    /* Déplace ou copie un dossier sous un autre parent du même compte.
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché du dossier
     *   parent    : chemin complet de la destination ("" : premier niveau)
     *   copy      : copier (dossier de même nom et copie des messages) */
    void MainWindow::MoveOrCopyFolder(hstring const& account, hstring const& full_name, hstring const& name,
                                      hstring const& parent, bool copy)
    {
        ShowStatus((copy ? L"Copie du dossier « " : L"Déplacement du dossier « ") + name + L" »…",
            InfoBarSeverity::Informational);

        m_core->MoveOrCopyFolder(account, full_name, parent, copy,
            [this, account, full_name, name, copy](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus((copy ? L"Copier « " : L"Déplacer « ") + name + L" » : " + error, InfoBarSeverity::Error);
                /* une copie interrompue laisse le dossier créé */
                if (copy)
                    ReloadAccountFolders(account, L"", L"");
                return;
            }

            JsonObject result;
            if (!JsonObject::TryParse(json, result))
                result = JsonObject{};
            hstring target = result.GetNamedString(L"full_name", L"");

            if (copy)
            {
                auto count = static_cast<uint32_t>(result.GetNamedNumber(L"count", 0));
                ShowStatus(hstring{ L"Dossier « " } + name + L" » copié (" + to_hstring(count) + L" message(s)).",
                    InfoBarSeverity::Success);
                ReloadAccountFolders(account, L"", target);
            }
            else
            {
                ShowStatus(L"Dossier « " + name + L" » déplacé.", InfoBarSeverity::Success);
                ReloadAccountFolders(account, full_name, target);
            }
        });
    }

    /* Supprime un dossier sans sous-dossiers, après confirmation : ses
     * messages sont supprimés définitivement.
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché du dossier
     *   total     : nombre de messages connu (0 si inconnu) */
    fire_and_forget MainWindow::DeleteFolderAsync(hstring account, hstring full_name, hstring name, int32_t total)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        ContentDialog confirm;
        confirm.XamlRoot(Content().XamlRoot());
        confirm.Title(box_value(L"Supprimer le dossier ?"));
        TextBlock text;
        text.TextWrapping(TextWrapping::Wrap);
        text.Text(total > 0
            ? hstring{ L"Le dossier « " } + name + L" » et ses " + to_hstring(total) +
                L" message(s) seront supprimés définitivement. Cette action est irréversible."
            : L"Le dossier « " + name + L" » et tous les messages qu'il contient seront supprimés définitivement. "
                L"Cette action est irréversible.");
        confirm.Content(text);
        confirm.PrimaryButtonText(L"Supprimer");
        confirm.CloseButtonText(L"Annuler");
        confirm.DefaultButton(ContentDialogButton::Close);

        m_dialog_open = true;
        auto result = co_await confirm.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        ShowStatus(L"Suppression du dossier « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->DeleteFolder(account, full_name, [this, account, full_name, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Supprimer « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            auto count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0u;
            ShowStatus(hstring{ L"Dossier « " } + name + L" » supprimé (" + to_hstring(count) + L" message(s)).",
                InfoBarSeverity::Success);
            ReloadAccountFolders(account, full_name, L"");
        });
    }

    /* Recharge les dossiers d'un compte après une opération de gestion, en
     * gardant les nœuds dépliés et la sélection.
     * Paramètres :
     *   account  : identifiant du compte
     *   old_name : dossier renommé, déplacé ou supprimé ("" : aucun)
     *   new_name : nouveau chemin du dossier renommé ou déplacé, ou dossier
     *              créé ("" pour une suppression) */
    void MainWindow::ReloadAccountFolders(hstring const& account, hstring const& old_name, hstring const& new_name)
    {
        /* chemin d'un dossier après l'opération (vide : supprimé) */
        auto map_name = [old_name, new_name](std::wstring const& path) -> std::wstring
        {
            if (!IsFolderOrSubfolder(path, old_name))
                return path;
            return new_name.empty() ? std::wstring{} : std::wstring{ new_name } + path.substr(old_name.size());
        };

        /* dossier affiché renommé, déplacé ou supprimé */
        if (account == m_account && IsFolderOrSubfolder(m_folder, old_name))
        {
            hstring current{ map_name(std::wstring{ m_folder }) };
            m_folder = current;
            if (current.empty())
            {
                ++m_message_request;
                MessageList().Items().Clear();
                m_message_states.clear();
                m_all_messages = nullptr;
                ClearReader();
                FolderTitle().Text(L"");
            }
            else
            {
                LoadMessages(true);
            }
        }

        /* nœuds dépliés, sous leur nouveau chemin */
        std::wstring prefix = std::wstring{ account } + L'\x1F';
        std::vector<std::wstring> expanded;
        for (auto const& [key, node] : m_nodes)
        {
            if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0 && node.IsExpanded())
                expanded.push_back(map_name(key.substr(prefix.size())));
        }

        m_core->ListFolders(account, [this, account, new_name, expanded](hstring const& folders, hstring const& error)
        {
            auto account_node = m_nodes.find(NodeKey(account, L""));
            if (!error.empty() || account_node == m_nodes.end())
            {
                if (!error.empty())
                    ShowStatus(L"Dossiers : " + error, InfoBarSeverity::Warning);
                return;
            }

            auto parsed = JsonArray::Parse(folders);
            m_account_folders[std::wstring{ account }] = parsed;

            std::wstring prefix = std::wstring{ account } + L'\x1F';
            for (auto it = m_nodes.begin(); it != m_nodes.end();)
            {
                if (it->first.size() > prefix.size() && it->first.compare(0, prefix.size(), prefix) == 0)
                    it = m_nodes.erase(it);
                else
                    ++it;
            }

            /* vider les enfants replie le nœud du compte : son état est rétabli */
            TreeViewNode root = account_node->second;
            bool root_expanded = root.IsExpanded();
            root.Children().Clear();
            AddFolders(root, parsed, account);
            UpdateMailBadge();
            root.IsExpanded(root_expanded);

            for (auto const& path : expanded)
            {
                if (auto node = m_nodes.find(NodeKey(account, hstring{ path })); node != m_nodes.end())
                    node->second.IsExpanded(true);
            }

            /* parents du dossier créé, renommé ou déplacé : dépliés */
            std::wstring path{ new_name };
            for (auto slash = path.rfind(L'/'); slash != std::wstring::npos; slash = path.rfind(L'/', slash - 1))
            {
                if (auto node = m_nodes.find(NodeKey(account, hstring{ path.substr(0, slash) })); node != m_nodes.end())
                    node->second.IsExpanded(true);
                if (slash == 0)
                    break;
            }

            if (account == m_account && !m_folder.empty())
            {
                if (auto node = m_nodes.find(NodeKey(account, m_folder)); node != m_nodes.end())
                    FolderTree().SelectedNode(node->second);
            }
        });
    }

    void MainWindow::OnFolderInvoked(TreeView const&, TreeViewItemInvokedEventArgs const& args)
    {
        auto node = args.InvokedItem().try_as<TreeViewNode>();
        auto item = node ? node.Content().try_as<EvolutionWin::FolderItem>() : nullptr;
        if (!item)
            return;

        hstring account = item.Account();
        hstring folder = item.Folder();

        /* accounts and non-selectable folders only expand */
        if (folder.empty())
        {
            node.IsExpanded(!node.IsExpanded());
            return;
        }

        m_account = account;
        m_folder = folder;
        ++m_search_request;

        FolderTitle().Text(item.Name());
        LoadFolderSort();
        UpdateRibbon();
        SearchBox().Text(L"");

        LoadMessages(false);
    }

    /* Élément de l'arborescence du dossier affiché (nullptr sans dossier). */
    EvolutionWin::FolderItem MainWindow::CurrentFolderItem()
    {
        if (m_account.empty() || m_folder.empty())
            return nullptr;
        auto node = m_nodes.find(NodeKey(m_account, m_folder));
        return node != m_nodes.end() ? node->second.Content().try_as<EvolutionWin::FolderItem>() : nullptr;
    }

    /* onglet Dossier : commandes sur le dossier affiché (ou au premier niveau
     * du compte affiché pour Nouveau dossier sans dossier) */
    void MainWindow::OnRibbonNewFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            CreateFolderAsync(m_account, m_folder, item.Name());
        else if (!m_account.empty())
            CreateFolderAsync(m_account, L"", L"");
    }

    void MainWindow::OnRibbonRenameFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RenameFolderAsync(m_account, m_folder, item.Name());
    }

    /* Déplacer / Copier : mêmes entrées que le menu contextuel du dossier */
    void MainWindow::OnRibbonFolderMenuOpening(IInspectable const&, IInspectable const&)
    {
        RibbonFolderMenu().Items().Clear();
        if (auto item = CurrentFolderItem())
            AppendFolderManagementItems(RibbonFolderMenu(), item, m_folder);
    }

    void MainWindow::OnRibbonMarkAllReadClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RunFolderActionAsync(item, L"mark-all-read");
    }

    void MainWindow::OnRibbonEmptyFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RunFolderActionAsync(item, L"empty");
    }

    void MainWindow::OnRibbonDeleteFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto item = CurrentFolderItem();
        if (!item)
            return;
        JsonObject folder{ nullptr };
        if (auto folders = m_account_folders.find(std::wstring{ m_account }); folders != m_account_folders.end() && folders->second)
            folder = FindFolderJson(folders->second, m_folder);
        DeleteFolderAsync(m_account, m_folder, item.Name(),
            folder ? static_cast<int32_t>(folder.GetNamedNumber(L"total", 0)) : 0);
    }
}
