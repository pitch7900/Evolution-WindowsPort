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
#include <unordered_set>

#include <chrono>

#include <cmath>

#include <cwctype>

#include <utility>

#include <vector>



/* Actions sur les messages : drapeaux, suppression, déplacement, copie et archivage. */



using namespace winrt;

using namespace Microsoft::UI::Xaml;

using namespace Microsoft::UI::Xaml::Controls;

using namespace Microsoft::UI::Xaml::Media;

using namespace Windows::Data::Json;

using namespace Windows::Foundation;



using namespace ::EvolutionWin::Helpers;



namespace winrt::EvolutionWin::implementation

{

    /* Modifie un indicateur ("seen" ou "flagged") sur un message côté interface

     * et côté moteur evo-core.

     * Paramètres :

     *   uid   : identifiant du message

     *   flag  : nom de l'indicateur ("seen" ou "flagged")

     *   value : nouvelle valeur booléenne de l'indicateur */

    void MainWindow::SetMessageFlag(hstring const& uid, hstring const& flag, bool value)

    {

        if (uid.empty() || m_account.empty() || m_folder.empty())

            return;



        auto& state = m_message_states[std::wstring{ uid }];

        if (flag == L"seen")

            state.seen = value;

        else if (flag == L"flagged")

            state.flagged = value;



        if (auto item = FindMessageItem(uid))

            UpdateMessageItemVisual(item, state.seen, state.flagged);



        if (uid == SelectedMessageUid())

            UpdateActionButtons(uid);



        /* Met aussi à jour la copie en mémoire pour que la recherche locale reste cohérente */

        if (m_all_messages)

        {

            for (auto const& entry : m_all_messages)

            {

                auto obj = entry.GetObject();

                if (obj.GetNamedString(L"uid", L"") == uid)

                {

                    obj.SetNamedValue(flag, JsonValue::CreateBooleanValue(value));

                    break;

                }

            }

            UpdateFocusedBarCounts();

        }



        auto loc = GetMessageLocation(uid);

        hstring account = loc.account.empty() ? m_account : loc.account;

        hstring folder = loc.folder.empty() ? m_folder : loc.folder;

        m_core->SetMessageFlags(account, folder, uid, flag, value,

            [this, account, folder, uid](hstring const& json, hstring const& error)

        {

            if (!error.empty())

            {

                ShowStatus(Tr(L"Indicateur : ") + error, InfoBarSeverity::Error);

                return;

            }



            if (!IsUnifiedView() && (account != m_account || folder != m_folder))

                return;



            JsonObject result;

            if (JsonObject::TryParse(json, result))

            {

                auto& current = m_message_states[std::wstring{ uid }];

                current.seen = result.GetNamedBoolean(L"seen", current.seen);

                current.flagged = result.GetNamedBoolean(L"flagged", current.flagged);

                if (auto item = FindMessageItem(uid))

                    UpdateMessageItemVisual(item, current.seen, current.flagged);

                if (uid == SelectedMessageUid())

                    UpdateActionButtons(uid);

            }

        });

    }



    /* Modifie un indicateur ("seen" ou "flagged") sur une liste de messages.

     * Paramètres :

     *   uids  : identifiants des messages

     *   flag  : nom de l'indicateur ("seen" ou "flagged")

     *   value : nouvelle valeur booléenne */

    void MainWindow::SetMessagesFlag(std::vector<hstring> const& uids, hstring const& flag, bool value)

    {

        for (auto const& u : uids)

            SetMessageFlag(u, flag, value);

    }



    /* Retire un message de la liste affichée et de la liste en mémoire, puis

     * sélectionne le message voisin s'il en reste.

     * Paramètres :

     *   uid : identifiant du message à retirer */

    void MainWindow::RemoveMessageFromList(hstring const& uid)

    {

        auto items = MessageList().Items();

        uint32_t removed_index = 0;

        bool found = false;

        bool was_selected = (SelectedMessageUid() == uid);



        for (uint32_t i = 0; i < items.Size(); ++i)

        {

            auto grid = items.GetAt(i).try_as<Grid>();

            if (grid && unbox_value_or<hstring>(grid.Tag(), L"") == uid)

            {

                removed_index = i;

                found = true;

                items.RemoveAt(i);

                break;

            }

        }



        m_message_states.erase(std::wstring{ uid });



        if (m_all_messages)

        {

            for (uint32_t i = 0; i < m_all_messages.Size(); ++i)

            {

                if (m_all_messages.GetAt(i).GetObject().GetNamedString(L"uid", L"") == uid)

                {

                    m_all_messages.RemoveAt(i);

                    break;

                }

            }

        }



        if (was_selected && MessageList().SelectedItems().Size() == 0)

        {

            if (found && items.Size() > 0)

            {

                uint32_t next_index = removed_index < items.Size() ? removed_index : items.Size() - 1;

                MessageList().SelectedIndex(static_cast<int32_t>(next_index));

            }

            else

            {

                ClearReader();

            }

        }

    }



