#include "pch.h"
#include "MainWindow.xaml.h"
#include "Signatures.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <algorithm>
#include <vector>

/* Paramètres > Signatures : liste, éditeur (texte ou HTML avec WebView2),
 * signature de chaque compte (nouveaux messages, réponses et transferts) et
 * position par rapport au texte cité. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace Sig = ::EvolutionWin::Signatures;

namespace
{
    Style LookupStyle(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<Style>();
    }

    /* aperçu d'une signature : première ligne non vide, tronquée */
    std::wstring SignaturePreview(JsonObject const& signature)
    {
        std::wstring content{ signature.GetNamedString(L"content", L"") };
        std::wstring text = signature.GetNamedString(L"mime_type", L"") == L"text/html" ? Sig::HtmlToText(content) : content;
        size_t start = text.find_first_not_of(L" \r\n");
        if (start == std::wstring::npos)
            return L"(vide)";
        size_t end = text.find_first_of(L"\r\n", start);
        std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (line.size() > 80)
            line = line.substr(0, 80) + L"…";
        return line;
    }
}

namespace winrt::EvolutionWin::implementation
{
    /* Recharge les signatures depuis evo-core et reconstruit la section. */
    void MainWindow::RefreshSignaturesView()
    {
        m_updating_signature_view = true;
        SignatureAboveQuoteSwitch().IsOn(::EvolutionWin::Settings::Instance().GetBoolean(Sig::AboveQuoteKey, false));
        m_updating_signature_view = false;

        m_core->ListSignatures([this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Signatures : " + error, InfoBarSeverity::Error);
                return;
            }

            m_signatures = JsonArray::Parse(json);
            RebuildSignatureLists();
        });
    }

    /* Reconstruit la liste des signatures et les choix de signature par compte. */
    void MainWindow::RebuildSignatureLists()
    {
        if (!m_signatures)
            return;

        /* signatures : nom, format, aperçu, Modifier / Supprimer */
        auto items = SignatureList().Items();
        items.Clear();
        for (auto const& value : m_signatures)
        {
            auto signature = value.GetObject();
            hstring uid = signature.GetNamedString(L"uid", L"");
            hstring name = signature.GetNamedString(L"name", L"");
            bool html = signature.GetNamedString(L"mime_type", L"") == L"text/html";

            Grid row;
            row.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition action_col;
            action_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(action_col);

            StackPanel info;
            info.Spacing(2);
            info.VerticalAlignment(VerticalAlignment::Center);
            TextBlock title;
            title.Text(name);
            title.Style(LookupStyle(L"BodyStrongTextBlockStyle"));
            info.Children().Append(title);
            TextBlock detail;
            detail.Text((html ? L"HTML  •  " : L"Texte brut  •  ") + hstring{ SignaturePreview(signature) });
            detail.Style(LookupStyle(L"CaptionTextBlockStyle"));
            detail.TextTrimming(TextTrimming::CharacterEllipsis);
            info.Children().Append(detail);
            row.Children().Append(info);

            StackPanel actions;
            actions.Orientation(Orientation::Horizontal);
            actions.Spacing(8);
            actions.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(actions, 1);

            Button edit_btn;
            edit_btn.Content(box_value(L"Modifier"));
            edit_btn.Click([this, signature](IInspectable const&, RoutedEventArgs const&)
            {
                OpenSignatureEditor(signature);
            });
            actions.Children().Append(edit_btn);

            Button remove_btn;
            remove_btn.Content(box_value(L"Supprimer"));
            remove_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
            {
                ConfirmDeleteSignatureAsync(uid, name);
            });
            actions.Children().Append(remove_btn);
            row.Children().Append(actions);

            items.Append(row);
        }

        bool empty = items.Size() == 0;
        SignaturesEmpty().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        SignatureList().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);

        /* choix par compte : uid de chaque entrée des listes déroulantes */
        std::vector<hstring> labels{ L"Aucune", L"Automatique (nom et adresse)" };
        std::vector<hstring> uids{ Sig::None, Sig::Autogenerated };
        for (auto const& value : m_signatures)
        {
            auto signature = value.GetObject();
            labels.push_back(signature.GetNamedString(L"name", L""));
            uids.push_back(signature.GetNamedString(L"uid", L""));
        }

        auto account_items = SignatureAccountList().Items();
        account_items.Clear();
        if (!m_accounts)
            return;

        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            if (account.GetNamedBoolean(L"local", false) || !account.HasKey(L"signature_uid"))
                continue;

            hstring uid = account.GetNamedString(L"uid", L"");
            hstring name = account.GetNamedString(L"name", uid);
            hstring address = account.GetNamedString(L"address", L"");

            Grid row;
            row.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
            row.ColumnSpacing(12);
            row.ColumnDefinitions().Append(ColumnDefinition{});
            for (int i = 0; i < 2; ++i)
            {
                ColumnDefinition col;
                col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(col);
            }

            StackPanel info;
            info.Spacing(2);
            info.VerticalAlignment(VerticalAlignment::Center);
            TextBlock title;
            title.Text(name);
            title.Style(LookupStyle(L"BodyStrongTextBlockStyle"));
            info.Children().Append(title);
            if (!address.empty() && address != name)
            {
                TextBlock subtitle;
                subtitle.Text(address);
                subtitle.Style(LookupStyle(L"CaptionTextBlockStyle"));
                info.Children().Append(subtitle);
            }
            row.Children().Append(info);

            /* nouveaux messages : signature de l'identité (EDS) */
            ComboBox new_box;
            new_box.Header(box_value(L"Nouveaux messages"));
            new_box.MinWidth(200);
            for (auto const& label : labels)
                new_box.Items().Append(box_value(label));
            auto current = std::find(uids.begin(), uids.end(), account.GetNamedString(L"signature_uid", Sig::None));
            new_box.SelectedIndex(current == uids.end() ? 0 : static_cast<int32_t>(current - uids.begin()));
            Grid::SetColumn(new_box, 1);
            new_box.SelectionChanged([this, account, uid, name, uids](IInspectable const& sender, SelectionChangedEventArgs const&)
            {
                int32_t index = sender.as<ComboBox>().SelectedIndex();
                if (index < 0 || index >= static_cast<int32_t>(uids.size()))
                    return;

                hstring signature_uid = uids[index];
                m_core->SetAccountSignature(uid, signature_uid, [this, account, signature_uid, name](hstring const&, hstring const& error)
                {
                    if (!error.empty())
                    {
                        ShowStatus(L"Signature de « " + name + L" » : " + error, InfoBarSeverity::Error);
                        RebuildSignatureLists();
                        return;
                    }
                    /* les fenêtres de rédaction suivantes utilisent le nouveau choix */
                    account.SetNamedValue(L"signature_uid", JsonValue::CreateStringValue(signature_uid));
                });
            });
            row.Children().Append(new_box);

            /* réponses et transferts : préférence de l'interface */
            std::vector<hstring> reply_uids{ L"" };
            reply_uids.insert(reply_uids.end(), uids.begin(), uids.end());
            ComboBox reply_box;
            reply_box.Header(box_value(L"Réponses et transferts"));
            reply_box.MinWidth(200);
            reply_box.Items().Append(box_value(L"Identique aux nouveaux messages"));
            for (auto const& label : labels)
                reply_box.Items().Append(box_value(label));
            auto reply_current = std::find(reply_uids.begin(), reply_uids.end(),
                ::EvolutionWin::Settings::Instance().GetString(Sig::ReplyKey(uid), L""));
            reply_box.SelectedIndex(reply_current == reply_uids.end() ? 0 : static_cast<int32_t>(reply_current - reply_uids.begin()));
            Grid::SetColumn(reply_box, 2);
            reply_box.SelectionChanged([uid, reply_uids](IInspectable const& sender, SelectionChangedEventArgs const&)
            {
                int32_t index = sender.as<ComboBox>().SelectedIndex();
                if (index >= 0 && index < static_cast<int32_t>(reply_uids.size()))
                    ::EvolutionWin::Settings::Instance().SetString(Sig::ReplyKey(uid), reply_uids[index]);
            });
            row.Children().Append(reply_box);

            account_items.Append(row);
        }
    }

    /* Gestionnaire du bouton « Nouvelle signature ». */
    void MainWindow::OnNewSignatureClick(IInspectable const&, RoutedEventArgs const&)
    {
        OpenSignatureEditor(nullptr);
    }

    /* Ouvre l'éditeur sous la liste des signatures.
     * Paramètres :
     *   signature : signature à modifier (objet JSON), nullptr pour une nouvelle */
    void MainWindow::OpenSignatureEditor(JsonObject const& signature)
    {
        bool html = !signature || signature.GetNamedString(L"mime_type", L"") == L"text/html";
        std::wstring content{ signature ? signature.GetNamedString(L"content", L"") : hstring{} };

        m_editing_signature_uid = signature ? signature.GetNamedString(L"uid", L"") : hstring{};
        SignatureEditorTitle().Text(signature ? L"Modifier « " + signature.GetNamedString(L"name", L"") + L" »"
                                              : hstring{ L"Nouvelle signature" });
        SignatureNameBox().Text(signature ? signature.GetNamedString(L"name", L"") : hstring{});

        m_updating_signature_view = true;
        SignatureFormatChoice().SelectedIndex(html ? 0 : 1);
        m_updating_signature_view = false;

        SignatureToolbar().Visibility(html ? Visibility::Visible : Visibility::Collapsed);
        SignatureHtmlContainer().Visibility(html ? Visibility::Visible : Visibility::Collapsed);
        SignatureTextBox().Visibility(html ? Visibility::Collapsed : Visibility::Visible);
        SignatureEditorPanel().Visibility(Visibility::Visible);

        if (html)
        {
            SignatureTextBox().Text(L"");
            SetSignatureEditorHtml(hstring{ content });
        }
        else
        {
            content.erase(std::remove(content.begin(), content.end(), L'\r'), content.end());
            std::replace(content.begin(), content.end(), L'\n', L'\r');
            SignatureTextBox().Text(hstring{ content });
            SetSignatureEditorHtml(L"");
        }

        SignatureNameBox().Focus(FocusState::Programmatic);
    }

    /* Affiche un contenu dans l'éditeur HTML, dès qu'il est prêt.
     * Paramètres :
     *   html : fragment HTML de la signature */
    void MainWindow::SetSignatureEditorHtml(hstring const& html)
    {
        if (m_signature_editor_ready && SignatureHtmlEditor().CoreWebView2())
        {
            JsonObject cfg;
            cfg.SetNamedValue(L"html", JsonValue::CreateStringValue(html));
            SignatureHtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoInit(" + cfg.Stringify() + L")");
            return;
        }

        m_signature_pending_html = html;
        if (SignatureHtmlContainer().Visibility() == Visibility::Visible)
            EnsureSignatureEditorAsync();
    }

    /* Initialise l'éditeur HTML des signatures (WebView2, contenteditable),
     * comme celui de la fenêtre de rédaction ; une seule fois. */
    fire_and_forget MainWindow::EnsureSignatureEditorAsync()
    {
        auto strong = get_strong();
        if (m_signature_editor_started)
            co_return;
        m_signature_editor_started = true;

        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        SignatureHtmlEditor().DefaultBackgroundColor(Microsoft::UI::Colors::White());
        co_await SignatureHtmlEditor().EnsureCoreWebView2Async();
        SignatureHtmlEditor().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = SignatureHtmlEditor().CoreWebView2();
        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);
        auto settings = core.Settings();
        settings.IsScriptEnabled(true);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(false);

        core.WebMessageReceived([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args)
        {
            if (args.TryGetWebMessageAsString() != L"ready")
                return;

            m_signature_editor_ready = true;
            SetSignatureEditorHtml(m_signature_pending_html);
            m_signature_pending_html = L"";
        });

        SignatureHtmlEditor().NavigateToString(Sig::EditorPage);
    }

    /* Contenu HTML de l'éditeur (ou contenu en attente s'il n'est pas prêt). */
    IAsyncOperation<hstring> MainWindow::SignatureEditorHtmlAsync()
    {
        auto strong = get_strong();
        if (!m_signature_editor_ready || !SignatureHtmlEditor().CoreWebView2())
            co_return m_signature_pending_html;

        hstring raw = co_await SignatureHtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoGetHtml()");
        JsonValue value{ nullptr };
        if (JsonValue::TryParse(raw, value) && value.ValueType() == JsonValueType::String)
            co_return value.GetString();
        co_return hstring{};
    }

    /* Choix du format : conversion du contenu entre HTML et texte brut. */
    void MainWindow::OnSignatureFormatChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        int32_t index = SignatureFormatChoice().SelectedIndex();
        if (m_updating_signature_view || index < 0)
            return;

        SwitchSignatureFormatAsync(index == 0);
    }

    /* Bascule l'éditeur de signature entre HTML et texte brut.
     * Paramètres :
     *   html : true pour passer en HTML, false pour passer en texte brut */
    fire_and_forget MainWindow::SwitchSignatureFormatAsync(bool html)
    {
        auto strong = get_strong();
        bool html_shown = SignatureHtmlContainer().Visibility() == Visibility::Visible;
        if (html == html_shown)
            co_return;

        if (html)
        {
            std::wstring text{ SignatureTextBox().Text() };
            std::replace(text.begin(), text.end(), L'\r', L'\n');
            SignatureToolbar().Visibility(Visibility::Visible);
            SignatureHtmlContainer().Visibility(Visibility::Visible);
            SignatureTextBox().Visibility(Visibility::Collapsed);
            SetSignatureEditorHtml(hstring{ text.empty() ? std::wstring{} : Sig::EscapeHtml(text) });
        }
        else
        {
            std::wstring text = Sig::HtmlToText(co_await SignatureEditorHtmlAsync());
            std::replace(text.begin(), text.end(), L'\n', L'\r');
            SignatureTextBox().Text(hstring{ text });
            SignatureToolbar().Visibility(Visibility::Collapsed);
            SignatureHtmlContainer().Visibility(Visibility::Collapsed);
            SignatureTextBox().Visibility(Visibility::Visible);
        }
    }

    /* Exécute une commande de mise en forme (execCommand) dans l'éditeur.
     * Paramètres :
     *   command  : commande DOM ("bold", "createLink", "insertImage"...)
     *   argument : argument de la commande (adresse, image) */
    fire_and_forget MainWindow::ExecuteSignatureCommandAsync(hstring command, hstring argument)
    {
        auto strong = get_strong();
        if (!m_signature_editor_ready || !SignatureHtmlEditor().CoreWebView2())
            co_return;

        JsonObject cfg;
        cfg.SetNamedValue(L"cmd", JsonValue::CreateStringValue(command));
        cfg.SetNamedValue(L"arg", JsonValue::CreateStringValue(argument));
        co_await SignatureHtmlEditor().CoreWebView2().ExecuteScriptAsync(L"window.evoExec(" + cfg.Stringify() + L")");
    }

    /* Boutons de mise en forme : commande dans la propriété Tag. */
    void MainWindow::OnSignatureToolClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto button = sender.try_as<Button>())
            ExecuteSignatureCommandAsync(unbox_value_or<hstring>(button.Tag(), L""), L"");
    }

    /* Insère un lien hypertexte sur la sélection de l'éditeur. */
    fire_and_forget MainWindow::OnSignatureLinkClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
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

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::Primary && !url_box.Text().empty())
            ExecuteSignatureCommandAsync(L"createLink", url_box.Text());
    }

    /* Insère une image choisie sur le disque, intégrée à la signature
     * (adresse data:, convertie en partie MIME liée à l'envoi). */
    fire_and_forget MainWindow::OnSignatureImageClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        constexpr uint32_t max_size = 512 * 1024;

        Windows::Storage::Pickers::FileOpenPicker picker;
        InitializePicker(picker);
        for (auto ext : { L".png", L".jpg", L".jpeg", L".gif" })
            picker.FileTypeFilter().Append(ext);

        auto file = co_await picker.PickSingleFileAsync();
        if (!file)
            co_return;

        auto buffer = co_await Windows::Storage::FileIO::ReadBufferAsync(file);
        if (buffer.Length() > max_size)
        {
            ShowStatus(L"Image trop volumineuse pour une signature (512 Ko au plus).", InfoBarSeverity::Warning);
            co_return;
        }

        std::wstring type{ file.FileType() };
        for (auto& c : type)
            c = static_cast<wchar_t>(towlower(c));
        hstring mime = type == L".png" ? L"image/png" : type == L".gif" ? L"image/gif" : L"image/jpeg";

        hstring data = L"data:" + mime + L";base64," +
            Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(buffer);
        ExecuteSignatureCommandAsync(L"insertImage", data);
    }

    /* Ferme l'éditeur sans enregistrer. */
    void MainWindow::OnSignatureEditorCancel(IInspectable const&, RoutedEventArgs const&)
    {
        SignatureEditorPanel().Visibility(Visibility::Collapsed);
        m_editing_signature_uid = L"";
    }

    /* Enregistre la signature éditée (création ou modification) via evo-core. */
    fire_and_forget MainWindow::OnSignatureEditorSave(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        std::wstring name{ SignatureNameBox().Text() };
        name.erase(0, name.find_first_not_of(L" \t"));
        name.erase(name.find_last_not_of(L" \t") + 1);
        if (name.empty())
        {
            ShowStatus(L"Indiquez un nom pour la signature.", InfoBarSeverity::Warning);
            SignatureNameBox().Focus(FocusState::Programmatic);
            co_return;
        }

        bool html = SignatureHtmlContainer().Visibility() == Visibility::Visible;
        hstring uid = m_editing_signature_uid;
        hstring content;
        if (html)
        {
            content = co_await SignatureEditorHtmlAsync();
        }
        else
        {
            std::wstring text{ SignatureTextBox().Text() };
            std::replace(text.begin(), text.end(), L'\r', L'\n');
            content = hstring{ text };
        }

        JsonObject signature;
        signature.SetNamedValue(L"uid", JsonValue::CreateStringValue(uid));
        signature.SetNamedValue(L"name", JsonValue::CreateStringValue(name));
        signature.SetNamedValue(L"mime_type", JsonValue::CreateStringValue(html ? L"text/html" : L"text/plain"));
        signature.SetNamedValue(L"content", JsonValue::CreateStringValue(content));

        hstring label{ name };
        m_core->SaveSignature(signature.Stringify(), [this, label](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Signature : " + error, InfoBarSeverity::Error);
                return;
            }

            SignatureEditorPanel().Visibility(Visibility::Collapsed);
            m_editing_signature_uid = L"";
            ShowStatus(L"Signature « " + label + L" » enregistrée.", InfoBarSeverity::Success);
            RefreshSignaturesView();
        });
    }

    /* Demande confirmation, puis supprime une signature ; les comptes qui
     * l'utilisaient (nouveaux messages ou réponses) n'ont plus de signature.
     * Paramètres :
     *   uid  : uid de la signature
     *   name : nom affiché */
    fire_and_forget MainWindow::ConfirmDeleteSignatureAsync(hstring uid, hstring name)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Supprimer la signature « " + name + L" » ?"));
        dialog.Content(box_value(L"Les comptes qui l'utilisent n'auront plus de signature."));
        dialog.PrimaryButtonText(L"Supprimer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Close);

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        m_core->DeleteSignature(uid, [this, uid, name](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression de la signature : " + error, InfoBarSeverity::Error);
                return;
            }

            if (m_accounts)
            {
                for (auto const& value : m_accounts)
                {
                    auto account = value.GetObject();
                    hstring key = Sig::ReplyKey(account.GetNamedString(L"uid", L""));
                    if (::EvolutionWin::Settings::Instance().GetString(key, L"") == uid)
                        ::EvolutionWin::Settings::Instance().SetString(key, Sig::None);
                    if (account.GetNamedString(L"signature_uid", L"") == uid)
                        account.SetNamedValue(L"signature_uid", JsonValue::CreateStringValue(Sig::None));
                }
            }

            if (m_editing_signature_uid == uid)
            {
                SignatureEditorPanel().Visibility(Visibility::Collapsed);
                m_editing_signature_uid = L"";
            }

            ShowStatus(L"Signature « " + name + L" » supprimée.", InfoBarSeverity::Informational);
            RefreshSignaturesView();
        });
    }

    /* Option « Position dans les réponses et transferts ». */
    void MainWindow::OnSignaturePlacementToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_updating_signature_view)
            ::EvolutionWin::Settings::Instance().SetBoolean(Sig::AboveQuoteKey, SignatureAboveQuoteSwitch().IsOn());
    }
}
