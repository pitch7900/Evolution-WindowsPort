#include "pch.h"
#include "ComposeWindow.xaml.h"
#if __has_include("ComposeWindow.g.cpp")
#include "ComposeWindow.g.cpp"
#endif

#include <winrt/Windows.Storage.h>
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::EvolutionWin::implementation
{
    ComposeWindow::ComposeWindow()
    {
    }

    /* Convertit une chaîne de texte brut en fragment HTML sécurisé (échappement
     * des entités HTML et conversion des sauts de ligne en <br>).
     * Paramètres :
     *   text : texte brut à convertir
     * Retourne : chaîne HTML correspondante. */
    hstring ComposeWindow::PlainTextToHtml(std::wstring_view text)
    {
        std::wstring out;
        out.reserve(text.size() + 64);

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

        if (out.empty())
            return L"<div><br></div>";

        return hstring{ L"<div>" + out + L"</div>" };
    }

    /* Initialise la fenêtre de rédaction avec les comptes disponibles et les
     * données initiales du message (nouveau message, réponse ou transfert).
     * Paramètres :
     *   core     : pont partagé vers le moteur evo-core
     *   accounts : tableau JSON des comptes de messagerie
     *   data     : valeurs initiales (destinataires, objet, corps, fil) */
    void ComposeWindow::Setup(std::shared_ptr<::EvolutionWin::CoreBridge> const& core,
                              JsonArray const& accounts,
                              ::EvolutionWin::ComposeData const& data)
    {
        m_core = core;
        m_data = data;

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 920, 780 });

        /* Comptes d'expédition : uniquement les comptes réseau (dotés d'une identité et d'un serveur SMTP) */
        int selected = 0;
        for (auto const& value : accounts)
        {
            auto account = value.GetObject();
            if (account.GetNamedBoolean(L"local", false))
                continue;

            hstring uid = account.GetNamedString(L"uid");
            hstring address = account.GetNamedString(L"address", L"");
            hstring name = account.GetNamedString(L"name", uid);

            if (uid == data.account)
                selected = static_cast<int>(m_account_uids.size());

            m_account_uids.push_back(uid);
            FromBox().Items().Append(box_value(address.empty() || address == name ? name : name + L" <" + address + L">"));
        }

        if (!m_account_uids.empty())
            FromBox().SelectedIndex(selected);

        ToBox().Text(data.to);
        CcBox().Text(data.cc);
        SubjectBox().Text(data.subject);
        BodyBox().Text(data.body);

        m_attachments = data.attachments;
        RebuildAttachmentPanel();

        m_pending_editor_html = !data.html_body.empty() ? data.html_body : PlainTextToHtml(data.body);
        m_focus_editor_on_ready = !data.to.empty();

        InitializeEditorAsync();

        if (data.to.empty())
            ToBox().Focus(FocusState::Programmatic);
    }

    /* Initialise le contrôle WebView2 servant d'éditeur HTML riche
     * (contenteditable), configure la passerelle JavaScript et injecte le
     * contenu initial du message. */
    fire_and_forget ComposeWindow::InitializeEditorAsync()
    {
        auto strong = get_strong();

        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        HtmlEditor().DefaultBackgroundColor(Microsoft::UI::Colors::White());
        co_await HtmlEditor().EnsureCoreWebView2Async();
        HtmlEditor().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = HtmlEditor().CoreWebView2();
        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);
        auto settings = core.Settings();
        settings.IsScriptEnabled(true);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(false);

        auto weak = get_weak();
        core.WebMessageReceived([weak](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args)
        {
            auto self = weak.get();
            if (!self)
                return;

            hstring msg = args.TryGetWebMessageAsString();
            if (msg == L"ready")
            {
                self->m_editor_ready = true;
                if (!self->m_pending_editor_html.empty())
                {
                    JsonObject payload;
                    payload.SetNamedValue(L"html", JsonValue::CreateStringValue(self->m_pending_editor_html));
                    payload.SetNamedValue(L"focus", JsonValue::CreateBooleanValue(self->m_focus_editor_on_ready));
                    self->HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoInit(" + payload.Stringify() + L")");
                    self->m_pending_editor_html = L"";
                }
            }
            else if (msg == L"send")
            {
                self->SendMessageAsync();
            }
        });

        static constexpr wchar_t kEditorPage[] =
            L"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            L"<style>"
            L"html, body { height: 100%; margin: 0; background: #ffffff; color: #1a1a1a; "
            L"font-family: 'Segoe UI', sans-serif; font-size: 11pt; line-height: 1.45; }"
            L"#editor { box-sizing: border-box; min-height: 100%; padding: 12px 14px; outline: none; word-wrap: break-word; }"
            L"blockquote { margin: 4px 0 4px 8px; padding-left: 12px; border-left: 2px solid #999; color: #444; }"
            L"a { color: #0067c0; }"
            L"</style></head><body>"
            L"<div id=\"editor\" contenteditable=\"true\" spellcheck=\"true\"><div><br></div></div>"
            L"<script>"
            L"const ed = document.getElementById('editor');"
            L"window.evoInit = function(cfg) {"
            L"  ed.innerHTML = cfg.html || '<div><br></div>';"
            L"  if (cfg.focus) { ed.focus(); const sel = window.getSelection(); sel.collapse(ed, 0); }"
            L"};"
            L"window.evoSetHtml = function(cfg) { ed.innerHTML = cfg.html || '<div><br></div>'; };"
            L"window.evoExec = function(cfg) {"
            L"  ed.focus();"
            L"  document.execCommand(cfg.cmd, false, cfg.arg || null);"
            L"};"
            L"window.evoGetPayload = function() {"
            L"  return { html: ed.innerHTML, text: ed.innerText || '' };"
            L"};"
            L"document.addEventListener('keydown', function(e) {"
            L"  if (e.ctrlKey && e.key === 'Enter') {"
            L"    e.preventDefault();"
            L"    window.chrome.webview.postMessage('send');"
            L"  }"
            L"});"
            L"window.chrome.webview.postMessage('ready');"
            L"</script></body></html>";

        HtmlEditor().NavigateToString(kEditorPage);
    }

    /* Exécute une commande d'édition HTML (execCommand) dans le WebView2.
     * Paramètres :
     *   command  : nom de la commande DOM (ex. "bold", "italic", "createLink")
     *   argument : argument optionnel de la commande (ex. URL ou balise) */
    fire_and_forget ComposeWindow::ExecuteEditorCommandAsync(hstring command, hstring argument)
    {
        auto strong = get_strong();
        if (!m_html_mode || !m_editor_ready || !HtmlEditor().CoreWebView2())
            co_return;

        JsonObject cfg;
        cfg.SetNamedValue(L"cmd", JsonValue::CreateStringValue(command));
        cfg.SetNamedValue(L"arg", JsonValue::CreateStringValue(argument));
        co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoExec(" + cfg.Stringify() + L")");
    }

    /* Bascule entre l'éditeur HTML riche (WebView2) et l'éditeur texte brut
     * (TextBox) en synchronisant le contenu saisi.
     * Paramètres :
     *   html_mode : true pour passer en mode HTML, false pour passer en texte brut */
    fire_and_forget ComposeWindow::SwitchFormatModeAsync(bool html_mode)
    {
        auto strong = get_strong();
        if (m_html_mode == html_mode)
            co_return;

        if (!html_mode)
        {
            /* Passage de HTML vers texte brut : récupère le texte brut de l'éditeur WebView2 */
            if (m_editor_ready && HtmlEditor().CoreWebView2())
            {
                hstring raw = co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoGetPayload()");
                JsonObject obj;
                if (JsonObject::TryParse(raw, obj))
                {
                    std::wstring text{ obj.GetNamedString(L"text", L"") };
                    for (auto& c : text)
                        if (c == L'\n')
                            c = L'\r';
                    BodyBox().Text(hstring{ text });
                }
            }

            m_html_mode = false;
            FormatToolbar().Visibility(Visibility::Collapsed);
            HtmlEditorContainer().Visibility(Visibility::Collapsed);
            BodyBox().Visibility(Visibility::Visible);
            BodyBox().Focus(FocusState::Programmatic);
        }
        else
        {
            /* Passage de texte brut vers HTML : convertit le texte brut en HTML */
            hstring converted = PlainTextToHtml(BodyBox().Text());
            m_html_mode = true;
            FormatToolbar().Visibility(Visibility::Visible);
            BodyBox().Visibility(Visibility::Collapsed);
            HtmlEditorContainer().Visibility(Visibility::Visible);

            if (m_editor_ready && HtmlEditor().CoreWebView2())
            {
                JsonObject cfg;
                cfg.SetNamedValue(L"html", JsonValue::CreateStringValue(converted));
                co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoSetHtml(" + cfg.Stringify() + L")");
            }
            else
            {
                m_pending_editor_html = converted;
            }
        }
    }

    /* Gestionnaire du bouton à bascule « Format HTML ». */
    void ComposeWindow::OnFormatModeToggle(IInspectable const&, RoutedEventArgs const&)
    {
        bool checked = HtmlModeButton().IsChecked() ? HtmlModeButton().IsChecked().Value() : true;
        SwitchFormatModeAsync(checked);
    }

    /* Gestionnaires des boutons de mise en forme HTML */
    void ComposeWindow::OnBoldClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"bold");
    }

    void ComposeWindow::OnItalicClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"italic");
    }

    void ComposeWindow::OnUnderlineClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"underline");
    }

    void ComposeWindow::OnStrikeClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"strikeThrough");
    }

    void ComposeWindow::OnBulletsClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"insertUnorderedList");
    }

    void ComposeWindow::OnNumberingClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"insertOrderedList");
    }

    void ComposeWindow::OnQuoteClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"formatBlock", L"blockquote");
    }

    /* Ouvre une boîte de dialogue demandant l'adresse URL d'un lien hypertexte
     * à insérer sur la sélection courante de l'éditeur HTML. */
    fire_and_forget ComposeWindow::OnInsertLinkClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        if (!m_html_mode)
            co_return;

        TextBox url_box;
        url_box.Header(box_value(L"Adresse du lien (URL)"));
        url_box.PlaceholderText(L"https://exemple.fr");
        url_box.Text(L"https://");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Insérer un lien hypertexte"));
        dialog.Content(url_box);
        dialog.PrimaryButtonText(L"Insérer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary && !url_box.Text().empty())
            ExecuteEditorCommandAsync(L"createLink", url_box.Text());
    }

    void ComposeWindow::OnClearFormatClick(IInspectable const&, RoutedEventArgs const&)
    {
        ExecuteEditorCommandAsync(L"removeFormat");
    }

    /* Affiche un message d'information ou d'erreur dans la barre d'état de la
     * fenêtre de rédaction. */
    void ComposeWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        StatusBar().Message(message);
        StatusBar().Severity(severity);
        StatusBar().IsOpen(!message.empty());
    }

    /* Met à jour le titre de la fenêtre au fil de la saisie de l'objet. */
    void ComposeWindow::OnSubjectChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        hstring subject = SubjectBox().Text();
        hstring title = subject.empty() ? hstring{ L"Nouveau message" } : subject;

        Title(title);
        TitleText().Text(title);
    }

    /* Ferme la fenêtre de rédaction sans envoyer le message. */
    void ComposeWindow::OnDiscardClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
    }

    /* Ouvre le sélecteur de fichiers Windows pour joindre un ou plusieurs
     * fichiers au message en cours de rédaction. */
    fire_and_forget ComposeWindow::OnAttachClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();

        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

        picker.FileTypeFilter().Append(L"*");

        auto files = co_await picker.PickMultipleFilesAsync();
        if (!files)
            co_return;

        for (auto const& file : files)
        {
            hstring path = file.Path();
            if (!path.empty() && std::find(m_attachments.begin(), m_attachments.end(), path) == m_attachments.end())
                m_attachments.push_back(path);
        }

        RebuildAttachmentPanel();
    }

    /* Reconstruit la barre des pièces jointes affichée sous le champ Objet, avec
     * un bouton permettant de retirer chaque fichier joint. */
    void ComposeWindow::RebuildAttachmentPanel()
    {
        auto children = AttachmentPanel().Children();
        children.Clear();

        for (size_t i = 0; i < m_attachments.size(); ++i)
        {
            std::wstring_view full{ m_attachments[i] };
            auto slash = full.find_last_of(L"\\/");
            std::wstring_view filename = (slash != std::wstring_view::npos) ? full.substr(slash + 1) : full;

            Button chip;
            chip.Content(box_value(L"\xD83D\xDCCE " + hstring{ filename } + L"  \x2715"));
            ToolTipService::SetToolTip(chip, box_value(L"Retirer « " + m_attachments[i] + L" »"));
            chip.Click([this, i](IInspectable const&, RoutedEventArgs const&)
            {
                if (i < m_attachments.size())
                {
                    m_attachments.erase(m_attachments.begin() + static_cast<ptrdiff_t>(i));
                    RebuildAttachmentPanel();
                }
            });
            children.Append(chip);
        }

        AttachmentPanel().Visibility(m_attachments.empty() ? Visibility::Collapsed : Visibility::Visible);
    }

    /* Gestionnaire du bouton « Envoyer » (et raccourci Ctrl+Entrée). */
    void ComposeWindow::OnSendClick(IInspectable const&, RoutedEventArgs const&)
    {
        SendMessageAsync();
    }

    /* Récupère le contenu du message (HTML + texte brut, ou texte brut seul),
     * construit la requête JSON et déclenche l'envoi SMTP via evo-core. */
    fire_and_forget ComposeWindow::SendMessageAsync()
    {
        auto strong = get_strong();
        auto core = m_core.lock();
        int index = FromBox().SelectedIndex();

        if (!core)
        {
            ShowStatus(L"La fenêtre principale a été fermée.", InfoBarSeverity::Error);
            co_return;
        }

        if (index < 0 || index >= static_cast<int>(m_account_uids.size()))
        {
            ShowStatus(L"Aucun compte d'envoi n'est configuré.", InfoBarSeverity::Error);
            co_return;
        }

        if (ToBox().Text().empty() && CcBox().Text().empty() && BccBox().Text().empty())
        {
            ShowStatus(L"Indiquez au moins un destinataire.", InfoBarSeverity::Warning);
            ToBox().Focus(FocusState::Programmatic);
            co_return;
        }

        hstring account_uid = m_account_uids[index];
        hstring to_text = ToBox().Text();
        hstring cc_text = CcBox().Text();
        hstring bcc_text = BccBox().Text();
        hstring subject_text = SubjectBox().Text();

        SendButton().IsEnabled(false);
        ShowStatus(L"Envoi en cours…", InfoBarSeverity::Informational);

        hstring plain_body;
        hstring html_document;

        if (m_html_mode && m_editor_ready && HtmlEditor().CoreWebView2())
        {
            hstring raw = co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoGetPayload()");
            JsonObject payload;
            if (JsonObject::TryParse(raw, payload))
            {
                hstring inner_html = payload.GetNamedString(L"html", L"");
                std::wstring text{ payload.GetNamedString(L"text", L"") };
                for (auto& c : text)
                    if (c == L'\r')
                        c = L'\n';
                plain_body = hstring{ text };
                html_document = L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
                    L"<body style=\"font-family:'Segoe UI',sans-serif;font-size:11pt;color:#1a1a1a;\">" +
                    inner_html + L"</body></html>";
            }
        }
        else
        {
            std::wstring body{ BodyBox().Text() };
            for (auto& c : body)
                if (c == L'\r')
                    c = L'\n';
            plain_body = hstring{ body };
        }

        JsonObject message;
        message.SetNamedValue(L"account", JsonValue::CreateStringValue(account_uid));
        message.SetNamedValue(L"to", JsonValue::CreateStringValue(to_text));
        message.SetNamedValue(L"cc", JsonValue::CreateStringValue(cc_text));
        message.SetNamedValue(L"bcc", JsonValue::CreateStringValue(bcc_text));
        message.SetNamedValue(L"subject", JsonValue::CreateStringValue(subject_text));
        message.SetNamedValue(L"text", JsonValue::CreateStringValue(plain_body));
        if (!html_document.empty())
            message.SetNamedValue(L"html", JsonValue::CreateStringValue(html_document));
        message.SetNamedValue(L"in_reply_to", JsonValue::CreateStringValue(m_data.in_reply_to));
        message.SetNamedValue(L"references", JsonValue::CreateStringValue(m_data.references));

        /* Options cryptographiques OpenPGP / S/MIME */
        bool pgp_sign = PgpSignButton().IsChecked() && PgpSignButton().IsChecked().Value();
        bool pgp_encrypt = PgpEncryptButton().IsChecked() && PgpEncryptButton().IsChecked().Value();
        bool smime_sign = SmimeSignButton().IsChecked() && SmimeSignButton().IsChecked().Value();
        bool smime_encrypt = SmimeEncryptButton().IsChecked() && SmimeEncryptButton().IsChecked().Value();
        message.SetNamedValue(L"pgp_sign", JsonValue::CreateBooleanValue(pgp_sign));
        message.SetNamedValue(L"pgp_encrypt", JsonValue::CreateBooleanValue(pgp_encrypt));
        message.SetNamedValue(L"smime_sign", JsonValue::CreateBooleanValue(smime_sign));
        message.SetNamedValue(L"smime_encrypt", JsonValue::CreateBooleanValue(smime_encrypt));

        if (!m_attachments.empty())
        {
            JsonArray att_array;
            for (auto const& path : m_attachments)
                att_array.Append(JsonValue::CreateStringValue(path));
            message.SetNamedValue(L"attachments", att_array);
        }

        auto weak = get_weak();
        core->SendMail(message.Stringify(), [weak](hstring const&, hstring const& error)
        {
            auto self = weak.get();
            if (!self)
                return;

            if (!error.empty())
            {
                self->SendButton().IsEnabled(true);
                self->ShowStatus(L"Le message n'a pas pu être envoyé : " + error, InfoBarSeverity::Error);
                return;
            }

            self->Close();
        });
    }
}
