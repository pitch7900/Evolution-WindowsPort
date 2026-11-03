#pragma once

/* Utilitaires partagés par les fichiers MainWindow.*.cpp et MessageWindow
 * (formatage des expéditeurs, dates, listes de confiance, réponses/transferts…). */

#include "ComposeWindow.xaml.h"
#include "Settings.h"
#include <cwctype>
#include <string>
#include <shellapi.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#include <string_view>
#include <vector>
#include <filesystem>

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
        /**
     * @brief Verifie si un objet commence par un prefixe de reponse standard ou multilingue (Re:, AW:, Antw:, R:, SV:...).
     * @param subject Objet du courriel.
     * @return true si un prefixe de reponse est detecte, false sinon.
     */
    inline bool HasReplyPrefix(std::wstring_view subject)
    {
        static const std::vector<std::wstring_view> prefixes = {
            L"re:", L"aw:", L"antw:", L"r:", L"rif:", L"sv:", L"vs:", L"res:"
        };
        for (auto const& p : prefixes)
        {
            if (StartsWithNoCase(subject, p))
                return true;
        }
        return false;
    }

    /**
     * @brief Verifie si un objet commence par un prefixe de transfert standard ou multilingue (TR:, Fwd:, FW:, WG:, I:, ENC:...).
     * @param subject Objet du courriel.
     * @return true si un prefixe de transfert est detecte, false sinon.
     */
    inline bool HasForwardPrefix(std::wstring_view subject)
    {
        static const std::vector<std::wstring_view> prefixes = {
            L"tr:", L"fwd:", L"fw:", L"wg:", L"doorst:", L"i:", L"vb:", L"vl:", L"enc:"
        };
        for (auto const& p : prefixes)
        {
            if (StartsWithNoCase(subject, p))
                return true;
        }
        return false;
    }

    /**
     * @brief Formate l'objet pour une reponse selon le prefixe configure.
     * @param subject Objet d'origine.
     * @param prefix Prefixe a appliquer si absent (defaut L"Re: ").
     * @return Objet formate avec le prefixe de reponse.
     */
    inline std::wstring FormatReplySubject(std::wstring_view subject, std::wstring_view prefix = L"Re: ")
    {
        if (HasReplyPrefix(subject))
            return std::wstring{ subject };
        std::wstring p{ prefix.empty() ? L"Re: " : prefix };
        if (p.back() != L' ' && p.back() != L':')
            p += L": ";
        else if (p.back() == L':')
            p += L" ";
        return p + std::wstring{ subject };
    }

    /**
     * @brief Formate l'objet pour un transfert selon le prefixe configure.
     * @param subject Objet d'origine.
     * @param prefix Prefixe a appliquer si absent (defaut L"TR: ").
     * @return Objet formate avec le prefixe de transfert.
     */
    inline std::wstring FormatForwardSubject(std::wstring_view subject, std::wstring_view prefix = L"TR: ")
    {
        if (HasForwardPrefix(subject))
            return std::wstring{ subject };
        std::wstring p{ prefix.empty() ? L"TR: " : prefix };
        if (p.back() != L' ' && p.back() != L':')
            p += L": ";
        else if (p.back() == L':')
            p += L" ";
        return p + std::wstring{ subject };
    }

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

        std::wstring lang{ ::EvolutionWin::Settings::Instance().GetString(L"general.language", L"system") };
        if (lang.empty() || lang == L"system")
            lang = L"fr";

        if (kind == ::EvolutionWin::ReplyKind::Forward)
        {
            std::wstring fwd_p{ ::EvolutionWin::Settings::Instance().GetString(L"mail.forward-prefix", L"TR: ") };
            data.subject = hstring{ FormatForwardSubject(subject, fwd_p) };

            std::wstring header_title = L"-------- Message transféré --------";
            std::wstring lbl_from = L"De : ";
            std::wstring lbl_date = L"Date : ";
            std::wstring lbl_subj = L"Objet : ";
            std::wstring lbl_to = L"À : ";
            std::wstring lbl_cc = L"Cc : ";

            if (lang == L"en" || lang.find(L"en-") == 0)
            {
                header_title = L"-------- Forwarded Message --------";
                lbl_from = L"From: "; lbl_date = L"Date: "; lbl_subj = L"Subject: "; lbl_to = L"To: "; lbl_cc = L"Cc: ";
            }
            else if (lang == L"de" || lang.find(L"de-") == 0)
            {
                header_title = L"-------- Weitergeleitete Nachricht --------";
                lbl_from = L"Von: "; lbl_date = L"Datum: "; lbl_subj = L"Betreff: "; lbl_to = L"An: "; lbl_cc = L"Cc: ";
            }
            else if (lang == L"it" || lang.find(L"it-") == 0)
            {
                header_title = L"-------- Messaggio Inoltrato --------";
                lbl_from = L"Da: "; lbl_date = L"Data: "; lbl_subj = L"Oggetto: "; lbl_to = L"A: "; lbl_cc = L"Cc: ";
            }
            else if (lang == L"es" || lang.find(L"es-") == 0)
            {
                header_title = L"-------- Mensaje reenviado --------";
                lbl_from = L"De: "; lbl_date = L"Fecha: "; lbl_subj = L"Asunto: "; lbl_to = L"Para: "; lbl_cc = L"Cc: ";
            }

            data.body = L"\r\r" + hstring{ header_title } + L"\r" + hstring{ lbl_from } + from + L"\r" + hstring{ lbl_date } + date +
                L"\r" + hstring{ lbl_subj } + subject + L"\r" + hstring{ lbl_to } + to + (cc.empty() ? hstring{} : L"\r" + hstring{ lbl_cc } + cc) +
                L"\r\r" + text;

            data.html_body = hstring{
                L"<div><br></div><hr style=\"border:none;border-top:1px solid #ccc;\">"
                L"<div style=\"color:#444;font-size:10pt;\">"
                L"<b>" + header_title + L"</b><br>"
                L"<b>" + lbl_from + L"</b>" + EscapeHtml(from) + L"<br>"
                L"<b>" + lbl_date + L"</b>" + EscapeHtml(date) + L"<br>"
                L"<b>" + lbl_subj + L"</b>" + EscapeHtml(subject) + L"<br>"
                L"<b>" + lbl_to + L"</b>" + EscapeHtml(to) +
                (cc.empty() ? std::wstring{} : L"<br><b>" + lbl_cc + L"</b>" + EscapeHtml(cc)) +
                L"</div><br><div>" + EscapeHtml(text) + L"</div>" };
        }
        else
        {
            hstring reply_to = message.GetNamedString(L"reply_to", L"");

            data.to = reply_to.empty() ? from : reply_to;
            std::wstring reply_p{ ::EvolutionWin::Settings::Instance().GetString(L"mail.reply-prefix", L"Re: ") };
            data.subject = hstring{ FormatReplySubject(subject, reply_p) };
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
            for (auto& ch : quoted)
                if (ch == L'\n')
                    ch = L'\r';

            std::wstring quote_lead = L"Le " + std::wstring{ date } + L", " + std::wstring{ from } + L" a écrit :";
            std::wstring html_quote_lead = L"Le " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" a écrit :";

            if (lang == L"en" || lang.find(L"en-") == 0)
            {
                quote_lead = L"On " + std::wstring{ date } + L", " + std::wstring{ from } + L" wrote:";
                html_quote_lead = L"On " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" wrote:";
            }
            else if (lang == L"de" || lang.find(L"de-") == 0)
            {
                quote_lead = L"Am " + std::wstring{ date } + L" schrieb " + std::wstring{ from } + L":";
                html_quote_lead = L"Am " + EscapeHtml(date) + L" schrieb " + EscapeHtml(from) + L":";
            }
            else if (lang == L"it" || lang.find(L"it-") == 0)
            {
                quote_lead = L"Il " + std::wstring{ date } + L", " + std::wstring{ from } + L" ha scritto:";
                html_quote_lead = L"Il " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" ha scritto:";
            }
            else if (lang == L"es" || lang.find(L"es-") == 0)
            {
                quote_lead = L"El " + std::wstring{ date } + L", " + std::wstring{ from } + L" escribió:";
                html_quote_lead = L"El " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" escribió:";
            }

            data.body = L"\r\r" + hstring{ quote_lead } + L"\r" + hstring{ quoted };
            data.html_body = hstring{
                L"<div><br></div><div>" + html_quote_lead + L"</div>"
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

    /**
     * @brief Extrait et nettoie le texte d'un courriel pour la lecture vocale (SpeechSynthesis).
     * @param from Nom ou adresse de l'expéditeur.
     * @param subject Objet du message.
     * @param text Corps en texte brut si disponible.
     * @param html Corps HTML si disponible (servant de repli si le texte brut est absent).
     * @return Texte fluide débarrassé de balises et prêt pour la synthèse vocale.
     */
    inline std::wstring ExtractPlainTextForSpeech(
        std::wstring const& from,
        std::wstring const& subject,
        std::wstring const& text,
        std::wstring const& html)
    {
        std::wstring speech;
        if (!from.empty())
        {
            speech += L"Message de " + std::wstring{ SenderName(hstring{ from }) } + L".\n";
        }
        if (!subject.empty())
        {
            speech += L"Objet : " + subject + L".\n\n";
        }

        std::wstring body_clean;
        if (!text.empty())
        {
            body_clean = text;
        }
        else if (!html.empty())
        {
            bool in_tag = false;
            for (size_t i = 0; i < html.size(); ++i)
            {
                wchar_t c = html[i];
                if (c == L'<')
                {
                    in_tag = true;
                    if (i + 3 <= html.size() && (_wcsnicmp(&html[i], L"<br", 3) == 0 || _wcsnicmp(&html[i], L"</p", 3) == 0 || (i + 5 <= html.size() && _wcsnicmp(&html[i], L"</div", 5) == 0)))
                    {
                        body_clean += L'\n';
                    }
                }
                else if (c == L'>')
                {
                    in_tag = false;
                }
                else if (!in_tag)
                {
                    body_clean += c;
                }
            }
        }

        auto replace_all = [](std::wstring& str, const std::wstring& from_str, const std::wstring& to_str) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from_str, start_pos)) != std::wstring::npos) {
                str.replace(start_pos, from_str.length(), to_str);
                start_pos += to_str.length();
            }
        };
        replace_all(body_clean, L"&nbsp;", L" ");
        replace_all(body_clean, L"&amp;", L"&");
        replace_all(body_clean, L"&lt;", L"<");
        replace_all(body_clean, L"&gt;", L">");
        replace_all(body_clean, L"&quot;", L"\"");
        replace_all(body_clean, L"&#39;", L"'");

        speech += body_clean;
        return speech;
    }

    /**
     * @brief Extrait le texte d'un message pour la synthèse vocale depuis son objet JSON.
     * @param message Objet JSON représentant le message.
     * @param fallback_html Chaîne HTML alternative en cas d'absence dans l'objet JSON.
     * @return Texte prêt pour la lecture vocale.
     */
    inline std::wstring ExtractPlainTextForSpeech(
        Windows::Data::Json::JsonObject const& message,
        winrt::hstring const& fallback_html = L"")
    {
        if (!message)
            return {};
        std::wstring from{ message.GetNamedString(L"from", L"") };
        std::wstring subject{ message.GetNamedString(L"subject", L"") };
        std::wstring text{ message.GetNamedString(L"text", L"") };
        std::wstring html{ message.GetNamedString(L"html", L"") };
        if (html.empty() && !fallback_html.empty())
            html = std::wstring{ fallback_html };

        return ExtractPlainTextForSpeech(from, subject, text, html);
    }

    /**
     * @brief Catégories d'aperçu pour les pièces jointes d'un message.
     */
    enum class AttachmentPreviewKind
    {
        Image,       ///< Fichier image décodable directement par BitmapImage
        Text,        ///< Fichier texte brut, code source, CSV, JSON, logs
        Unsupported  ///< Type non pris en charge ou exécutable à risque
    };

    /**
     * @brief Détermine la catégorie d'aperçu sécurisé selon l'extension du nom de fichier.
     * @param filename Nom ou chemin d'accès au fichier joint.
     * @return Catégorie d'aperçu (Image, Text ou Unsupported).
     */
    inline AttachmentPreviewKind DetectAttachmentPreviewKind(std::wstring_view filename)
    {
        auto dot = filename.rfind(L'.');
        if (dot == std::wstring_view::npos || dot + 1 >= filename.size())
            return AttachmentPreviewKind::Unsupported;

        std::wstring ext = ToLower(filename.substr(dot));

        // Formats d'images pris en charge nativement par WinUI 3 (BitmapImage / WIC)
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" ||
            ext == L".bmp" || ext == L".webp" || ext == L".ico" || ext == L".tif" || ext == L".tiff")
        {
            return AttachmentPreviewKind::Image;
        }

        // Fichiers texte brut affichés sans interprétation de code
        if (ext == L".txt" || ext == L".csv" || ext == L".tsv" || ext == L".log" ||
            ext == L".json" || ext == L".xml" || ext == L".ini" || ext == L".cfg" ||
            ext == L".conf" || ext == L".md" || ext == L".patch" || ext == L".diff" ||
            ext == L".yaml" || ext == L".yml" || ext == L".sql" || ext == L".c" ||
            ext == L".cpp" || ext == L".h" || ext == L".hpp" || ext == L".cs" ||
            ext == L".js" || ext == L".ts" || ext == L".py")
        {
            return AttachmentPreviewKind::Text;
        }

        return AttachmentPreviewKind::Unsupported;
    }

    /**
     * @brief Formate un libellé lisible de suggestion de contact pour l'AutoSuggestBox ("Nom <email>").
     * @param full_name Nom complet du contact.
     * @param email Adresse électronique principale.
     * @return Libellé complet ou repli sur l'un des deux champs.
     */
    inline std::wstring FormatContactSuggestion(std::wstring_view full_name, std::wstring_view email)
    {
        if (full_name.empty() && email.empty())
            return L"(Sans nom)";
        if (email.empty())
            return std::wstring{ full_name };
        if (full_name.empty())
            return std::wstring{ email };
        return std::wstring{ full_name } + L" <" + std::wstring{ email } + L">";
    }
    inline uint64_t PurgeDirectoryFiles(std::wstring const& dir_path)
    {
        uint64_t bytes_freed = 0;
        try {
            std::filesystem::path p(dir_path);
            if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                for (auto const& entry : std::filesystem::directory_iterator(p)) {
                    if (entry.is_regular_file()) {
                        std::error_code ec;
                        auto sz = entry.file_size(ec);
                        if (!ec) bytes_freed += sz;
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
        } catch (...) {}
        return bytes_freed;
    }
    /**
     * @brief Émet le signal sonore configuré pour les notifications de nouveau courrier.
     * @param sound_kind Type de son ("windows", "chime", "custom", "none").
     * @param custom_path Chemin d'accès au fichier .wav personnalisé si sound_kind == "custom".
     */
    inline void PlayNewMailSound(std::wstring_view sound_kind, std::wstring_view custom_path = {})
    {
        if (sound_kind == L"none")
            return;

        if (sound_kind == L"custom" && !custom_path.empty())
        {
            if (::GetFileAttributesW(std::wstring{ custom_path }.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ::PlaySoundW(std::wstring{ custom_path }.c_str(), NULL, SND_FILENAME | SND_ASYNC);
                return;
            }
        }

        if (sound_kind == L"chime")
        {
            ::PlaySoundW(L"Notification.Default", NULL, SND_ALIAS | SND_ASYNC);
            return;
        }

        // Par défaut : son système Windows de nouveau message
        if (!::PlaySoundW(L"MailBeep", NULL, SND_ALIAS | SND_ASYNC))
        {
            ::MessageBeep(MB_ICONASTERISK);
        }
    }

    /**
     * @brief Calcule le décalage UTC en heures pour un fuseau horaire spécifié.
     * @param tz Identifiant ou nom du fuseau horaire (ex: "UTC", "America/New_York", etc.).
     * @param is_dst true si l'heure d'été est active, false sinon.
     * @return Décalage en heures par rapport à UTC.
     */
    inline int GetTimezoneUtcOffsetHours(std::wstring_view tz, bool is_dst = false)
    {
        if (tz == L"UTC") return 0;
        if (tz == L"Europe/London") return is_dst ? 1 : 0;
        if (tz == L"America/New_York") return is_dst ? -4 : -5;
        if (tz == L"America/Los_Angeles") return is_dst ? -7 : -8;
        if (tz == L"Europe/Paris") return is_dst ? 2 : 1;
        if (tz == L"Asia/Tokyo") return 9;
        return 0;
    }

    /**
     * @brief Renvoie le libellé court d'un fuseau horaire (ex: "UTC", "NYC", "PAR").
     * @param tz Identifiant du fuseau horaire.
     * @return Libellé court de 3 ou 4 lettres.
     */
    inline std::wstring GetTimezoneShortLabel(std::wstring_view tz)
    {
        if (tz == L"UTC") return L"UTC";
        if (tz == L"America/New_York") return L"NYC";
        if (tz == L"America/Los_Angeles") return L"LA";
        if (tz == L"Europe/Paris") return L"PAR";
        if (tz == L"Europe/London") return L"LON";
        if (tz == L"Asia/Tokyo") return L"TYO";
        return L"TZ2";
    }

    /**
     * @brief Calcule l'heure correspondante dans un second fuseau horaire pour une heure locale donnée.
     * @param local_hour Heure locale (0 à 23).
     * @param local_offset Décalage UTC de l'heure locale en heures.
     * @param target_offset Décalage UTC du second fuseau en heures.
     * @return Heure dans le second fuseau (0 à 23).
     */
    inline int CalculateEquivalentHour(int local_hour, int local_offset, int target_offset)
    {
        int diff = target_offset - local_offset;
        int target = (local_hour + diff) % 24;
        if (target < 0)
            target += 24;
        return target;
    }

    /**
     * @brief Détermine le décalage actuel en heures entre l'heure locale et le temps universel UTC.
     * @return Décalage en heures par rapport à UTC.
     */
    inline int GetLocalUtcOffsetHours()
    {
        time_t now = time(nullptr);
        struct tm local_tm{}, gm_tm{};
        localtime_s(&local_tm, &now);
        gmtime_s(&gm_tm, &now);

        int offset = local_tm.tm_hour - gm_tm.tm_hour;
        if (local_tm.tm_mday != gm_tm.tm_mday)
        {
            if (local_tm.tm_mday > gm_tm.tm_mday || (local_tm.tm_mon > gm_tm.tm_mon && local_tm.tm_mday == 1))
                offset += 24;
            else
                offset -= 24;
        }
        return offset;
    }

    /**
     * @brief Vérifie si un message correspond à une requête de recherche textuelle,
     *        y compris les filtres par catégorie avec préfixe 'cat:' ou 'category:'.
     * @param query Texte de recherche saisi.
     * @param subject Objet du message.
     * @param from Expéditeur du message.
     * @param labels Liste des catégories ou étiquettes associées.
     * @return true si le message correspond à la recherche, false sinon.
     */
    /**
     * @brief Verifie si l'application est configuree pour demarrer automatiquement avec Windows.
     * @return true si la cle de registre HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run contient EvolutionWin.
     */
    inline bool IsAutoStartupEnabled()
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            wchar_t buffer[MAX_PATH] = { 0 };
            DWORD size = sizeof(buffer);
            DWORD type = 0;
            LONG res = RegQueryValueExW(hKey, L"EvolutionWin", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
            RegCloseKey(hKey);
            return (res == ERROR_SUCCESS);
        }
        return false;
    }

    /**
     * @brief Configure le demarrage automatique d'EvolutionWin lors de l'ouverture de session Windows.
     * @param enable true pour inscrire l'application dans HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run, false pour supprimer l'entree.
     * @return true si l'operation dans le registre Windows a reussi, false en cas d'erreur.
     */
    inline bool SetAutoStartupEnabled(bool enable)
    {
        HKEY hKey = nullptr;
        LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
        if (res != ERROR_SUCCESS)
            return false;

        if (enable)
        {
            wchar_t exe_path[MAX_PATH] = { 0 };
            GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            std::wstring command = L"\"" + std::wstring{ exe_path } + L"\"";
            res = RegSetValueExW(hKey, L"EvolutionWin", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            res = RegDeleteValueW(hKey, L"EvolutionWin");
            if (res == ERROR_FILE_NOT_FOUND)
                res = ERROR_SUCCESS;
        }
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }

    inline bool MatchesMessageSearch(
        std::wstring_view query,
        std::wstring_view subject,
        std::wstring_view from,
        std::vector<std::wstring> const& labels)
    {
        if (query.empty())
            return true;

        std::wstring needle{ query };
        for (auto& ch : needle) ch = static_cast<wchar_t>(std::towlower(ch));

        std::wstring haystack = std::wstring{ subject } + L" " + std::wstring{ from };
        for (auto const& label : labels)
        {
            haystack += L" cat:" + label + L" category:" + label + L" " + label;
        }
        for (auto& ch : haystack) ch = static_cast<wchar_t>(std::towlower(ch));

        return haystack.find(needle) != std::wstring::npos;
    }

    /**
     * @brief Description d'une catégorie de classement et de sa couleur associée.
     */
    struct CategoryDefinition
    {
        std::wstring name;       ///< Libellé de la catégorie (ex. "Catégorie rouge", "Travail", "Important")
        uint8_t r{ 0 };          ///< Composante rouge de la couleur (0-255)
        uint8_t g{ 0 };          ///< Composante verte de la couleur (0-255)
        uint8_t b{ 0 };          ///< Composante bleue de la couleur (0-255)
        std::wstring shortcut;   ///< Raccourci clavier rapide (ex. "Ctrl+F2")
    };

    /**
     * @brief Renvoie la liste standard des catégories prédéfinies avec leurs couleurs Fluent.
     * @return Vecteur contenant la liste ordonnée des catégories.
     */
    inline std::vector<CategoryDefinition> GetDefaultCategories()
    {
        return {
            { L"Catégorie rouge",    232, 17,  35,  L"Ctrl+F2" },
            { L"Catégorie orange",   255, 140, 0,   L"Ctrl+F3" },
            { L"Catégorie jaune",    255, 185, 0,   L"Ctrl+F4" },
            { L"Catégorie verte",    16,  124, 65,  L"Ctrl+F5" },
            { L"Catégorie bleue",    0,   120, 212, L"Ctrl+F6" },
            { L"Catégorie violette", 136, 23,  152, L"Ctrl+F7" },
            { L"Important",          209, 52,  56,  L"" },
            { L"Travail",            0,   99,  177, L"" },
            { L"Personnel",          0,   130, 114, L"" },
            { L"À suivre",           227, 0,   140, L"" }
        };
    }

    /**
     * @brief Obtient la couleur RVB associée à une catégorie donnée.
     * @param category_name Nom de la catégorie recherchée.
     * @param out_r Référence de sortie pour la composante rouge.
     * @param out_g Référence de sortie pour la composante verte.
     * @param out_b Référence de sortie pour la composante bleue.
     * @return true si la catégorie correspond à une catégorie standard, false si une couleur de repli a été calculée.
     */
    inline bool GetCategoryColor(std::wstring_view category_name, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b)
    {
        auto cats = GetDefaultCategories();
        for (auto const& c : cats)
        {
            if (c.name.size() == category_name.size())
            {
                bool eq = true;
                for (size_t i = 0; i < c.name.size(); ++i)
                {
                    if (std::towlower(c.name[i]) != std::towlower(category_name[i]))
                    {
                        eq = false;
                        break;
                    }
                }
                if (eq)
                {
                    out_r = c.r; out_g = c.g; out_b = c.b;
                    return true;
                }
            }
        }

        uint32_t hash = 5381;
        for (wchar_t ch : category_name)
            hash = ((hash << 5) + hash) + ch;

        static const uint8_t fallback_palette[][3] = {
            { 0, 120, 212 }, { 16, 124, 65 }, { 255, 140, 0 }, { 232, 17, 35 },
            { 136, 23, 152 }, { 0, 153, 188 }, { 135, 100, 184 }, { 180, 0, 158 }
        };
        size_t idx = hash % (sizeof(fallback_palette) / sizeof(fallback_palette[0]));
        out_r = fallback_palette[idx][0];
        out_g = fallback_palette[idx][1];
        out_b = fallback_palette[idx][2];
        return false;
    }

    /**
     * @brief Recherche la position d'un en-tête RFC 822 ou d'une frontière MIME dans un flux brut.
     * @param raw_content Contenu textuel intégral du message RFC 822.
     * @param needle Motif textuel recherché (insensible à la casse).
     * @return Indice du début de l'occurrence trouvée, ou std::wstring::npos si absente.
     */
    inline size_t FindInRawMime(std::wstring_view raw_content, std::wstring_view needle)
    {
        if (needle.empty() || raw_content.empty())
            return std::wstring::npos;

        std::wstring haystack_lower{ raw_content };
        for (auto& ch : haystack_lower) ch = static_cast<wchar_t>(std::towlower(ch));

        std::wstring needle_lower{ needle };
        for (auto& ch : needle_lower) ch = static_cast<wchar_t>(std::towlower(ch));

        return haystack_lower.find(needle_lower);
    }

    /**
     * @brief Définition structurée d'un raccourci clavier de l'application.
     */
    struct ShortcutItem
    {
        std::wstring category;        ///< Catégorie fonctionnelle (Navigation, Courrier, Calendrier, Contacts, Tâches)
        std::wstring action;          ///< Libellé de l'action utilisateur
        std::wstring keys_outlook;    ///< Combinaison de touches sous le profil Outlook (standard Windows)
        std::wstring keys_evolution;  ///< Combinaison de touches sous le profil Evolution GNOME
    };

    /**
     * @brief Fournit la liste de l'ensemble des raccourcis clavier disponibles.
     * @return Vecteur contenant les définitions de tous les raccourcis.
     */
    inline std::vector<ShortcutItem> GetKeyboardShortcuts()
    {
        return {
            /* Navigation globale */
            { L"Navigation", L"Aller au module Courrier", L"Ctrl+Alt+1", L"Ctrl+1" },
            { L"Navigation", L"Aller au module Calendrier", L"Ctrl+Alt+2", L"Ctrl+2" },
            { L"Navigation", L"Aller au module Contacts", L"Ctrl+Alt+3", L"Ctrl+3" },
            { L"Navigation", L"Aller au module Tâches", L"Ctrl+Alt+4", L"Ctrl+4" },
            { L"Navigation", L"Rechercher dans les éléments", L"Ctrl+E", L"Ctrl+F" },
            { L"Navigation", L"Afficher les raccourcis clavier", L"F1", L"F1" },

            /* Courrier & Rédaction */
            { L"Courrier", L"Nouveau message", L"Ctrl+N", L"Ctrl+N" },
            { L"Courrier", L"Répondre à l'expéditeur", L"Ctrl+R", L"Ctrl+R" },
            { L"Courrier", L"Répondre à tous les destinataires", L"Ctrl+Maj+R", L"Ctrl+Maj+R" },
            { L"Courrier", L"Transférer le message", L"Ctrl+F", L"Ctrl+J" },
            { L"Courrier", L"Transférer en pièce jointe (.eml)", L"Ctrl+Maj+F", L"Ctrl+Alt+F" },
            { L"Courrier", L"Enregistrer sous le format .eml", L"Ctrl+S", L"Ctrl+S" },
            { L"Courrier", L"Afficher la source brute MIME (RFC 822)", L"Ctrl+Maj+U", L"Ctrl+U" },
            { L"Courrier", L"Attribuer rapidement une catégorie", L"Ctrl+F2", L"Ctrl+F2" },
            { L"Courrier", L"Marquer comme lu", L"Ctrl+Q", L"Ctrl+K" },
            { L"Courrier", L"Marquer comme non lu", L"Ctrl+U", L"Ctrl+Maj+K" },
            { L"Courrier", L"Envoyer et recevoir (synchronisation)", L"F9", L"F9" },
            { L"Courrier", L"Déplacer vers la Corbeille", L"Suppr", L"Suppr" },
            { L"Courrier", L"Supprimer définitivement", L"Maj+Suppr", L"Maj+Suppr" },
            { L"Courrier", L"Archiver le message sélectionné", L"Retour arrière", L"Ctrl+Alt+A" },
            { L"Courrier", L"Imprimer le message", L"Ctrl+P", L"Ctrl+P" },

            /* Calendrier & Rendez-vous */
            { L"Calendrier", L"Nouveau rendez-vous / événement", L"Ctrl+N", L"Ctrl+Maj+E" },
            { L"Calendrier", L"Atteindre une date précise", L"Ctrl+G", L"Ctrl+T" },
            { L"Calendrier", L"Revenir à la date du jour", L"Ctrl+T", L"Ctrl+J" },
            { L"Calendrier", L"Actualiser le calendrier", L"F5", L"F5" },

            /* Contacts */
            { L"Contacts", L"Nouveau contact", L"Ctrl+N", L"Ctrl+Maj+C" },
            { L"Contacts", L"Écrire au contact sélectionné", L"Ctrl+Maj+C", L"Ctrl+N" },

            /* Tâches & To-Do */
            { L"Tâches", L"Nouvelle tâche", L"Ctrl+Maj+K", L"Ctrl+Maj+T" },
            { L"Tâches", L"Afficher / masquer le volet des tâches", L"Alt+F2", L"F7" }
        };
    }

    /**
     * @brief Filtre la liste des raccourcis selon une requête textuelle (catégorie, action ou combinaison de touches).
     * @param all Liste complète des raccourcis.
     * @param query Chaîne de recherche (insensible à la casse).
     * @return Liste filtrée des raccourcis correspondants.
     */
    inline std::vector<ShortcutItem> FilterKeyboardShortcuts(std::vector<ShortcutItem> const& all, std::wstring_view query)
    {
        if (query.empty())
            return all;

        std::wstring q_lower{ query };
        for (auto& ch : q_lower) ch = static_cast<wchar_t>(std::towlower(ch));

        std::vector<ShortcutItem> result;
        for (auto const& item : all)
        {
            auto match = [&](std::wstring const& str) {
                std::wstring s_lower{ str };
                for (auto& ch : s_lower) ch = static_cast<wchar_t>(std::towlower(ch));
                return s_lower.find(q_lower) != std::wstring::npos;
            };

            if (match(item.category) || match(item.action) || match(item.keys_outlook) || match(item.keys_evolution))
            {
                result.push_back(item);
            }
        }
        return result;
    }

    /**
     * @brief Obtient le chemin absolu du dossier temporaire réservé à l'exportation par glisser-déposer (Drag & Drop).
     * @return Chemin complet terminé par une barre oblique inverse.
     */
    inline std::wstring GetDragDropTempDir()
    {
        wchar_t temp_buf[MAX_PATH]{};
        DWORD len = GetTempPathW(MAX_PATH, temp_buf);
        std::wstring path = (len > 0) ? std::wstring(temp_buf, len) : L"C:\\Temp\\";
        path += L"EvolutionWin\\dragdrop\\";
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }

    /**
     * @brief Convertit le choix « Taille de texte pour la lecture » (Paramètres › Apparence) en facteur de zoom du lecteur.
     * @param name Valeur mémorisée : « small », « normal », « large » ou « extralarge ».
     * @return Facteur de zoom correspondant (0.85, 1.0, 1.25 ou 1.5) ; 1.0 pour toute valeur inconnue.
     */
    inline double FontSizePresetToZoom(std::wstring_view name)
    {
        if (name == L"small") return 0.85;
        if (name == L"large") return 1.25;
        if (name == L"extralarge") return 1.5;
        return 1.0;
    }

    /**
     * @brief Borne le facteur d'échelle de zoom du volet de lecture entre 50% et 300%.
     * @param factor Facteur de zoom souhaité (1.0 = 100%).
     * @return Facteur de zoom normalisé et sécurisé.
     */
    inline double ClampZoomFactor(double factor)
    {
        if (factor < 0.5) return 0.5;
        if (factor > 3.0) return 3.0;
        return factor;
    }

    /**
     * @brief Calcule le palier supérieur de zoom (+10%).
     * @param current Facteur de zoom actuel.
     * @return Nouveau facteur de zoom incrémenté.
     */
    inline double StepZoomIn(double current)
    {
        return ClampZoomFactor(std::round((current + 0.10) * 100.0) / 100.0);
    }

    /**
     * @brief Calcule le palier inférieur de zoom (-10%).
     * @param current Facteur de zoom actuel.
     * @return Nouveau facteur de zoom décrémenté.
     */
    inline double StepZoomOut(double current)
    {
        return ClampZoomFactor(std::round((current - 0.10) * 100.0) / 100.0);
    }

    /**
     * @brief Formate un facteur de zoom sous forme de pourcentage lisible (ex. "100 %").
     * @param factor Facteur de zoom (1.0 = 100%).
     * @return Chaîne textuelle du pourcentage.
     */
    inline std::wstring FormatZoomPercentage(double factor)
    {
        int percent = static_cast<int>(std::round(ClampZoomFactor(factor) * 100.0));
        return std::to_wstring(percent) + L" %";
    }

    /**
     * @brief Encapsule le processus courant et tous ses processus enfants (services EDS, D-Bus, etc.)
     *        dans un Job Object Windows configuré avec JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
     *        Garantit que tous les processus satellites sont immédiatement et automatiquement
     *        terminés si l'application s'arrête (normalement ou par arrêt inopiné).
     * @return true si le Job Object a été initialisé et associé au processus, false sinon.
     */
    inline bool InitializeJobObject()
    {
        static HANDLE s_hJobObject = NULL;
        if (!s_hJobObject)
        {
            s_hJobObject = CreateJobObjectW(NULL, NULL);
            if (s_hJobObject)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
                jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
                if (SetInformationJobObject(s_hJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
                {
                    AssignProcessToJobObject(s_hJobObject, GetCurrentProcess());
                    return true;
                }
            }
        }
        return s_hJobObject != NULL;
    }

    /**
     * @brief Vérifie si un nom d'exécutable correspond à l'un des processus satellites
     *        connus d'Evolution (services EDS, gdbus, helpers).
     * @param exe_name Nom du fichier exécutable (ex. "evolution-source-registry.exe").
     * @return true si l'exécutable est un satellite d'Evolution, false sinon.
     */
    inline bool IsSatelliteProcessName(std::wstring_view exe_name)
    {
        std::wstring name_lower{ exe_name };
        for (auto& ch : name_lower) ch = static_cast<wchar_t>(std::towlower(ch));

        static const std::wstring_view s_satellites[] = {
            L"evolution-source-registry.exe",
            L"evolution-addressbook-factory.exe",
            L"evolution-calendar-factory.exe",
            L"camel-index-control.exe",
            L"gdbus.exe",
            L"gspawn-win64-helper.exe"
        };

        for (auto const& sat : s_satellites)
        {
            if (name_lower == sat)
                return true;
        }
        return false;
    }

    /**
     * @brief Nettoyage de sécurité résiduel à la fermeture de l'application :
     *        parcourt les processus actifs de la session utilisateur, identifie les processus
     *        satellites orphelins (services EDS, gdbus...) et les termine proprement.
     * @param timeout_ms Délai d'attente maximum en millisecondes avant terminaison forcée.
     * @return Nombre de processus orphelins résiduels arrêtés.
     */
    inline int TerminateResidualSatelliteProcesses(DWORD timeout_ms = 500)
    {
        int terminated_count = 0;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE)
            return 0;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);

        DWORD current_pid = GetCurrentProcessId();

        if (Process32FirstW(hSnapshot, &pe))
        {
            do
            {
                if (pe.th32ProcessID != current_pid && IsSatelliteProcessName(pe.szExeFile))
                {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                    if (hProcess)
                    {
                        if (TerminateProcess(hProcess, 0))
                        {
                            WaitForSingleObject(hProcess, timeout_ms);
                            terminated_count++;
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(hSnapshot, &pe));
        }

        CloseHandle(hSnapshot);
        return terminated_count;
    }

    /**
     * @brief Niveaux d'importance d'un message électronique.
     */
    enum class MessageImportanceLevel
    {
        Normal = 0,
        High = 1,
        Low = 2
    };

    /**
     * @brief Détermine le niveau d'importance d'un message selon ses en-têtes ou propriétés.
     * @param importance Chaîne indicative d'importance ("high", "urgent", "low", etc.).
     * @param priority Valeur numérique de priorité (1-2: haute, 3: normale, 4-5: basse).
     * @return Niveau d'importance normalisé (Normal, High ou Low).
     */
    inline MessageImportanceLevel ParseMessageImportance(std::wstring_view importance, double priority = 3.0)
    {
        if (importance == L"high" || importance == L"urgent" || (priority > 0 && priority < 3.0))
            return MessageImportanceLevel::High;
        if (importance == L"low" || priority > 3.0)
            return MessageImportanceLevel::Low;
        return MessageImportanceLevel::Normal;
    }
}
