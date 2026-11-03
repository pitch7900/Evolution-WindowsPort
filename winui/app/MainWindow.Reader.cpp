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

/* Volet de lecture (WebView2) : affichage du message, contenu distant et
 * listes de confiance, bandeau de sécurité, impression, vue Confidentialité. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    /* Origine des documents du lecteur : nom réservé (.invalid), jamais
     * résolu, toutes ses requêtes étant servies par WebResourceRequested.
     * En http (et non https) pour que les images http autorisées ne soient
     * pas bloquées comme contenu mixte. */
    constexpr std::wstring_view ReaderOrigin = L"http://reader.evolution.invalid/";

    /* clés des préférences de confidentialité (voir Settings.h) */
    constexpr wchar_t RemoteContentKey[] = L"privacy.remote-content";

    constexpr wchar_t TrustedSendersKey[] = L"privacy.trusted-senders";

    constexpr wchar_t TrustedDomainsKey[] = L"privacy.trusted-domains";

    bool StartsWith(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    /* Requête vers un serveur distant (images, CSS, polices, cadres, pixels
     * de suivi…), par opposition aux data:, about:, blob: du document. */
    bool IsRemoteUri(std::wstring_view uri)
    {
        auto lower = ToLower(uri.substr(0, 8));
        return StartsWith(lower, L"http:") || StartsWith(lower, L"https:") ||
               StartsWith(lower, L"ws:") || StartsWith(lower, L"wss:") || StartsWith(lower, L"ftp:");
    }

    /* Adresse électronique en minuscules d'un expéditeur « Nom <adresse> »
     * ou « adresse » (la première s'il y en a plusieurs) ; vide si aucune.
     * Paramètres :
     *   from : champ From tel que fourni par le moteur */
    std::wstring SenderAddress(hstring const& from)
    {
        std::wstring_view view{ from };
        std::wstring_view address;

        auto open = view.find(L'<');
        if (open != std::wstring_view::npos)
        {
            auto close = view.find(L'>', open);
            address = view.substr(open + 1, close == std::wstring_view::npos ? std::wstring_view::npos : close - open - 1);
        }
        else
        {
            address = view.substr(0, view.find(L','));
        }

        address = Trim(address);
        auto at = address.rfind(L'@');
        if (at == std::wstring_view::npos || at == 0 || at + 1 >= address.size() ||
            address.find_first_of(L" \t\"<>,;") != std::wstring_view::npos)
            return {};

        return ToLower(address);
    }

    /* domaine d'une adresse (« exemple.ch » pour « a@exemple.ch ») */
    std::wstring AddressDomain(std::wstring const& address)
    {
        auto at = address.rfind(L'@');
        return at == std::wstring::npos ? std::wstring{} : address.substr(at + 1);
    }

    /* Liste de confiance (tableau JSON de chaînes) lue dans les préférences.
     * Paramètres :
     *   key : TrustedSendersKey ou TrustedDomainsKey */
    std::vector<std::wstring> ReadTrustList(wchar_t const* key)
    {
        std::vector<std::wstring> list;
        for (auto const& value : ::EvolutionWin::Settings::Instance().GetArray(key))
        {
            if (value.ValueType() == JsonValueType::String)
            {
                auto entry = ToLower(Trim(value.GetString()));
                if (!entry.empty() && std::find(list.begin(), list.end(), entry) == list.end())
                    list.push_back(entry);
            }
        }
        return list;
    }

    void WriteTrustList(wchar_t const* key, std::vector<std::wstring> const& list)
    {
        JsonArray array;
        for (auto const& entry : list)
            array.Append(JsonValue::CreateStringValue(entry));
        ::EvolutionWin::Settings::Instance().Set(key, array);
    }

    /* "never", "trusted" (par défaut) ou "always" */
    std::wstring RemoteContentMode()
    {
        std::wstring mode{ ::EvolutionWin::Settings::Instance().GetString(RemoteContentKey, L"trusted") };
        return mode == L"never" || mode == L"always" ? mode : L"trusted";
    }

    /* Le contenu distant d'un message de cet expéditeur peut-il être chargé
     * sans demande ? « Jamais » l'emporte sur les listes de confiance.
     * Paramètres :
     *   from : champ From du message */
    bool IsRemoteContentTrusted(hstring const& from)
    {
        auto mode = RemoteContentMode();
        if (mode == L"always")
            return true;
        if (mode == L"never")
            return false;

        auto address = SenderAddress(from);
        if (address.empty())
            return false;

        auto senders = ReadTrustList(TrustedSendersKey);
        if (std::find(senders.begin(), senders.end(), address) != senders.end())
            return true;

        auto domains = ReadTrustList(TrustedDomainsKey);
        return std::find(domains.begin(), domains.end(), AddressDomain(address)) != domains.end();
    }
}