    /* Supprime un message identifié par son UID (déplacement vers la corbeille

     * ou suppression définitive).

     * Paramètres :

     *   uid : identifiant du message à supprimer */

    void MainWindow::DeleteMessageByUid(hstring const& uid)

    {

        if (uid.empty())

            return;



        auto loc = GetMessageLocation(uid);

        hstring account = loc.account.empty() ? m_account : loc.account;

        hstring folder = loc.folder.empty() ? m_folder : loc.folder;

        if (account.empty() || folder.empty())

            return;



        RemoveMessageFromList(uid);



        m_core->DeleteMessage(account, folder, uid, [this](hstring const&, hstring const& error)

        {

            if (!error.empty())

            {

                ShowStatus(Tr(L"Suppression : ") + error, InfoBarSeverity::Error);

                LoadMessages(false);

            }

        });

    }



    /* Supprime un ensemble de messages identifiés par leurs UID.

     * Paramètres :

     *   uids : liste des identifiants des messages à supprimer */

    void MainWindow::DeleteMessagesByUids(std::vector<hstring> const& uids)

    {

        for (auto const& u : uids)

            DeleteMessageByUid(u);

    }



    /* Déplace un message identifié par son UID vers un autre dossier.

     * Paramètres :

     *   uid          : identifiant du message à déplacer

     *   dest_account : identifiant du compte de destination

     *   dest_folder  : chemin complet du dossier de destination */

    void MainWindow::MoveMessageByUid(hstring const& uid, hstring const& dest_account, hstring const& dest_folder)

    {

        if (uid.empty() || dest_account.empty() || dest_folder.empty())

            return;



        auto loc = GetMessageLocation(uid);

        hstring src_account = loc.account.empty() ? m_account : loc.account;

        hstring src_folder = loc.folder.empty() ? m_folder : loc.folder;

        if (src_account.empty() || src_folder.empty())

            return;



        if (dest_account == src_account && dest_folder == src_folder)

            return;



        RemoveMessageFromList(uid);

        RememberRecentFolder(dest_account, dest_folder);



        m_core->MoveMessage(src_account, src_folder, uid, dest_account, dest_folder,

            [this, dest_folder](hstring const&, hstring const& error)

        {

            if (!error.empty())

            {

                ShowStatus(Tr(L"Déplacement : ") + error, InfoBarSeverity::Error);

                LoadMessages(false);

                return;

            }



            ShowStatus(Fmt(Tr(L"Message déplacé vers « {0} »."), { dest_folder }), InfoBarSeverity::Informational);

        });

    }



    /* Déplace un ensemble de messages identifiés par leurs UID vers un autre dossier.

     * Paramètres :

     *   uids         : liste des identifiants des messages à déplacer

     *   dest_account : identifiant du compte de destination

     *   dest_folder  : chemin complet du dossier de destination */

