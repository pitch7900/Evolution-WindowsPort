#include "pch.h"
#include "ComposeWindow.xaml.h"
#include "WindowIcon.h"
#include "Signatures.h"
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
        m_accounts = accounts;

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 920, 780 });
        ::EvolutionWin::SetWindowIcon(AppWindow());

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
        LoadSignatures();

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
                /* signature chargée avant l'éditeur */
                self->ApplySignatureAsync();
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
            /* signature : bloc #evo-signature remplacé ou retiré à chaque changement ;
               créé après la première ligne (au-dessus du texte cité) ou à la fin */
            L"window.evoSetSignature = function(cfg) {"
            L"  let sig = document.getElementById('evo-signature');"
            L"  if (!cfg.html) { if (sig) sig.remove(); return; }"
            L"  if (!sig) {"
            L"    sig = document.createElement('div');"
            L"    sig.id = 'evo-signature';"
            L"    if (cfg.top && ed.firstChild) ed.insertBefore(sig, ed.firstChild.nextSibling);"
            L"    else ed.appendChild(sig);"
            L"  }"
            L"  sig.innerHTML = cfg.html;"
            L"};"
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
                /* la signature HTML est retirée, puis réinsérée en texte brut */
                co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoSetSignature({})");
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
            m_plain_signature.clear();
            FormatToolbar().Visibility(Visibility::Collapsed);
            HtmlEditorContainer().Visibility(Visibility::Collapsed);
            BodyBox().Visibility(Visibility::Visible);
            BodyBox().Focus(FocusState::Programmatic);
            ApplySignatureAsync();
        }
        else
        {
            /* Passage de texte brut vers HTML : convertit le texte brut en HTML
             * (sans la signature, réinsérée en HTML) */
            hstring converted = PlainTextToHtml(RemovePlainSignature(std::wstring{ BodyBox().Text() }));
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
            ApplySignatureAsync();
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

    /* Charge les signatures depuis evo-core et remplit le sélecteur :
     * « Aucune », « Automatique », puis les signatures par nom ; sélectionne
     * ensuite celle du compte expéditeur. */
    void ComposeWindow::LoadSignatures()
    {
        auto core = m_core.lock();
        if (!core)
            return;

        auto weak = get_weak();
        core->ListSignatures([weak](hstring const& json, hstring const& error)
        {
            auto self = weak.get();
            if (!self)
                return;

            JsonArray signatures;
            if (!error.empty() || !JsonArray::TryParse(json, signatures))
                signatures = JsonArray{};
            self->m_signatures = signatures;

            self->m_updating_signature = true;
            auto items = self->SignatureBox().Items();
            items.Clear();
            self->m_signature_choices.clear();
            items.Append(box_value(L"Aucune"));
            self->m_signature_choices.push_back(::EvolutionWin::Signatures::None);
            items.Append(box_value(L"Automatique (nom et adresse)"));
            self->m_signature_choices.push_back(::EvolutionWin::Signatures::Autogenerated);
            for (auto const& value : signatures)
            {
                auto signature = value.GetObject();
                items.Append(box_value(signature.GetNamedString(L"name", L"")));
                self->m_signature_choices.push_back(signature.GetNamedString(L"uid", L""));
            }
            self->m_updating_signature = false;

            self->SignatureBox().IsEnabled(true);
            self->SelectAccountSignature();
        });
    }

    /* Compte expéditeur sélectionné (objet JSON de evo_core_list_accounts),
     * nullptr si aucun. */
    JsonObject ComposeWindow::SelectedAccount()
    {
        int index = FromBox().SelectedIndex();
        if (!m_accounts || index < 0 || index >= static_cast<int>(m_account_uids.size()))
            return nullptr;

        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            if (account.GetNamedString(L"uid", L"") == m_account_uids[index])
                return account;
        }
        return nullptr;
    }

    /* Sélectionne la signature du compte expéditeur (nouveaux messages, ou
     * réponses et transferts) ; le changement de sélection l'insère. */
    void ComposeWindow::SelectAccountSignature()
    {
        if (!m_signatures)
            return;

        auto account = SelectedAccount();
        hstring uid = account ? ::EvolutionWin::Signatures::ForAccount(account, m_data.reply)
                              : hstring{ ::EvolutionWin::Signatures::None };
        auto found = std::find(m_signature_choices.begin(), m_signature_choices.end(), uid);
        int index = found == m_signature_choices.end() ? 0 : static_cast<int>(found - m_signature_choices.begin());

        if (SignatureBox().SelectedIndex() == index)
            ApplySignatureAsync();  /* même choix : la signature automatique dépend du compte */
        else
            SignatureBox().SelectedIndex(index);
    }

    /* Changement de compte expéditeur : signature de ce compte. */
    void ComposeWindow::OnFromChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        SelectAccountSignature();
    }

    /* Changement de signature dans le sélecteur : remplacement dans le message. */
    void ComposeWindow::OnSignatureChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_updating_signature)
            ApplySignatureAsync();
    }

    /* Contenu de la signature choisie.
     * Paramètres :
     *   html : true pour un fragment HTML, false pour du texte brut (\n)
     * Retourne : contenu, vide pour « Aucune ». */
    std::wstring ComposeWindow::SignatureContent(bool html)
    {
        int index = SignatureBox().SelectedIndex();
        if (index < 0 || index >= static_cast<int>(m_signature_choices.size()))
            return {};

        hstring uid = m_signature_choices[index];
        if (uid == ::EvolutionWin::Signatures::None)
            return {};

        if (uid == ::EvolutionWin::Signatures::Autogenerated)
        {
            auto account = SelectedAccount();
            return account ? ::EvolutionWin::Signatures::Autogenerate(account, html) : std::wstring{};
        }

        for (auto const& value : m_signatures)
        {
            auto signature = value.GetObject();
            if (signature.GetNamedString(L"uid", L"") != uid)
                continue;

            std::wstring content{ signature.GetNamedString(L"content", L"") };
            bool is_html = signature.GetNamedString(L"mime_type", L"") == L"text/html";
            if (html)
                return is_html ? content : ::EvolutionWin::Signatures::EscapeHtml(content);
            if (is_html)
                return ::EvolutionWin::Signatures::HtmlToText(content);
            content.erase(std::remove(content.begin(), content.end(), L'\r'), content.end());
            return content;
        }
        return {};
    }

    /* Retire du texte brut le bloc de signature inséré précédemment.
     * Paramètres :
     *   text : texte du message (sauts de ligne \r)
     * Retourne : texte sans la signature. */
    std::wstring ComposeWindow::RemovePlainSignature(std::wstring text)
    {
        if (!m_plain_signature.empty())
        {
            size_t pos = text.find(m_plain_signature);
            if (pos != std::wstring::npos)
                text.erase(pos, m_plain_signature.size());
            m_plain_signature.clear();
        }
        return text;
    }

    /* Insère ou remplace la signature choisie dans le message (bloc
     * #evo-signature de l'éditeur HTML, ou bloc « -- » du texte brut), au-dessus
     * du texte cité ou à la fin selon la préférence. */
    fire_and_forget ComposeWindow::ApplySignatureAsync()
    {
        auto strong = get_strong();
        if (!m_signatures || SignatureBox().SelectedIndex() < 0)
            co_return;

        bool top = m_data.reply &&
            ::EvolutionWin::Settings::Instance().GetBoolean(::EvolutionWin::Signatures::AboveQuoteKey, false);

        if (m_html_mode)
        {
            /* éditeur pas encore prêt : appliquée à son message "ready" */
            if (!m_editor_ready || !HtmlEditor().CoreWebView2())
                co_return;

            std::wstring content = SignatureContent(true);
            std::wstring html;
            if (!content.empty())
            {
                /* séparateur usuel « -- » (espace final conservé), sauf s'il y est déjà */
                bool delimited = ::EvolutionWin::Signatures::HtmlToText(content).rfind(L"--", 0) == 0;
                html = (delimited ? std::wstring{} : std::wstring{ L"<div style=\"white-space:pre\">-- </div>" }) + content;
            }

            JsonObject cfg;
            cfg.SetNamedValue(L"html", JsonValue::CreateStringValue(html));
            cfg.SetNamedValue(L"top", JsonValue::CreateBooleanValue(top));
            co_await HtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoSetSignature(" + cfg.Stringify() + L")");
        }
        else
        {
            std::wstring text = RemovePlainSignature(std::wstring{ BodyBox().Text() });
            std::wstring content = SignatureContent(false);

            if (!content.empty())
            {
                for (auto& c : content)
                    if (c == L'\n')
                        c = L'\r';

                std::wstring block = (content.rfind(L"--", 0) == 0 ? L"\r" : L"\r-- \r") + content;
                /* au-dessus du texte cité : après la première ligne (saisie) */
                size_t pos = top ? text.find(L'\r') : std::wstring::npos;
                text.insert(pos == std::wstring::npos ? text.size() : pos, block);
                m_plain_signature = block;
            }

            BodyBox().Text(hstring{ text });
        }
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

            self->RememberRecipients();
            self->Close();
        });
    }

    /* Destinataire en cours de saisie : texte après le dernier « ; » ou « , ». */
    static size_t RecipientTokenStart(std::wstring const& text)
    {
        size_t sep = text.find_last_of(L";,");
        size_t start = sep == std::wstring::npos ? 0 : sep + 1;
        while (start < text.size() && iswspace(text[start]))
            start++;
        return start;
    }

    /* Adresses récemment utilisées (préférence "recipients.recent", 50 au plus). */
    static std::vector<std::wstring> RecentRecipients()
    {
        std::vector<std::wstring> recent;
        for (auto const& value : ::EvolutionWin::Settings::Instance().GetArray(L"recipients.recent"))
            if (value.ValueType() == Windows::Data::Json::JsonValueType::String)
                recent.push_back(std::wstring{ value.GetString() });
        return recent;
    }

    /* Saisie dans À / Cc / Cci : suggestions pour le destinataire en cours
     * (2 caractères au moins), adresses récentes d'abord, puis carnets
     * d'adresses (GAL comprise) via evo-core. */
    void ComposeWindow::OnRecipientTextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;

        std::wstring text{ sender.Text() };
        std::wstring token = text.substr(RecipientTokenStart(text));
        uint64_t request = ++m_recipient_request;

        if (token.size() < 2)
        {
            sender.ItemsSource(nullptr);
            return;
        }

        auto lower = [](std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), ::towlower);
            return value;
        };
        std::wstring needle = lower(token);

        /* adresses récentes : nom ou adresse commençant par la saisie */
        std::vector<std::wstring> recent_matches;
        for (auto const& entry : RecentRecipients())
        {
            std::wstring low = lower(entry);
            size_t lt = low.find(L'<');
            bool match = low.rfind(needle, 0) == 0 || (lt != std::wstring::npos && low.compare(lt + 1, needle.size(), needle) == 0);
            if (match && recent_matches.size() < 5)
                recent_matches.push_back(entry);
        }

        auto core = m_core.lock();
        if (!core)
            return;

        auto weak = get_weak();
        core->SearchRecipients(hstring{ token }, 15, [weak, sender, request, recent_matches](hstring const& json, hstring const& error)
        {
            auto self = weak.get();
            if (!self || request != self->m_recipient_request)
                return;

            Windows::Data::Json::JsonArray results;
            if (!error.empty() || !Windows::Data::Json::JsonArray::TryParse(json, results))
                results = Windows::Data::Json::JsonArray{};

            /* libellé affiché -> texte inséré */
            self->m_suggestion_values.clear();
            auto items = single_threaded_vector<IInspectable>();
            std::vector<std::wstring> inserted;

            for (auto const& entry : recent_matches)
            {
                std::wstring label = entry + L"  (récent)";
                self->m_suggestion_values[label] = entry;
                inserted.push_back(entry);
                items.Append(box_value(hstring{ label }));
            }

            for (auto const& value : results)
            {
                auto person = value.GetObject();
                std::wstring name{ person.GetNamedString(L"name", L"") };
                std::wstring email{ person.GetNamedString(L"email", L"") };
                std::wstring address = name.empty() ? email : name + L" <" + email + L">";
                if (std::find(inserted.begin(), inserted.end(), address) != inserted.end())
                    continue;

                std::wstring label = address + L"  — " + std::wstring{ person.GetNamedString(L"book", L"") };
                self->m_suggestion_values[label] = address;
                inserted.push_back(address);
                items.Append(box_value(hstring{ label }));
            }

            sender.ItemsSource(items.Size() > 0 ? items : nullptr);
        });
    }

    /* Suggestion choisie (Entrée, clic) : remplace le destinataire en cours
     * et ajoute « ; » pour le suivant. */
    void ComposeWindow::OnRecipientQuerySubmitted(AutoSuggestBox const& sender, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        if (!args.ChosenSuggestion())
            return;

        std::wstring label{ unbox_value_or<hstring>(args.ChosenSuggestion(), L"") };
        auto it = m_suggestion_values.find(label);
        if (it == m_suggestion_values.end())
            return;

        std::wstring text{ sender.Text() };
        text = text.substr(0, RecipientTokenStart(text)) + it->second + L"; ";
        ++m_recipient_request;
        sender.Text(hstring{ text });
        sender.ItemsSource(nullptr);
    }

    /* Message envoyé : ses destinataires passent en tête des adresses récentes. */
    void ComposeWindow::RememberRecipients()
    {
        std::vector<std::wstring> sent;
        for (auto const& box : { ToBox(), CcBox(), BccBox() })
        {
            std::wstring text{ box.Text() };
            size_t start = 0;
            while (start <= text.size())
            {
                size_t end = text.find_first_of(L";,", start);
                std::wstring part = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                size_t first = part.find_first_not_of(L" \t");
                size_t last = part.find_last_not_of(L" \t");
                if (first != std::wstring::npos && part.find(L'@') != std::wstring::npos)
                    sent.push_back(part.substr(first, last - first + 1));
                if (end == std::wstring::npos)
                    break;
                start = end + 1;
            }
        }

        Windows::Data::Json::JsonArray recent;
        auto add = [&recent](std::wstring const& value)
        {
            for (auto const& existing : recent)
                if (existing.GetString() == value)
                    return;
            if (recent.Size() < 50)
                recent.Append(Windows::Data::Json::JsonValue::CreateStringValue(value));
        };
        for (auto const& value : sent)
            add(value);
        for (auto const& value : RecentRecipients())
            add(value);

        ::EvolutionWin::Settings::Instance().Set(L"recipients.recent", recent);
    }
}
