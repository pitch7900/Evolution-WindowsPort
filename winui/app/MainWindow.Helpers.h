#pragma once

/* Utilitaires partagés par les fichiers MainWindow.*.cpp et MessageWindow
 * (formatage des expéditeurs, dates, listes de confiance, réponses/transferts…). */

#include "ComposeWindow.xaml.h"
#include <cwctype>
#include <string>
#include <shellapi.h>
#include <string_view>
#include <vector>

namespace EvolutionWin::Helpers
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Windows::Data::Json;
    using namespace Windows::Foundation;

    /* Segoe Fluent Icons glyphs (et Segoe UI Symbol pour le sablier de la boîte d'envoi) */
    inline hstring FolderGlyph(hstring const& type)
    {
        if (type == L"inbox") return L"\xE715";
        if (type == L"outbox") return L"\x231B";
        if (type == L"sent") return L"\xE724";
        if (type == L"drafts") return L"\xE70F";
        if (type == L"trash") return L"\xE74D";
        if (type == L"junk") return L"\xE7BA";
        if (type == L"archive") return L"\xE7B8";
        return L"\xE8B7";
    }

    inline FontIcon CreateFolderIcon(hstring const& type)
    {
        FontIcon icon;
        hstring glyph = FolderGlyph(type);
        icon.Glyph(glyph);
        if (glyph == L"\x231B" || glyph == L"\x23F3")
            icon.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe UI Symbol" });
        else
            icon.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe Fluent Icons" });
        return icon;
    }

    inline hstring FormatDate(double unix_time)
    {
        if (unix_time <= 0)
            return L"";

        auto time = winrt::clock::from_time_t(static_cast<time_t>(unix_time));
        Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"shortdate shorttime" };

        return formatter.Format(time);
    }

    /* display name of "Name <address>" */
    inline hstring SenderName(hstring const& from)
    {
        std::wstring_view view{ from };
        auto pos = view.find(L" <");

        if (pos != std::wstring_view::npos && pos > 0)
        {
            auto name = view.substr(0, pos);
            if (name.size() >= 2 && name.front() == L'"' && name.back() == L'"')
                name = name.substr(1, name.size() - 2);
            return hstring{ name };
        }

        return from;
    }

    inline std::wstring ToLower(std::wstring_view text)
    {
        std::wstring lower{ text };
        for (auto& c : lower)
            c = static_cast<wchar_t>(std::towlower(c));
        return lower;
    }

    inline std::wstring_view Trim(std::wstring_view text)
    {
        while (!text.empty() && std::iswspace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && std::iswspace(text.back()))
            text.remove_suffix(1);
        return text;
    }

    /* Remplace les caractères interdits dans un nom de fichier Windows.
     * Paramètres :
     *   name     : nom proposé (nom d'agenda, de carnet d'adresses…)
     *   fallback : nom utilisé si le résultat est vide */
    inline hstring SanitizeFileName(hstring const& name, hstring const& fallback)
    {
        std::wstring clean{ name };
        for (auto& c : clean)
            if (wcschr(L"\\/:*?\"<>|", c) || c < 32)
                c = L'_';
        return clean.empty() ? fallback : hstring{ clean };
    }

    inline bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() &&
            CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()),
                prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    }

    /* Échappe les caractères spéciaux HTML d'une chaîne brute. */
    inline std::wstring EscapeHtml(std::wstring_view text)
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

    /* Préfixe "> " devant chaque ligne */
    inline std::wstring Quote(std::wstring_view text)
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

    /* Liste d'adresses séparées par des virgules sans celle de l'utilisateur */
    inline std::wstring WithoutAddress(std::wstring_view addresses, std::wstring_view own_address)
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

    /* Construit la structure ComposeData (destinataires, objet, corps cité) à
     * partir d'un objet JSON de message pour une réponse ou un transfert.
     * Paramètres :
     *   kind        : type d'action (Sender, All ou Forward)
     *   message     : objet JSON complet du message source
     *   account_uid : identifiant du compte d'envoi
     *   accounts    : liste JSON des comptes configurés (pour l'adresse de l'utilisateur)
     * Retourne : la structure ComposeData prête pour OpenCompose. */
    inline ::EvolutionWin::ComposeData BuildReplyComposeData(
        ::EvolutionWin::ReplyKind kind,
        JsonObject const& message,
        hstring const& account_uid,
        JsonArray const& accounts)
    {
        hstring subject = message.GetNamedString(L"subject", L"");
        hstring from = message.GetNamedString(L"from", L"");
        hstring to = message.GetNamedString(L"to", L"");
        hstring cc = message.GetNamedString(L"cc", L"");
        hstring date = FormatDate(message.GetNamedNumber(L"date", 0));
        hstring text = message.GetNamedString(L"text", L"");
        hstring message_id = message.GetNamedString(L"message_id", L"");

        ::EvolutionWin::ComposeData data;
        data.account = account_uid;
        data.reply = true;

        if (kind == ::EvolutionWin::ReplyKind::Forward)
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

            if (kind == ::EvolutionWin::ReplyKind::All)
            {
                hstring own_address;
                if (accounts)
                {
                    for (auto const& value : accounts)
                    {
                        auto account = value.GetObject();
                        if (account.GetNamedString(L"uid") == account_uid)
                            own_address = account.GetNamedString(L"address", L"");
                    }
                }

                std::wstring others = std::wstring{ to } + (cc.empty() ? L"" : L", " + std::wstring{ cc });
                data.cc = hstring{ WithoutAddress(others, own_address) };
            }

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

    /**
     * @brief Détecte si un texte contient des mentions explicites de pièces jointes.
     * @param text Texte à analyser (sujet ou corps de courriel).
     * @return true si au moins un mot-clé caractéristique est détecté, false sinon.
     */
    inline bool HasAttachmentKeywords(std::wstring_view text)
    {
        if (text.empty())
            return false;

        std::wstring lower;
        lower.reserve(text.size());
        for (wchar_t ch : text)
        {
            lower.push_back(std::towlower(ch));
        }

        static const wchar_t* const patterns[] = {
            L"pièce jointe",
            L"pièces jointes",
            L"piece jointe",
            L"pieces jointes",
            L"ci-joint",
            L"ci-jointe",
            L"ci-joints",
            L"ci-jointes",
            L"en pièce jointe",
            L"en piece jointe",
            L"en pj",
            L"attachment",
            L"attachments",
            L"attached file",
            L"attached files"
        };

        for (const auto* p : patterns)
        {
            if (lower.find(p) != std::wstring::npos)
            {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Construit un document HTML complet et autonome à partir des données d'un courriel.
     * @param from Expéditeur
     * @param to Destinataires
     * @param date Date formatée
     * @param subject Objet du message
     * @param html_body Corps HTML (si présent)
     * @param text_body Corps texte brut (si pas de HTML)
     * @return Document HTML complet avec métadonnées et styles adaptés pour affichage web.
     */
    inline std::wstring BuildStandaloneHtmlMessage(
        std::wstring_view from,
        std::wstring_view to,
        std::wstring_view date,
        std::wstring_view subject,
        std::wstring_view html_body,
        std::wstring_view text_body)
    {
        std::wstring out;
        out += L"<!DOCTYPE html>\n<html>\n<head>\n";
        out += L"<meta charset=\"utf-8\">\n";
        out += L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        out += L"<title>";
        out += subject.empty() ? L"(Sans objet)" : EscapeHtml(subject);
        out += L"</title>\n";
        out += L"<style>\n";
        out += L"body { font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif; margin: 0; padding: 24px; color: #202020; background: #fafafa; }\n";
        out += L".email-header { background: #ffffff; border: 1px solid #e0e0e0; border-radius: 8px; padding: 18px 24px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.04); }\n";
        out += L".email-title { font-size: 20px; font-weight: 600; margin: 0 0 12px 0; color: #111111; }\n";
        out += L".email-meta { font-size: 13px; line-height: 1.6; color: #555555; }\n";
        out += L".email-meta strong { color: #222222; }\n";
        out += L".email-body { background: #ffffff; border: 1px solid #e0e0e0; border-radius: 8px; padding: 24px; min-height: 300px; box-shadow: 0 2px 4px rgba(0,0,0,0.04); }\n";
        out += L"</style>\n</head>\n<body>\n";
        out += L"<div class=\"email-header\">\n";
        out += L"  <h1 class=\"email-title\">" + (subject.empty() ? std::wstring{ L"(Sans objet)" } : EscapeHtml(subject)) + L"</h1>\n";
        out += L"  <div class=\"email-meta\">\n";
        out += L"    <div><strong>De :</strong> " + EscapeHtml(from) + L"</div>\n";
        if (!to.empty())
            out += L"    <div><strong>Ã :</strong> " + EscapeHtml(to) + L"</div>\n";
        if (!date.empty())
            out += L"    <div><strong>Date :</strong> " + EscapeHtml(date) + L"</div>\n";
        out += L"  </div>\n</div>\n";
        out += L"<div class=\"email-body\">\n";
        if (!html_body.empty())
        {
            out += html_body;
        }
        else
        {
            out += L"<pre style=\"font-family: inherit; white-space: pre-wrap; word-wrap: break-word;\">" + EscapeHtml(text_body) + L"</pre>";
        }
        out += L"\n</div>\n</body>\n</html>";
        return out;
    }

    /**
     * @brief Ouvre un message dans le navigateur Web par défaut du système Windows.
     * @param message Données JSON du message contenant subject, from, to, date, html, text.
     * @param fallback_html Document HTML déjà généré pour le lecteur (si disponible).
     * @return true si le lancement a réussi, false sinon.
     */
    inline bool OpenInDefaultBrowser(Windows::Data::Json::JsonObject const& message, std::wstring_view fallback_html = L"")
    {
        if (!message)
            return false;

        std::wstring subject{ message.GetNamedString(L"subject", L"") };
        std::wstring from{ message.GetNamedString(L"from", L"") };
        std::wstring to{ message.GetNamedString(L"to", L"") };
        std::wstring date{ message.GetNamedString(L"date", L"") };
        std::wstring html{ message.GetNamedString(L"html", L"") };
        std::wstring text{ message.GetNamedString(L"text", L"") };

        if (html.empty() && !fallback_html.empty())
            html = std::wstring{ fallback_html };

        std::wstring standalone = BuildStandaloneHtmlMessage(from, to, date, subject, html, text);

        wchar_t temp_dir[MAX_PATH];
        DWORD dir_len = GetTempPathW(MAX_PATH, temp_dir);
        if (dir_len == 0 || dir_len > MAX_PATH)
            return false;

        std::wstring evo_temp = std::wstring{ temp_dir } + L"EvolutionWin";
        CreateDirectoryW(evo_temp.c_str(), nullptr);

        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::wstring filename = evo_temp + L"\\message_" + std::to_wstring(ms) + L".html";

        HANDLE file = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        int utf8_size = WideCharToMultiByte(CP_UTF8, 0, standalone.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8_size > 0)
        {
            std::string utf8_buf;
            utf8_buf.resize(utf8_size - 1);
            WideCharToMultiByte(CP_UTF8, 0, standalone.c_str(), -1, utf8_buf.data(), utf8_size, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(file, utf8_buf.data(), static_cast<DWORD>(utf8_buf.size()), &written, nullptr);
        }
        CloseHandle(file);

        HINSTANCE res = ShellExecuteW(nullptr, L"open", filename.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(res) > 32;
    }
}
