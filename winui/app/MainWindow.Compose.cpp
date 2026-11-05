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

/* Rédaction : nouveau message, réponse, transfert. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    void MainWindow::OnNewMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        ::EvolutionWin::ComposeData data;

        /* nouveau message : compte par défaut, sinon celui du dossier affiché
         * (les réponses et transferts gardent le compte du message) */
        data.account = m_account;
        if (m_accounts)
        {
            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                if (account.GetNamedBoolean(L"default", false))
                {
                    data.account = account.GetNamedString(L"uid", m_account);
                    break;
                }
            }
        }
        OpenCompose(data);
    }

    void MainWindow::OpenCompose(::EvolutionWin::ComposeData const& data)
    {
        if (!m_accounts)
        {
            ShowStatus(L"Les comptes ne sont pas encore chargés.", InfoBarSeverity::Warning);
            return;
        }

        auto window = make<ComposeWindow>();
        m_open_compose_windows.push_back(window);
        window.Closed([this, window](auto const&, auto const&)
        {
            auto it = std::find(m_open_compose_windows.begin(), m_open_compose_windows.end(), window);
            if (it != m_open_compose_windows.end())
                m_open_compose_windows.erase(it);
        });
        get_self<ComposeWindow>(window)->Setup(m_core, m_accounts, data);
        window.Activate();
    }

    void MainWindow::OnReplyClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Sender);
    }

    void MainWindow::OnReplyAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::All);
    }

    void MainWindow::OnForwardClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Forward);
    }

    /**
     * Gestionnaire du clic sur "Transférer en pièce jointe".
     * Exporte le ou les courriels sélectionnés en fichiers .eml
     * et ouvre la fenêtre de composition avec ces pièces jointes attachées.
     * Paramètres :
     *   sender : objet ayant émis le clic
     *   args   : arguments de l'événement routé
     */
    void MainWindow::OnForwardAsAttachmentClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty() && !m_current_message_uid.empty())
            uids.push_back(m_current_message_uid);

        if (!uids.empty())
        {
            ForwardMessagesAsAttachments(uids);
        }
    }

    /* Construit la structure ComposeData (destinataires, objet, corps cité) à
     * partir d'un objet JSON de message pour une réponse ou un transfert simple.
     * Paramètres :
     *   kind    : type d'action (Sender, All ou Forward)
     *   message : objet JSON complet du message source
     * Retourne : la structure ComposeData prête pour OpenCompose. */
    ::EvolutionWin::ComposeData MainWindow::BuildReplyComposeData(ReplyKind kind, JsonObject const& message)
    {
        return ::EvolutionWin::Helpers::BuildReplyComposeData(kind, message, m_account, m_accounts);
    }

    /* Transfère un ou plusieurs messages en les exportant sous forme de pièces
     * jointes .eml attachées à un nouveau courriel.
     * Paramètres :
     *   uids : identifiants des messages à joindre en .eml */
    void MainWindow::ForwardMessagesAsAttachments(std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        hstring first_subject;
        if (m_current_message && m_current_message_uid == uids.front())
        {
            first_subject = m_current_message.GetNamedString(L"subject", L"");
        }
        else if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                if (obj.GetNamedString(L"uid", L"") == uids.front())
                {
                    first_subject = obj.GetNamedString(L"subject", L"");
                    break;
                }
            }
        }

        JsonObject req = BuildMessagesRequest(uids);

        hstring account = req.GetNamedString(L"account", m_account);
        ShowStatus(L"Préparation des courriels en pièces jointes…", InfoBarSeverity::Informational);

        m_core->ExportMessagesEml(req.Stringify(),
            [this, account, first_subject](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Transfert : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonArray paths;
            if (!JsonArray::TryParse(json, paths))
                return;

            ::EvolutionWin::ComposeData data;
            data.account = account;
            data.reply = true;
            std::wstring fwd_p{ ::EvolutionWin::Settings::Instance().GetString(L"mail.forward-prefix", L"TR: ") };
            if (!first_subject.empty())
            {
                data.subject = hstring{ FormatForwardSubject(first_subject, fwd_p) };
            }
            else
            {
                data.subject = hstring{ FormatForwardSubject(L"Messages transférés", fwd_p) };
            }

            for (auto const& val : paths)
                data.attachments.push_back(val.GetString());

            HideStatus();
            OpenCompose(data);
        });
    }

    /* Répond ou transfère un ou plusieurs messages :
     * - Pour un transfert (Forward), joint tous les courriels sélectionnés en
     *   pièces jointes (.eml) à un nouveau message ;
     * - Pour une réponse (Sender / All), répond au premier courriel sélectionné
     *   et joint tous les autres courriels sélectionnés en pièces jointes (.eml).
     * Paramètres :
     *   kind : type d'action (Sender, All ou Forward)
     *   uids : liste ordonnée des identifiants des messages sélectionnés */
    void MainWindow::ReplyToMessages(ReplyKind kind, std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        /* Pour un transfert avec plusieurs courriels sélectionnés, on joint tous les messages en .eml */
        if (kind == ReplyKind::Forward && uids.size() > 1)
        {
            ForwardMessagesAsAttachments(uids);
            return;
        }

        hstring first_uid = uids.front();
        std::vector<hstring> extra_uids(uids.begin() + 1, uids.end());

        auto proceed_with_first = [this, kind, extra_uids](JsonObject const& first_msg)
        {
            auto data = BuildReplyComposeData(kind, first_msg);

            if (extra_uids.empty())
            {
                OpenCompose(data);
                return;
            }

            JsonObject req = BuildMessagesRequest(extra_uids);

            ShowStatus(L"Ajout des autres courriels sélectionnés en pièces jointes…", InfoBarSeverity::Informational);

            m_core->ExportMessagesEml(req.Stringify(),
                [this, data](hstring const& json, hstring const& error) mutable
            {
                if (!error.empty())
                {
                    ShowStatus(L"Pièces jointes : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonArray paths;
                if (JsonArray::TryParse(json, paths))
                {
                    for (auto const& val : paths)
                        data.attachments.push_back(val.GetString());
                }

                HideStatus();
                OpenCompose(data);
            });
        };

        if (m_current_message && m_current_message_uid == first_uid)
        {
            proceed_with_first(m_current_message);
        }
        else
        {
            auto loc = GetMessageLocation(first_uid);
            hstring account = loc.account.empty() ? m_account : loc.account;
            hstring folder = loc.folder.empty() ? m_folder : loc.folder;

            m_core->FetchMessage(account, folder, first_uid,
                [this, proceed_with_first](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Message : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonObject msg;
                if (JsonObject::TryParse(json, msg))
                    proceed_with_first(msg);
            });
        }
    }

    void MainWindow::Reply(ReplyKind kind)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty() && !m_current_message_uid.empty())
            uids.push_back(m_current_message_uid);

        ReplyToMessages(kind, uids);
    }
}
