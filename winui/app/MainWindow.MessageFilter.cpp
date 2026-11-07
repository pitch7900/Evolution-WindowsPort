#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "CategoryStore.h"

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

/* Liste des messages : filtres rapides, recherche et tri. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Filtre instantanément en mémoire les messages chargés du dossier selon
     * l'objet et l'expéditeur.
     * Paramètres :
     *   query : texte saisi dans la zone de recherche */
    void MainWindow::FilterLocalMessages(hstring const& query)
    {
        if (!m_all_messages)
            return;

        if (query.empty())
        {
            ShowMessages(m_all_messages);
            return;
        }

        JsonArray filtered;
        for (auto const& value : m_all_messages)
        {
            auto msg = value.GetObject();
            std::vector<std::wstring> labels;
            if (msg.HasKey(L"labels"))
            {
                auto arr = msg.GetNamedArray(L"labels");
                for (auto const& item : arr)
                    labels.push_back(std::wstring{ item.GetString() });
            }

            if (MatchesMessageSearch(query, msg.GetNamedString(L"subject", L""), msg.GetNamedString(L"from", L""), labels))
                filtered.Append(msg);
        }

        ShowMessages(filtered);
    }

    /* Filtres rapides au-dessus de la liste (Tous / Non lus / Lus, indicateur,
     * pièces jointes, cette semaine), appliqués par ShowMessages à toute liste
     * affichée, donc combinés avec la recherche.
     * Paramètres :
     *   message : résumé JSON d'un message
     * Retourne : true si le message passe tous les filtres actifs. */
    bool MainWindow::MatchesQuickFilters(JsonObject const& message)
    {
        bool seen = message.GetNamedBoolean(L"seen", true);

        if (auto item = ReadFilterBar().SelectedItem())
        {
            hstring read_filter = unbox_value_or<hstring>(item.Tag(), L"all");
            if ((read_filter == L"unread" && seen) || (read_filter == L"read" && !seen))
                return false;
        }

        /* Boîte de réception prioritaire : onglet « Prioritaire » ou « Autres » (sans effet pendant une recherche) */
        if (m_focused_inbox_active && SearchBox().Text().empty())
        {
            auto tab = FocusedBar().SelectedItem();
            bool other_tab = tab && unbox_value_or<hstring>(tab.Tag(), L"focused") == L"other";
            bool is_other = m_inbox_other_ids.count(std::wstring{ message.GetNamedString(L"uid", L"") }) != 0;
            if (other_tab != is_other)
                return false;
        }

        if (!::EvolutionWin::Categories::MatchesCategoryFilter(::EvolutionWin::CategoryStore::CategoriesOf(message), m_mail_category_filter))
            return false;

        if (FlaggedFilterButton().IsChecked().GetBoolean() && !message.GetNamedBoolean(L"flagged", false))
            return false;
        if (AttachmentFilterButton().IsChecked().GetBoolean() && !message.GetNamedBoolean(L"attachments", false))
            return false;

        if (ThisWeekFilterButton().IsChecked().GetBoolean())
        {
            /* depuis lundi 0 h (heure locale) */
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            local.tm_mday -= (local.tm_wday + 6) % 7;
            local.tm_hour = local.tm_min = local.tm_sec = 0;
            local.tm_isdst = -1;
            if (message.GetNamedNumber(L"date", 0) < static_cast<double>(std::mktime(&local)))
                return false;
        }

        return true;
    }

    /* Réaffiche la liste courante avec les filtres rapides et la recherche locale. */
    void MainWindow::ReapplyMessageFilters()
    {
        if (!m_all_messages)
            return;
        if (!SearchBox().Text().empty())
            FilterLocalMessages(SearchBox().Text());
        else
            ShowMessages(m_all_messages);
    }

    void MainWindow::OnQuickFilterChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&)
    {
        ReapplyMessageFilters();
    }

    void MainWindow::OnQuickFilterToggled(IInspectable const&, RoutedEventArgs const&)
    {
        ReapplyMessageFilters();
    }

    void MainWindow::OnFocusedBarChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&)
    {
        ReapplyMessageFilters();
    }

    /* Boîte de réception prioritaire (Focused Inbox) : affiche les onglets « Prioritaire » / « Autres » dans la
     * Boîte de réception d'un compte Microsoft 365 (réglage « account.<uid>.focused-inbox », activé par défaut)
     * et lit les messages classés « Autres » par Exchange Online. */
    void MainWindow::UpdateFocusedInbox()
    {
        bool active = false;
        if (!IsUnifiedView() && !m_account.empty() && !m_folder.empty())
        {
            bool m365 = false, inbox = false;
            for (auto const& value : m_accounts ? m_accounts : JsonArray{})
            {
                auto account = value.GetObject();
                if (account.GetNamedString(L"uid", L"") == m_account && account.GetNamedString(L"protocol", L"") == L"microsoft365")
                    m365 = true;
            }
            auto folders = m_account_folders.find(std::wstring{ m_account });
            if (folders != m_account_folders.end() && folders->second)
                if (auto folder = FindFolderJson(folders->second, m_folder))
                    inbox = folder.GetNamedString(L"type", L"") == L"inbox";
            active = m365 && inbox && ::EvolutionWin::Settings::Instance().GetBoolean(L"account." + m_account + L".focused-inbox", true);
        }

        FocusedBar().Visibility(active ? Visibility::Visible : Visibility::Collapsed);
        if (!active)
        {
            m_focused_inbox_active = false;
            m_inbox_other_ids.clear();
            return;
        }

        if (!m_focused_inbox_active)
            m_inbox_other_ids.clear();
        m_focused_inbox_active = true;

        hstring account = m_account;
        m_core->ListInboxClassification(account, [this, account](hstring const& json, hstring const& error)
        {
            if (account != m_account || !m_focused_inbox_active || !error.empty())
                return;
            JsonObject result;
            if (!JsonObject::TryParse(json, result))
                return;
            std::set<std::wstring> others;
            for (auto const& id : result.GetNamedArray(L"other", JsonArray{}))
                others.insert(std::wstring{ id.GetString() });
            m_inbox_other_ids = std::move(others);
            ReapplyMessageFilters();
        });
    }

    /* Compteurs de messages non lus des onglets « Prioritaire (n) » et « Autres (n) », d'après la liste
     * complète du dossier (m_all_messages) et le classement connu. */
    void MainWindow::UpdateFocusedBarCounts()
    {
        if (!m_focused_inbox_active)
            return;

        int focused = 0, other = 0;
        for (auto const& value : m_all_messages ? m_all_messages : JsonArray{})
        {
            auto message = value.GetObject();
            if (message.GetNamedBoolean(L"seen", true))
                continue;
            if (m_inbox_other_ids.count(std::wstring{ message.GetNamedString(L"uid", L"") }))
                ++other;
            else
                ++focused;
        }

        auto items = FocusedBar().Items();
        if (items.Size() >= 2)
        {
            items.GetAt(0).Text(focused > 0 ? Fmt(Tr(L"Prioritaire ({0})", L"Focused ({0})"), { to_hstring(focused) }) : Tr(L"Prioritaire", L"Focused"));
            items.GetAt(1).Text(other > 0 ? Fmt(Tr(L"Autres ({0})", L"Other ({0})"), { to_hstring(other) }) : Tr(L"Autres", L"Other"));
        }
    }

    /* Classe un message dans « Prioritaire » ou « Autres » (menu contextuel), avec ou sans règle pour l'expéditeur.
     * Paramètres :
     *   uid             : identifiant du message
     *   classification  : « focused » ou « other »
     *   remember_sender : mémoriser la règle pour l'expéditeur (« toujours déplacer… ») */
    void MainWindow::ClassifyMessage(hstring const& uid, hstring const& classification, bool remember_sender)
    {
        hstring sender;
        if (remember_sender && m_all_messages)
        {
            for (auto const& value : m_all_messages)
            {
                auto message = value.GetObject();
                if (message.GetNamedString(L"uid", L"") != uid)
                    continue;
                std::wstring from{ message.GetNamedString(L"from", L"") };
                size_t open = from.find(L'<'), close = from.find(L'>');
                sender = hstring{ open != std::wstring::npos && close != std::wstring::npos && close > open
                    ? from.substr(open + 1, close - open - 1) : from };
            }
        }

        bool to_other = classification == L"other";
        m_core->SetMessageClassification(m_account, uid, classification, sender,
            [this, uid, to_other, remember_sender](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Tr(L"Classement du message : ", L"Message classification: ") + error, InfoBarSeverity::Error);
                return;
            }
            if (to_other)
                m_inbox_other_ids.insert(std::wstring{ uid });
            else
                m_inbox_other_ids.erase(std::wstring{ uid });
            ReapplyMessageFilters();

            JsonObject outcome;
            hstring override_error = JsonObject::TryParse(json, outcome) ? outcome.GetNamedString(L"override_error", L"") : hstring{};
            hstring text = to_other ? Tr(L"Message déplacé vers « Autres ».", L"Message moved to \"Other\".")
                                    : Tr(L"Message déplacé vers « Prioritaire ».", L"Message moved to \"Focused\".");
            if (remember_sender && !override_error.empty())
                ShowStatus(Fmt(Tr(L"{0} La règle pour l'expéditeur n'a pas pu être mémorisée : {1}", L"{0} The sender rule could not be saved: {1}"), { text, override_error }),
                    InfoBarSeverity::Warning);
            else
                ShowStatus(text, InfoBarSeverity::Success);
        });
    }

    /* Renvoie l'identifiant de la portée actuellement sélectionnée
     * ("folder", "subfolders", "account", "all").
     * Retourne : hstring correspondant au tag de l'option choisie. */
    hstring MainWindow::CurrentSearchScope()
    {
        auto scope_box = SearchScopeBox();
        if (!scope_box)
            return L"folder";

        if (auto item = scope_box.SelectedItem().try_as<ComboBoxItem>())
            return unbox_value_or<hstring>(item.Tag(), L"folder");
        return L"folder";
    }

    /* Met à jour le texte d'aide du champ de recherche en fonction de la portée active. */
    void MainWindow::UpdateSearchPlaceholder()
    {
        auto box = SearchBox();
        if (!box)
            return;

        hstring scope = CurrentSearchScope();
        if (scope == L"subfolders")
            box.PlaceholderText(Tr(L"Rechercher dans le dossier et sous-dossiers (Ctrl+E)", L"Search folder and subfolders (Ctrl+E)", L"SearchScope_Subfolders"));
        else if (scope == L"account")
            box.PlaceholderText(Tr(L"Rechercher dans tout le compte (Ctrl+E)", L"Search entire account (Ctrl+E)", L"SearchScope_Account"));
        else if (scope == L"all")
            box.PlaceholderText(Tr(L"Rechercher dans tous les comptes (Ctrl+E)", L"Search all accounts (Ctrl+E)", L"SearchScope_AllAccounts"));
        else
            box.PlaceholderText(Tr(L"Rechercher dans le dossier (Ctrl+E)", L"Search folder (Ctrl+E)", L"SearchScope_Folder"));
    }

    /* Changement de portée de la recherche : adapte le texte d'aide et relance
     * automatiquement la recherche si des mots-clés sont déjà saisis.
     * Paramètres :
     *   sender : contrôle ComboBox émetteur
     *   args   : arguments de changement de sélection */
    void MainWindow::OnSearchScopeChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        UpdateSearchPlaceholder();
        auto box = SearchBox();
        if (box && !box.Text().empty())
        {
            AutoSuggestBoxQuerySubmittedEventArgs empty_args{ nullptr };
            OnSearchQuerySubmitted(box, empty_args);
        }
    }

    /* Gestionnaire de modification du texte de recherche : filtre la liste en
     * direct ou la restaure lorsque le champ est vidé. */
    void MainWindow::OnSearchTextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;

        /* recherche de la barre de titre : contacts en vue Contacts, sinon le
           dossier courant du courrier (vue Courrier affichée au besoin) */
        if (m_view_tag == L"contacts")
        {
            ContactSearchBox().Text(sender.Text());
            LoadContacts(sender.Text());
            return;
        }
        if (m_view_tag == L"tasks")
        {
            m_task_search_query = sender.Text();
            if (TaskSearchBox() && TaskSearchBox().Text() != sender.Text())
                TaskSearchBox().Text(sender.Text());
            ShowTasks();
            return;
        }
        if (m_view_tag == L"calendar")
        {
            m_calendar_search_query = sender.Text();
            if (CalendarSearchBox() && CalendarSearchBox().Text() != sender.Text())
                CalendarSearchBox().Text(sender.Text());
            FilterAndRenderCalendarEvents();
            return;
        }
        if (m_view_tag != L"mail")
            Navigation().SelectedItem(MailItem());

        ++m_search_request;

        /* Si la portée est le dossier courant, filtrage rapide en local */
        if (CurrentSearchScope() == L"folder")
        {
            FilterLocalMessages(sender.Text());
        }
        else if (sender.Text().empty())
        {
            /* Si le texte est effacé hors dossier courant, rétablir les messages du dossier */
            LoadMessages(false);
        }
    }

    /* Ctrl+E : place le curseur dans la recherche de la barre de titre. */
    void MainWindow::OnSearchAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        SearchBox().Focus(FocusState::Keyboard);
    }

    /* Échap dans la recherche : efface le texte et rétablit la liste. */
    void MainWindow::OnSearchKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() != Windows::System::VirtualKey::Escape || SearchBox().Text().empty())
            return;

        args.Handled(true);
        SearchBox().Text(L"");
        if (m_view_tag == L"contacts")
        {
            ContactSearchBox().Text(L"");
            LoadContacts(L"");
            return;
        }
        if (m_view_tag == L"tasks")
        {
            m_task_search_query = L"";
            if (TaskSearchBox())
                TaskSearchBox().Text(L"");
            ShowTasks();
            return;
        }
        if (m_view_tag == L"calendar")
        {
            m_calendar_search_query = L"";
            if (CalendarSearchBox())
                CalendarSearchBox().Text(L"");
            FilterAndRenderCalendarEvents();
            return;
        }
        ++m_search_request;
        if (CurrentSearchScope() == L"folder")
            FilterLocalMessages(L"");
        else
            LoadMessages(false);
    }

    /* Gestionnaire de validation de la recherche (touche Entrée ou icône loupe) :
     * lance une recherche complète (en-têtes et corps) selon la portée choisie
     * (dossier courant, sous-dossiers, compte courant ou tous les comptes).
     * Paramètres :
     *   sender : contrôle AutoSuggestBox émetteur
     *   args   : arguments de validation contenant la requête */
    void MainWindow::OnSearchQuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        hstring query = args ? args.QueryText() : SearchBox().Text();
        if (m_view_tag == L"contacts")
        {
            LoadContacts(query);
            return;
        }
        if (m_view_tag == L"tasks")
        {
            m_task_search_query = query;
            ShowTasks();
            return;
        }
        if (m_view_tag == L"calendar")
        {
            m_calendar_search_query = query;
            if (CalendarSearchBox() && CalendarSearchBox().Text() != query)
                CalendarSearchBox().Text(query);
            FilterAndRenderCalendarEvents();
            return;
        }

        hstring scope = CurrentSearchScope();
        /* une vue unifiée n'a ni sous-dossiers ni compte propre : la recherche élargie porte sur tous les comptes */
        if (IsUnifiedView() && scope != L"folder")
            scope = L"all";

        if (query.empty())
        {
            if (scope == L"folder")
                FilterLocalMessages(query);
            else
                LoadMessages(false);
            return;
        }

        if (scope != L"all" && m_account.empty())
            return;

        hstring account = m_account;
        hstring folder = m_folder;
        uint64_t request = ++m_search_request;

        ListProgress().IsActive(true);

        m_core->SearchMessagesScoped(account, folder, scope, query, 500,
            [this, account, folder, scope, request](hstring const& json, hstring const& error)
        {
            if (request != m_search_request)
                return;
            if (scope == L"folder" && (account != m_account || folder != m_folder))
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(Tr(L"Recherche : ") + error, InfoBarSeverity::Error);
                return;
            }

            JsonArray results;
            if (JsonArray::TryParse(json, results))
            {
                m_all_messages = results;
                ShowMessages(results);
                if (results.Size() == 0)
                    ShowStatus(Tr(L"Aucun message trouvé pour cette recherche."), InfoBarSeverity::Informational);
            }
        });
    }

    /* onglet Affichage (Courrier) : mêmes filtres que la barre au-dessus de la liste */
    void MainWindow::OnRibbonReadFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        hstring tag = unbox_value_or<hstring>(sender.as<FrameworkElement>().Tag(), L"all");
        for (auto const& item : ReadFilterBar().Items())
        {
            if (unbox_value_or<hstring>(item.Tag(), L"") == tag)
                ReadFilterBar().SelectedItem(item);
        }
    }

    /* Critères de tri de la liste des messages : clé mémorisée, libellé,
     * ordre par défaut (décroissant pour la date et la taille). */
    struct SortCriterion
    {
        wchar_t const* key;
        hstring (*label)();
        bool descending;
    };

    static SortCriterion const SortCriteria[] = {
        { L"date", []() -> hstring { return Tr(L"Date"); }, true },
        { L"from", []() -> hstring { return Tr(L"Expéditeur"); }, false },
        { L"to", []() -> hstring { return Tr(L"Destinataire"); }, false },
        { L"subject", []() -> hstring { return Tr(L"Objet"); }, false },
        { L"size", []() -> hstring { return Tr(L"Taille"); }, true },
        { L"attachments", []() -> hstring { return Tr(L"Pièces jointes"); }, true },
        { L"flagged", []() -> hstring { return Tr(L"Indicateur"); }, true },
        { L"seen", []() -> hstring { return Tr(L"Lu / non lu"); }, false },
        { L"categories", []() -> hstring { return Tr(L"Catégories"); }, false },
    };

    /* Préférence de tri du dossier affiché ("sort.<compte>/<dossier>" =
     * "<critère>:asc|desc", par défaut la date décroissante). */
    void MainWindow::LoadFolderSort()
    {
        hstring saved = ::EvolutionWin::Settings::Instance().GetString(
            L"sort." + m_account + L"/" + m_folder, L"date:desc");
        std::wstring value{ saved };
        auto colon = value.find(L':');

        m_sort_key = L"date";
        m_sort_descending = true;
        for (auto const& criterion : SortCriteria)
        {
            if (value.substr(0, colon) == criterion.key)
            {
                m_sort_key = criterion.key;
                m_sort_descending = colon == std::wstring::npos || value.substr(colon + 1) != L"asc";
            }
        }
        UpdateSortHeader();
    }

    /**
     * @brief Charge la préférence d'affichage en conversations pour le dossier actif.
     */
    void MainWindow::LoadFolderConversations()
    {
        if (m_account.empty() || m_folder.empty())
        {
            m_conversations_enabled = false;
        }
        else
        {
            m_conversations_enabled = ::EvolutionWin::Settings::Instance().GetBoolean(
                L"conversations." + m_account + L"/" + m_folder, false);
        }

        UpdateGroupHeader();
    }

    /**
     * @brief Met à jour le libellé du bouton « Grouper par » et le bouton « Conversations » du ruban.
     */
    void MainWindow::UpdateGroupHeader()
    {
        GroupButton().Content(box_value(m_conversations_enabled ? Tr(L"Conversation") : Tr(L"Aucun")));
        if (auto toggle = RibbonMailConversationToggle())
            toggle.IsChecked(m_conversations_enabled);
    }

    /**
     * @brief Menu « Grouper par » : Aucun ou Conversation (choix unique, mémorisé par dossier).
     * @param sender Menu contextuel émetteur.
     */
    void MainWindow::OnGroupMenuOpening(IInspectable const& sender, IInspectable const&)
    {
        auto menu = sender.as<MenuFlyout>();
        menu.Items().Clear();
        for (bool conversation : { false, true })
        {
            RadioMenuFlyoutItem entry;
            entry.Text(conversation ? Tr(L"Conversation") : Tr(L"Aucun"));
            entry.GroupName(L"group-key");
            entry.IsChecked(m_conversations_enabled == conversation);
            entry.Click([this, conversation](IInspectable const&, RoutedEventArgs const&)
            {
                SaveFolderConversations(conversation);
            });
            menu.Items().Append(entry);
        }

        /* regroupement par date (liste triée par date) : en-têtes Aujourd'hui, Hier, jours, semaines, mois */
        menu.Items().Append(MenuFlyoutSeparator{});
        ToggleMenuFlyoutItem by_date;
        by_date.Text(Tr(L"Regrouper par date", L"Group by date"));
        by_date.IsChecked(::EvolutionWin::Settings::Instance().GetBoolean(L"mail.group-by-date", true));
        by_date.Click([this](IInspectable const& sender, RoutedEventArgs const&)
        {
            if (auto toggle = sender.try_as<ToggleMenuFlyoutItem>())
                ::EvolutionWin::Settings::Instance().SetBoolean(L"mail.group-by-date", toggle.IsChecked());
            ReapplyMessageFilters();
        });
        menu.Items().Append(by_date);
    }

    /**
     * @brief Enregistre la préférence d'affichage en conversations pour le dossier actif.
     * @param enabled Indique si les courriels doivent être regroupés en fils de discussion.
     */
    void MainWindow::SaveFolderConversations(bool enabled)
    {
        m_conversations_enabled = enabled;
        if (!m_account.empty() && !m_folder.empty())
        {
            ::EvolutionWin::Settings::Instance().SetBoolean(
                L"conversations." + m_account + L"/" + m_folder, enabled);
        }
        UpdateGroupHeader();
        ReapplyMessageFilters();
    }

    /**
     * @brief Gestionnaire du bouton à bascule « Conversations » du ruban.
     * @param sender Bouton émetteur.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnRibbonConversationToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto toggle = RibbonMailConversationToggle())
        {
            bool is_checked = toggle.IsChecked() && toggle.IsChecked().Value();
            SaveFolderConversations(is_checked);
        }
    }

    void MainWindow::SaveFolderSort()
    {
        if (!m_account.empty() && !m_folder.empty())
            ::EvolutionWin::Settings::Instance().SetString(L"sort." + m_account + L"/" + m_folder,
                m_sort_key + (m_sort_descending ? L":desc" : L":asc"));
        UpdateSortHeader();
        ReapplyMessageFilters();
    }

    /* Ligne « Trier par » : critère et sens (flèche). */
    void MainWindow::UpdateSortHeader()
    {
        for (auto const& criterion : SortCriteria)
            if (m_sort_key == criterion.key)
                SortButton().Content(box_value(criterion.label()));
        SortDirectionIcon().Glyph(m_sort_descending ? L"\xE74B" : L"\xE74A");
        ToolTipService::SetToolTip(SortDirectionButton(), box_value(m_sort_descending
            ? Tr(L"Ordre décroissant (cliquer pour inverser)")
            : Tr(L"Ordre croissant (cliquer pour inverser)")));
    }

    /* Menu « Trier par » (ligne au-dessus de la liste et ruban Affichage) :
     * critères puis sens, à choix unique. */
    void MainWindow::OnSortMenuOpening(IInspectable const& sender, IInspectable const&)
    {
        auto menu = sender.as<MenuFlyout>();
        menu.Items().Clear();

        for (auto const& criterion : SortCriteria)
        {
            RadioMenuFlyoutItem entry;
            entry.Text(criterion.label());
            entry.GroupName(L"sort-key");
            entry.IsChecked(m_sort_key == criterion.key);
            hstring key = criterion.key;
            bool descending = criterion.descending;
            entry.Click([this, key, descending](IInspectable const&, RoutedEventArgs const&)
            {
                if (m_sort_key != key)
                {
                    m_sort_key = key;
                    m_sort_descending = descending;
                }
                SaveFolderSort();
            });
            menu.Items().Append(entry);
        }

        menu.Items().Append(MenuFlyoutSeparator{});
        for (bool descending : { false, true })
        {
            RadioMenuFlyoutItem entry;
            entry.Text(descending ? Tr(L"Décroissant") : Tr(L"Croissant"));
            entry.GroupName(L"sort-direction");
            entry.IsChecked(m_sort_descending == descending);
            entry.Click([this, descending](IInspectable const&, RoutedEventArgs const&)
            {
                m_sort_descending = descending;
                SaveFolderSort();
            });
            menu.Items().Append(entry);
        }
    }

    void MainWindow::OnSortDirectionClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_sort_descending = !m_sort_descending;
        SaveFolderSort();
    }

    /* Objet sans préfixes de réponse / transfert, pour le tri. */
    static std::wstring NormalizedSubject(hstring const& subject)
    {
        std::wstring text{ subject };
        for (bool stripped = true; stripped;)
        {
            stripped = false;
            for (wchar_t const* prefix : { L"re:", L"tr:", L"fw:", L"fwd:", L"aw:", L"wg:" })
            {
                size_t len = wcslen(prefix);
                if (text.size() >= len && _wcsnicmp(text.c_str(), prefix, len) == 0)
                {
                    text.erase(0, len);
                    while (!text.empty() && iswspace(text.front()))
                        text.erase(0, 1);
                    stripped = true;
                }
            }
        }
        return text;
    }

    /* Comparaison de textes selon les règles de la langue de l'utilisateur
     * (accents, casse ignorée) ; négatif, 0 ou positif. */
    static int CompareText(std::wstring const& a, std::wstring const& b)
    {
        int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC | SORT_DIGITSASNUMBERS,
            a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), nullptr, nullptr, 0);
        return result == 0 ? 0 : result - CSTR_EQUAL;
    }

    /* Trie les messages affichés selon le critère et le sens choisis ; à
     * égalité, du plus récent au plus ancien.
     * Paramètres :
     *   messages : résumés JSON à trier (sur place) */
    void MainWindow::SortMessages(std::vector<JsonObject>& messages)
    {
        hstring key = m_sort_key;
        bool descending = m_sort_descending;

        auto first_label = [](JsonObject const& message)
        {
            auto labels = message.GetNamedArray(L"labels", JsonArray{});
            return labels.Size() > 0 ? std::wstring{ labels.GetStringAt(0) } : std::wstring{};
        };

        auto compare = [&](JsonObject const& a, JsonObject const& b) -> int
        {
            if (key == L"from")
                return CompareText(std::wstring{ SenderName(a.GetNamedString(L"from", L"")) },
                                   std::wstring{ SenderName(b.GetNamedString(L"from", L"")) });
            if (key == L"to")
                return CompareText(std::wstring{ SenderName(a.GetNamedString(L"to", L"")) },
                                   std::wstring{ SenderName(b.GetNamedString(L"to", L"")) });
            if (key == L"subject")
                return CompareText(NormalizedSubject(a.GetNamedString(L"subject", L"")),
                                   NormalizedSubject(b.GetNamedString(L"subject", L"")));
            if (key == L"size")
            {
                double sa = a.GetNamedNumber(L"size", 0), sb = b.GetNamedNumber(L"size", 0);
                return sa < sb ? -1 : sa > sb ? 1 : 0;
            }
            if (key == L"attachments" || key == L"flagged" || key == L"seen")
                return static_cast<int>(a.GetNamedBoolean(key, false)) - static_cast<int>(b.GetNamedBoolean(key, false));
            if (key == L"categories")
            {
                /* sans catégorie : toujours à la fin */
                auto la = first_label(a), lb = first_label(b);
                if (la.empty() != lb.empty())
                    return la.empty() ? (descending ? -1 : 1) : (descending ? 1 : -1);
                return CompareText(la, lb);
            }
            double da = a.GetNamedNumber(L"date", 0), db = b.GetNamedNumber(L"date", 0);
            return da < db ? -1 : da > db ? 1 : 0;
        };

        std::stable_sort(messages.begin(), messages.end(), [&](JsonObject const& a, JsonObject const& b)
        {
            int result = compare(a, b);
            if (result != 0)
                return descending ? result > 0 : result < 0;
            /* à égalité : du plus récent au plus ancien */
            return a.GetNamedNumber(L"date", 0) > b.GetNamedNumber(L"date", 0);
        });
    }
}
