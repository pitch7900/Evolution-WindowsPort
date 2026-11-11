#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "UnifiedViews.h"

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
#include <functional>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <vector>
#include <set>

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
                ShowStatus(Tr(L"Comptes : ") + error, InfoBarSeverity::Error);
                return;
            }

            static bool traced_accounts = false;
            if (!traced_accounts)
            {
                traced_accounts = true;
                evo_core_trace("interface : liste des comptes reçue");
            }

            FolderTree().RootNodes().Clear();
            m_nodes.clear();
            m_favorite_nodes.clear();
            m_favorites_root = nullptr;
            m_search_root = nullptr;
            m_account_folders.clear();
            m_accounts = JsonArray::Parse(json);
            RefreshAccountsView();

            /* états de connexion des comptes supprimés : sans cette purge, une erreur
             * restée en mémoire garde la pastille rouge alors que le compte a disparu */
            for (auto it = m_account_states.begin(); it != m_account_states.end();)
            {
                bool known = false;
                for (auto const& value : m_accounts)
                    if (value.GetObject().GetNamedString(L"uid", L"") == it->first)
                    {
                        known = true;
                        break;
                    }
                if (known || it->first == L"global")
                    ++it;
                else
                    it = m_account_states.erase(it);
            }
            RefreshConnectionIndicator();

            std::vector<JsonObject> sorted_accounts;
            for (auto const& value : m_accounts)
                sorted_accounts.push_back(value.GetObject());

            /* Ordonnancement ergonomique des comptes :
             * 1. Compte principal par défaut en tête de liste
             * 2. Autres comptes distants par ordre alphabétique
             * 3. Dossiers locaux ("Sur cet ordinateur") en fin de liste */
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

            /* vues unifiées (Boîte de réception, Envoyés, Brouillons, Corbeille de tous les comptes) en tête */
            AddUnifiedViewsNodes();

            /* nombre de listes de dossiers attendues : quand elles sont toutes arrivées sans qu'aucun dossier
               n'ait pu être restauré, aucune liste de messages ne viendra (aucun compte, boîtes introuvables) :
               l'écran de démarrage ne doit pas attendre */
            auto pending_folder_lists = std::make_shared<size_t>(sorted_accounts.size());
            if (sorted_accounts.empty())
                RevealMainWindow(L"aucun compte");

            for (auto const& account : sorted_accounts)
            {
                hstring uid = account.GetNamedString(L"uid");
                bool local = account.GetNamedBoolean(L"local", false);

                bool enabled = account.GetNamedBoolean(L"enabled", true);
                hstring acc_name = account.GetNamedString(L"name", uid);
                if (!enabled)
                    acc_name = Fmt(Tr(L"{0} (Désactivé)", L"{0} (Disabled)"), { acc_name });

                TreeViewNode node;
                node.Content(EvolutionWin::FolderItem(acc_name,
                    local ? L"\xE7F8" : (enabled ? L"\xE715" : L"\xE8D8"), 0, uid, L"", true));
                node.IsExpanded(true);
                FolderTree().RootNodes().Append(node);
                m_nodes[NodeKey(uid, L"")] = node;

                m_core->ListFolders(uid, [this, node, uid, pending_folder_lists](hstring const& folders, hstring const& folders_error)
                {
                    /* dernière liste de dossiers reçue, sans dossier à afficher : on ne reste pas sur l'écran de démarrage */
                    auto finish_wait = [this, pending_folder_lists]()
                    {
                        if (*pending_folder_lists > 0 && --(*pending_folder_lists) == 0 && (m_account.empty() || m_folder.empty()))
                            RevealMainWindow(L"aucun dossier à afficher");
                    };

                    if (!folders_error.empty())
                    {
                        ShowStatus(Tr(L"Dossiers : ") + folders_error, InfoBarSeverity::Warning);
                        finish_wait();
                        return;
                    }

                    evo_core_trace("interface : arborescence des dossiers reçue");
                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ uid }] = parsed;
                    AddFolders(node, parsed, uid);
                    RefreshFavoritesNode();
                    UpdateMailBadge();
                    RestoreSessionFolder();
                    finish_wait();
                });
            }

            /* dossiers de recherche enregistrés : groupe en bas de l'arborescence */
            RefreshSearchFoldersNode();

            /* Relève automatique au démarrage si l'option mail.sync-on-startup est activée */
            if (!m_startup_synced && ::EvolutionWin::Settings::Instance().GetBoolean(L"mail.sync-on-startup", true))
            {
                m_startup_synced = true;
                /* dossiers de base seulement : les autres se synchronisent à l'ouverture (liste depuis le cache, relève en fond) */
                m_core->RefreshBase();
            }
        });
    }

    /* Rang d'un dossier spécial dans l'arborescence (ordre d'Outlook), les
     * autres dossiers après, par ordre alphabétique. */
    static int SpecialFolderRank(hstring const& type)
    {
        static wchar_t const* const order[] = { L"inbox", L"sent", L"drafts", L"trash", L"archive", L"junk", L"outbox" };
        for (int ii = 0; ii < static_cast<int>(std::size(order)); ii++)
            if (type == order[ii])
                return ii;
        return static_cast<int>(std::size(order));
    }

    /* Nom affiché d'un dossier spécial, quelle que soit la langue du serveur
     * (« INBOX », « Sent Items »…) ; vide pour un dossier ordinaire. */
    static hstring SpecialFolderName(hstring const& type)
    {
        if (type == L"inbox") return Tr(L"Boîte de réception", L"Inbox", L"SpecialFolder_Inbox");
        if (type == L"drafts") return Tr(L"Brouillons", L"Drafts", L"SpecialFolder_Drafts");
        if (type == L"sent") return Tr(L"Éléments envoyés", L"Sent Items", L"SpecialFolder_Sent");
        if (type == L"trash") return Tr(L"Éléments supprimés", L"Deleted Items", L"SpecialFolder_Trash");
        if (type == L"archive") return Tr(L"Archive", L"Archive", L"SpecialFolder_Archive");
        if (type == L"junk") return Tr(L"Courrier indésirable", L"Junk Email", L"SpecialFolder_Junk");
        if (type == L"outbox") return Tr(L"Boîte d'envoi", L"Outbox", L"SpecialFolder_Outbox");
        return {};
    }

    void MainWindow::AddFolders(TreeViewNode const& parent, JsonArray const& folders, hstring const& account)
    {
        /* dossiers spéciaux d'abord, puis ordre alphabétique (règles de la langue) */
        std::vector<JsonObject> sorted;
        for (auto const& value : folders)
            sorted.push_back(value.GetObject());

        /* Respect de la préférence de tri : surcharge spécifique au compte account.<account>.folder-sort
         * avec repli sur le paramètre global mail.folder-sort ("special-first" par défaut ou "alphabetical"). */
        hstring acc_sort_key = L"account." + account + L".folder-sort";
        hstring account_sort = ::EvolutionWin::Settings::Instance().GetString(acc_sort_key, L"");
        hstring effective_sort = !account_sort.empty()
            ? account_sort
            : ::EvolutionWin::Settings::Instance().GetString(L"mail.folder-sort", L"special-first");
        bool special_first = (effective_sort != L"alphabetical");

        std::stable_sort(sorted.begin(), sorted.end(), [special_first](JsonObject const& a, JsonObject const& b)
        {
            if (special_first)
            {
                int ra = SpecialFolderRank(a.GetNamedString(L"type", L"")), rb = SpecialFolderRank(b.GetNamedString(L"type", L""));
                if (ra != rb)
                    return ra < rb;
            }
            hstring type_a = a.GetNamedString(L"type", L"");
            hstring na = SpecialFolderName(type_a);
            if (na.empty()) na = a.GetNamedString(L"name", L"");

            hstring type_b = b.GetNamedString(L"type", L"");
            hstring nb = SpecialFolderName(type_b);
            if (nb.empty()) nb = b.GetNamedString(L"name", L"");

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

            SetFolderUnread(account, full_name, static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)));

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

        /* dossiers de recherche : menu propre (nouveau, modifier, supprimer) */
        if (item.Account() == ::EvolutionWin::UnifiedViews::kSearchAccountId)
        {
            MenuFlyout search_menu;
            BuildSearchFolderMenu(search_menu, item);
            Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions search_options;
            Point search_position;
            if (args.TryGetPosition(container, search_position))
                search_options.Position(search_position);
            if (search_menu.Items().Size() > 0)
                search_menu.ShowAt(container, search_options);
            return;
        }

        /* vues unifiées : pas de gestion de dossier (ce ne sont pas de vrais dossiers) ; idem pour le groupe « Favoris » */
        if (::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ item.Account() }) ||
            (item.IsAccount() && item.Account() == L"favorites"))
            return;

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
            if (item.IsAccount())
            {
                ToggleMenuFlyoutItem notif_account_item;
                notif_account_item.Text(Tr(L"Notifications pour ce compte"));
                FontIcon notif_icon;
                notif_icon.Glyph(L"\xEA8F");
                notif_account_item.Icon(notif_icon);
                hstring account_key = L"account." + item.Account() + L".notifications-enabled";
                bool notif_acc_active = ::EvolutionWin::Settings::Instance().GetBoolean(account_key, true);
                notif_account_item.IsChecked(notif_acc_active);
                notif_account_item.Click([this, account_key](IInspectable const& sender, RoutedEventArgs const&)
                {
                    if (auto toggle = sender.try_as<ToggleMenuFlyoutItem>())
                    {
                        ::EvolutionWin::Settings::Instance().SetBoolean(account_key, toggle.IsChecked());
                    }
                });
                menu.Items().Append(notif_account_item);
                menu.Items().Append(MenuFlyoutSeparator{});

                /* compte Microsoft 365 : ouverture d'une boîte partagée ou d'un autre utilisateur */
                bool m365_account = false, shared_account = false;
                for (auto const& value : m_accounts ? m_accounts : JsonArray{})
                {
                    auto account = value.GetObject();
                    if (account.GetNamedString(L"uid", L"") == item.Account() && account.GetNamedString(L"protocol", L"") == L"microsoft365")
                    {
                        m365_account = true;
                        shared_account = !account.GetNamedString(L"shared", L"").empty();
                    }
                }
                if (shared_account)
                {
                    MenuFlyoutItem close_item;
                    close_item.Text(Tr(L"Fermer la boîte partagée", L"Close the shared mailbox"));
                    FontIcon close_icon;
                    close_icon.Glyph(L"\xE711");
                    close_item.Icon(close_icon);
                    close_item.Click([this, account_uid = item.Account()](IInspectable const&, RoutedEventArgs const&)
                    {
                        CloseSharedMailboxAsync(account_uid);
                    });
                    menu.Items().Append(close_item);
                    menu.Items().Append(MenuFlyoutSeparator{});
                }
                else if (m365_account)
                {
                    MenuFlyoutItem shared_item;
                    shared_item.Text(Tr(L"Ouvrir la boîte d'un autre utilisateur…", L"Open another user's mailbox…"));
                    FontIcon shared_icon;
                    shared_icon.Glyph(L"\xE716");
                    shared_item.Icon(shared_icon);
                    shared_item.Click([this, account_uid = item.Account()](IInspectable const&, RoutedEventArgs const&)
                    {
                        ShowSharedMailboxDialogAsync(account_uid);
                    });
                    menu.Items().Append(shared_item);
                    menu.Items().Append(MenuFlyoutSeparator{});
                }
                if (m365_account)
                {
                    ToggleMenuFlyoutItem focused_item;
                    focused_item.Text(Tr(L"Boîte de réception prioritaire", L"Focused Inbox"));
                    FontIcon focused_icon;
                    focused_icon.Glyph(L"\xE8D5");
                    focused_item.Icon(focused_icon);
                    hstring focused_key = L"account." + item.Account() + L".focused-inbox";
                    focused_item.IsChecked(::EvolutionWin::Settings::Instance().GetBoolean(focused_key, true));
                    focused_item.Click([this, focused_key](IInspectable const& sender, RoutedEventArgs const&)
                    {
                        if (auto toggle = sender.try_as<ToggleMenuFlyoutItem>())
                            ::EvolutionWin::Settings::Instance().SetBoolean(focused_key, toggle.IsChecked());
                        UpdateFocusedInbox();
                        ReapplyMessageFilters();
                    });
                    menu.Items().Append(focused_item);

                    MenuFlyoutItem recover_item;
                    recover_item.Text(Tr(L"Récupérer les éléments supprimés du serveur…", L"Recover deleted items from the server…"));
                    FontIcon recover_icon;
                    recover_icon.Glyph(L"\xE777");
                    recover_item.Icon(recover_icon);
                    recover_item.Click([this, account_uid = item.Account()](IInspectable const&, RoutedEventArgs const&)
                    {
                        ShowRecoverableItemsDialogAsync(account_uid);
                    });
                    menu.Items().Append(recover_item);
                    menu.Items().Append(MenuFlyoutSeparator{});
                }
            }

            AppendFolderManagementItems(menu, item, NodeFolderName(node));
            if (menu.Items().Size() > 0)
                menu.ShowAt(container, options);
            return;
        }

        /* Favoris : ajout / retrait, et ordre dans la section « Favoris » */
        {
            hstring fav_account = item.Account();
            hstring fav_folder = item.Folder();
            bool favorite = IsFavoriteFolder(fav_account, fav_folder);
            MenuFlyoutItem favorite_item;
            favorite_item.Text(favorite ? Tr(L"Retirer des favoris", L"Remove from Favorites") : Tr(L"Ajouter aux favoris", L"Add to Favorites"));
            FontIcon favorite_icon;
            favorite_icon.Glyph(favorite ? L"\xE735" : L"\xE734");
            favorite_item.Icon(favorite_icon);
            favorite_item.Click([this, fav_account, fav_folder, favorite](IInspectable const&, RoutedEventArgs const&)
            {
                SetFavoriteFolder(fav_account, fav_folder, !favorite);
            });
            menu.Items().Append(favorite_item);

            /* copie du dossier dans la section « Favoris » : Monter / Descendre */
            if (favorite && node.Parent() && m_favorites_root && node.Parent() == m_favorites_root)
            {
                MenuFlyoutItem up_item;
                up_item.Text(Tr(L"Monter dans les favoris", L"Move up in Favorites"));
                FontIcon up_icon;
                up_icon.Glyph(L"\xE74A");
                up_item.Icon(up_icon);
                up_item.Click([this, fav_account, fav_folder](IInspectable const&, RoutedEventArgs const&) { MoveFavoriteFolder(fav_account, fav_folder, -1); });
                menu.Items().Append(up_item);
                MenuFlyoutItem down_item;
                down_item.Text(Tr(L"Descendre dans les favoris", L"Move down in Favorites"));
                FontIcon down_icon;
                down_icon.Glyph(L"\xE74B");
                down_item.Icon(down_icon);
                down_item.Click([this, fav_account, fav_folder](IInspectable const&, RoutedEventArgs const&) { MoveFavoriteFolder(fav_account, fav_folder, 1); });
                menu.Items().Append(down_item);
            }
            menu.Items().Append(MenuFlyoutSeparator{});
        }

        MenuFlyoutItem mark_read;
        mark_read.Text(Tr(L"Marquer tout comme lu", L"Mark all as read", L"FolderAction_MarkAllAsRead"));
        mark_read.Icon(FontIcon{});
        mark_read.Icon().as<FontIcon>().Glyph(L"\xE8C3");
        mark_read.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"mark-all-read");
        });
        menu.Items().Append(mark_read);
        menu.Items().Append(MenuFlyoutSeparator{});

        MenuFlyoutItem empty;
        empty.Text(trash ? Tr(L"Vider la Corbeille", L"Empty Deleted Items", L"FolderAction_EmptyTrash")
                         : junk ? Tr(L"Vider les Indésirables", L"Empty Junk Email", L"FolderAction_EmptyJunk")
                                : Tr(L"Vider le dossier", L"Empty folder", L"FolderAction_EmptyFolder"));
        empty.Icon(FontIcon{});
        empty.Icon().as<FontIcon>().Glyph(L"\xE74D");
        empty.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"empty");
        });
        menu.Items().Append(empty);
        ToggleMenuFlyoutItem notif_folder_item;
        notif_folder_item.Text(Tr(L"Notifications pour ce dossier", L"Notifications for this folder"));
        FontIcon notif_icon_fld;
        notif_icon_fld.Glyph(L"\xEA8F");
        notif_folder_item.Icon(notif_icon_fld);
        hstring folder_key = L"folder." + item.Account() + L"." + item.Folder() + L".notifications-enabled";
        bool notif_fld_active = ::EvolutionWin::Settings::Instance().GetBoolean(folder_key, true);
        notif_folder_item.IsChecked(notif_fld_active);
        notif_folder_item.Click([this, folder_key](IInspectable const& sender, RoutedEventArgs const&)
        {
            if (auto toggle = sender.try_as<ToggleMenuFlyoutItem>())
            {
                ::EvolutionWin::Settings::Instance().SetBoolean(folder_key, toggle.IsChecked());
            }
        });
        menu.Items().Append(notif_folder_item);

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
            confirm.Title(box_value(permanent
                ? Tr(L"Supprimer définitivement ?", L"Permanently delete?")
                : Tr(L"Vider le dossier ?", L"Empty folder?")));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            text.Text(permanent
                ? Fmt(Tr(L"Tous les messages de « {0} » seront supprimés définitivement. Cette action est irréversible.",
                         L"All messages in \"{0}\" will be permanently deleted. This action cannot be undone."), { item.Name() })
                : Fmt(Tr(L"Tous les messages de « {0} » seront déplacés vers la Corbeille.",
                         L"All messages in \"{0}\" will be moved to Deleted Items."), { item.Name() }));
            confirm.Content(text);
            confirm.PrimaryButtonText(permanent ? Tr(L"Supprimer", L"Delete", L"Common_Delete") : Tr(L"Vider", L"Empty", L"Common_Empty"));
            confirm.CloseButtonText(Tr(L"Annuler", L"Cancel", L"Common_Cancel"));
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
        ShowStatus(Fmt(action == L"empty" ? Tr(L"Suppression des messages de « {0} »…") : Tr(L"Marquage comme lus dans « {0} »…"), { name }),
            InfoBarSeverity::Informational);

        m_core->FolderAction(account, folder, action, [this, account, folder, name, action](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"« {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            uint32_t count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0;
            ShowStatus(action == L"empty"
                ? Fmt(Tr(L"{0} message(s) supprimé(s) de « {1} »."), { to_hstring(count), name })
                : Fmt(Tr(L"{0} message(s) marqué(s) comme lu(s) dans « {1} »."), { to_hstring(count), name }),
                InfoBarSeverity::Success);

            /* dossier affiché (ou inclus dans la vue unifiée affichée) : la liste est mise à jour tout de suite */
            if ((account == m_account && folder == m_folder) || UnifiedFolderMatches(account, folder))
            {
                if (action != L"empty" && m_all_messages)
                {
                    /* « Tout marquer comme lu » : les messages du dossier passent en lu à l'écran sans attendre le rechargement */
                    uint32_t updated = 0;
                    for (uint32_t i = 0; i < m_all_messages.Size(); ++i)
                    {
                        auto message = m_all_messages.GetAt(i).GetObject();
                        hstring uid = message.GetNamedString(L"uid", L"");
                        auto location = GetMessageLocation(uid);
                        hstring message_account = location.account.empty() ? m_account : location.account;
                        hstring message_folder = location.folder.empty() ? m_folder : location.folder;
                        if (uid.empty() || message_account != account || message_folder != folder)
                            continue;
                        message.SetNamedValue(L"seen", JsonValue::CreateBooleanValue(true));
                        auto& state = m_message_states[std::wstring{ uid }];
                        state.seen = true;
                        if (auto grid = FindMessageItem(uid))
                        {
                            UpdateMessageItemVisual(grid, true, state.flagged);
                            ++updated;
                        }
                    }
                    wchar_t info[120]{};
                    swprintf_s(info, L"tout marquer comme lu : %u message(s) mis à jour à l'écran sur %u", updated, m_all_messages.Size());
                    ::EvolutionWin::LogError(L"liste", 0, info);
                }
                /* « Tout marquer comme lu » : pas de rechargement immédiat, qui relirait le résumé local avant que le serveur
                   ait enregistré les nouveaux indicateurs et ré-afficherait les messages non lus */
                if (action == L"empty")
                    LoadMessages(false);
            }
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
                item.Icon(CreateFolderIcon(folder.GetNamedString(L"type", L"")));
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
        if (!folders && !is_account)
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

        add_item(Tr(L"Nouveau dossier…", L"New folder…", L"FolderAction_NewFolder"), L"\xE8F4", selectable,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                CreateFolderAsync(account, full_name, name);
            });

        if (!is_account)
            add_item(Tr(L"Nouveau dossier à la racine du compte…"), L"\xE8F4", true,
                [this, account](IInspectable const&, RoutedEventArgs const&)
                {
                    CreateFolderAsync(account, L"", L"");
                });

        add_item(Tr(L"Synchronisation hors connexion…"), L"\xE895", selectable,
            [this, account, full_name, name, is_account](IInspectable const&, RoutedEventArgs const&)
            {
                OpenOfflineSyncDialog(account, is_account ? hstring{} : full_name, name);
            });

        if (is_account)
            add_item(Tr(L"Délégations et autorisations…"), L"\xE716", true,
                [this, account, name](IInspectable const&, RoutedEventArgs const&)
                {
                    ShowDelegatesDialogAsync(account, name);
                });

        if (is_account)
        {
            // Sous-menu de tri des dossiers pour ce compte
            MenuFlyoutSubItem sort_sub;
            sort_sub.Text(Tr(L"Ordre des dossiers", L"Folder sort order"));
            sort_sub.Icon(FontIcon{});
            sort_sub.Icon().as<FontIcon>().Glyph(L"\xE8CB");

            hstring acc_sort_key = L"account." + account + L".folder-sort";
            hstring acc_sort = ::EvolutionWin::Settings::Instance().GetString(acc_sort_key, L"");
            hstring effective_sort = !acc_sort.empty()
                ? acc_sort
                : ::EvolutionWin::Settings::Instance().GetString(L"mail.folder-sort", L"special-first");

            MenuFlyoutItem special_first_item;
            special_first_item.Text((effective_sort != L"alphabetical" ? L"✔ " : L"    ") + Tr(L"Dossiers spéciaux d'abord", L"Special folders first"));
            special_first_item.Click([this, acc_sort_key](IInspectable const&, RoutedEventArgs const&)
            {
                ::EvolutionWin::Settings::Instance().SetString(acc_sort_key, L"special-first");
                LoadAccounts();
            });
            sort_sub.Items().Append(special_first_item);

            MenuFlyoutItem alpha_item;
            alpha_item.Text((effective_sort == L"alphabetical" ? L"✔ " : L"    ") + Tr(L"Trier tous les dossiers de A à Z", L"Sort all folders A to Z"));
            alpha_item.Click([this, acc_sort_key](IInspectable const&, RoutedEventArgs const&)
            {
                ::EvolutionWin::Settings::Instance().SetString(acc_sort_key, L"alphabetical");
                LoadAccounts();
            });
            sort_sub.Items().Append(alpha_item);

            if (!acc_sort.empty())
            {
                sort_sub.Items().Append(MenuFlyoutSeparator{});
                MenuFlyoutItem reset_item;
                reset_item.Text(Tr(L"Rétablir l'ordre global par défaut", L"Reset to default global sort order"));
                reset_item.Click([this, acc_sort_key](IInspectable const&, RoutedEventArgs const&)
                {
                    ::EvolutionWin::Settings::Instance().SetString(acc_sort_key, L"");
                    LoadAccounts();
                });
                sort_sub.Items().Append(reset_item);
            }

            menu.Items().Append(sort_sub);
            menu.Items().Append(MenuFlyoutSeparator{});

            if (account != L"local")
            {
                add_item(Tr(L"Réponses automatiques (absence du bureau)…", L"Automatic replies (Out of Office)…"), L"\xE789", true,
                    [this, account](IInspectable const&, RoutedEventArgs const&)
                    {
                        ShowOutOfOfficeDialogAsync(account);
                    });

                add_item(Tr(L"Propriétés du compte…", L"Account properties…"), L"\xE713", true,
                    [this, account](IInspectable const&, RoutedEventArgs const&)
                    {
                        ShowEditAccountDialogAsync(account);
                    });
            }
            return;
        }

        add_item(Tr(L"Renommer…", L"Rename…", L"FolderAction_Rename"), L"\xE8AC", !special,
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
            /* ouvre la fenêtre de choix du dossier de destination (arborescence par compte, avec recherche) */
            MenuFlyoutItem pick_item;
            pick_item.Text(copy ? Tr(L"Copier vers", L"Copy to", L"FolderAction_CopyTo") : Tr(L"Déplacer vers", L"Move to", L"FolderAction_MoveTo"));
            pick_item.Icon(FontIcon{});
            pick_item.Icon().as<FontIcon>().Glyph(copy ? L"\xE8C8" : L"\xE8DE");
            pick_item.IsEnabled(copy ? selectable : !special);
            pick_item.Click([this, account, full_name, name, copy, parent](IInspectable const&, RoutedEventArgs const&)
            {
                FolderPickOptions options;
                options.title = Fmt(copy ? Tr(L"Copier le dossier « {0} » vers…") : Tr(L"Déplacer le dossier « {0} » vers…"), { name });
                options.confirm_label = copy ? Tr(L"Copier ici") : Tr(L"Déplacer ici");
                options.allow_account_root = true;
                /* destinations sans effet : le parent actuel ; pour un déplacement, le dossier lui-même et ses sous-dossiers */
                options.blocked = [this, account, full_name, parent, copy](hstring const& target_account, hstring const& target)
                {
                    /* un autre compte accepte tout emplacement */
                    if (target_account != account)
                        return false;
                    if (target == parent)
                        return true;
                    return !copy && !target.empty() && IsFolderOrSubfolder(target, full_name);
                };
                PickFolderAsync(options, [this, account, full_name, name, copy](hstring const& target_account, hstring const& target)
                {
                    if (target_account == account)
                        MoveOrCopyFolder(account, full_name, name, target, copy);
                    else
                        TransferFolderToAccount(account, full_name, name, target_account, target, copy);
                });
            });
            menu.Items().Append(pick_item);
        }

        add_item(Tr(L"Supprimer le dossier…", L"Delete folder…", L"FolderAction_Delete"), L"\xE74D", !special && !has_children,
            [this, account, full_name, name, folder](IInspectable const&, RoutedEventArgs const&)
            {
                DeleteFolderAsync(account, full_name, name, static_cast<int32_t>(folder.GetNamedNumber(L"total", 0)));
            });

        menu.Items().Append(MenuFlyoutSeparator{});
        add_item(Tr(L"Exporter tous les messages (.eml)…", L"Export all messages (.eml)…", L"FolderAction_ExportEml"), L"\xEDE1", true,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                ExportFolderEmlAsync(account, full_name, name);
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
        dialog.PrimaryButtonText(Tr(L"OK"));
        dialog.CloseButtonText(Tr(L"Annuler"));
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
        if (parent.empty() && parent_name.empty())
        {
            /* racine du compte : affiche le nom du compte dans la boîte de saisie */
            parent_name = account;
            for (auto const& value : m_accounts ? m_accounts : JsonArray{})
            {
                auto entry = value.GetObject();
                if (entry.GetNamedString(L"uid", L"") == account)
                    parent_name = entry.GetNamedString(L"name", account);
            }
        }
        hstring name = co_await PromptFolderNameAsync(Tr(L"Nouveau dossier"),
            Fmt(parent.empty() ? Tr(L"Nom du nouveau dossier au premier niveau de « {0} »") : Tr(L"Nom du nouveau sous-dossier de « {0} »"),
                { parent_name }),
            L"");
        if (name.empty())
            co_return;

        ShowStatus(Fmt(Tr(L"Création du dossier « {0} »…"), { name }), InfoBarSeverity::Informational);
        m_core->CreateFolder(account, parent, name, [this, account, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Nouveau dossier « {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring full_name = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(Fmt(Tr(L"Dossier « {0} » créé."), { name }), InfoBarSeverity::Success);
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
        hstring new_name = co_await PromptFolderNameAsync(Tr(L"Renommer le dossier"), Fmt(Tr(L"Nouveau nom de « {0} »"), { name }), name);
        if (new_name.empty() || new_name == name)
            co_return;

        ShowStatus(Fmt(Tr(L"Renommage de « {0} »…"), { name }), InfoBarSeverity::Informational);
        m_core->RenameFolder(account, full_name, new_name,
            [this, account, full_name, name, new_name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Renommer « {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring renamed = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(Fmt(Tr(L"Dossier « {0} » renommé en « {1} »."), { name, new_name }), InfoBarSeverity::Success);
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
        ShowStatus(Fmt(copy ? Tr(L"Copie du dossier « {0} »…") : Tr(L"Déplacement du dossier « {0} »…"), { name }),
            InfoBarSeverity::Informational);

        m_core->MoveOrCopyFolder(account, full_name, parent, copy,
            [this, account, full_name, name, copy](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(copy ? Tr(L"Copier « {0} » : {1}") : Tr(L"Déplacer « {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
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
                ShowStatus(Fmt(Tr(L"Dossier « {0} » copié ({1} message(s))."), { name, to_hstring(count) }),
                    InfoBarSeverity::Success);
                ReloadAccountFolders(account, L"", target);
            }
            else
            {
                ShowStatus(Fmt(Tr(L"Dossier « {0} » déplacé."), { name }), InfoBarSeverity::Success);
                ReloadAccountFolders(account, full_name, target);
            }
        });
    }

    /* Copie ou déplace un dossier (avec ses sous-dossiers et ses messages) vers un autre compte.
     * Un déplacement ne supprime l'original qu'une fois toute la copie réussie.
     * Paramètres :
     *   account      : compte du dossier
     *   full_name    : chemin complet du dossier
     *   name         : nom affiché du dossier
     *   dest_account : compte de destination
     *   dest_parent  : chemin complet du parent de destination ("" : racine du compte)
     *   copy         : copier (l'original reste) plutôt que déplacer */
    fire_and_forget MainWindow::TransferFolderToAccount(hstring account, hstring full_name, hstring name,
                                                        hstring dest_account, hstring dest_parent, bool copy)
    {
        auto strong = get_strong();

        /* un dossier de même nom existe déjà à la destination : on propose de le renommer */
        auto exists_at_destination = [this, &dest_account, &dest_parent](std::wstring const& candidate)
        {
            auto folders_it = m_account_folders.find(std::wstring{ dest_account });
            if (folders_it == m_account_folders.end() || !folders_it->second)
                return false;
            std::wstring wanted = dest_parent.empty() ? candidate : std::wstring{ dest_parent } + L"/" + candidate;
            std::function<bool(JsonArray const&)> search = [&](JsonArray const& list)
            {
                for (auto const& value : list)
                {
                    auto folder = value.GetObject();
                    hstring existing = folder.GetNamedString(L"full_name", L"");
                    if (CompareStringOrdinal(existing.c_str(), static_cast<int>(existing.size()),
                            wanted.c_str(), static_cast<int>(wanted.size()), TRUE) == CSTR_EQUAL)
                        return true;
                    if (search(folder.GetNamedArray(L"children", JsonArray{})))
                        return true;
                }
                return false;
            };
            return search(folders_it->second);
        };

        hstring new_name = name;
        while (exists_at_destination(std::wstring{ new_name }))
        {
            hstring suggestion = Fmt(Tr(L"{0} (copie)"), { new_name });
            new_name = co_await PromptFolderNameAsync(Tr(L"Dossier déjà existant"),
                Fmt(Tr(L"Un dossier « {0} » existe déjà à la destination. Saisissez un autre nom pour « {1} » :"), { new_name, name }),
                suggestion);
            if (new_name.empty())
                co_return; /* annulé : rien n'est copié ni déplacé */
        }

        ShowStatus(Fmt(copy ? Tr(L"Copie du dossier « {0} »…") : Tr(L"Déplacement du dossier « {0} »…"), { name }),
            InfoBarSeverity::Informational);

        m_core->TransferFolder(account, full_name, dest_account, dest_parent, new_name == name ? hstring{} : new_name, copy,
            [this, account, full_name, name, dest_account, copy](hstring const& json, hstring const& error)
        {
            /* des dossiers ont pu être créés même en cas d'échec : on relit la destination */
            if (!error.empty())
            {
                ShowStatus(Fmt(copy ? Tr(L"Copier « {0} » : {1}") : Tr(L"Déplacer « {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
                ReloadAccountFolders(dest_account, L"", L"");
                if (!copy)
                    ReloadAccountFolders(account, L"", L"");
                return;
            }

            JsonObject result;
            if (!JsonObject::TryParse(json, result))
                result = JsonObject{};
            hstring target = result.GetNamedString(L"full_name", L"");
            auto count = static_cast<uint32_t>(result.GetNamedNumber(L"count", 0));

            ShowStatus(Fmt(copy ? Tr(L"Dossier « {0} » copié ({1} message(s)).") : Tr(L"Dossier « {0} » déplacé ({1} message(s))."),
                { name, to_hstring(count) }), InfoBarSeverity::Success);
            ReloadAccountFolders(dest_account, L"", target);
            if (!copy)
                ReloadAccountFolders(account, full_name, L"");
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
        confirm.Title(box_value(Tr(L"Supprimer le dossier ?")));
        TextBlock text;
        text.TextWrapping(TextWrapping::Wrap);
        text.Text(total > 0
            ? Fmt(Tr(L"Le dossier « {0} » et ses {1} message(s) seront supprimés définitivement. Cette action est irréversible."),
                { name, to_hstring(total) })
            : Fmt(Tr(L"Le dossier « {0} » et tous les messages qu'il contient seront supprimés définitivement. Cette action est irréversible."),
                { name }));
        confirm.Content(text);
        confirm.PrimaryButtonText(Tr(L"Supprimer"));
        confirm.CloseButtonText(Tr(L"Annuler"));
        confirm.DefaultButton(ContentDialogButton::Close);

        m_dialog_open = true;
        auto result = co_await confirm.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        ShowStatus(Fmt(Tr(L"Suppression du dossier « {0} »…"), { name }), InfoBarSeverity::Informational);
        m_core->DeleteFolder(account, full_name, [this, account, full_name, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Supprimer « {0} » : {1}"), { name, error }), InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            auto count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0u;
            ShowStatus(Fmt(Tr(L"Dossier « {0} » supprimé ({1} message(s))."), { name, to_hstring(count) }),
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
                    ShowStatus(Tr(L"Dossiers : ") + error, InfoBarSeverity::Warning);
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
            RefreshFavoritesNode();
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

    /* Indique si le dossier affiché est une vue unifiée multi-comptes
     * (Boîte de réception unifiée, Envoyés, Brouillons ou Corbeille de tous les comptes). */
    /* Favoris : dossiers (de n'importe quel compte) listés dans le réglage « mail.favorites »
     * ([{"account", "folder"}, ...]) et affichés dans une section « Favoris » en tête de l'arborescence. */
    static JsonArray LoadFavorites()
    {
        return ::EvolutionWin::Settings::Instance().GetArray(L"mail.favorites");
    }

    bool MainWindow::IsFavoriteFolder(hstring const& account, hstring const& folder)
    {
        for (auto const& value : LoadFavorites())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto entry = value.GetObject();
            if (entry.GetNamedString(L"account", L"") == account && entry.GetNamedString(L"folder", L"") == folder)
                return true;
        }
        return false;
    }

    /**
     * @brief Ajoute ou retire un dossier des favoris, puis reconstruit la section « Favoris ».
     * @param account Compte du dossier.
     * @param folder Chemin complet du dossier.
     * @param favorite true pour l'ajouter, false pour le retirer.
     */
    void MainWindow::SetFavoriteFolder(hstring const& account, hstring const& folder, bool favorite)
    {
        JsonArray updated;
        for (auto const& value : LoadFavorites())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto entry = value.GetObject();
            if (entry.GetNamedString(L"account", L"") == account && entry.GetNamedString(L"folder", L"") == folder)
                continue;
            updated.Append(value);
        }
        if (favorite)
        {
            JsonObject entry;
            entry.SetNamedValue(L"account", JsonValue::CreateStringValue(account));
            entry.SetNamedValue(L"folder", JsonValue::CreateStringValue(folder));
            updated.Append(entry);
        }
        ::EvolutionWin::Settings::Instance().Set(L"mail.favorites", updated);
        RefreshFavoritesNode();
    }

    /**
     * @brief Déplace un favori d'un rang vers le haut (delta < 0) ou le bas (delta > 0).
     */
    void MainWindow::MoveFavoriteFolder(hstring const& account, hstring const& folder, int delta)
    {
        std::vector<IJsonValue> entries;
        for (auto const& value : LoadFavorites())
            entries.push_back(value);

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].ValueType() != JsonValueType::Object)
                continue;
            auto entry = entries[i].GetObject();
            if (entry.GetNamedString(L"account", L"") != account || entry.GetNamedString(L"folder", L"") != folder)
                continue;
            long long target = static_cast<long long>(i) + delta;
            if (target < 0 || target >= static_cast<long long>(entries.size()))
                return;
            std::swap(entries[i], entries[static_cast<size_t>(target)]);
            break;
        }

        JsonArray updated;
        for (auto const& value : entries)
            updated.Append(value);
        ::EvolutionWin::Settings::Instance().Set(L"mail.favorites", updated);
        RefreshFavoritesNode();
    }

    /**
     * @brief Reconstruit la section « Favoris » (en tête de l'arborescence) d'après les réglages : un nœud par
     *        dossier connu, qui se comporte comme le dossier réel (même compte et même dossier).
     *        Rien n'est affiché sans favori.
     */
    void MainWindow::RefreshFavoritesNode()
    {
        auto roots = FolderTree().RootNodes();
        if (m_favorites_root)
        {
            uint32_t index = 0;
            if (roots.IndexOf(m_favorites_root, index))
                roots.RemoveAt(index);
            m_favorites_root = nullptr;
        }
        m_favorite_nodes.clear();

        std::vector<TreeViewNode> children;
        for (auto const& value : LoadFavorites())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto entry = value.GetObject();
            hstring account = entry.GetNamedString(L"account", L"");
            hstring folder = entry.GetNamedString(L"folder", L"");
            auto real = m_nodes.find(NodeKey(account, folder));
            if (real == m_nodes.end())
                continue;
            auto real_item = real->second.Content().try_as<EvolutionWin::FolderItem>();
            if (!real_item)
                continue;

            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(real_item.Name(), real_item.Glyph(), real_item.Unread(), account, folder, false));
            children.push_back(node);
            m_favorite_nodes.emplace(NodeKey(account, folder), node);
        }
        if (children.empty())
            return;

        TreeViewNode root;
        root.Content(EvolutionWin::FolderItem(Tr(L"Favoris", L"Favorites"), L"\xE734", 0, L"favorites", L"", true));
        for (auto const& child : children)
            root.Children().Append(child);
        root.IsExpanded(true);
        roots.InsertAt(0, root);
        m_favorites_root = root;
    }

    /**
     * @brief Met à jour le nombre de messages non lus d'un dossier, dans l'arborescence et dans ses copies « Favoris ».
     */
    void MainWindow::SetFolderUnread(hstring const& account, hstring const& folder, int32_t unread)
    {
        std::wstring key = NodeKey(account, folder);
        if (auto node = m_nodes.find(key); node != m_nodes.end())
            if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                item.Unread(unread);
        auto range = m_favorite_nodes.equal_range(key);
        for (auto it = range.first; it != range.second; ++it)
            if (auto item = it->second.Content().try_as<EvolutionWin::FolderItem>())
                item.Unread(unread);
    }

    bool MainWindow::IsUnifiedView() const
    {
        return ::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ m_account });
    }

    /* Ajoute en tête de l'arborescence le groupe « Tous les comptes » et ses
     * vues unifiées. Le groupe n'est proposé que s'il y a au moins deux comptes
     * distants activés ; le dossier local et les comptes désactivés en sont exclus. */
    void MainWindow::AddUnifiedViewsNodes()
    {
        namespace Unified = ::EvolutionWin::UnifiedViews;

        std::vector<Unified::Account> accounts;
        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            accounts.push_back({ std::wstring{ account.GetNamedString(L"uid", L"") },
                account.GetNamedBoolean(L"enabled", true), account.GetNamedBoolean(L"local", false) });
        }

        auto included = Unified::IncludedAccounts(accounts);
        m_unified_accounts = hstring{ Unified::ToCsv(included) };
        if (!Unified::ShouldOffer(included))
            return;

        TreeViewNode root;
        root.Content(EvolutionWin::FolderItem(Tr(L"Tous les comptes", L"All accounts"), L"\xE716", 0,
            Unified::kAccountId, L"", true));
        root.IsExpanded(true);
        FolderTree().RootNodes().Append(root);
        m_nodes[NodeKey(Unified::kAccountId, L"")] = root;

        /* titre traduisible de chaque vue unifiée (équivalent localisé de Unified::Title) */
        auto unified_title = [](std::wstring_view role) -> hstring
        {
            if (role == L"inbox")
                return Tr(L"Boîte de réception unifiée", L"Unified Inbox");
            if (role == L"sent")
                return Tr(L"Tous les éléments envoyés", L"All Sent Items");
            if (role == L"drafts")
                return Tr(L"Tous les brouillons", L"All Drafts");
            if (role == L"trash")
                return Tr(L"Tous les éléments supprimés", L"All Deleted Items");
            return hstring{};
        };

        for (auto const& role : Unified::Roles())
        {
            hstring role_name{ role };
            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(unified_title(role), FolderGlyph(role_name), 0,
                Unified::kAccountId, role_name, false));
            root.Children().Append(node);
            m_nodes[NodeKey(Unified::kAccountId, role_name)] = node;
        }
    }

    /* Met à jour le nombre de non lus de la Boîte de réception unifiée : somme
     * des non lus des boîtes de réception des comptes inclus. */
    void MainWindow::UpdateUnifiedCounts()
    {
        namespace Unified = ::EvolutionWin::UnifiedViews;

        auto unified = m_nodes.find(NodeKey(Unified::kAccountId, L"inbox"));
        if (unified == m_nodes.end())
            return;

        std::wstring included = L"," + std::wstring{ m_unified_accounts } + L",";
        hstring inbox_glyph = FolderGlyph(L"inbox");
        int unread = 0;

        for (auto const& [key, node] : m_nodes)
        {
            auto item = node.Content().try_as<EvolutionWin::FolderItem>();
            if (!item || item.IsAccount() || item.Glyph() != inbox_glyph || Unified::IsUnifiedAccount(std::wstring_view{ item.Account() }))
                continue;
            if (included.find(L"," + std::wstring{ item.Account() } + L",") != std::wstring::npos)
                unread += item.Unread();
        }

        if (auto item = unified->second.Content().try_as<EvolutionWin::FolderItem>())
            item.Unread(Unified::DisplayedUnread(L"inbox", unread));
    }

    /* Indique si un dossier réel alimente la vue unifiée affichée (même rôle,
     * compte inclus) : ses changements doivent recharger la vue. */
    bool MainWindow::UnifiedFolderMatches(hstring const& account, hstring const& folder) const
    {
        if (!IsUnifiedView())
            return false;

        auto node = m_nodes.find(NodeKey(account, folder));
        if (node == m_nodes.end())
            return false;

        auto item = node->second.Content().try_as<EvolutionWin::FolderItem>();
        if (!item || item.Glyph() != FolderGlyph(m_folder))
            return false;

        std::wstring included = L"," + std::wstring{ m_unified_accounts } + L",";
        return included.find(L"," + std::wstring{ account } + L",") != std::wstring::npos;
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

        /* Mémorise le dernier dossier actif pour la restauration rapide au prochain démarrage
           (une vue unifiée n'est pas mémorisée : elle dépend des comptes présents) */
        if (!IsUnifiedView())
        {
            ::EvolutionWin::Settings::Instance().SetString(L"session.account", m_account);
            ::EvolutionWin::Settings::Instance().SetString(L"session.folder", m_folder);
        }

        FolderTitle().Text(item.Name());
        LoadFolderSort();
        LoadFolderConversations();
        UpdateRibbon();
        SearchBox().Text(L"");
        UpdateOutOfOfficeBanner();

        LoadMessages(false);
    }

    /* Restaure automatiquement le dernier dossier ouvert lors de la session précédente
     * (ou la boîte de réception du premier compte), sélectionne son nœud dans l'arborescence
     * et affiche ses messages sans nécessiter de clic utilisateur. */
    void MainWindow::RestoreSessionFolder()
    {
        if (!m_account.empty() && !m_folder.empty())
            return;

        std::wstring saved_acc{ ::EvolutionWin::Settings::Instance().GetString(L"session.account", L"") };
        std::wstring saved_fld{ ::EvolutionWin::Settings::Instance().GetString(L"session.folder", L"") };

        TreeViewNode target_node{ nullptr };
        EvolutionWin::FolderItem target_item{ nullptr };

        /* 1. Essai avec le dossier mémorisé lors de la session précédente */
        if (!saved_acc.empty() && !saved_fld.empty())
        {
            auto it = m_nodes.find(NodeKey(hstring{ saved_acc }, hstring{ saved_fld }));
            if (it != m_nodes.end())
            {
                target_node = it->second;
                target_item = target_node.Content().try_as<EvolutionWin::FolderItem>();
            }
        }

        /* 2. Repli : première boîte de réception disponible */
        if (!target_item)
        {
            hstring inbox_glyph = FolderGlyph(L"inbox");
            for (auto const& [key, node] : m_nodes)
            {
                if (auto item = node.Content().try_as<EvolutionWin::FolderItem>(); item && !item.IsAccount() && item.Glyph() == inbox_glyph
                    && !::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ item.Account() }))
                {
                    target_node = node;
                    target_item = item;
                    break;
                }
            }
        }

        if (target_node && target_item && !target_item.Folder().empty())
        {
            m_account = target_item.Account();
            m_folder = target_item.Folder();
            ++m_search_request;

            FolderTree().SelectedNode(target_node);
            FolderTitle().Text(target_item.Name());
            LoadFolderSort();
        LoadFolderConversations();
            UpdateRibbon();
            SearchBox().Text(L"");
            UpdateOutOfOfficeBanner();

            LoadMessages(false);
        }
    }

    /* Élément de l'arborescence du dossier affiché (nullptr sans dossier). */
    EvolutionWin::FolderItem MainWindow::CurrentFolderItem()
    {
        /* une vue unifiée n'est pas un dossier : aucune commande de dossier */
        if (m_account.empty() || m_folder.empty() || IsUnifiedView())
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
        else if (!m_account.empty() && !IsUnifiedView())
            CreateFolderAsync(m_account, L"", L"");
    }

    /* onglet Dossier : nouveau dossier au premier niveau du compte affiché */
    void MainWindow::OnRibbonNewRootFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_account.empty() && !IsUnifiedView())
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

    /**
     * @brief Purge l'ensemble des éléments supprimés (dossiers Corbeille) de tous les comptes configurés.
     *        Utilisé lors de la fermeture de l'application si la préférence 'mail.empty-trash-on-exit' est active.
     */
    void MainWindow::EmptyAllTrashFolders()
    {
        if (!m_core)
            return;

        // Purge les dossiers détectés comme Corbeille dans l'arborescence
        std::set<std::pair<std::wstring, std::wstring>> emptied;

        for (auto const& [account_str, folders_array] : m_account_folders)
        {
            hstring account{ account_str };
            for (auto const& item_val : folders_array)
            {
                auto obj = item_val.GetObject();
                hstring type = obj.GetNamedString(L"type", L"");
                hstring full_name = obj.GetNamedString(L"full_name", L"");
                if (type == L"trash" && !full_name.empty())
                {
                    emptied.insert({ account_str, std::wstring{ full_name } });
                    m_core->FolderAction(account, full_name, L"empty", [](hstring const&, hstring const&) {});
                }
            }
        }

        // Parcourt également les nœuds enregistrés pour les dossiers spéciaux personnalisés
        for (auto const& [key, node] : m_nodes)
        {
            if (auto folder_item = node.Content().try_as<EvolutionWin::FolderItem>())
            {
                if (folder_item.Glyph() == FolderGlyph(L"trash") && !folder_item.Folder().empty()
                    && !::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ folder_item.Account() }))
                {
                    std::pair<std::wstring, std::wstring> p{ std::wstring{ folder_item.Account() }, std::wstring{ folder_item.Folder() } };
                    if (emptied.find(p) == emptied.end())
                    {
                        emptied.insert(p);
                        m_core->FolderAction(folder_item.Account(), folder_item.Folder(), L"empty", [](hstring const&, hstring const&) {});
                    }
                }
            }
        }
    }

    /**
     * @brief Met à jour le bandeau d'alerte des réponses automatiques d'absence du bureau pour le compte actif.
     */
    void MainWindow::UpdateOutOfOfficeBanner()
    {
        if (m_account.empty() || m_account == L"local" || IsUnifiedView())
        {
            ClearBanner(L"oof:banner");
            return;
        }

        std::wstring banner_id = L"oof:" + std::wstring{ m_account };
        hstring oof_key = L"account." + m_account + L".out-of-office-enabled";
        bool oof_enabled = ::EvolutionWin::Settings::Instance().GetBoolean(oof_key, false);

        if (!oof_enabled)
        {
            ClearBanner(banner_id);
            return;
        }

        // Vérification de la date de fin (expiration éventuelle)
        hstring until_key = L"account." + m_account + L".out-of-office-until";
        std::wstring until_str{ ::EvolutionWin::Settings::Instance().GetString(until_key, L"") };
        int64_t until_unix = 0;
        if (!until_str.empty())
        {
            try { until_unix = std::stoll(until_str); } catch (...) {}
        }

        int64_t now_unix = static_cast<int64_t>(std::time(nullptr));
        bool expired = (until_unix > 0 && now_unix > until_unix);

        hstring acc_name = m_account;
        if (m_accounts)
        {
            for (auto const& val : m_accounts)
            {
                auto acc_obj = val.GetObject();
                if (acc_obj.GetNamedString(L"uid", L"") == m_account)
                {
                    acc_name = acc_obj.GetNamedString(L"name", m_account);
                    break;
                }
            }
        }

        if (expired)
        {
            ShowBanner(
                banner_id,
                Tr(L"Réponses automatiques expirées"),
                Fmt(Tr(L"La période d'absence programmée pour le compte « {0} » est terminée. Souhaitez-vous la désactiver ?"), { acc_name }),
                InfoBarSeverity::Warning,
                Tr(L"Désactiver"),
                [this, banner_id, account_uid = m_account]()
                {
                    /* désactivation sur le serveur (Microsoft 365 / Gmail), puis mise à jour du bandeau */
                    ClearBanner(banner_id);
                    DisableAutoReply(account_uid);
                }
            );
        }
        else
        {
            ShowBanner(
                banner_id,
                Tr(L"Réponses automatiques activées"),
                Fmt(Tr(L"Les réponses automatiques d'absence du bureau sont actives pour le compte « {0} »."), { acc_name }),
                InfoBarSeverity::Warning, /* fond ambre : se détache du reste de l'interface */
                Tr(L"Désactiver"),
                [this, banner_id, account_uid = m_account]()
                {
                    /* désactivation sur le serveur (Microsoft 365 / Gmail), puis mise à jour du bandeau */
                    ClearBanner(banner_id);
                    DisableAutoReply(account_uid);
                }
            );
        }
    }

    /**
     * @brief Retrouve le dossier de l'arborescence situé sous le pointeur d'un glisser-déposer, par test
     *        de position (plus fiable que la source de l'événement, qui peut être l'arbre lui-même).
     * @param tree Arborescence des dossiers.
     * @param args Arguments de l'événement de glisser-déposer.
     * @param item Reçoit le dossier correspondant (nul si aucun).
     * @return Le TreeViewItem concerné, ou nul.
     */
    static TreeViewItem FolderContainerAtPointer(
        TreeView const& tree, DragEventArgs const& args, EvolutionWin::FolderItem& item)
    {
        item = nullptr;
        try
        {
            auto hits = VisualTreeHelper::FindElementsInHostCoordinates(args.GetPosition(nullptr), tree);
            for (auto const& hit : hits)
            {
                DependencyObject element = hit;
                while (element)
                {
                    if (auto container = element.try_as<TreeViewItem>())
                    {
                        auto node = tree.NodeFromContainer(container);
                        if (node)
                        {
                            item = node.Content().try_as<EvolutionWin::FolderItem>();
                            if (item)
                                return container;
                        }
                    }
                    element = VisualTreeHelper::GetParent(element);
                }
            }
        }
        catch (...)
        {
        }
        return nullptr;
    }

    /**
     * @brief Retrouve le conteneur et l'élément de dossier de l'arborescence situés sous une source de glisser-déposer.
     * @param source Élément visuel sous le pointeur (OriginalSource de l'événement).
     * @param item Reçoit le dossier correspondant (nul si aucun).
     * @return Le TreeViewItem concerné, ou nul.
     */
    TreeViewItem MainWindow::FolderContainerAt(IInspectable const& source, EvolutionWin::FolderItem& item)
    {
        item = nullptr;
        DependencyObject element = source.try_as<DependencyObject>();
        while (element)
        {
            if (auto container = element.try_as<TreeViewItem>())
            {
                /* le gabarit contient lui-même un TreeViewItem : seul le conteneur réel a un nœud */
                auto node = FolderTree().NodeFromContainer(container);
                if (node)
                {
                    item = node.Content().try_as<EvolutionWin::FolderItem>();
                    return container;
                }
            }
            element = VisualTreeHelper::GetParent(element);
        }
        return nullptr;
    }

    /**
     * @brief Surligne (couleur d'accentuation) le dossier cible d'un glisser-déposer de messages ; nul efface le surlignage.
     * @param container Conteneur à surligner, ou nul.
     */
    void MainWindow::SetFolderDropHighlight(TreeViewItem const& container)
    {
        /* le fond du TreeViewItem est repris par ses états visuels (survol, glisser) : il clignotait.
           On colore donc la grille du gabarit, qui appartient à l'application (premier Grid à 3 colonnes). */
        Grid target{ nullptr };
        if (container)
        {
            std::function<Grid(DependencyObject const&)> find = [&find](DependencyObject const& parent) -> Grid
            {
                int32_t count = VisualTreeHelper::GetChildrenCount(parent);
                for (int32_t i = 0; i < count; ++i)
                {
                    auto child = VisualTreeHelper::GetChild(parent, i);
                    if (auto grid = child.try_as<Grid>(); grid && grid.ColumnDefinitions().Size() == 3)
                        return grid;
                    if (auto found = find(child))
                        return found;
                }
                return nullptr;
            };
            target = find(container);
        }

        if (m_drop_highlight_grid == target)
            return;
        if (m_drop_highlight_grid)
            m_drop_highlight_grid.ClearValue(Controls::Panel::BackgroundProperty());
        m_drop_highlight_grid = target;
        if (m_drop_highlight_grid)
            m_drop_highlight_grid.Background(Application::Current().Resources()
                .Lookup(box_value(L"AccentFillColorSecondaryBrush")).as<Media::Brush>());
    }

    /**
     * @brief Indique si un dossier peut recevoir des messages déplacés (dossier réel, sélectionnable, hors vues unifiées et recherches).
     * @param item Dossier cible.
     * @return Vrai si le dépôt est autorisé.
     */
    static bool IsFolderDropTarget(EvolutionWin::FolderItem const& item)
    {
        return item && !item.IsAccount() && !item.Folder().empty() &&
            !::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ item.Account() });
    }

    /**
     * @brief Survol de l'arborescence pendant le glisser de messages : surligne le dossier cible et masque l'aperçu du glisser.
     */
    void MainWindow::OnFolderTreeDragOver(IInspectable const&, DragEventArgs const& args)
    {
        EvolutionWin::FolderItem item{ nullptr };
        auto container = FolderContainerAtPointer(FolderTree(), args, item);
        if (!container)
            container = FolderContainerAt(args.OriginalSource(), item);

        if (m_drag_move_uids.empty() || !container || !IsFolderDropTarget(item))
        {
            SetFolderDropHighlight(nullptr);
            args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
            return;
        }

        SetFolderDropHighlight(container);
        args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move);
        auto ui = args.DragUIOverride();
        ui.IsContentVisible(false);
        ui.IsGlyphVisible(false);
        ui.IsCaptionVisible(false);
        args.Handled(true);
    }

    /**
     * @brief Survol de la fenêtre pendant le glisser de messages : ne garde que le pointeur
     *        (ni aperçu du texte du message, ni pictogramme, ni légende).
     */
    void MainWindow::OnWindowDragOver(IInspectable const&, DragEventArgs const& args)
    {
        if (m_drag_move_uids.empty())
            return;
        auto ui = args.DragUIOverride();
        ui.IsContentVisible(false);
        ui.IsGlyphVisible(false);
        ui.IsCaptionVisible(false);
    }

    /**
     * @brief Le pointeur quitte l'arborescence : retire le surlignage.
     */
    void MainWindow::OnFolderTreeDragLeave(IInspectable const&, DragEventArgs const& args)
    {
        /* DragLeave remonte aussi quand le pointeur passe d'un élément enfant à un autre :
           le surlignage ne s'efface que si le pointeur est réellement sorti de l'arborescence */
        try
        {
            auto position = args.GetPosition(FolderTree());
            if (position.X >= 0 && position.Y >= 0 &&
                position.X < FolderTree().ActualWidth() && position.Y < FolderTree().ActualHeight())
                return;
        }
        catch (...)
        {
        }
        SetFolderDropHighlight(nullptr);
    }

    /**
     * @brief Dépôt de messages sur un dossier : les déplace vers ce dossier.
     */
    void MainWindow::OnFolderTreeDrop(IInspectable const&, DragEventArgs const& args)
    {
        EvolutionWin::FolderItem item{ nullptr };
        auto container = FolderContainerAtPointer(FolderTree(), args, item);
        if (!container)
            container = FolderContainerAt(args.OriginalSource(), item);
        SetFolderDropHighlight(nullptr);

        if (m_drag_move_uids.empty() || !container || !IsFolderDropTarget(item))
        {
            wchar_t info[160]{};
            swprintf_s(info, L"dépôt ignoré : messages=%zu, dossier trouvé=%d, valide=%d",
                m_drag_move_uids.size(), container ? 1 : 0, IsFolderDropTarget(item) ? 1 : 0);
            ::EvolutionWin::LogError(L"glisser-déposer", 0, info);
            return;
        }

        auto uids = std::move(m_drag_move_uids);
        m_drag_move_uids.clear();
        args.Handled(true);
        MoveMessagesByUids(uids, item.Account(), item.Folder());
    }
}

