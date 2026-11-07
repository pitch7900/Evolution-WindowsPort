#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"
#include "UnifiedViews.h"

#include <algorithm>
#include <string>
#include <vector>

/* Dossiers de recherche enregistrés (comme dans Outlook) : une requête mémorisée (mots libres et opérateurs
 * is:unread, is:flagged, has:attachment, larger:<octets>, from:<texte>) et une portée (tous les comptes ou un
 * compte). Le groupe « Dossiers de recherche » de l'arborescence les liste ; un dossier s'ouvre comme une vue
 * multi-comptes : chaque message garde son compte et son dossier d'origine (voir UnifiedViews.h).
 * Réglage « mail.search-folders » : [{"id", "name", "query", "scope" : "all" | "account", "account"}]. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;

using namespace ::EvolutionWin::Helpers;
using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** @brief Dossiers de recherche mémorisés. */
        JsonArray LoadSearchFolders()
        {
            return ::EvolutionWin::Settings::Instance().GetArray(L"mail.search-folders");
        }

        /**
         * @brief Définition d'un dossier de recherche d'après son identifiant.
         * @param id Identifiant mémorisé.
         * @return Objet {"id", "name", "query", "scope", "account"} ou nullptr.
         */
        JsonObject FindSearchFolder(hstring const& id)
        {
            for (auto const& value : LoadSearchFolders())
            {
                if (value.ValueType() == JsonValueType::Object && value.GetObject().GetNamedString(L"id", L"") == id)
                    return value.GetObject();
            }
            return nullptr;
        }

        /** @brief Crée la définition d'un dossier de recherche. */
        JsonObject MakeSearchFolder(hstring const& id, hstring const& name, hstring const& query, hstring const& scope, hstring const& account)
        {
            JsonObject folder;
            folder.SetNamedValue(L"id", JsonValue::CreateStringValue(id));
            folder.SetNamedValue(L"name", JsonValue::CreateStringValue(name));
            folder.SetNamedValue(L"query", JsonValue::CreateStringValue(query));
            folder.SetNamedValue(L"scope", JsonValue::CreateStringValue(scope));
            folder.SetNamedValue(L"account", JsonValue::CreateStringValue(account));
            return folder;
        }
    }

    /**
     * @brief Demande au moteur les messages d'une vue multi-comptes : une vue unifiée (messages d'un rôle de
     *        dossier de tous les comptes) ou un dossier de recherche (requête et portée mémorisées).
     * @param role Rôle du dossier unifié, ou identifiant du dossier de recherche.
     * @param handler Rappel recevant le tableau JSON des messages.
     */
    void MainWindow::RequestUnifiedMessages(hstring const& role, std::function<void(hstring const&, hstring const&)> handler)
    {
        if (m_account == ::EvolutionWin::UnifiedViews::kSearchAccountId)
        {
            auto folder = FindSearchFolder(role);
            if (!folder)
            {
                handler(L"[]", L"");
                return;
            }
            hstring scope = folder.GetNamedString(L"scope", L"all");
            m_core->SearchMessagesScoped(scope == L"account" ? folder.GetNamedString(L"account", L"") : hstring{}, L"", scope,
                folder.GetNamedString(L"query", L""), ::EvolutionWin::UnifiedViews::kMaxMessages, std::move(handler));
            return;
        }
        m_core->ListUnified(role, m_unified_accounts, ::EvolutionWin::UnifiedViews::kMaxMessages, std::move(handler));
    }

    /**
     * @brief (Re)construit le groupe « Dossiers de recherche » en bas de l'arborescence.
     */
    void MainWindow::RefreshSearchFoldersNode()
    {
        auto roots = FolderTree().RootNodes();
        if (m_search_root)
        {
            uint32_t index = 0;
            if (roots.IndexOf(m_search_root, index))
                roots.RemoveAt(index);
            m_search_root = nullptr;
        }

        TreeViewNode root;
        root.Content(EvolutionWin::FolderItem(Tr(L"Dossiers de recherche", L"Search folders"), L"\xE721", 0,
            ::EvolutionWin::UnifiedViews::kSearchAccountId, L"", true));
        for (auto const& value : LoadSearchFolders())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto folder = value.GetObject();
            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(folder.GetNamedString(L"name", L""), L"\xE721", 0,
                ::EvolutionWin::UnifiedViews::kSearchAccountId, folder.GetNamedString(L"id", L""), false));
            root.Children().Append(node);
        }
        root.IsExpanded(root.Children().Size() > 0);
        roots.Append(root);
        m_search_root = root;
    }

    /**
     * @brief Menu contextuel du groupe « Dossiers de recherche » (nouveau dossier, modèles) ou d'un dossier de
     *        recherche (modifier, supprimer).
     * @param menu Menu à remplir.
     * @param item Élément de l'arborescence sous le pointeur.
     */
    void MainWindow::BuildSearchFolderMenu(MenuFlyout const& menu, EvolutionWin::FolderItem const& item)
    {
        auto add_item = [&menu](hstring const& text, wchar_t const* glyph, std::function<void()> action)
        {
            MenuFlyoutItem entry;
            entry.Text(text);
            FontIcon icon;
            icon.Glyph(glyph);
            entry.Icon(icon);
            entry.Click([action](IInspectable const&, RoutedEventArgs const&) { action(); });
            menu.Items().Append(entry);
        };

        if (item.IsAccount())
        {
            /* modèles prédéfinis : créés tout de suite, modifiables ensuite */
            struct Model { wchar_t const* fr; wchar_t const* en; wchar_t const* query; };
            static const Model models[] = {
                { L"Messages non lus", L"Unread messages", L"is:unread" },
                { L"Messages marqués", L"Flagged messages", L"is:flagged" },
                { L"Messages avec pièces jointes", L"Messages with attachments", L"has:attachment" },
                { L"Gros messages (plus de 5 Mo)", L"Large messages (over 5 MB)", L"larger:5242880" },
            };
            MenuFlyoutSubItem templates;
            templates.Text(Tr(L"Nouveau dossier de recherche", L"New search folder"));
            FontIcon templates_icon;
            templates_icon.Glyph(L"\xE710");
            templates.Icon(templates_icon);
            for (auto const& model : models)
            {
                MenuFlyoutItem entry;
                entry.Text(Tr(model.fr, model.en));
                hstring name = Tr(model.fr, model.en);
                hstring query = model.query;
                entry.Click([this, name, query](IInspectable const&, RoutedEventArgs const&)
                {
                    JsonArray updated = LoadSearchFolders();
                    updated.Append(MakeSearchFolder(hstring{ std::to_wstring(GetTickCount64()) }, name, query, L"all", L""));
                    ::EvolutionWin::Settings::Instance().Set(L"mail.search-folders", updated);
                    RefreshSearchFoldersNode();
                });
                templates.Items().Append(entry);
            }
            templates.Items().Append(MenuFlyoutSeparator{});
            MenuFlyoutItem sender_entry;
            sender_entry.Text(Tr(L"Messages d'une personne…", L"Messages from a person…"));
            sender_entry.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                ShowSearchFolderDialogAsync(MakeSearchFolder(L"", Tr(L"Messages de…", L"Messages from…"), L"from:", L"all", L""));
            });
            templates.Items().Append(sender_entry);
            MenuFlyoutItem custom_entry;
            custom_entry.Text(Tr(L"Personnalisé…", L"Custom…"));
            custom_entry.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                ShowSearchFolderDialogAsync(MakeSearchFolder(L"", L"", L"", L"all", L""));
            });
            templates.Items().Append(custom_entry);
            menu.Items().Append(templates);
            return;
        }

        hstring id = item.Folder();
        add_item(Tr(L"Modifier le dossier de recherche…", L"Edit search folder…"), L"\xE70F", [this, id]()
        {
            if (auto folder = FindSearchFolder(id))
                ShowSearchFolderDialogAsync(folder);
        });
        add_item(Tr(L"Supprimer le dossier de recherche", L"Delete search folder"), L"\xE74D", [this, id]()
        {
            JsonArray updated;
            for (auto const& value : LoadSearchFolders())
                if (value.ValueType() != JsonValueType::Object || value.GetObject().GetNamedString(L"id", L"") != id)
                    updated.Append(value);
            ::EvolutionWin::Settings::Instance().Set(L"mail.search-folders", updated);
            RefreshSearchFoldersNode();
            if (m_account == ::EvolutionWin::UnifiedViews::kSearchAccountId && m_folder == id)
            {
                m_account = L"";
                m_folder = L"";
                MessageList().Items().Clear();
                ClearReader();
            }
        });
    }

    /**
     * @brief Boîte de dialogue de création / modification d'un dossier de recherche : nom, critères et portée.
     * @param definition Définition de départ ("id" vide : nouveau dossier).
     */
    fire_and_forget MainWindow::ShowSearchFolderDialogAsync(JsonObject definition)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(definition.GetNamedString(L"id", L"").empty() ? Tr(L"Nouveau dossier de recherche", L"New search folder")
                                                                             : Tr(L"Dossier de recherche", L"Search folder")));
        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(460);
        TextBox name_box;
        name_box.Header(box_value(Tr(L"Nom", L"Name")));
        name_box.Text(definition.GetNamedString(L"name", L""));
        panel.Children().Append(name_box);
        TextBox query_box;
        query_box.Header(box_value(Tr(L"Critères", L"Criteria")));
        query_box.Text(definition.GetNamedString(L"query", L""));
        panel.Children().Append(query_box);
        TextBlock help;
        help.TextWrapping(TextWrapping::Wrap);
        help.Text(Tr(L"Mots libres (objet, expéditeur, destinataires, texte) et opérateurs : is:unread, is:read, is:flagged, has:attachment, larger:5242880 (octets), from:nom. Les dossiers Éléments supprimés et Courrier indésirable sont ignorés quand un opérateur est utilisé.",
                     L"Free words (subject, sender, recipients, text) and operators: is:unread, is:read, is:flagged, has:attachment, larger:5242880 (bytes), from:name. Deleted Items and Junk are skipped when an operator is used."));
        help.Opacity(0.7);
        panel.Children().Append(help);

        ComboBox scope_combo;
        scope_combo.Header(box_value(Tr(L"Chercher dans", L"Search in")));
        scope_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        std::vector<hstring> scope_accounts{ L"" };
        scope_combo.Items().Append(box_value(Tr(L"Tous les comptes", L"All accounts")));
        int selected = 0;
        for (auto const& value : m_accounts ? m_accounts : JsonArray{})
        {
            auto account = value.GetObject();
            if (account.GetNamedBoolean(L"local", false) && account.GetNamedString(L"uid", L"") != L"local")
                continue;
            hstring uid = account.GetNamedString(L"uid", L"");
            scope_combo.Items().Append(box_value(Fmt(Tr(L"Compte : {0}", L"Account: {0}"), { account.GetNamedString(L"name", uid) })));
            if (definition.GetNamedString(L"scope", L"all") == L"account" && definition.GetNamedString(L"account", L"") == uid)
                selected = static_cast<int>(scope_accounts.size());
            scope_accounts.push_back(uid);
        }
        scope_combo.SelectedIndex(selected);
        panel.Children().Append(scope_combo);

        dialog.Content(panel);
        dialog.PrimaryButtonText(Tr(L"Enregistrer", L"Save"));
        dialog.CloseButtonText(Tr(L"Annuler", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        hstring name{ ::EvolutionWin::Helpers::Trim(name_box.Text()) };
        hstring query{ ::EvolutionWin::Helpers::Trim(query_box.Text()) };
        if (name.empty() || query.empty())
        {
            ShowStatus(Tr(L"Un dossier de recherche demande un nom et des critères.", L"A search folder needs a name and criteria."), InfoBarSeverity::Warning);
            co_return;
        }
        int scope_index = scope_combo.SelectedIndex();
        hstring account_uid = scope_index > 0 && static_cast<size_t>(scope_index) < scope_accounts.size() ? scope_accounts[scope_index] : hstring{};
        hstring id = definition.GetNamedString(L"id", L"");
        bool is_new = id.empty();
        if (is_new)
            id = hstring{ std::to_wstring(GetTickCount64()) };

        JsonArray updated;
        bool replaced = false;
        for (auto const& value : LoadSearchFolders())
        {
            if (value.ValueType() == JsonValueType::Object && value.GetObject().GetNamedString(L"id", L"") == id)
            {
                updated.Append(MakeSearchFolder(id, name, query, account_uid.empty() ? L"all" : L"account", account_uid));
                replaced = true;
            }
            else
                updated.Append(value);
        }
        if (!replaced)
            updated.Append(MakeSearchFolder(id, name, query, account_uid.empty() ? L"all" : L"account", account_uid));
        ::EvolutionWin::Settings::Instance().Set(L"mail.search-folders", updated);
        RefreshSearchFoldersNode();

        /* dossier affiché : recharge avec la nouvelle définition */
        if (!is_new && m_account == ::EvolutionWin::UnifiedViews::kSearchAccountId && m_folder == id)
        {
            FolderTitle().Text(name);
            LoadMessages(false);
        }
    }
}
