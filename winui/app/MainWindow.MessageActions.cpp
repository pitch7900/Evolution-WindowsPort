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
        }

        auto loc = GetMessageLocation(uid);
        hstring account = loc.account.empty() ? m_account : loc.account;
        hstring folder = loc.folder.empty() ? m_folder : loc.folder;
        m_core->SetMessageFlags(account, folder, uid, flag, value,
            [this, account, folder, uid](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Indicateur : " + error, InfoBarSeverity::Error);
                return;
            }

            if (account != m_account || folder != m_folder)
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
                ShowStatus(L"Suppression : " + error, InfoBarSeverity::Error);
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
                ShowStatus(L"Déplacement : " + error, InfoBarSeverity::Error);
                LoadMessages(false);
                return;
            }

            ShowStatus(L"Message déplacé vers « " + dest_folder + L" ».", InfoBarSeverity::Informational);
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
            m_core->CopyMessage(m_account, m_folder, uid, dest_account, dest_folder,
                [this, dest_folder, remaining, count = uids.size()](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(L"Copie : " + error, InfoBarSeverity::Error);
                else if (--*remaining == 0)
                    ShowStatus(count > 1
                        ? hstring{ std::to_wstring(count) + L" messages copiés vers « " } + dest_folder + L" »."
                        : L"Message copié vers « " + dest_folder + L" ».", InfoBarSeverity::Informational);
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
        if (uids.empty() || m_account.empty())
            return;

        hstring archive;
        if (auto it = m_account_folders.find(std::wstring{ m_account }); it != m_account_folders.end() && it->second)
            archive = FindArchiveFolder(it->second);

        if (archive == m_folder && !archive.empty())
        {
            ShowStatus(L"Ces messages sont déjà dans le dossier Archive.", InfoBarSeverity::Informational);
            return;
        }

        if (!archive.empty())
        {
            MoveMessagesByUids(uids, m_account, archive);
            return;
        }

        hstring account = m_account;
        m_core->CreateFolder(account, L"", L"Archive", [this, uids, account](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création du dossier Archive : " + error, InfoBarSeverity::Error);
                return;
            }
            if (account == m_account)
                MoveMessagesByUids(uids, account, L"Archive");
        });
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
    void MainWindow::PopulateMoveMenuItems(Collections::IVector<MenuFlyoutItemBase> const& items, std::vector<hstring> const& uids, bool copy)
    {
        items.Clear();

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

        if (items.Size() == 0)
        {
            MenuFlyoutItem empty_item;
            empty_item.Text(L"Aucun autre dossier disponible");
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
        copy_sub.Text(L"Copier vers");
        copy_sub.Icon(FontIcon{});
        copy_sub.Icon().as<FontIcon>().Glyph(L"\xE8C8");
        PopulateMoveMenuItems(copy_sub.Items(), uids, true);
        MoveMenu().Items().Append(MenuFlyoutSeparator{});
        MoveMenu().Items().Append(copy_sub);
    }
}
