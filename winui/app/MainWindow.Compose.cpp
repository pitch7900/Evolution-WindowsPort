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

    namespace
    {
        bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
        {
            return text.size() >= prefix.size() &&
                CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        }

        /* Échappe les caractères spéciaux HTML d'une chaîne brute. */
        std::wstring EscapeHtml(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size() + 32);

            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'&': out += L"&amp;"; break;
                case L'<': out += L"&lt;"; break;
                case L'>': out += L"&gt;"; break;
                case L'"': out += L"&quot;"; break;
                case L'\r': break;
                case L'\n': out += L"<br>"; break;
                default: out += c; break;
                }
            }

            return out;
        }

        /* "> " before each line */
        std::wstring Quote(std::wstring_view text)
        {
            std::wstring quoted = L"> ";

            for (auto c : text)
            {
                if (c == L'\r')
                    continue;
                quoted += c;
                if (c == L'\n')
                    quoted += L"> ";
            }

            return quoted;
        }

        /* comma-separated addresses without those containing own_address */
        std::wstring WithoutAddress(std::wstring_view addresses, std::wstring_view own_address)
        {
            std::wstring result;
            size_t start = 0;

            while (start <= addresses.size())
            {
                size_t end = addresses.find(L',', start);
                if (end == std::wstring_view::npos)
                    end = addresses.size();

                std::wstring part{ addresses.substr(start, end - start) };
                part.erase(0, part.find_first_not_of(L' '));

                std::wstring lower_part = part, lower_own{ own_address };
                CharLowerBuffW(lower_part.data(), static_cast<DWORD>(lower_part.size()));
                CharLowerBuffW(lower_own.data(), static_cast<DWORD>(lower_own.size()));

                if (!part.empty() && (lower_own.empty() || lower_part.find(lower_own) == std::wstring::npos))
                {
                    if (!result.empty())
                        result += L", ";
                    result += part;
                }

                start = end + 1;
            }

            return result;
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
        hstring subject = message.GetNamedString(L"subject", L"");
        hstring from = message.GetNamedString(L"from", L"");
        hstring to = message.GetNamedString(L"to", L"");
        hstring cc = message.GetNamedString(L"cc", L"");
        hstring date = FormatDate(message.GetNamedNumber(L"date", 0));
        hstring text = message.GetNamedString(L"text", L"");
        hstring message_id = message.GetNamedString(L"message_id", L"");

        ::EvolutionWin::ComposeData data;
        data.account = m_account;
        data.reply = true;

        if (kind == ReplyKind::Forward)
        {
            data.subject = StartsWithNoCase(subject, L"TR:") || StartsWithNoCase(subject, L"Fwd:") ? subject : L"TR: " + subject;
            data.body = L"\r\r-------- Message transféré --------\rDe : " + from + L"\rDate : " + date +
                L"\rObjet : " + subject + L"\rÀ : " + to + (cc.empty() ? hstring{} : L"\rCc : " + cc) +
                L"\r\r" + text;
            data.html_body = hstring{
                L"<div><br></div><hr style=\"border:none;border-top:1px solid #ccc;\">"
                L"<div style=\"color:#444;font-size:10pt;\">"
                L"<b>-------- Message transféré --------</b><br>"
                L"<b>De :</b> " + EscapeHtml(from) + L"<br>"
                L"<b>Date :</b> " + EscapeHtml(date) + L"<br>"
                L"<b>Objet :</b> " + EscapeHtml(subject) + L"<br>"
                L"<b>À :</b> " + EscapeHtml(to) +
                (cc.empty() ? std::wstring{} : L"<br><b>Cc :</b> " + EscapeHtml(cc)) +
                L"</div><br><div>" + EscapeHtml(text) + L"</div>" };
        }
        else
        {
            hstring reply_to = message.GetNamedString(L"reply_to", L"");

            data.to = reply_to.empty() ? from : reply_to;
            data.subject = StartsWithNoCase(subject, L"Re:") ? subject : L"Re: " + subject;
            data.in_reply_to = message_id;

            hstring references = message.GetNamedString(L"references", L"");
            data.references = references.empty() ? message_id : references + L" " + message_id;

            if (kind == ReplyKind::All)
            {
                hstring own_address;
                for (auto const& value : m_accounts)
                {
                    auto account = value.GetObject();
                    if (account.GetNamedString(L"uid") == m_account)
                        own_address = account.GetNamedString(L"address", L"");
                }

                std::wstring others = std::wstring{ to } + (cc.empty() ? L"" : L", " + std::wstring{ cc });
                data.cc = hstring{ WithoutAddress(others, own_address) };
            }

            /* the text box uses \r line ends */
            std::wstring quoted = Quote(text);
            for (auto& c : quoted)
                if (c == L'\n')
                    c = L'\r';

            data.body = L"\r\rLe " + date + L", " + from + L" a écrit :\r" + hstring{ quoted };
            data.html_body = hstring{
                L"<div><br></div><div>Le " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" a écrit :</div>"
                L"<blockquote>" + EscapeHtml(text) + L"</blockquote>" };
        }

        return data;
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

        JsonObject req;
        req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uid_array;
        for (auto const& u : uids)
            uid_array.Append(JsonValue::CreateStringValue(u));
        req.SetNamedValue(L"uids", uid_array);

        hstring account = m_account;
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
            if (!first_subject.empty())
            {
                data.subject = StartsWithNoCase(first_subject, L"TR:") || StartsWithNoCase(first_subject, L"Fwd:")
                    ? first_subject
                    : L"TR: " + first_subject;
            }
            else
            {
                data.subject = L"TR: Messages transférés";
            }

            for (auto const& val : paths)
                data.attachments.push_back(val.GetString());

            StatusBar().IsOpen(false);
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

        /* Pour le transfert, on joint le ou les mails sélectionnés en pièces jointes (.eml) */
        if (kind == ReplyKind::Forward)
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

            JsonObject req;
            req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
            req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
            JsonArray uid_array;
            for (auto const& u : extra_uids)
                uid_array.Append(JsonValue::CreateStringValue(u));
            req.SetNamedValue(L"uids", uid_array);

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

                StatusBar().IsOpen(false);
                OpenCompose(data);
            });
        };

        if (m_current_message && m_current_message_uid == first_uid)
        {
            proceed_with_first(m_current_message);
        }
        else
        {
            m_core->FetchMessage(m_account, m_folder, first_uid,
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