    void MainWindow::MoveMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder)

    {

        for (auto const& u : uids)

            MoveMessageByUid(u, dest_account, dest_folder);

    }



    /* Copie des messages dans un autre dossier (les originaux restent).

     * Paramètres :

     *   uids         : identifiants des messages à copier

     *   dest_account : compte de destination

     *   dest_folder  : chemin complet du dossier de destination */

    void MainWindow::CopyMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder)

    {

        if (uids.empty() || m_account.empty() || m_folder.empty())

            return;



        RememberRecentFolder(dest_account, dest_folder);



        auto remaining = std::make_shared<size_t>(uids.size());

        for (auto const& uid : uids)

        {

            m_core->CopyMessage(GetMessageLocation(uid).account, GetMessageLocation(uid).folder, uid, dest_account, dest_folder,

                [this, dest_folder, remaining, count = uids.size()](hstring const&, hstring const& error)

            {

                if (!error.empty())

                    ShowStatus(Tr(L"Copie : ") + error, InfoBarSeverity::Error);

                else if (--*remaining == 0)

                    ShowStatus(count > 1

                        ? Fmt(Tr(L"{0} messages copiés vers « {1} »."), { to_hstring(count), dest_folder })

                        : Fmt(Tr(L"Message copié vers « {0} »."), { dest_folder }), InfoBarSeverity::Informational);

            });

        }

    }



    /* Dossier Archive d'un compte (type "archive", cf. evo-core) ; vide

     * s'il n'y en a pas. */

    static hstring FindArchiveFolder(JsonArray const& folders)

    {

        for (auto const& value : folders)

        {

            auto folder = value.GetObject();

            if (folder.GetNamedString(L"type", L"") == L"archive" && folder.GetNamedBoolean(L"selectable", true))

                return folder.GetNamedString(L"full_name");

            if (auto found = FindArchiveFolder(folder.GetNamedArray(L"children", JsonArray{})); !found.empty())

                return found;

        }

        return {};

    }



    /* Archiver (Retour arrière) : déplace les messages dans le dossier

     * Archive du compte, créé au premier usage s'il n'existe pas. */

    void MainWindow::ArchiveMessagesByUids(std::vector<hstring> const& uids)
    {
        for (auto const& [location, group] : GroupUidsByLocation(uids))
            ArchiveMessagesInFolder(hstring{ location.first }, hstring{ location.second }, group);
    }

    /* Marque des messages comme indésirables (ou non indésirables), un lot par
     * compte et dossier d'origine. */
    void MainWindow::MarkAsJunkByUids(std::vector<hstring> const& uids)
    {
        for (auto const& [location, group] : GroupUidsByLocation(uids))
            MarkAsJunkInFolder(hstring{ location.first }, hstring{ location.second }, group);
    }

    /* Archive les messages d'un dossier d'un compte (créé au premier usage
     * si le dossier Archive n'existe pas). */
    void MainWindow::ArchiveMessagesInFolder(hstring const& account, hstring const& folder, std::vector<hstring> const& uids)

    {

        if (uids.empty() || account.empty())

            return;



        hstring archive;

        if (auto it = m_account_folders.find(std::wstring{ account }); it != m_account_folders.end() && it->second)

            archive = FindArchiveFolder(it->second);



        if (archive == folder && !archive.empty())

        {

            ShowStatus(Tr(L"Ces messages sont déjà dans le dossier Archive."), InfoBarSeverity::Informational);

            return;

        }



        if (!archive.empty())

        {

            MoveMessagesByUids(uids, account, archive);

            return;

        }



        

        m_core->CreateFolder(account, L"", L"Archive", [this, uids, account](hstring const&, hstring const& error)

        {

            if (!error.empty())

            {

                ShowStatus(Tr(L"Création du dossier Archive : ") + error, InfoBarSeverity::Error);

                return;

            }

            /* le compte est explicite : pas de dépendance au dossier affiché */

                MoveMessagesByUids(uids, account, L"Archive");

        });

    }



    /**
     * @brief Cherche, dans l'arborescence d'un compte, le premier dossier sélectionnable d'un type donné.
     * @param folders Arborescence JSON des dossiers du compte.
     * @param type Type de dossier recherché (« junk », « inbox »…, cf. evo-core).
     * @return Chemin complet du dossier, ou chaîne vide s'il n'existe pas.
     */
    static hstring FindFolderByType(JsonArray const& folders, hstring const& type)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            if (folder.GetNamedString(L"type", L"") == type && folder.GetNamedBoolean(L"selectable", true))
                return folder.GetNamedString(L"full_name");
            if (auto found = FindFolderByType(folder.GetNamedArray(L"children", JsonArray{}), type); !found.empty())
                return found;
        }
        return {};
    }

    /**
     * @brief Marque des messages comme indésirables, ou comme non indésirables s'ils sont déjà dans le dossier Indésirables.
     *        Déplace les messages vers le dossier Indésirables du compte (ou vers la Boîte de réception pour l'opération inverse).
     * @param uids Identifiants des messages concernés (dossier courant).
     */
    void MainWindow::MarkAsJunkInFolder(hstring const& account, hstring const& folder, std::vector<hstring> const& uids)
    {
        if (uids.empty() || account.empty())
            return;

        hstring junk;
        hstring inbox;
        if (auto it = m_account_folders.find(std::wstring{ account }); it != m_account_folders.end() && it->second)
        {
            junk = FindFolderByType(it->second, L"junk");
            inbox = FindFolderByType(it->second, L"inbox");
        }

        if (junk.empty())
        {
            ShowStatus(Tr(L"Ce compte n'a pas de dossier Courrier indésirable (voir Propriétés du compte › Dossiers spéciaux)."), InfoBarSeverity::Warning);
            return;
        }

        if (folder == junk)
        {
            /* Non indésirable : retour vers la Boîte de réception */
            if (inbox.empty())
            {
                ShowStatus(Tr(L"Boîte de réception introuvable pour ce compte."), InfoBarSeverity::Warning);
                return;
            }
            MoveMessagesByUids(uids, account, inbox);
            return;
        }

        MoveMessagesByUids(uids, account, junk);
    }

    /**
     * @brief Gestionnaire du bouton « Indésirable / Non indésirable » : applique l'action aux messages sélectionnés.
     */
    void MainWindow::OnJunkClick(IInspectable const&, RoutedEventArgs const&)
    {
        MarkAsJunkByUids(SelectedMessageUids());
    }

    void MainWindow::OnArchiveClick(IInspectable const&, RoutedEventArgs const&)

    {

        ArchiveMessagesByUids(SelectedMessageUids());

    }



    /* Derniers dossiers de destination (Déplacer / Copier), en tête des

     * menus : préférence "move.recent" ("<compte>\x1F<dossier>", 5 au plus). */

    void MainWindow::RememberRecentFolder(hstring const& account, hstring const& folder)

    {

        auto& settings = ::EvolutionWin::Settings::Instance();

        hstring key = hstring{ NodeKey(account, folder) };

        JsonArray recent;

        recent.Append(JsonValue::CreateStringValue(key));



        for (auto const& value : settings.GetArray(L"move.recent"))

        {

            if (recent.Size() >= 5)

                break;

            if (value.ValueType() == JsonValueType::String && value.GetString() != key)

                recent.Append(value);

        }



        settings.Set(L"move.recent", recent);

    }



    /* Ajoute récursivement les dossiers d'un compte dans un menu « Déplacer vers ».

     * Paramètres :

     *   items   : collection d'éléments du menu à remplir

     *   folders : tableau JSON des dossiers à parcourir

     *   account : identifiant du compte propriétaire des dossiers

     *   prefix  : préfixe d'affichage pour les sous-dossiers

     *   uids    : identifiants des messages à déplacer */

    void MainWindow::AppendFolderMoveItems(

        Collections::IVector<MenuFlyoutItemBase> const& items,

        JsonArray const& folders,

        hstring const& account,

        hstring const& prefix,

        std::vector<hstring> const& uids,

        bool copy)

    {

        for (auto const& value : folders)

        {

            auto folder = value.GetObject();

            hstring full_name = folder.GetNamedString(L"full_name");

            hstring name = folder.GetNamedString(L"name", full_name);

            hstring label = prefix.empty() ? name : prefix + L" / " + name;

            bool selectable = folder.GetNamedBoolean(L"selectable", true);



            if (selectable && !(account == m_account && full_name == m_folder))

            {

                MenuFlyoutItem item;

                item.Text(label);

                item.Icon(CreateFolderIcon(folder.GetNamedString(L"type", L"")));

                item.Click([this, uids, account, full_name, copy](IInspectable const&, RoutedEventArgs const&)

                {

                    if (copy)

                        CopyMessagesByUids(uids, account, full_name);

                    else

                        MoveMessagesByUids(uids, account, full_name);

                });

                items.Append(item);

            }



            AppendFolderMoveItems(items, folder.GetNamedArray(L"children", JsonArray{}), account, label, uids, copy);

        }

    }



    /* Remplit un menu « Déplacer vers » avec les dossiers du compte courant.

     * Paramètres :

     *   items : collection d'éléments du menu à remplir

     *   uids  : identifiants des messages à déplacer */

    void MainWindow::OnMoveFolderDialogClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
        {
            auto sel_uid = SelectedMessageUid();
            if (!sel_uid.empty())
                uids.push_back(sel_uid);
        }
        if (uids.empty())
            return;
        ShowMoveFolderDialogAsync(uids, false);
    }

    void MainWindow::OnMoveFolderAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        OnMoveFolderDialogClick(nullptr, nullptr);
    }

    /**
     * @brief Ouvre la fenêtre de choix du dossier de destination pour déplacer ou copier des messages.
     * @param uids Identifiants des messages concernés.
     * @param copy Vrai pour copier (les originaux restent), faux pour déplacer.
     */
    fire_and_forget MainWindow::ShowMoveFolderDialogAsync(std::vector<hstring> uids, bool copy)
    {
        if (uids.empty())
            co_return;

        FolderPickOptions options;
        options.title = copy ? Tr(L"Copier vers le dossier") : Tr(L"Déplacer vers le dossier");
        options.confirm_label = copy ? Tr(L"Copier ici") : Tr(L"Déplacer ici");
        /* le dossier affiché ne peut être sa propre destination */
        options.blocked = [this](hstring const& account, hstring const& folder)
        {
            return account == m_account && folder == m_folder;
        };

        PickFolderAsync(options, [this, uids, copy](hstring const& account, hstring const& folder)
        {
            if (account.empty() || folder.empty())
                return;
            if (copy)
                CopyMessagesByUids(uids, account, folder);
            else
                MoveMessagesByUids(uids, account, folder);
        });
        co_return;
    }

    void MainWindow::PopulateMoveMenuItems(Collections::IVector<MenuFlyoutItemBase> const& items, std::vector<hstring> const& uids, bool copy)
    {
        items.Clear();

        /* Option de sélection par boîte de dialogue avancée (Ctrl+Maj+V) */
        MenuFlyoutItem dialog_item;
        dialog_item.Text(Tr(L"Autre dossier... (Ctrl+Maj+V)"));
        dialog_item.Icon(FontIcon{});
        dialog_item.Icon().as<FontIcon>().Glyph(L"\xE8DE");
        dialog_item.Click([this, uids, copy](IInspectable const&, RoutedEventArgs const&)
        {
            ShowMoveFolderDialogAsync(uids, copy);
        });
        items.Append(dialog_item);
        items.Append(MenuFlyoutSeparator{});

        auto it = m_account_folders.find(std::wstring{ m_account });



        /* derniers dossiers utilisés du compte, puis tous les dossiers */

        uint32_t recent_count = 0;

        for (auto const& value : ::EvolutionWin::Settings::Instance().GetArray(L"move.recent"))

        {

            if (value.ValueType() != JsonValueType::String)

                continue;

            std::wstring key{ value.GetString() };

            std::wstring prefix = std::wstring{ m_account } + L'\x1F';

            if (key.rfind(prefix, 0) != 0)

                continue;



            hstring full_name{ key.substr(prefix.size()) };

            if (full_name == m_folder)

                continue;



            MenuFlyoutItem item;

            item.Text(full_name);

            item.Icon(FontIcon{});

            item.Icon().as<FontIcon>().Glyph(L"\xE81C");

            hstring account = m_account;

            item.Click([this, uids, account, full_name, copy](IInspectable const&, RoutedEventArgs const&)

            {

                if (copy)

                    CopyMessagesByUids(uids, account, full_name);

                else

                    MoveMessagesByUids(uids, account, full_name);

            });

            items.Append(item);

            recent_count++;

        }

        if (recent_count > 0)

            items.Append(MenuFlyoutSeparator{});



        if (it != m_account_folders.end() && it->second)
            AppendFolderMoveItems(items, it->second, m_account, L"", uids, copy);

        /* Dossiers des autres comptes enregistrés */
        auto get_account_name = [this](hstring const& acc_uid) -> hstring
        {
            if (m_accounts)
            {
                for (auto const& item : m_accounts)
                {
                    auto obj = item.GetObject();
                    if (obj.GetNamedString(L"uid", L"") == acc_uid)
                        return obj.GetNamedString(L"name", obj.GetNamedString(L"display_name", acc_uid));
                }
            }
            return acc_uid;
        };

        for (auto const& [acc_key, folders_arr] : m_account_folders)
        {
            if (acc_key == std::wstring{ m_account })
                continue;

            hstring other_account{ acc_key };
            hstring acc_display = get_account_name(other_account);

            MenuFlyoutSubItem account_sub;
            account_sub.Text(acc_display);
            account_sub.Icon(FontIcon{});
            account_sub.Icon().as<FontIcon>().Glyph(L"\xE779");

            AppendFolderMoveItems(account_sub.Items(), folders_arr, other_account, L"", uids, copy);
            if (account_sub.Items().Size() > 0)
            {
                items.Append(account_sub);
            }
        }



        if (items.Size() == 0)

        {

            MenuFlyoutItem empty_item;

            empty_item.Text(Tr(L"Aucun autre dossier disponible"));

            empty_item.IsEnabled(false);

            items.Append(empty_item);

        }

    }



    /* Gestionnaire du bouton « Lu / Non lu » du volet de lecture. */

    void MainWindow::OnToggleReadClick(IInspectable const&, RoutedEventArgs const&)

    {

        auto uids = SelectedMessageUids();

        if (uids.empty())

            return;



        auto it = m_message_states.find(std::wstring{ uids.front() });

        bool seen = it != m_message_states.end() ? it->second.seen : true;

        SetMessagesFlag(uids, L"seen", !seen);

    }



    /* Gestionnaire du bouton « Suivi » (drapeau) du volet de lecture. */

    void MainWindow::OnToggleFlagClick(IInspectable const&, RoutedEventArgs const&)

    {

        auto uids = SelectedMessageUids();

        if (uids.empty())

            return;



        auto it = m_message_states.find(std::wstring{ uids.front() });

        bool flagged = it != m_message_states.end() ? it->second.flagged : false;

        SetMessagesFlag(uids, L"flagged", !flagged);

    }



    /* Gestionnaire du bouton « Supprimer » du volet de lecture. */

    void MainWindow::OnDeleteClick(IInspectable const&, RoutedEventArgs const&)

    {

        auto uids = SelectedMessageUids();

        ConfirmAndDeleteMessages(uids, IsTrashFolder());

    }



    /* Gestionnaire d'ouverture du menu « Déplacer » du volet de lecture. */

    void MainWindow::OnMoveMenuOpening(IInspectable const&, IInspectable const&)

    {

        auto uids = SelectedMessageUids();

        PopulateMoveMenuItems(MoveMenu().Items(), uids);



        MenuFlyoutSubItem copy_sub;

        copy_sub.Text(Tr(L"Copier vers"));

        copy_sub.Icon(FontIcon{});

        copy_sub.Icon().as<FontIcon>().Glyph(L"\xE8C8");

        PopulateMoveMenuItems(copy_sub.Items(), uids, true);

        MoveMenu().Items().Append(MenuFlyoutSeparator{});

        MoveMenu().Items().Append(copy_sub);

    }



    /**

     * @brief Ouvre le message sélectionné dans le navigateur web par défaut du système Windows.

     * @param sender Bouton de commande émetteur.

     * @param args Arguments de l'événement de clic.

     */

    void MainWindow::OnOpenInBrowserClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        if (!m_current_message)

        {

            ShowStatus(Tr(L"Sélectionnez d'abord un message à afficher."), InfoBarSeverity::Informational);

            return;

        }



        bool ok = ::EvolutionWin::Helpers::OpenInDefaultBrowser(m_current_message, m_reader_html);

        if (!ok)

        {

            ShowStatus(Tr(L"Impossible d'ouvrir le navigateur web par défaut."), InfoBarSeverity::Error);

        }

    }



    /**

     * @brief Arrête la synthèse vocale en cours et réinitialise l'icône du bouton.

     */

    void MainWindow::StopSpeech()

    {

        if (m_speech_player)

        {

            try

            {

                m_speech_player.Pause();

                m_speech_player.Source(nullptr);

            }

            catch (...)

            {

            }

        }

        m_is_speaking = false;

        if (m_initialized)

        {

            try

            {

                ReadAloudIcon().Glyph(L"\xEC92");

                ReadAloudButton().Label(Tr(L"Lire à voix haute"));

            }

            catch (...)

            {

            }

        }

    }



    /**

     * @brief Déclenche ou interrompt la lecture à voix haute du message courant via Windows.Media.SpeechSynthesis.

     * @param sender Bouton émetteur de l'action.

     * @param args Arguments de l'événement routé.

     */

    fire_and_forget MainWindow::OnReadAloudClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        if (m_is_speaking)

        {

            StopSpeech();

            ShowStatus(Tr(L"Lecture vocale interrompue."), InfoBarSeverity::Informational);

            co_return;

        }



        if (!m_current_message)

        {

            ShowStatus(Tr(L"Sélectionnez d'abord un message à lire."), InfoBarSeverity::Informational);

            co_return;

        }



        std::wstring speech_text = ::EvolutionWin::Helpers::ExtractPlainTextForSpeech(m_current_message, m_reader_html);

        if (speech_text.empty())

        {

            ShowStatus(Tr(L"Le message ne contient aucun texte à lire."), InfoBarSeverity::Warning);

            co_return;

        }



        try

        {

            if (!m_speech_synth)

            {

                m_speech_synth = Windows::Media::SpeechSynthesis::SpeechSynthesizer();

            }

            if (!m_speech_player)

            {

                m_speech_player = Windows::Media::Playback::MediaPlayer();

                m_speech_player.MediaEnded([this](Windows::Media::Playback::MediaPlayer const&, IInspectable const&)

                {

                    DispatcherQueue().TryEnqueue([this]()

                    {

                        StopSpeech();

                    });

                });

            }



            auto stream = co_await m_speech_synth.SynthesizeTextToStreamAsync(hstring{ speech_text });

            auto source = Windows::Media::Core::MediaSource::CreateFromStream(stream, stream.ContentType());

            m_speech_player.Source(source);

            m_speech_player.Play();



            m_is_speaking = true;

            ReadAloudIcon().Glyph(L"\xE71A");

            ReadAloudButton().Label(Tr(L"Arrêter la lecture"));

            ShowStatus(Tr(L"Lecture à voix haute en cours..."), InfoBarSeverity::Informational);

        }

        catch (hresult_error const& ex)

        {

            StopSpeech();

            ShowStatus(Tr(L"Erreur lors de la synthèse vocale : ") + ex.message(), InfoBarSeverity::Error);

        }

        catch (...)

        {

            StopSpeech();

            ShowStatus(Tr(L"Erreur inattendue lors de la lecture vocale."), InfoBarSeverity::Error);

        }

    }


    /**
     * @brief Remplit dynamiquement la liste des catégories dans un menu déroulant ou sous-menu.
     * @param items Collection d'éléments de menu (MenuFlyout ou MenuFlyoutSubItem) à peupler.
     * @param uids Liste des identifiants uniques des messages concernés par l'opération.
     */
    void MainWindow::PopulateCategorizeMenuItems(
        Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
        std::vector<hstring> const& uids)
    {
        items.Clear();

        if (uids.empty())
        {
            MenuFlyoutItem empty_item;
            empty_item.Text(Tr(L"Aucun message sélectionné"));
            empty_item.IsEnabled(false);
            items.Append(empty_item);
            return;
        }

        /* Compte les occurrences de chaque catégorie sur l'ensemble des messages cibles */
        std::unordered_map<std::wstring, size_t> cat_counts;
        if (m_all_messages)
        {
            std::unordered_set<std::wstring> target_set;
            for (auto const& u : uids)
                target_set.insert(std::wstring{ u });

            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                std::wstring uid{ obj.GetNamedString(L"uid", L"") };
                if (target_set.count(uid) && obj.HasKey(L"labels"))
                {
                    auto arr = obj.GetNamedArray(L"labels");
                    for (auto const& l : arr)
                    {
                        std::wstring label_str{ l.GetString() };
                        cat_counts[label_str]++;
                    }
                }
            }
        }

        /* Registre unique des catégories (modifiable dans Paramètres > Catégories) */
        auto const& categories = ::EvolutionWin::CategoryStore::Registry().Items();

        size_t cat_index = 0;
        for (auto const& cat : categories)
        {
            ToggleMenuFlyoutItem item;
            item.Text(hstring{ cat.name });
            std::wstring cat_shortcut = ::EvolutionWin::Categories::CategoryRegistry::ShortcutForIndex(cat_index++);
            if (!cat_shortcut.empty())
                item.KeyboardAcceleratorTextOverride(hstring{ cat_shortcut });

            FontIcon icon;
            icon.Glyph(L"\xF127");
            icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, cat.r, cat.g, cat.b } });
            item.Icon(icon);

            size_t count = cat_counts[cat.name];
            bool all_have = (count == uids.size() && !uids.empty());
            item.IsChecked(all_have);

            std::wstring cat_name = cat.name;
            item.Click([this, uids, cat_name, all_have](IInspectable const&, RoutedEventArgs const&)
            {
                ToggleCategoryForMessages(uids, cat_name, !all_have);
            });

            items.Append(item);
        }

        items.Append(MenuFlyoutSeparator{});

        MenuFlyoutItem clear_item;
        clear_item.Text(Tr(L"Effacer toutes les catégories"));
        FontIcon clear_icon;
        clear_icon.Glyph(L"\xE74D");
        clear_item.Icon(clear_icon);
        clear_item.Click([this, uids](IInspectable const&, RoutedEventArgs const&)
        {
            ClearCategoriesForMessages(uids);
        });
        items.Append(clear_item);
    }

    /**
     * @brief Attribue ou supprime une catégorie pour un ensemble de messages sélectionnés.
     * @param uids Liste des UIDs des messages à mettre à jour.
     * @param category_name Nom de la catégorie à attribuer ou retirer.
     * @param apply true pour attribuer la catégorie, false pour la retirer.
     */
    void MainWindow::ToggleCategoryForMessages(
        std::vector<hstring> const& uids,
        std::wstring const& category_name,
        bool apply)
    {
        if (uids.empty() || category_name.empty())
            return;

        std::unordered_set<std::wstring> target_set;
        for (auto const& u : uids)
            target_set.insert(std::wstring{ u });

        /* 1. Mise à jour de la liste locale en mémoire pour réactivité immédiate */
        if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                std::wstring uid{ obj.GetNamedString(L"uid", L"") };
                if (target_set.count(uid))
                {
                    JsonArray labels = obj.GetNamedArray(L"labels", JsonArray{});
                    JsonArray new_labels;
                    bool exists = false;
                    for (auto const& item : labels)
                    {
                        std::wstring val{ item.GetString() };
                        if (val == category_name)
                        {
                            exists = true;
                            if (apply)
                                new_labels.Append(item);
                        }
                        else
                        {
                            new_labels.Append(item);
                        }
                    }

                    if (apply && !exists)
                    {
                        new_labels.Append(JsonValue::CreateStringValue(hstring{ category_name }));
                    }

                    obj.SetNamedValue(L"labels", new_labels);
                }
            }
        }

        /* 2. Persistance et synchronisation via le moteur evo-core */
        for (auto const& uid : uids)
        {
            auto loc = GetMessageLocation(uid);
            hstring acc = loc.account.empty() ? m_account : loc.account;
            hstring fld = loc.folder.empty() ? m_folder : loc.folder;

            if (m_core && !acc.empty() && !fld.empty())
            {
                m_core->SetMessageFlags(acc, fld, uid, hstring{ category_name }, apply,
                    [](hstring const&, hstring const&) {});
            }
        }

        /* 3. Rechargement visuel de la liste des messages */
        if (m_all_messages)
        {
            ShowMessages(m_all_messages);
        }

        hstring action_msg = apply
            ? Fmt(Tr(L"Catégorie « {0} » attribuée."), { hstring{ category_name } })
            : Fmt(Tr(L"Catégorie « {0} » retirée."), { hstring{ category_name } });
        ShowStatus(action_msg, InfoBarSeverity::Informational);
    }

    /**
     * @brief Supprime toutes les catégories attribuées aux messages sélectionnés.
     * @param uids Liste des UIDs des messages dont les catégories doivent être effacées.
     */
    void MainWindow::ClearCategoriesForMessages(std::vector<hstring> const& uids)
    {
        if (uids.empty())
            return;

        std::unordered_set<std::wstring> target_set;
        for (auto const& u : uids)
            target_set.insert(std::wstring{ u });

        if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                std::wstring uid{ obj.GetNamedString(L"uid", L"") };
                if (target_set.count(uid) && obj.HasKey(L"labels"))
                {
                    auto labels = obj.GetNamedArray(L"labels");
                    for (auto const& item : labels)
                    {
                        std::wstring label{ item.GetString() };
                        auto loc = GetMessageLocation(hstring{ uid });
                        hstring acc = loc.account.empty() ? m_account : loc.account;
                        hstring fld = loc.folder.empty() ? m_folder : loc.folder;
                        if (m_core && !acc.empty() && !fld.empty())
                        {
                            m_core->SetMessageFlags(acc, fld, hstring{ uid }, hstring{ label }, false,
                                [](hstring const&, hstring const&) {});
                        }
                    }
                    obj.SetNamedValue(L"labels", JsonArray{});
                }
            }
        }

        if (m_all_messages)
        {
            ShowMessages(m_all_messages);
        }

        ShowStatus(Tr(L"Toutes les catégories ont été retirées des messages sélectionnés."), InfoBarSeverity::Informational);
    }

    /**
     * @brief Gestionnaire de l'événement d'ouverture du menu déroulant de catégorisation du ruban.
     * @param sender Élément déclencheur.
     * @param args Arguments de l'événement d'ouverture.
     */
    void MainWindow::OnCategorizeMenuOpening(IInspectable const&, IInspectable const&)
    {
        if (auto menu = RibbonCategorizeMenu())
        {
            auto uids = SelectedMessageUids();
            if (uids.empty())
            {
                hstring current = SelectedMessageUid();
                if (!current.empty())
                    uids.push_back(current);
            }
            PopulateCategorizeMenuItems(menu.Items(), uids);
        }
    }

    /**
     * @brief Gestionnaire du raccourci clavier rapide (Ctrl+F2) pour basculer la catégorie rapide.
     * @param sender Contrôle accélérateur déclencheur.
     * @param args Arguments de l'événement d'accélérateur clavier.
     */
    void MainWindow::OnCategorizeQuickAccelerator(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        auto uids = SelectedMessageUids();
        if (uids.empty())
        {
            hstring current = SelectedMessageUid();
            if (!current.empty())
                uids.push_back(current);
        }
        if (!uids.empty())
        {
            auto const& cats = ::EvolutionWin::CategoryStore::Registry().Items();
            if (!cats.empty())
            {
                ToggleCategoryForMessages(uids, cats.front().name, true);
            }
        }
    }

}