namespace winrt::EvolutionWin::implementation
{
    fire_and_forget MainWindow::InitializeReaderAsync()
    {
        auto strong = get_strong();

        /* writable data folder: next to the executable is not (Program Files) */
        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        /* messages are designed for a light background (like Outlook's
           default reading pane), whatever the Windows theme */
        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        try
        {
            co_await Reader().EnsureCoreWebView2Async();
        }
        catch (...)
        {
            co_return;
        }

        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = Reader().CoreWebView2();
        if (!core)
            co_return;

        /* Force le schéma de couleurs clair dans Chromium pour éviter que le
         * mode sombre de Windows 11 n'active un fond noir sous un texte noir */
        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);

        auto settings = core.Settings();

        /* messages are documents: no scripts, no remote content (privacy,
           as in Evolution), links open in the default browser */
        settings.IsScriptEnabled(false);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(true);

        core.AddWebResourceRequestedFilter(L"*", Microsoft::Web::WebView2::Core::CoreWebView2WebResourceContext::All);
        core.WebResourceRequested([this](auto const& sender, Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args)
        {
            OnReaderResourceRequested(sender, args);
        });

        core.NavigationStarting([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring_view uri{ args.Uri() };

            /* le document demandé par ShowInReader (CoreWebView2::Navigate) :
               à tester en premier, WebView2 considère les navigations lancées
               par l'application comme « initiées par l'utilisateur » */
            if (uri == std::wstring_view{ m_reader_uri })
            {
                m_reader_navigation_id = args.NavigationId();
                m_reader_navigation_known = true;
                return;
            }

            if (args.IsUserInitiated())
            {
                if (uri.rfind(L"data:", 0) == 0 || uri.rfind(L"about:", 0) == 0)
                    return;
                /* lien cliqué : ouvert dans le navigateur ; liens relatifs
                   d'un message : rien à ouvrir */
                args.Cancel(true);
                if (!StartsWith(uri, ReaderOrigin))
                    Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
                return;
            }

            /* autres navigations non demandées par l'utilisateur : refusées
               (pas de <meta refresh> vers un site distant) */
            if (uri != L"about:blank")
                args.Cancel(true);
        });

        /* le document demandé remplace le précédent : ses requêtes seules
           arrivent désormais, la décision de confiance s'applique */
        core.ContentLoading([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2ContentLoadingEventArgs const& args)
        {
            if (!m_reader_navigation_known || args.NavigationId() != m_reader_navigation_id)
                return;

            m_reader_navigation_known = false;
            m_reader_document_active = true;
            m_reader_remote_allowed = m_reader_pending_allow;
            m_reader_blocked_count = 0;
        });

        core.NewWindowRequested([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& args)
        {
            args.Handled(true);
            Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
        });

        m_reader_zoom = ::EvolutionWin::Settings::Instance().GetDouble(L"mail.reading-zoom", 1.0);
        m_view_plain_text = ::EvolutionWin::Settings::Instance().GetBoolean(L"mail.reading-plain-text", false);

        core.NavigationCompleted([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const&)
        {
            if (m_reader_zoom != 1.0)
            {
                ApplyReaderZoom(m_reader_zoom);
            }
        });

        m_reader_ready = true;

        /* document demandé avant la fin de l'initialisation */
        if (!m_reader_uri.empty())
            core.Navigate(m_reader_uri);
    }

    /* Affiche un document HTML dans le lecteur. Le document reçoit une
     * adresse unique ; tant qu'il n'est pas chargé, aucune requête distante
     * n'est autorisée ni comptée (une requête tardive du message précédent
     * ne peut donc ni charger de contenu ni afficher le bandeau).
     * Paramètres :
     *   html         : document à afficher
     *   allow_remote : autoriser le chargement du contenu distant */
    void MainWindow::ShowInReader(hstring const& html, bool allow_remote)
    {
        ++m_reader_generation;
        m_reader_uri = hstring{ std::wstring{ ReaderOrigin } + L"message-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(m_reader_generation) };
        m_reader_html = html;
        m_reader_pending_allow = allow_remote;
        m_reader_navigation_known = false;
        m_reader_document_active = false;
        m_reader_remote_allowed = false;
        m_reader_blocked_count = 0;
        RemoteContentBar().IsOpen(false);

        if (!m_reader_ready)
            return;

        try
        {
            Reader().CoreWebView2().Navigate(m_reader_uri);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'afficher le message : " + e.message(), InfoBarSeverity::Warning);
        }
    }

    /* Affiche le message courant (m_current_message) dans le lecteur, avec
     * le contenu distant si l'expéditeur est de confiance ou si demandé.
     * Paramètres :
     *   force_remote : « Afficher les images pour ce message » (cette fois
     *                  seulement, même si l'option globale est « Jamais ») */
    void MainWindow::ShowCurrentMessageInReader(bool force_remote)
    {
        if (!m_current_message)
            return;

        if (m_view_plain_text)
        {
            hstring body_text = m_current_message.GetNamedString(L"body", L"");
            if (body_text.empty())
                body_text = m_current_message.GetNamedString(L"text", L"");
            if (body_text.empty())
                body_text = ::EvolutionWin::Helpers::ExtractPlainTextForSpeech(m_current_message);

            std::wstring plain_html = L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
                L"body { margin:0; padding:16px; background:#fff; color:#1a1a1a; font-family:'Segoe UI Variable Text','Segoe UI',sans-serif; font-size:14px; line-height:1.6; word-break:break-word; white-space:pre-wrap; }"
                L"</style></head><body>" + std::wstring{ ::EvolutionWin::Helpers::EscapeHtml(body_text) } + L"</body></html>";

            ShowInReader(hstring{ plain_html }, false);
            return;
        }

        bool allow = force_remote || IsRemoteContentTrusted(m_current_message.GetNamedString(L"from", L""));
        ShowInReader(m_current_message.GetNamedString(L"html", L""), allow);
    }

    /* Toutes les requêtes du lecteur : sert le document courant, bloque
     * (403) le contenu distant non autorisé et compte les blocages pour le
     * bandeau « Contenu externe bloqué ».
     * Paramètres :
     *   sender : moteur WebView2 du lecteur
     *   args   : requête interceptée */
    void MainWindow::OnReaderResourceRequested(
        Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
        Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args)
    {
        hstring request_uri = args.Request().Uri();
        std::wstring_view uri{ request_uri };

        if (StartsWith(ToLower(uri.substr(0, ReaderOrigin.size())), ReaderOrigin))
        {
            if (request_uri == m_reader_uri)
                ServeReaderDocumentAsync(sender, args, m_reader_html);
            else
                args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 404, L"Not Found", L""));
            return;
        }

        if (!IsRemoteUri(uri))
            return;

        if (m_reader_document_active && m_reader_remote_allowed)
            return;

        args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 403, L"Blocked", L""));

        if (m_reader_document_active)
        {
            ++m_reader_blocked_count;
            UpdateRemoteContentBanner();
        }
    }

    /* Répond à la requête du document du lecteur avec son HTML (UTF-8).
     * Paramètres :
     *   sender : moteur WebView2 du lecteur
     *   args   : requête du document
     *   html   : contenu à servir */
    fire_and_forget MainWindow::ServeReaderDocumentAsync(
        Microsoft::Web::WebView2::Core::CoreWebView2 sender,
        Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs args,
        hstring html)
    {
        auto strong = get_strong();
        auto deferral = args.GetDeferral();

        try
        {
            Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            Windows::Storage::Streams::DataWriter writer{ stream };
            writer.UnicodeEncoding(Windows::Storage::Streams::UnicodeEncoding::Utf8);
            writer.WriteString(html);
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            args.Response(sender.Environment().CreateWebResourceResponse(stream, 200, L"OK",
                L"Content-Type: text/html; charset=utf-8\r\n"
                L"Cache-Control: no-store\r\n"
                L"Referrer-Policy: no-referrer"));
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'afficher le message : " + e.message(), InfoBarSeverity::Warning);
        }

        deferral.Complete();
    }

    /* Ouvre ou met à jour le bandeau « Contenu externe bloqué » pour le
     * message affiché (boutons de confiance selon l'expéditeur et l'option
     * globale). */
    void MainWindow::UpdateRemoteContentBanner()
    {
        if (!m_current_message || m_reader_blocked_count == 0)
        {
            RemoteContentBar().IsOpen(false);
            return;
        }

        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        bool never = RemoteContentMode() == L"never";

        std::wstring msg = m_reader_blocked_count == 1
            ? L"1 élément distant (image, style, pixel de suivi…) n'a pas été chargé pour protéger votre vie privée."
            : std::to_wstring(m_reader_blocked_count) +
              L" éléments distants (images, styles, pixels de suivi…) n'ont pas été chargés pour protéger votre vie privée.";
        if (never)
            msg += L" Le chargement est désactivé pour tous les messages (Paramètres › Confidentialité).";
        RemoteContentBar().Message(hstring{ msg });

        bool can_trust = !never && !address.empty();
        TrustSenderButton().Visibility(can_trust ? Visibility::Visible : Visibility::Collapsed);
        TrustDomainButton().Visibility(can_trust ? Visibility::Visible : Visibility::Collapsed);
        if (can_trust)
        {
            TrustSenderButton().Content(box_value(hstring{ L"Toujours faire confiance à " + address }));
            TrustDomainButton().Content(box_value(hstring{ L"Toujours faire confiance au domaine @" + AddressDomain(address) }));
        }

        /* ouvert au premier blocage seulement : une image chargée plus tard
           ne rouvre pas un bandeau fermé par l'utilisateur */
        if (m_reader_blocked_count == 1)
            RemoteContentBar().IsOpen(true);
    }

    /* « Afficher les images pour ce message » : recharge le message courant
     * avec le contenu distant, cette fois seulement. */
    void MainWindow::OnShowRemoteContentClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowCurrentMessageInReader(true);
    }

    void MainWindow::OnTrustSenderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        TrustCurrentSender(TrustedSendersKey, address, hstring{ address });
    }

    void MainWindow::OnTrustDomainClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto domain = AddressDomain(SenderAddress(m_current_message.GetNamedString(L"from", L"")));
        TrustCurrentSender(TrustedDomainsKey, domain, hstring{ L"@" + domain });
    }

    /* Ajoute une entrée à une liste de confiance puis recharge le message
     * courant avec son contenu distant.
     * Paramètres :
     *   key   : TrustedSendersKey ou TrustedDomainsKey
     *   entry : adresse ou domaine en minuscules
     *   label : texte affiché dans la barre d'état */
    void MainWindow::TrustCurrentSender(hstring const& key, std::wstring const& entry, hstring const& label)
    {
        if (entry.empty())
            return;

        auto list = ReadTrustList(key.c_str());
        if (std::find(list.begin(), list.end(), entry) == list.end())
        {
            list.push_back(entry);
            WriteTrustList(key.c_str(), list);
        }

        ShowStatus(L"Le contenu externe des messages de « " + label + L" » sera chargé.", InfoBarSeverity::Success);
        ShowCurrentMessageInReader(false);
    }

    /* Vide le volet de lecture lorsqu'aucun message n'est sélectionné. */
    void MainWindow::ClearReader()
    {
        CloseAttachmentPreview();
        StopSpeech();
        ++m_message_request;
        m_current_message = nullptr;
        m_current_message_uid = L"";
        SecurityBar().IsOpen(false);
        AttachmentBar().Children().Clear();
        AttachmentBar().Visibility(Visibility::Collapsed);
        MessageHeader().Visibility(Visibility::Collapsed);
        ReaderContainer().Visibility(Visibility::Collapsed);
        ReaderPlaceholder().Visibility(Visibility::Visible);
        ShowInReader(L"<!DOCTYPE html><html><body style=\"background:#fff;color:#1a1a1a;\"></body></html>");
    }

    /* Gestionnaire du bouton « Imprimer » (Ctrl+P) du volet de lecture :
     * ouvre la boîte de dialogue d'impression native de WebView2 pour le
     * message actuellement affiché. */
    void MainWindow::OnPrintClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message || !m_reader_ready || !Reader().CoreWebView2())
        {
            ShowStatus(L"Sélectionnez d'abord un message à imprimer.", InfoBarSeverity::Informational);
            return;
        }

        try
        {
            Reader().CoreWebView2().ShowPrintUI(Microsoft::Web::WebView2::Core::CoreWebView2PrintDialogKind::Browser);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'ouvrir l'impression : " + e.message(), InfoBarSeverity::Error);
        }
    }

    /* Met à jour le bandeau de sécurité cryptographique (OpenPGP / S/MIME) du
     * volet de lecture selon l'objet JSON "security" renvoyé par evo-core.
     * Paramètres :
     *   security : objet JSON ("encrypted", "signed", "valid", "protocol", "signer", "description") */
    void MainWindow::UpdateSecurityBanner(JsonObject const& security)
    {
        bool encrypted = security.GetNamedBoolean(L"encrypted", false);
        bool signed_msg = security.GetNamedBoolean(L"signed", false);

        if (!encrypted && !signed_msg)
        {
            SecurityBar().IsOpen(false);
            return;
        }

        bool valid = security.GetNamedBoolean(L"valid", false);
        hstring protocol = security.GetNamedString(L"protocol", L"");
        hstring signer = security.GetNamedString(L"signer", L"");
        hstring desc = security.GetNamedString(L"description", L"");

        std::wstring title = protocol.empty() ? L"Sécurité" : std::wstring{ protocol } + L" : ";
        if (encrypted && signed_msg)
            title += valid ? L"Message déchiffré et signature vérifiée" : L"Message chiffré / signé (vérification incomplète)";
        else if (encrypted)
            title += valid ? L"Message déchiffré" : L"Échec du déchiffrement";
        else
            title += valid ? L"Signature numérique valide" : L"Signature numérique non vérifiée ou invalide";

        std::wstring msg;
        if (!signer.empty())
            msg = L"Signataire : " + std::wstring{ signer };
        if (!desc.empty())
        {
            if (!msg.empty())
                msg += L" — ";
            msg += desc;
        }

        SecurityBar().Title(hstring{ title });
        SecurityBar().Message(hstring{ msg });
        SecurityBar().Severity(valid ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        SecurityBar().IsOpen(true);
    }

    /* Met à jour la section « Confidentialité » de la vue Paramètres :
     * option globale de contenu externe et listes de confiance. */
    void MainWindow::RefreshPrivacyView()
    {
        auto mode = RemoteContentMode();
        m_updating_privacy_view = true;
        RemoteContentModeChoice().SelectedIndex(mode == L"never" ? 0 : mode == L"always" ? 2 : 1);
        m_updating_privacy_view = false;

        auto items = TrustedSenderList().Items();
        items.Clear();

        auto add_rows = [&](wchar_t const* key, bool domains)
        {
            for (auto const& entry : ReadTrustList(key))
            {
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
                title.Text(hstring{ domains ? L"@" + entry : entry });
                title.IsTextSelectionEnabled(true);
                info.Children().Append(title);
                TextBlock kind;
                kind.Text(domains ? L"Domaine" : L"Expéditeur");
                kind.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                info.Children().Append(kind);
                row.Children().Append(info);

                Button remove_btn;
                remove_btn.Content(box_value(L"Retirer"));
                remove_btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(remove_btn, 1);
                remove_btn.Click([this, key, entry](IInspectable const&, RoutedEventArgs const&)
                {
                    auto list = ReadTrustList(key);
                    list.erase(std::remove(list.begin(), list.end(), entry), list.end());
                    WriteTrustList(key, list);
                    RefreshPrivacyView();
                    /* le message affiché perd la confiance immédiatement */
                    ShowCurrentMessageInReader(false);
                });
                row.Children().Append(remove_btn);

                items.Append(row);
            }
        };
        add_rows(TrustedSendersKey, false);
        add_rows(TrustedDomainsKey, true);

        bool empty = items.Size() == 0;
        TrustedSendersEmpty().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        TrustedSenderList().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
    }

    /* Option globale « Charger le contenu externe des messages ». */
    void MainWindow::OnRemoteContentModeChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        int32_t index = RemoteContentModeChoice().SelectedIndex();
        if (m_updating_privacy_view || index < 0)
            return;

        hstring mode = index == 0 ? L"never" : index == 2 ? L"always" : L"trusted";
        if (std::wstring_view{ mode } == RemoteContentMode())
            return;

        ::EvolutionWin::Settings::Instance().SetString(RemoteContentKey, mode);
        /* le message affiché suit la nouvelle option */
        ShowCurrentMessageInReader(false);
    }

    /**
     * @brief Applique un facteur d'echelle de zoom au volet de lecture WebView2.
     * @param zoom Facteur de zoom (1.0 = 100%, borne entre 0.5 et 3.0).
     */
    void MainWindow::ApplyReaderZoom(double zoom)
    {
        m_reader_zoom = ::EvolutionWin::Helpers::ClampZoomFactor(zoom);
        ::EvolutionWin::Settings::Instance().SetDouble(L"mail.reading-zoom", m_reader_zoom);

        if (m_reader_ready && Reader().CoreWebView2())
        {
            std::wstring js = L"if (document.body) { document.body.style.zoom = '" + std::to_wstring(m_reader_zoom) + L"'; }";
            Reader().CoreWebView2().ExecuteScriptAsync(js);
        }
    }

    void MainWindow::OnZoomInClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(::EvolutionWin::Helpers::StepZoomIn(m_reader_zoom));
        ShowStatus(L"Niveau de zoom : " + hstring{ ::EvolutionWin::Helpers::FormatZoomPercentage(m_reader_zoom) }, InfoBarSeverity::Informational);
    }

    void MainWindow::OnZoomOutClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(::EvolutionWin::Helpers::StepZoomOut(m_reader_zoom));
        ShowStatus(L"Niveau de zoom : " + hstring{ ::EvolutionWin::Helpers::FormatZoomPercentage(m_reader_zoom) }, InfoBarSeverity::Informational);
    }

    void MainWindow::OnZoomResetClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(1.0);
        ShowStatus(L"Zoom reinitialise a 100 %", InfoBarSeverity::Informational);
    }

    /**
     * @brief Bascule l'affichage du courriel entre rendu HTML enrichi et texte brut securise.
     */
    void MainWindow::OnTogglePlainTextClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_view_plain_text = !m_view_plain_text;
        ::EvolutionWin::Settings::Instance().SetBoolean(L"mail.reading-plain-text", m_view_plain_text);

        if (m_current_message)
        {
            ShowCurrentMessageInReader(false);
            ShowStatus(m_view_plain_text ? L"Affichage en texte brut active" : L"Affichage HTML active", InfoBarSeverity::Informational);
        }
    }
}
