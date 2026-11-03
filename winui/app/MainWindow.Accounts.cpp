#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "OAuth2LoopbackListener.h"

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

/* Comptes : ajout et détection, authentification (mot de passe, OAuth2,
 * certificat TLS) et vue Comptes des paramètres. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* -------------------------------------------------------------------- */
    /* Étape 2 : Gestion des comptes et boîtes de dialogue d'authentification */
    /* -------------------------------------------------------------------- */

    /* Affiche une boîte de dialogue modale demandant le mot de passe d'une
     * source EDS lorsque celui-ci est absent ou refusé par le serveur, puis
     * l'enregistre et relance la synchronisation.
     * Paramètres :
     *   source_uid : identifiant de la source EDS concernée
     *   name       : nom d'affichage du compte ou du serveur
     *   user       : identifiant utilisateur associé
     *   rejected   : true si le mot de passe précédent a été refusé */
    fire_and_forget MainWindow::PromptPasswordAsync(hstring source_uid, hstring name, hstring user, bool rejected)
    {
        auto strong = get_strong();
        std::wstring banner_id = L"auth:" + std::wstring{ source_uid };
        auto remind = [this, banner_id, source_uid, name, user]()
        {
            /* fenêtre fermée ou déjà une autre boîte ouverte : rappel dans le bandeau */
            ShowBanner(banner_id, L"Connexion requise",
                L"Le compte « " + name + L" » attend son mot de passe.",
                InfoBarSeverity::Warning, L"Saisir le mot de passe",
                [this, source_uid, name, user]() { PromptPasswordAsync(source_uid, name, user, false); });
        };

        if (m_dialog_open || !Content() || !Content().XamlRoot())
        {
            remind();
            co_return;
        }

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Authentification requise"));
        dialog.PrimaryButtonText(L"Valider");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);

        TextBlock description;
        description.TextWrapping(TextWrapping::Wrap);
        description.Text((rejected
            ? L"Le serveur a refusé le mot de passe pour « "
            : L"Veuillez saisir le mot de passe pour « ")
            + name + (user.empty() ? L" » :" : L" » (" + user + L") :"));
        panel.Children().Append(description);

        PasswordBox password_box;
        password_box.PlaceholderText(L"Mot de passe");
        panel.Children().Append(password_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        hstring password = password_box.Password();
        if (result != ContentDialogResult::Primary || password.empty())
        {
            remind();
            co_return;
        }

        m_core->SavePassword(source_uid, password, [this, name, banner_id](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Mot de passe : " + error, InfoBarSeverity::Error);
                return;
            }
            ClearBanner(banner_id);
            ShowStatus(L"Mot de passe enregistré pour « " + name + L" ».", InfoBarSeverity::Success);
            m_core->Refresh();
        });
    }

    /* Affiche une boîte de dialogue modale permettant d'inspecter et d'accepter
     * (pour la session ou de façon permanente) ou de refuser un certificat TLS
     * présentant des erreurs de validation.
     * Paramètres :
     *   object : objet JSON de l'événement "tls-trust-required" */
    fire_and_forget MainWindow::PromptTlsCertificateAsync(JsonObject object)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;
        hstring trust_id = object.GetNamedString(L"id", L"");
        if (trust_id.empty())
            co_return;

        /* Si une autre boîte de dialogue est ouverte, attend sa fermeture sans perdre la demande */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }

        if (!Content() || !Content().XamlRoot() || !m_core)
        {
            if (m_core)
                m_core->RespondTlsTrust(trust_id, 0);
            co_return;
        }

        m_dialog_open = true;

        hstring name = object.GetNamedString(L"name", L"Serveur");
        hstring host = object.GetNamedString(L"host", L"");
        hstring subject = object.GetNamedString(L"subject", host);
        hstring issuer = object.GetNamedString(L"issuer", L"(inconnu)");
        hstring fingerprint = object.GetNamedString(L"fingerprint", L"");
        hstring errors = object.GetNamedString(L"errors", L"");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Certificat TLS non vérifié"));
        dialog.PrimaryButtonText(L"Toujours approuver");
        dialog.SecondaryButtonText(L"Accepter (session)");
        dialog.CloseButtonText(L"Refuser");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(8);
        panel.MaxWidth(480);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(L"Le serveur « " + host + L" » (" + name + L") a présenté un certificat TLS qui n'a pas pu être validé automatiquement :");
        panel.Children().Append(intro);

        TextBlock err_block;
        err_block.TextWrapping(TextWrapping::Wrap);
        err_block.Text(errors);
        err_block.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        panel.Children().Append(err_block);

        TextBlock details;
        details.TextWrapping(TextWrapping::Wrap);
        details.IsTextSelectionEnabled(true);
        details.Text(
            L"Objet : " + subject + L"\n" +
            L"Émetteur : " + issuer + L"\n" +
            L"Empreinte SHA-256 :\n" + fingerprint);
        details.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Cascadia Mono, Consolas" });
        details.FontSize(12);
        panel.Children().Append(details);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (!m_core)
            co_return;

        if (result == ContentDialogResult::Primary)
        {
            m_core->RespondTlsTrust(trust_id, 2);
            ShowStatus(L"Certificat TLS approuvé de façon permanente pour « " + host + L" ».", InfoBarSeverity::Success);
            m_core->Refresh();
        }
        else if (result == ContentDialogResult::Secondary)
        {
            m_core->RespondTlsTrust(trust_id, 1);
            ShowStatus(L"Certificat TLS accepté pour cette session (« " + host + L" »).", InfoBarSeverity::Informational);
            m_core->Refresh();
        }
        else
        {
            m_core->RespondTlsTrust(trust_id, 0);
            ShowStatus(L"Certificat TLS refusé pour « " + host + L" ».", InfoBarSeverity::Warning);
        }
    }

    /* Exécute un flux d'authentification OAuth2 interactif avec PKCE dans un
     * contrôle WebView2 intégré pour Microsoft 365 / Outlook.com ou Google / Gmail,
     * puis échange le code d'autorisation contre un jeton d'accès et un jeton de
     * rafraîchissement.
     * Paramètres :
     *   provider_index : 1 = Microsoft 365 / Outlook.com, 2 = Google / Gmail
     *   client_id      : identifiant d'application OAuth2
     *   tenant_id      : locataire Entra ID ("common" par défaut)
     *   login_hint     : adresse électronique de l'utilisateur
     * Retourne : chaîne JSON contenant access_token, refresh_token, etc., ou "" si annulé. */
    IAsyncOperation<hstring> MainWindow::StartOAuth2SignInAsync(
        int provider_index, hstring client_id, hstring tenant_id, hstring login_hint)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (!Content() || !Content().XamlRoot() || client_id.empty())
            co_return L"";

        /* Génération du code_verifier et du code_challenge PKCE (S256) */
        auto rnd_buf = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(32);
        std::wstring verifier{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(rnd_buf) };
        for (auto& c : verifier)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        verifier.erase(std::remove(verifier.begin(), verifier.end(), L'='), verifier.end());

        auto utf8_ver = Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(
            verifier, Windows::Security::Cryptography::BinaryStringEncoding::Utf8);
        auto sha256 = Windows::Security::Cryptography::Core::HashAlgorithmProvider::OpenAlgorithm(
            Windows::Security::Cryptography::Core::HashAlgorithmNames::Sha256());
        auto hash_buf = sha256.HashData(utf8_ver);
        std::wstring challenge{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(hash_buf) };
        for (auto& c : challenge)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        challenge.erase(std::remove(challenge.begin(), challenge.end(), L'='), challenge.end());

        std::wstring redirect_uri = L"http://localhost/oauth2callback";
        std::wstring tenant = tenant_id.empty() ? L"common" : std::wstring{ tenant_id };
        std::wstring auth_url;
        std::wstring token_endpoint;
        std::wstring scope;

        if (provider_index == 1)
        {
            /* Microsoft 365 / Outlook.com (courrier, calendrier et contacts) */
            scope = L"https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send https://outlook.office.com/Calendars.ReadWrite https://outlook.office.com/Contacts.ReadWrite offline_access";
            auth_url = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/authorize"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&response_mode=query"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/token";
        }
        else
        {
            /* Google / Gmail (courrier, calendrier, contacts et tâches) */
            scope = L"https://mail.google.com/ https://www.googleapis.com/auth/calendar https://www.googleapis.com/auth/carddav https://www.googleapis.com/auth/tasks";
            auth_url = L"https://accounts.google.com/o/oauth2/v2/auth"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&access_type=offline&prompt=consent"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://oauth2.googleapis.com/token";
        }

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(provider_index == 1
            ? L"Connexion OAuth2 — Microsoft 365"
            : L"Connexion OAuth2 — Google / Gmail"));
        dialog.CloseButtonText(L"Annuler");

        WebView2 oauth_web;
        oauth_web.MinWidth(480);
        oauth_web.MinHeight(540);
        dialog.Content(oauth_web);

        auto captured_code = std::make_shared<std::wstring>();
        auto captured_err = std::make_shared<std::wstring>();

        co_await oauth_web.EnsureCoreWebView2Async();
        auto core_wv = oauth_web.CoreWebView2();

        core_wv.NavigationStarting([dialog, captured_code, captured_err, redirect_uri](
            auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring uri{ args.Uri() };
            if (uri.rfind(redirect_uri, 0) == 0)
            {
                args.Cancel(true);
                try
                {
                    Uri parsed{ uri };
                    auto decoder = parsed.QueryParsed();
                    for (uint32_t i = 0; i < decoder.Size(); ++i)
                    {
                        auto entry = decoder.GetAt(i);
                        if (entry.Name() == L"code")
                            *captured_code = entry.Value();
                        else if (entry.Name() == L"error_description" || entry.Name() == L"error")
                            *captured_err = entry.Value();
                    }
                }
                catch (...) {}
                dialog.Hide();
            }
        });

        oauth_web.Source(Uri{ auth_url });
        co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (captured_code->empty())
        {
            if (!captured_err->empty())
                ShowStatus(L"Authentification OAuth2 refusée : " + hstring{ *captured_err }, InfoBarSeverity::Error);
            co_return L"";
        }

        /* Échange du code d'autorisation contre access_token + refresh_token
         * (co_await interdit dans un catch : l'erreur est affichée après) */
        hstring network_error;
        try
        {
            Windows::Web::Http::HttpClient http;
            std::map<hstring, hstring> form_fields{
                { L"client_id", client_id },
                { L"grant_type", L"authorization_code" },
                { L"code", hstring{ *captured_code } },
                { L"redirect_uri", hstring{ redirect_uri } },
                { L"code_verifier", hstring{ verifier } }
            };
            Windows::Web::Http::HttpFormUrlEncodedContent form_content{ form_fields };
            auto response = co_await http.PostAsync(Uri{ token_endpoint }, form_content);
            hstring body = co_await response.Content().ReadAsStringAsync();
            co_await ui_thread;

            JsonObject tok_obj;
            if (JsonObject::TryParse(body, tok_obj))
            {
                hstring access_tok = tok_obj.GetNamedString(L"access_token", L"");
                if (!access_tok.empty())
                {
                    int64_t exp_in = static_cast<int64_t>(tok_obj.GetNamedNumber(L"expires_in", 3600));
                    int64_t now_s = static_cast<int64_t>(std::time(nullptr));
                    tok_obj.SetNamedValue(L"expires_at", JsonValue::CreateNumberValue(static_cast<double>(now_s + exp_in)));
                    tok_obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id));
                    tok_obj.SetNamedValue(L"token_endpoint", JsonValue::CreateStringValue(token_endpoint));
                    co_return tok_obj.Stringify();
                }
                hstring err_desc = tok_obj.GetNamedString(L"error_description", tok_obj.GetNamedString(L"error", L"Jeton invalide"));
                ShowStatus(L"Échange du jeton OAuth2 : " + err_desc, InfoBarSeverity::Error);
            }
        }
        catch (hresult_error const& e)
        {
            network_error = e.message();
        }

        if (!network_error.empty())
        {
            co_await ui_thread;
            ShowStatus(L"Erreur réseau OAuth2 : " + network_error, InfoBarSeverity::Error);
        }

        co_return L"";
    }

    /* Connexion OAuth2 d'un compte géré par EDS (Microsoft 365...), à l'ajout
     * du compte ou sur l'événement "oauth2-required" : la page de connexion du
     * fournisseur s'ouvre dans un WebView2 ; la navigation vers l'adresse de
     * redirection (code d'autorisation) est interceptée et transmise au
     * moteur, qui enregistre les jetons (renouvelés ensuite par EDS).
     * Paramètres :
     *   source_uid : uid du compte ou de sa collection
     *   name       : nom du compte, pour le titre et les messages */
    fire_and_forget MainWindow::SignInOAuth2Async(hstring source_uid, hstring name)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (source_uid.empty() || !m_core)
            co_return;

        /* une autre boîte de dialogue est ouverte : attendre sa fermeture */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }
        if (!Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        /* étape 1 : adresse de connexion (rappel reçu sur ce thread) */
        auto begin_json = std::make_shared<hstring>();
        auto begin_error = std::make_shared<hstring>();
        winrt::handle begun{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE begun_event = begun.get();
        m_core->OAuth2Begin(source_uid, [begin_json, begin_error, begun_event](hstring const& json, hstring const& error)
        {
            *begin_json = json;
            *begin_error = error;
            SetEvent(begun_event);
        });
        co_await winrt::resume_on_signal(begun_event);
        co_await ui_thread;

        JsonObject begin;
        if (!begin_error->empty() || !JsonObject::TryParse(*begin_json, begin))
        {
            m_dialog_open = false;
            ShowStatus(L"Connexion à « " + name + L" » : " + *begin_error, InfoBarSeverity::Error);
            co_return;
        }

        hstring credentials_uid = begin.GetNamedString(L"source_uid", source_uid);
        Uri redirect{ begin.GetNamedString(L"redirect_uri", L"") };
        hstring service = begin.GetNamedString(L"service", L"OAuth2");

        if (_wcsicmp(service.c_str(), L"Google") == 0)
        {
            m_dialog_open = false;
            SignInGoogleOAuth2BrowserAsync(credentials_uid, name);
            co_return;
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Connexion " + service + L" — " + name));
        dialog.CloseButtonText(L"Annuler");

        WebView2 web;
        web.MinWidth(480);
        web.MinHeight(600);
        dialog.Content(web);

        /* même schéma, même hôte et même chemin que l'adresse de redirection :
           fin ; Google revient sur un schéma personnalisé
           (com.googleusercontent.apps…:/oauth2redirect), que WebView2 propose
           de lancer à l'extérieur au lieu de naviguer */
        auto reached = std::make_shared<hstring>();
        auto is_redirect = [redirect](hstring const& value)
        {
            try
            {
                Uri target{ value };
                return _wcsicmp(target.SchemeName().c_str(), redirect.SchemeName().c_str()) == 0 &&
                       _wcsicmp(target.Host().c_str(), redirect.Host().c_str()) == 0 &&
                       _wcsicmp(target.Path().c_str(), redirect.Path().c_str()) == 0;
            }
            catch (...)
            {
                return false;
            }
        };
        web.NavigationStarting([dialog, reached, is_redirect](WebView2 const&,
            Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            if (is_redirect(args.Uri()))
            {
                args.Cancel(true);
                *reached = args.Uri();
                dialog.Hide();
            }
        });
        web.CoreWebView2Initialized([dialog, reached, is_redirect](WebView2 const& sender, auto const&)
        {
            if (!sender.CoreWebView2())
                return;
            sender.CoreWebView2().LaunchingExternalUriScheme([dialog, reached, is_redirect](auto const&,
                Microsoft::Web::WebView2::Core::CoreWebView2LaunchingExternalUriSchemeEventArgs const& args)
            {
                if (is_redirect(args.Uri()))
                {
                    args.Cancel(true);
                    *reached = args.Uri();
                    dialog.Hide();
                }
            });
        });
        web.Source(Uri{ begin.GetNamedString(L"uri", L"") });

        co_await dialog.ShowAsync();
        m_dialog_open = false;

        std::wstring banner_id = L"auth:" + std::wstring{ source_uid };
        if (reached->empty())
        {
            ShowStatus(L"Connexion à « " + name + L" » annulée.", InfoBarSeverity::Warning);
            ShowBanner(banner_id, L"Connexion requise",
                L"Le compte « " + name + L" » n'est pas connecté.",
                InfoBarSeverity::Warning, L"Se connecter",
                [this, source_uid, name]() { SignInOAuth2Async(source_uid, name); });
            co_return;
        }

        /* étape 2 : échange du code et enregistrement des jetons */
        ShowStatus(L"Connexion à « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->OAuth2Finish(credentials_uid, *reached, [this, name, banner_id, source_uid](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Connexion à « " + name + L" » : " + error, InfoBarSeverity::Error);
                ShowBanner(banner_id, L"Connexion requise",
                    L"La connexion au compte « " + name + L" » a échoué : " + error,
                    InfoBarSeverity::Error, L"Réessayer",
                    [this, source_uid, name]() { SignInOAuth2Async(source_uid, name); });
                return;
            }
            ClearBanner(banner_id);
            ShowStatus(L"Connecté à « " + name + L" ».", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* Exécute le flux d'authentification OAuth2 Google via le navigateur système
     * par défaut conformément à la RFC 8252, avec un écouteur loopback local
     * sur 127.0.0.1. Ceci évite le blocage disallowed_useragent de Google dans WebView2.
     * Paramètres :
     *   source_uid : identifiant de la collection ou de la source EDS
     *   name       : nom d'affichage du compte Google */
    fire_and_forget MainWindow::SignInGoogleOAuth2BrowserAsync(hstring source_uid, hstring name)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (source_uid.empty() || !m_core)
            co_return;

        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }
        if (!Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        /* Démarrage du serveur loopback local sur 127.0.0.1 */
        auto loopback = std::make_shared<::EvolutionWin::OAuth2LoopbackListener>();
        auto reached = std::make_shared<hstring>();
        winrt::handle received{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE received_event = received.get();

        hstring redirect_uri = co_await loopback->StartAsync([reached, received_event](hstring const& full_url)
        {
            *reached = full_url;
            SetEvent(received_event);
        });

        /* Étape 1 : Récupération de l'adresse de connexion auprès du moteur */
        auto begin_json = std::make_shared<hstring>();
        auto begin_error = std::make_shared<hstring>();
        winrt::handle begun{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE begun_event = begun.get();

        m_core->OAuth2Begin(source_uid, redirect_uri, [begin_json, begin_error, begun_event](hstring const& json, hstring const& error)
        {
            *begin_json = json;
            *begin_error = error;
            SetEvent(begun_event);
        });
        co_await winrt::resume_on_signal(begun_event);
        co_await ui_thread;

        JsonObject begin;
        if (!begin_error->empty() || !JsonObject::TryParse(*begin_json, begin))
        {
            loopback->Close();
            m_dialog_open = false;
            ShowStatus(L"Connexion Google à « " + name + L" » : " + *begin_error, InfoBarSeverity::Error);
            co_return;
        }

        hstring credentials_uid = begin.GetNamedString(L"source_uid", source_uid);
        hstring auth_url = begin.GetNamedString(L"uri", L"");

        /* Lancement du navigateur Web par défaut */
        co_await Windows::System::Launcher::LaunchUriAsync(Uri{ auth_url });

        /* Boîte de dialogue d'attente conviviale */
        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Connexion Google — " + name));
        dialog.SecondaryButtonText(L"Rouvrir le navigateur");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(14);
        panel.MinWidth(420);

        ProgressRing ring;
        ring.IsActive(true);
        ring.Width(32);
        ring.Height(32);
        panel.Children().Append(ring);

        TextBlock desc;
        desc.TextWrapping(TextWrapping::Wrap);
        desc.Text(L"Une page de connexion Google sécurisée s'est ouverte dans votre navigateur par défaut.\n\n"
                  L"Veuillez vous authentifier et autoriser l'accès pour Evolution. Dès validation, "
                  L"cette boîte se fermera automatiquement.");
        panel.Children().Append(desc);

        HyperlinkButton app_pwd_link;
        app_pwd_link.Content(box_value(L"Aide : créer un mot de passe d'application Google"));
        app_pwd_link.NavigateUri(Uri{ L"https://myaccount.google.com/apppasswords" });
        panel.Children().Append(app_pwd_link);

        dialog.Content(panel);

        dialog.SecondaryButtonClick([auth_url](auto const&, ContentDialogButtonClickEventArgs const& args)
        {
            args.Cancel(true);
            Windows::System::Launcher::LaunchUriAsync(Uri{ auth_url });
        });

        /* Surveillance de la réponse du loopback pour fermer la boîte */
        auto check_and_hide = [dialog, received_event, ui_thread, loopback]() -> fire_and_forget
        {
            co_await winrt::resume_on_signal(received_event);
            co_await ui_thread;
            try
            {
                dialog.Hide();
            }
            catch (...) {}
        };
        check_and_hide();

        co_await dialog.ShowAsync();
        loopback->Close();
        m_dialog_open = false;

        std::wstring banner_id = L"auth:" + std::wstring{ source_uid };
        if (reached->empty())
        {
            ShowStatus(L"Connexion Google à « " + name + L" » annulée.", InfoBarSeverity::Warning);
            ShowBanner(banner_id, L"Connexion requise",
                L"Le compte « " + name + L" » n'est pas connecté.",
                InfoBarSeverity::Warning, L"Se connecter",
                [this, source_uid, name]() { SignInGoogleOAuth2BrowserAsync(source_uid, name); });
            co_return;
        }

        /* Étape 2 : Échange du code contre les jetons EDS */
        ShowStatus(L"Finalisation de la connexion à « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->OAuth2Finish(credentials_uid, *reached, [this, name, banner_id, source_uid](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Connexion Google à « " + name + L" » : " + error, InfoBarSeverity::Error);
                ShowBanner(banner_id, L"Connexion requise",
                    L"La connexion au compte Google « " + name + L" » a échoué : " + error,
                    InfoBarSeverity::Error, L"Réessayer",
                    [this, source_uid, name]() { SignInGoogleOAuth2BrowserAsync(source_uid, name); });
                return;
            }
            ClearBanner(banner_id);
            ShowStatus(L"Connecté avec succès au compte Google « " + name + L" ».", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* Met à jour la liste des comptes affichée dans la vue Paramètres. */
    void MainWindow::RefreshAccountsView()
    {
        auto items = AccountList().Items();
        items.Clear();

        if (!m_accounts)
            return;

        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            hstring uid = account.GetNamedString(L"uid", L"");
            hstring name = account.GetNamedString(L"name", uid);
            hstring address = account.GetNamedString(L"address", L"");
            hstring backend = account.GetNamedString(L"backend", L"");
            bool local = account.GetNamedBoolean(L"local", false);
            bool is_default = account.GetNamedBoolean(L"default", false);

            Grid row;
            row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition action_col;
            action_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(action_col);

            StackPanel info;
            info.Spacing(2);

            StackPanel title_line;
            title_line.Orientation(Orientation::Horizontal);
            title_line.Spacing(8);
            TextBlock title;
            title.Text(name);
            title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            title_line.Children().Append(title);
            if (is_default)
            {
                /* badge du compte par défaut */
                Border badge;
                badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                badge.Padding(ThicknessHelper::FromLengths(6, 1, 6, 1));
                badge.VerticalAlignment(VerticalAlignment::Center);
                badge.Background(Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>());
                TextBlock badge_text;
                badge_text.Text(L"Par défaut");
                badge_text.FontSize(11);
                badge_text.Foreground(Application::Current().Resources().Lookup(box_value(L"TextOnAccentFillColorPrimaryBrush")).as<Brush>());
                badge.Child(badge_text);
                title_line.Children().Append(badge);
            }
            info.Children().Append(title_line);

            TextBlock subtitle;
            subtitle.Text((address.empty() ? L"Compte local" : address) +
                (backend.empty() ? hstring{} : L"  •  Protocole : " + backend));
            subtitle.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            info.Children().Append(subtitle);

            row.Children().Append(info);

            if (!local)
            {
                StackPanel actions;
                actions.Orientation(Orientation::Horizontal);
                actions.Spacing(8);
                actions.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(actions, 1);

                if (!is_default)
                {
                    Button default_btn;
                    default_btn.Content(box_value(L"Définir par défaut"));
                    ToolTipService::SetToolTip(default_btn, box_value(L"Compte utilisé pour les nouveaux messages"));
                    default_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
                    {
                        m_core->SetDefaultAccount(uid, [this, name](hstring const&, hstring const& error)
                        {
                            if (!error.empty())
                            {
                                ShowStatus(L"Compte par défaut : " + error, InfoBarSeverity::Error);
                                return;
                            }
                            ShowStatus(L"« " + name + L" » est le compte par défaut.", InfoBarSeverity::Success);
                            LoadAccounts();
                        });
                    });
                    actions.Children().Append(default_btn);
                }

                Button edit_btn;
                edit_btn.Content(box_value(L"Modifier"));
                ToolTipService::SetToolTip(edit_btn, box_value(L"Modifier les serveurs, le mot de passe et les dossiers spéciaux"));
                edit_btn.Click([this, uid](IInspectable const&, RoutedEventArgs const&)
                {
                    ShowEditAccountDialogAsync(uid);
                });
                actions.Children().Append(edit_btn);

                Button remove_btn;
                remove_btn.Content(box_value(L"Supprimer"));
                remove_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->RemoveAccount(uid, [this, name](hstring const&, hstring const& error)
                    {
                        if (!error.empty())
                        {
                            ShowStatus(L"Suppression du compte : " + error, InfoBarSeverity::Error);
                            return;
                        }
                        ShowStatus(L"Compte « " + name + L" » supprimé.", InfoBarSeverity::Informational);
                        LoadAccounts();
                    });
                });
                actions.Children().Append(remove_btn);
                row.Children().Append(actions);
            }

            items.Append(row);
        }
    }

    /* Gestionnaire du bouton « Ajouter un compte ». */
    void MainWindow::OnAddAccountClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAccountDetectionDialogAsync();
    }

    /* Première étape de l'ajout d'un compte : adresse et nom, puis détection
     * du type de compte et des serveurs par evo-core (fournisseurs connus,
     * MX, Autodiscover, autoconfig, SRV) pendant que la boîte reste ouverte.
     * « Configuration manuelle » passe directement au formulaire. */
    fire_and_forget MainWindow::ShowAccountDetectionDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un compte"));
        dialog.PrimaryButtonText(L"Suivant");
        dialog.SecondaryButtonText(L"Configuration manuelle");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(440);

        TextBlock intro;
        intro.Text(L"Le type de compte (Microsoft 365, Exchange, Gmail, IMAP / SMTP) et les serveurs "
                   L"sont détectés à partir de votre adresse ; vous pourrez les vérifier ensuite.");
        intro.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(intro);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"pierre@exemple.fr");
        panel.Children().Append(email_box);

        TextBox full_name_box;
        full_name_box.Header(box_value(L"Votre nom complet"));
        full_name_box.PlaceholderText(L"Pierre Dupont");
        panel.Children().Append(full_name_box);

        TextBlock invalid;
        invalid.Text(L"Saisissez une adresse électronique complète (nom@domaine).");
        invalid.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
        invalid.Visibility(Visibility::Collapsed);
        panel.Children().Append(invalid);

        StackPanel progress;
        progress.Orientation(Orientation::Horizontal);
        progress.Spacing(8);
        progress.Visibility(Visibility::Collapsed);
        ProgressRing ring;
        ring.IsActive(true);
        ring.Width(16);
        ring.Height(16);
        progress.Children().Append(ring);
        TextBlock progress_text;
        progress_text.Text(L"Détection du type de compte…");
        progress.Children().Append(progress_text);
        panel.Children().Append(progress);

        dialog.Content(panel);

        /* détection pendant que la boîte reste ouverte (report du clic) */
        auto detected = std::make_shared<JsonObject>(nullptr);
        dialog.PrimaryButtonClick([this, email_box, full_name_box, invalid, progress, detected](
            ContentDialog const& sender, ContentDialogButtonClickEventArgs const& args)
        {
            std::wstring email{ Trim(email_box.Text()) };
            auto at = email.find(L'@');
            if (at == std::wstring::npos || at == 0 || email.find(L'.', at) == std::wstring::npos)
            {
                invalid.Visibility(Visibility::Visible);
                email_box.Focus(FocusState::Programmatic);
                args.Cancel(true);
                return;
            }

            invalid.Visibility(Visibility::Collapsed);
            progress.Visibility(Visibility::Visible);
            email_box.IsEnabled(false);
            full_name_box.IsEnabled(false);
            sender.IsPrimaryButtonEnabled(false);
            sender.IsSecondaryButtonEnabled(false);

            auto deferral = args.GetDeferral();
            m_core->DetectAccount(hstring{ email }, [deferral, detected](hstring const& json, hstring const& error)
            {
                JsonObject result;
                if (error.empty() && JsonObject::TryParse(json, result))
                    *detected = result;
                /* boîte déjà fermée entre-temps : rien à terminer */
                try
                {
                    deferral.Complete();
                }
                catch (...)
                {
                    ::EvolutionWin::LogCurrentException(L"détection du compte");
                }
            });
        });

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::None)
            co_return;

        JsonObject prefill = result == ContentDialogResult::Primary && *detected ? *detected : JsonObject{};
        prefill.SetNamedValue(L"email", JsonValue::CreateStringValue(hstring{ Trim(email_box.Text()) }));
        prefill.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name_box.Text()));
        ShowAddAccountDialogAsync(prefill);
    }

    /* Libellés de la détection (evo_core_detect_account) */
    static hstring DetectedTypeLabel(hstring const& type)
    {
        if (type == L"m365") return L"Microsoft 365 / Outlook.com";
        if (type == L"exchange") return L"Exchange (serveur de l'entreprise)";
        if (type == L"google") return L"Google / Gmail";
        return L"IMAP / SMTP";
    }

    static hstring DetectedSourceLabel(hstring const& source)
    {
        if (source == L"known") return L"base des fournisseurs connus (ISPDB de Thunderbird)";
        if (source == L"mx") return L"serveurs de messagerie (MX) du domaine";
        if (source == L"autodiscover") return L"Autodiscover Exchange / Microsoft 365";
        if (source == L"autoconfig") return L"fichier autoconfig publié par le domaine";
        if (source == L"ispdb") return L"base ISPDB en ligne de Thunderbird";
        if (source == L"srv") return L"enregistrements DNS SRV du domaine";
        return source;
    }

    /* Structure représentant une option de dossier pour l'attribution des rôles spéciaux. */
    struct FolderChoice
    {
        hstring display_label;
        hstring full_name;
    };

    /* Remplit récursivement la liste plate des dossiers d'un compte.
     * Paramètres :
     *   folders : tableau JSON des dossiers retourné par le moteur
     *   out     : vecteur recevant les options de dossiers
     *   prefix  : préfixe pour les sous-dossiers */
    static void FlattenFoldersForChoice(
        JsonArray const& folders,
        std::vector<FolderChoice>& out,
        hstring const& prefix = L"")
    {
        if (!folders)
            return;
        for (auto const& item : folders)
        {
            if (item.ValueType() != JsonValueType::Object)
                continue;
            auto obj = item.GetObject();
            hstring full_name = obj.GetNamedString(L"full_name", L"");
            hstring name = obj.GetNamedString(L"name", full_name);
            bool selectable = obj.GetNamedBoolean(L"selectable", true);
            hstring label = prefix.empty() ? name : prefix + L" / " + name;

            if (selectable && !full_name.empty())
            {
                out.push_back({ label, full_name });
            }

            if (obj.HasKey(L"children"))
            {
                auto children = obj.GetNamedArray(L"children", nullptr);
                if (children && children.Size() > 0)
                    FlattenFoldersForChoice(children, out, label);
            }
        }
    }

    /* Crée et initialise une liste déroulante pour la sélection d'une méthode de sécurité.
     * Paramètres :
     *   default_sec : nom de la méthode par défaut ("tls", "starttls", "none")
     * Retourne : ComboBox configurée */
    static ComboBox MakeSecurityComboBox(hstring const& default_sec)
    {
        ComboBox combo;
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        combo.Items().Append(box_value(L"TLS (chiffré dès la connexion)"));
        combo.Items().Append(box_value(L"STARTTLS (négociation chiffrée)"));
        combo.Items().Append(box_value(L"Aucun (non chiffré)"));
        if (default_sec == L"starttls")
            combo.SelectedIndex(1);
        else if (default_sec == L"none")
            combo.SelectedIndex(2);
        else
            combo.SelectedIndex(0);
        return combo;
    }

    /* Convertit l'indice sélectionné d'une ComboBox de sécurité en chaîne Camel ("tls", "starttls", "none").
     * Paramètres :
     *   combo : ComboBox de sécurité
     * Retourne : méthode de sécurité sous forme de chaîne */
    static hstring GetSecurityMethodString(ComboBox const& combo)
    {
        int idx = combo.SelectedIndex();
        if (idx == 1) return L"starttls";
        if (idx == 2) return L"none";
        return L"tls";
    }

    /* Crée une ligne de sélection de dossier spécial avec en-tête et liste déroulante.
     * Paramètres :
     *   label       : libellé du rôle de dossier
     *   current_val : valeur actuellement affectée (nom de dossier)
     *   options     : liste des dossiers disponibles
     *   parent      : panneau conteneur
     * Retourne : ComboBox configurée */
    static ComboBox MakeSpecialFolderRow(
        hstring const& label,
        hstring const& current_val,
        std::vector<FolderChoice> const& options,
        StackPanel& parent)
    {
        StackPanel row;
        row.Spacing(4);

        TextBlock header;
        header.Text(label);
        header.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        row.Children().Append(header);

        ComboBox combo;
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);

        ComboBoxItem def_item;
        def_item.Content(box_value(L"(Dossier par défaut du système)"));
        def_item.Tag(box_value(L""));
        combo.Items().Append(def_item);

        int selected_idx = 0;
        int idx = 1;
        for (auto const& opt : options)
        {
            ComboBoxItem c_item;
            c_item.Content(box_value(opt.display_label));
            c_item.Tag(box_value(opt.full_name));
            if (!current_val.empty() && opt.full_name == current_val)
            {
                selected_idx = idx;
            }
            combo.Items().Append(c_item);
            idx++;
        }

        combo.SelectedIndex(selected_idx);
        row.Children().Append(combo);
        parent.Children().Append(row);
        return combo;
    }

    /* Récupère la valeur du dossier sélectionné dans une ComboBox de dossier spécial.
     * Paramètres :
     *   combo : ComboBox du dossier
     * Retourne : chemin du dossier sélectionné ou chaîne vide si par défaut */
    static hstring GetSelectedFolderChoice(ComboBox const& combo)
    {
        int idx = combo.SelectedIndex();
        if (idx <= 0 || idx >= static_cast<int>(combo.Items().Size()))
            return L"";
        auto item = combo.Items().GetAt(idx).as<ComboBoxItem>();
        return unbox_value_or<hstring>(item.Tag(), L"");
    }

    /* Formulaire d'ajout d'un compte épuré : masque les serveurs techniques sous
     * un Expander replié par défaut, propose l'authentification moderne ou mot de passe,
     * et intègre un test de connectivité préalable avant création effective.
     * Paramètres :
     *   prefill : {"email", "full_name"} et le résultat de la détection
     *             (evo_core_detect_account), vide en configuration manuelle */
    fire_and_forget MainWindow::ShowAddAccountDialogAsync(JsonObject prefill)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un compte"));
        dialog.PrimaryButtonText(L"Ajouter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(440);

        hstring detected_type = prefill.GetNamedString(L"type", L"");
        if (!detected_type.empty())
        {
            hstring source = prefill.GetNamedString(L"source", L"");
            hstring provider = prefill.GetNamedString(L"provider", L"");
            InfoBar detection;
            detection.IsOpen(true);
            detection.IsClosable(false);
            if (source == L"guess")
            {
                detection.Severity(InfoBarSeverity::Warning);
                detection.Title(L"Aucune configuration trouvée");
                detection.Message(L"Ce domaine ne publie pas sa configuration : les serveurs proposés "
                                  L"sont supposés. Vérifiez-les auprès de votre fournisseur.");
            }
            else
            {
                detection.Severity(InfoBarSeverity::Success);
                detection.Title(L"Type détecté : " + DetectedTypeLabel(detected_type));
                detection.Message((provider.empty() ? hstring{} : provider + L" — ") + L"d'après : " +
                                  DetectedSourceLabel(source) + L". Vérifiez les paramètres proposés ou "
                                  L"choisissez un autre type de compte.");
            }
            panel.Children().Append(detection);
        }

        ComboBox provider_box;
        provider_box.Header(box_value(L"Type de compte"));
        provider_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        provider_box.Items().Append(box_value(L"IMAP / SMTP standard (mot de passe)"));
        provider_box.Items().Append(box_value(L"Microsoft 365 / Outlook.com (OAuth2 XOAUTH2)"));
        provider_box.Items().Append(box_value(L"Google / Gmail (courrier, agenda, contacts)"));
        provider_box.Items().Append(box_value(L"Microsoft 365 / Exchange Online (Graph)"));
        provider_box.Items().Append(box_value(L"Exchange de l'entreprise (EWS, mot de passe)"));
        panel.Children().Append(provider_box);

        TextBox display_name_box;
        display_name_box.Header(box_value(L"Nom du compte"));
        display_name_box.PlaceholderText(L"Personnel, Travail…");
        panel.Children().Append(display_name_box);

        TextBox full_name_box;
        full_name_box.Header(box_value(L"Votre nom complet"));
        full_name_box.PlaceholderText(L"Pierre Dupont");
        full_name_box.Text(prefill.GetNamedString(L"full_name", L""));
        panel.Children().Append(full_name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"pierre@exemple.fr");
        email_box.Text(prefill.GetNamedString(L"email", L""));
        panel.Children().Append(email_box);

        PasswordBox password_box;
        password_box.Header(box_value(L"Mot de passe"));
        password_box.PlaceholderText(L"Mot de passe du compte");
        panel.Children().Append(password_box);

        /* Panneau spécifique pour la connexion Google / Gmail */
        StackPanel google_panel;
        google_panel.Spacing(8);
        google_panel.Visibility(Visibility::Collapsed);

        TextBlock google_info;
        google_info.TextWrapping(TextWrapping::Wrap);
        google_info.Text(L"Authentification moderne Google : la connexion s'effectuera en toute sécurité "
                         L"dans votre navigateur par défaut (RFC 8252, compatible avec la validation en deux étapes).");
        google_panel.Children().Append(google_info);

        CheckBox google_app_pwd_check;
        google_app_pwd_check.Content(box_value(L"Utiliser un mot de passe d'application dédié"));
        google_panel.Children().Append(google_app_pwd_check);

        StackPanel google_app_pwd_panel;
        google_app_pwd_panel.Spacing(4);
        google_app_pwd_panel.Visibility(Visibility::Collapsed);

        PasswordBox google_app_pwd_box;
        google_app_pwd_box.Header(box_value(L"Mot de passe d'application Google (16 caractères)"));
        google_app_pwd_box.PlaceholderText(L"xxxx xxxx xxxx xxxx");
        google_app_pwd_panel.Children().Append(google_app_pwd_box);

        HyperlinkButton google_help_btn;
        google_help_btn.Content(box_value(L"Comment créer un mot de passe d'application Google ?"));
        google_help_btn.NavigateUri(Uri{ L"https://myaccount.google.com/apppasswords" });
        google_app_pwd_panel.Children().Append(google_help_btn);

        google_panel.Children().Append(google_app_pwd_panel);
        panel.Children().Append(google_panel);

        /* Panneau d'information pour Microsoft 365 */
        TextBlock m365_info;
        m365_info.TextWrapping(TextWrapping::Wrap);
        m365_info.Text(L"La fenêtre d'authentification sécurisée Microsoft 365 (OAuth2) s'ouvrira "
                       L"automatiquement après validation.");
        m365_info.Visibility(Visibility::Collapsed);
        panel.Children().Append(m365_info);

        /* Expander replié par défaut pour masquer les détails techniques des serveurs */
        Expander server_expander;
        server_expander.Header(box_value(L"Paramètres du serveur avancés"));
        server_expander.IsExpanded(false);
        server_expander.HorizontalAlignment(HorizontalAlignment::Stretch);

        StackPanel server_panel;
        server_panel.Spacing(8);
        server_panel.Padding(ThicknessHelper::FromLengths(10, 8, 10, 8));

        TextBox user_box;
        user_box.Header(box_value(L"Nom d'utilisateur (si différent de l'adresse)"));
        user_box.PlaceholderText(L"Adresse électronique par défaut");
        user_box.Text(prefill.GetNamedString(L"user", L""));
        server_panel.Children().Append(user_box);

        TextBox ews_url_box;
        ews_url_box.Header(box_value(L"Adresse du serveur EWS"));
        ews_url_box.PlaceholderText(L"https://mail.exemple.fr/EWS/Exchange.asmx");
        ews_url_box.Text(prefill.GetNamedString(L"ews_url", L""));
        ews_url_box.Visibility(Visibility::Collapsed);
        server_panel.Children().Append(ews_url_box);

        /* Paramètres IMAP */
        Grid imap_grid;
        ColumnDefinition col_ih, col_ip;
        col_ih.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
        col_ip.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        imap_grid.ColumnDefinitions().Append(col_ih);
        imap_grid.ColumnDefinitions().Append(col_ip);

        TextBox imap_host_box;
        imap_host_box.Header(box_value(L"Serveur entrant (IMAP)"));
        imap_host_box.PlaceholderText(L"imap.exemple.fr");
        imap_host_box.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
        Grid::SetColumn(imap_host_box, 0);
        imap_grid.Children().Append(imap_host_box);

        TextBox imap_port_box;
        imap_port_box.Header(box_value(L"Port entrant"));
        imap_port_box.Text(L"993");
        Grid::SetColumn(imap_port_box, 1);
        imap_grid.Children().Append(imap_port_box);
        server_panel.Children().Append(imap_grid);

        ComboBox imap_sec_combo = MakeSecurityComboBox(prefill.GetNamedString(L"imap_security", L"tls"));
        imap_sec_combo.Header(box_value(L"Sécurité IMAP"));
        server_panel.Children().Append(imap_sec_combo);

        /* Paramètres SMTP */
        Grid smtp_grid;
        ColumnDefinition col_sh, col_sp;
        col_sh.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
        col_sp.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        smtp_grid.ColumnDefinitions().Append(col_sh);
        smtp_grid.ColumnDefinitions().Append(col_sp);

        TextBox smtp_host_box;
        smtp_host_box.Header(box_value(L"Serveur sortant (SMTP)"));
        smtp_host_box.PlaceholderText(L"smtp.exemple.fr");
        smtp_host_box.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
        Grid::SetColumn(smtp_host_box, 0);
        smtp_grid.Children().Append(smtp_host_box);

        TextBox smtp_port_box;
        smtp_port_box.Header(box_value(L"Port sortant"));
        smtp_port_box.Text(L"465");
        Grid::SetColumn(smtp_port_box, 1);
        smtp_grid.Children().Append(smtp_port_box);
        server_panel.Children().Append(smtp_grid);

        ComboBox smtp_sec_combo = MakeSecurityComboBox(prefill.GetNamedString(L"smtp_security", L"tls"));
        smtp_sec_combo.Header(box_value(L"Sécurité SMTP"));
        server_panel.Children().Append(smtp_sec_combo);

        /* Paramètres OAuth pour Microsoft 365 */
        StackPanel oauth_panel;
        oauth_panel.Spacing(8);
        oauth_panel.Visibility(Visibility::Collapsed);

        wchar_t env_cid[256]{};
        wchar_t env_tid[256]{};
        GetEnvironmentVariableW(L"EVO_OAUTH2_CLIENT_ID", env_cid, 256);
        GetEnvironmentVariableW(L"EVO_OAUTH2_TENANT_ID", env_tid, 256);

        TextBox client_id_box;
        client_id_box.Header(box_value(L"Client ID OAuth2"));
        client_id_box.PlaceholderText(L"Identifiant d'application Entra ID ou Google Cloud");
        client_id_box.Text(env_cid);
        oauth_panel.Children().Append(client_id_box);

        TextBox tenant_id_box;
        tenant_id_box.Header(box_value(L"Tenant ID Microsoft 365"));
        tenant_id_box.Text(env_tid[0] ? env_tid : L"common");
        oauth_panel.Children().Append(tenant_id_box);
        server_panel.Children().Append(oauth_panel);

        /* Test de connectivité préalable */
        StackPanel test_area;
        test_area.Spacing(6);
        test_area.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));

        StackPanel test_btn_row;
        test_btn_row.Orientation(Orientation::Horizontal);
        test_btn_row.Spacing(8);

        Button test_btn;
        test_btn.Content(box_value(L"Tester la connectivité des serveurs"));
        test_btn_row.Children().Append(test_btn);

        ProgressRing test_ring;
        test_ring.IsActive(false);
        test_ring.Width(18);
        test_ring.Height(18);
        test_ring.Visibility(Visibility::Collapsed);
        test_btn_row.Children().Append(test_ring);
        test_area.Children().Append(test_btn_row);

        TextBlock test_status;
        test_status.TextWrapping(TextWrapping::Wrap);
        test_status.FontSize(12);
        test_status.Visibility(Visibility::Collapsed);
        test_area.Children().Append(test_status);

        CheckBox skip_test_check;
        skip_test_check.Content(box_value(L"Ignorer les erreurs de test et ajouter quand même"));
        test_area.Children().Append(skip_test_check);

        server_panel.Children().Append(test_area);
        server_expander.Content(server_panel);
        panel.Children().Append(server_expander);

        /* Fonction locale pour exécuter le test de connectivité */
        auto connection_tested_ok = std::make_shared<bool>(false);
        auto run_test_async = [this, imap_host_box, imap_port_box, imap_sec_combo,
                               smtp_host_box, smtp_port_box, smtp_sec_combo,
                               test_btn, test_ring, test_status, connection_tested_ok](auto on_done)
        {
            hstring target_imap_host{ Trim(imap_host_box.Text()) };
            hstring target_smtp_host{ Trim(smtp_host_box.Text()) };
            if (target_imap_host.empty() && target_smtp_host.empty())
            {
                test_status.Text(L"Veuillez renseigner au moins un serveur entrant ou sortant.");
                test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
                test_status.Visibility(Visibility::Visible);
                on_done(false);
                return;
            }

            test_btn.IsEnabled(false);
            test_ring.IsActive(true);
            test_ring.Visibility(Visibility::Visible);
            test_status.Text(L"Vérification de la connectivité réseau et TLS en cours…");
            test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorPrimaryBrush")).as<Brush>());
            test_status.Visibility(Visibility::Visible);

            JsonObject req;
            req.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(target_imap_host));
            int i_port = _wtoi(imap_port_box.Text().c_str());
            req.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(i_port > 0 ? i_port : 993));
            req.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(GetSecurityMethodString(imap_sec_combo)));

            req.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(target_smtp_host));
            int s_port = _wtoi(smtp_port_box.Text().c_str());
            req.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(s_port > 0 ? s_port : 465));
            req.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(GetSecurityMethodString(smtp_sec_combo)));

            m_core->TestConnection(req.Stringify(), [test_btn, test_ring, test_status, connection_tested_ok, on_done](
                hstring const& res_json, hstring const& error)
            {
                test_btn.IsEnabled(true);
                test_ring.IsActive(false);
                test_ring.Visibility(Visibility::Collapsed);

                if (!error.empty())
                {
                    test_status.Text(L"Erreur de test : " + error);
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
                    *connection_tested_ok = false;
                    on_done(false);
                    return;
                }

                JsonObject res;
                if (!JsonObject::TryParse(res_json, res))
                {
                    test_status.Text(L"Réponse de test invalide");
                    *connection_tested_ok = false;
                    on_done(false);
                    return;
                }

                bool imap_ok = res.GetNamedBoolean(L"imap_ok", false);
                bool smtp_ok = res.GetNamedBoolean(L"smtp_ok", false);
                hstring imap_err = res.GetNamedString(L"imap_error", L"");
                hstring smtp_err = res.GetNamedString(L"smtp_error", L"");

                if (imap_ok && smtp_ok)
                {
                    test_status.Text(L"✓ Connectivité établie avec succès pour IMAP et SMTP.");
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorSuccessBrush")).as<Brush>());
                    *connection_tested_ok = true;
                    on_done(true);
                }
                else
                {
                    std::wstring msg;
                    if (!imap_ok) msg += L"Échec IMAP : " + std::wstring{ imap_err } + L"  ";
                    if (!smtp_ok) msg += L"Échec SMTP : " + std::wstring{ smtp_err };
                    test_status.Text(hstring{ msg });
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
                    *connection_tested_ok = false;
                    on_done(false);
                }
            });
        };

        test_btn.Click([run_test_async](IInspectable const&, RoutedEventArgs const&)
        {
            run_test_async([](bool) {});
        });

        /* Gestionnaire de changement de mot de passe d'application Google */
        google_app_pwd_check.Click([google_app_pwd_check, google_app_pwd_panel, password_box, server_expander,
                                    imap_host_box, imap_port_box, smtp_host_box, smtp_port_box](IInspectable const&, RoutedEventArgs const&)
        {
            bool use_app_pwd = google_app_pwd_check.IsChecked().Value();
            google_app_pwd_panel.Visibility(use_app_pwd ? Visibility::Visible : Visibility::Collapsed);
            password_box.Visibility(Visibility::Collapsed);
            server_expander.Visibility(use_app_pwd ? Visibility::Visible : Visibility::Collapsed);
            if (use_app_pwd)
            {
                imap_host_box.Text(L"imap.gmail.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.gmail.com");
                smtp_port_box.Text(L"465");
            }
        });

        /* Mise à jour dynamique de la visibilité selon le type sélectionné */
        auto update_provider_ui = [provider_box, password_box, google_panel, google_app_pwd_check,
                                   google_app_pwd_panel, m365_info, server_expander, ews_url_box,
                                   oauth_panel, imap_grid, smtp_grid, imap_sec_combo, smtp_sec_combo,
                                   imap_host_box, imap_port_box, smtp_host_box, smtp_port_box]()
        {
            int idx = provider_box.SelectedIndex();
            google_panel.Visibility(idx == 2 ? Visibility::Visible : Visibility::Collapsed);
            m365_info.Visibility(idx == 1 || idx == 3 ? Visibility::Visible : Visibility::Collapsed);

            bool is_google_app_pwd = (idx == 2 && google_app_pwd_check.IsChecked().Value());

            if (idx == 2)
            {
                password_box.Visibility(Visibility::Collapsed);
                server_expander.Visibility(is_google_app_pwd ? Visibility::Visible : Visibility::Collapsed);
                ews_url_box.Visibility(Visibility::Collapsed);
                oauth_panel.Visibility(Visibility::Collapsed);
                imap_grid.Visibility(Visibility::Visible);
                smtp_grid.Visibility(Visibility::Visible);
                imap_sec_combo.Visibility(Visibility::Visible);
                smtp_sec_combo.Visibility(Visibility::Visible);
            }
            else if (idx == 3) /* Microsoft Graph */
            {
                password_box.Visibility(Visibility::Collapsed);
                server_expander.Visibility(Visibility::Visible);
                ews_url_box.Visibility(Visibility::Collapsed);
                oauth_panel.Visibility(Visibility::Visible);
                imap_grid.Visibility(Visibility::Collapsed);
                smtp_grid.Visibility(Visibility::Collapsed);
                imap_sec_combo.Visibility(Visibility::Collapsed);
                smtp_sec_combo.Visibility(Visibility::Collapsed);
            }
            else if (idx == 4) /* Exchange EWS */
            {
                password_box.Visibility(Visibility::Visible);
                password_box.PlaceholderText(L"Mot de passe Windows / Exchange");
                server_expander.Visibility(Visibility::Visible);
                ews_url_box.Visibility(Visibility::Visible);
                oauth_panel.Visibility(Visibility::Collapsed);
                imap_grid.Visibility(Visibility::Collapsed);
                smtp_grid.Visibility(Visibility::Collapsed);
                imap_sec_combo.Visibility(Visibility::Collapsed);
                smtp_sec_combo.Visibility(Visibility::Collapsed);
            }
            else if (idx == 1) /* Microsoft 365 IMAP/SMTP */
            {
                password_box.Visibility(Visibility::Collapsed);
                server_expander.Visibility(Visibility::Visible);
                ews_url_box.Visibility(Visibility::Collapsed);
                oauth_panel.Visibility(Visibility::Visible);
                imap_grid.Visibility(Visibility::Visible);
                smtp_grid.Visibility(Visibility::Visible);
                imap_sec_combo.Visibility(Visibility::Visible);
                smtp_sec_combo.Visibility(Visibility::Visible);
                imap_host_box.Text(L"outlook.office365.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.office365.com");
                smtp_port_box.Text(L"587");
            }
            else /* IMAP / SMTP standard */
            {
                password_box.Visibility(Visibility::Visible);
                password_box.PlaceholderText(L"Mot de passe de messagerie");
                server_expander.Visibility(Visibility::Visible);
                ews_url_box.Visibility(Visibility::Collapsed);
                oauth_panel.Visibility(Visibility::Collapsed);
                imap_grid.Visibility(Visibility::Visible);
                smtp_grid.Visibility(Visibility::Visible);
                imap_sec_combo.Visibility(Visibility::Visible);
                smtp_sec_combo.Visibility(Visibility::Visible);
            }
        };

        provider_box.SelectionChanged([update_provider_ui](IInspectable const&, SelectionChangedEventArgs const&)
        {
            update_provider_ui();
        });

        /* Initialisation d'après la détection */
        provider_box.SelectedIndex(detected_type == L"m365" ? 3
            : detected_type == L"google" ? 2
            : detected_type == L"exchange" ? 4 : 0);

        if (!prefill.GetNamedString(L"imap_host", L"").empty())
        {
            imap_host_box.Text(prefill.GetNamedString(L"imap_host"));
            imap_port_box.Text(to_hstring(static_cast<int>(prefill.GetNamedNumber(L"imap_port", 993))));
            smtp_host_box.Text(prefill.GetNamedString(L"smtp_host", L""));
            smtp_port_box.Text(to_hstring(static_cast<int>(prefill.GetNamedNumber(L"smtp_port", 465))));
        }
        update_provider_ui();

        /* Validation et test préalable avant création effective */
        auto ready_to_commit = std::make_shared<bool>(false);
        dialog.PrimaryButtonClick([this, provider_box, email_box, display_name_box, full_name_box,
                                   password_box, google_app_pwd_check, google_app_pwd_box,
                                   imap_host_box, imap_port_box, imap_sec_combo,
                                   smtp_host_box, smtp_port_box, smtp_sec_combo,
                                   user_box, ews_url_box, client_id_box, tenant_id_box,
                                   skip_test_check, server_expander, run_test_async,
                                   connection_tested_ok, ready_to_commit](
            ContentDialog const& sender, ContentDialogButtonClickEventArgs const& args)
        {
            if (*ready_to_commit)
                return;

            int provider_idx = provider_box.SelectedIndex();
            hstring email{ Trim(email_box.Text()) };
            if (email.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique.", InfoBarSeverity::Warning);
                args.Cancel(true);
                return;
            }

            bool is_google_standard = (provider_idx == 2 && !google_app_pwd_check.IsChecked().Value());
            bool is_m365_graph = (provider_idx == 3);
            bool is_ews = (provider_idx == 4);

            if (is_google_standard || is_m365_graph || is_ews)
            {
                *ready_to_commit = true;
                return;
            }

            /* Pour IMAP / SMTP standard ou mot de passe d'application Google */
            hstring imap_host{ Trim(imap_host_box.Text()) };
            hstring smtp_host{ Trim(smtp_host_box.Text()) };
            if (imap_host.empty() || smtp_host.empty())
            {
                ShowStatus(L"Veuillez renseigner les serveurs IMAP et SMTP dans les paramètres avancés.", InfoBarSeverity::Warning);
                server_expander.IsExpanded(true);
                args.Cancel(true);
                return;
            }

            if (skip_test_check.IsChecked().Value() || *connection_tested_ok)
            {
                *ready_to_commit = true;
                return;
            }

            /* Test préalable requis : suspendre le clic */
            args.Cancel(true);
            auto deferral = args.GetDeferral();
            run_test_async([sender, ready_to_commit, server_expander, deferral](bool ok)
            {
                if (ok)
                {
                    *ready_to_commit = true;
                    try
                    {
                        sender.Hide();
                    }
                    catch (...) {}
                }
                else
                {
                    server_expander.IsExpanded(true);
                }
                try
                {
                    deferral.Complete();
                }
                catch (...) {}
            });
        });

        ScrollViewer scroll;
        scroll.Content(panel);
        scroll.MaxHeight(540);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary && !*ready_to_commit)
            co_return;

        int provider_idx = provider_box.SelectedIndex();
        hstring email{ Trim(email_box.Text()) };
        hstring user = user_box.Text().empty() ? email : hstring{ Trim(user_box.Text()) };
        hstring imap_host{ Trim(imap_host_box.Text()) };
        hstring smtp_host{ Trim(smtp_host_box.Text()) };
        hstring account_name = display_name_box.Text().empty() ? email : display_name_box.Text();
        hstring full_name = full_name_box.Text().empty() ? email : full_name_box.Text();

        /* Cas 1 : Microsoft 365 Exchange Online (Graph) */
        if (provider_idx == 3)
        {
            JsonObject m365;
            m365.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            m365.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            m365.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name));
            m365.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id_box.Text()));
            m365.SetNamedValue(L"tenant", JsonValue::CreateStringValue(
                tenant_id_box.Text() == L"common" ? L"" : tenant_id_box.Text()));

            m_core->AddMicrosoft365Account(m365.Stringify(), [this, account_name](hstring const& json, hstring const& error)
            {
                JsonObject created;
                if (!error.empty() || !JsonObject::TryParse(json, created))
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                LoadAccounts();
                SignInOAuth2Async(created.GetNamedString(L"collection_uid", L""), account_name);
            });
            co_return;
        }

        /* Cas 2 : Google / Gmail standard (OAuth2 via navigateur système RFC 8252) */
        if (provider_idx == 2 && !google_app_pwd_check.IsChecked().Value())
        {
            JsonObject google;
            google.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            google.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            google.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name));

            m_core->AddGoogleAccount(google.Stringify(), [this, account_name](hstring const& json, hstring const& error)
            {
                JsonObject created;
                if (!error.empty() || !JsonObject::TryParse(json, created))
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                LoadAccounts();
                SignInOAuth2Async(created.GetNamedString(L"collection_uid", L""), account_name);
            });
            co_return;
        }

        /* Cas 3 : Exchange EWS */
        if (provider_idx == 4)
        {
            hstring ews_url{ Trim(ews_url_box.Text()) };
            JsonObject ews;
            ews.SetNamedValue(L"backend", JsonValue::CreateStringValue(L"ews"));
            ews.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            ews.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            ews.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name));
            ews.SetNamedValue(L"hosturl", JsonValue::CreateStringValue(ews_url));
            ews.SetNamedValue(L"user", JsonValue::CreateStringValue(user));
            ews.SetNamedValue(L"password", JsonValue::CreateStringValue(password_box.Password()));

            m_core->AddMicrosoft365Account(ews.Stringify(), [this, account_name](hstring const&, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(L"Compte « " + account_name + L" » ajouté.", InfoBarSeverity::Success);
                LoadAccounts();
            });
            co_return;
        }

        /* Cas 4 : IMAP / SMTP standard ou mot de passe d'application Google */
        hstring secret = (provider_idx == 2 && google_app_pwd_check.IsChecked().Value())
            ? google_app_pwd_box.Password()
            : password_box.Password();

        if (provider_idx == 1 && secret.empty())
        {
            if (client_id_box.Text().empty())
            {
                ShowStatus(L"Veuillez renseigner le Client ID OAuth2.", InfoBarSeverity::Warning);
                co_return;
            }
            secret = co_await StartOAuth2SignInAsync(provider_idx, client_id_box.Text(), tenant_id_box.Text(), email);
            if (secret.empty())
                co_return;
        }

        int imap_port = _wtoi(imap_port_box.Text().c_str());
        if (imap_port <= 0) imap_port = 993;
        int smtp_port = _wtoi(smtp_port_box.Text().c_str());
        if (smtp_port <= 0) smtp_port = 465;

        JsonObject payload;
        payload.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
        payload.SetNamedValue(L"user", JsonValue::CreateStringValue(user));
        payload.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(imap_host));
        payload.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(imap_port));
        payload.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(GetSecurityMethodString(imap_sec_combo)));
        payload.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(smtp_host));
        payload.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(smtp_port));
        payload.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(GetSecurityMethodString(smtp_sec_combo)));
        if (provider_idx == 1)
            payload.SetNamedValue(L"auth_mechanism", JsonValue::CreateStringValue(L"XOAUTH2"));
        payload.SetNamedValue(L"password", JsonValue::CreateStringValue(secret));

        m_core->AddAccount(payload.Stringify(), [this, email](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Compte « " + email + L" » ajouté.", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* Affiche une boîte de dialogue modale complète permettant d'éditer un compte
     * existant : modification de ses serveurs IMAP / SMTP, réauthentification ou
     * changement de mot de passe, et attribution des rôles de dossiers spéciaux
     * (CAMEL_FOLDER_TYPE_* : Envoyés, Brouillons, Corbeille, Indésirables, Archives).
     * Paramètres :
     *   account_uid : identifiant unique du compte à modifier */
    fire_and_forget MainWindow::ShowEditAccountDialogAsync(hstring account_uid)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (account_uid.empty() || account_uid == L"local" || !m_core)
            co_return;

        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }

        if (!Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        /* Étape 1 : Récupération des propriétés détaillées du compte auprès du moteur */
        auto acct_json = std::make_shared<hstring>();
        auto acct_error = std::make_shared<hstring>();
        winrt::handle done{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE done_event = done.get();

        m_core->GetAccount(account_uid, [acct_json, acct_error, done_event](hstring const& json, hstring const& error)
        {
            *acct_json = json;
            *acct_error = error;
            SetEvent(done_event);
        });
        co_await winrt::resume_on_signal(done_event);
        co_await ui_thread;

        JsonObject acct;
        if (!acct_error->empty() || !JsonObject::TryParse(*acct_json, acct))
        {
            m_dialog_open = false;
            ShowStatus(L"Impossible de charger les paramètres du compte : " + *acct_error, InfoBarSeverity::Error);
            co_return;
        }

        hstring display_name = acct.GetNamedString(L"display_name", acct.GetNamedString(L"name", account_uid));
        hstring real_name = acct.GetNamedString(L"real_name", L"");
        hstring email = acct.GetNamedString(L"email", acct.GetNamedString(L"address", L""));
        bool is_default_initial = acct.GetNamedBoolean(L"default", false);
        bool is_enabled_initial = acct.GetNamedBoolean(L"enabled", true);

        hstring imap_host = acct.GetNamedString(L"imap_host", L"");
        int imap_port = static_cast<int>(acct.GetNamedNumber(L"imap_port", 993));
        hstring imap_user = acct.GetNamedString(L"imap_user", L"");
        hstring imap_security = acct.GetNamedString(L"imap_security", L"tls");

        hstring smtp_host = acct.GetNamedString(L"smtp_host", L"");
        int smtp_port = static_cast<int>(acct.GetNamedNumber(L"smtp_port", 465));
        hstring smtp_user = acct.GetNamedString(L"smtp_user", L"");
        hstring smtp_security = acct.GetNamedString(L"smtp_security", L"tls");

        JsonObject sf_obj = acct.GetNamedObject(L"special_folders", JsonObject{});
        hstring cur_sent = sf_obj.GetNamedString(L"sent", L"");
        hstring cur_drafts = sf_obj.GetNamedString(L"drafts", L"");
        hstring cur_trash = sf_obj.GetNamedString(L"trash", L"");
        hstring cur_junk = sf_obj.GetNamedString(L"junk", L"");
        hstring cur_archive = sf_obj.GetNamedString(L"archive", L"");

        /* Récupération des dossiers du compte pour les listes déroulantes */
        std::vector<FolderChoice> folder_choices;
        auto folders_it = m_account_folders.find(std::wstring{ account_uid });
        if (folders_it != m_account_folders.end() && folders_it->second)
        {
            FlattenFoldersForChoice(folders_it->second, folder_choices);
        }

        /* Construction de la boîte de dialogue */
        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Propriétés du compte — " + display_name));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel main_panel;
        main_panel.Spacing(12);
        main_panel.MinWidth(460);

        /* Section 1 : Identité du compte */
        TextBlock sec1_header;
        sec1_header.Text(L"Identité du compte");
        sec1_header.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
        main_panel.Children().Append(sec1_header);

        TextBox display_name_box;
        display_name_box.Header(box_value(L"Nom d'affichage du compte"));
        display_name_box.Text(display_name);
        main_panel.Children().Append(display_name_box);

        TextBox real_name_box;
        real_name_box.Header(box_value(L"Votre nom complet (expéditeur)"));
        real_name_box.Text(real_name);
        main_panel.Children().Append(real_name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.Text(email);
        main_panel.Children().Append(email_box);

        CheckBox is_enabled_check;
        is_enabled_check.Content(box_value(L"Activer ce compte (synchronisation et relève des courriels)"));
        is_enabled_check.IsChecked(is_enabled_initial);
        main_panel.Children().Append(is_enabled_check);

        CheckBox is_default_check;
        is_default_check.Content(box_value(L"Définir comme compte par défaut pour les nouveaux messages"));
        is_default_check.IsChecked(is_default_initial);
        main_panel.Children().Append(is_default_check);

        /* Section 2 : Configuration des serveurs et sécurité */
        Expander server_expander;
        server_expander.Header(box_value(L"Configuration des serveurs et authentification"));
        server_expander.IsExpanded(!imap_host.empty());
        server_expander.HorizontalAlignment(HorizontalAlignment::Stretch);

        StackPanel server_panel;
        server_panel.Spacing(8);
        server_panel.Padding(ThicknessHelper::FromLengths(10, 8, 10, 8));

        TextBox imap_host_box;
        imap_host_box.Header(box_value(L"Serveur entrant (IMAP)"));
        imap_host_box.Text(imap_host);
        server_panel.Children().Append(imap_host_box);

        Grid imap_port_grid;
        ColumnDefinition col_ip, col_is;
        col_ip.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        col_is.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
        imap_port_grid.ColumnDefinitions().Append(col_ip);
        imap_port_grid.ColumnDefinitions().Append(col_is);

        TextBox imap_port_box;
        imap_port_box.Header(box_value(L"Port IMAP"));
        imap_port_box.Text(to_hstring(imap_port));
        imap_port_box.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
        Grid::SetColumn(imap_port_box, 0);
        imap_port_grid.Children().Append(imap_port_box);

        ComboBox imap_sec_combo = MakeSecurityComboBox(imap_security);
        imap_sec_combo.Header(box_value(L"Sécurité IMAP"));
        Grid::SetColumn(imap_sec_combo, 1);
        imap_port_grid.Children().Append(imap_sec_combo);
        server_panel.Children().Append(imap_port_grid);

        TextBox imap_user_box;
        imap_user_box.Header(box_value(L"Nom d'utilisateur IMAP"));
        imap_user_box.Text(imap_user);
        server_panel.Children().Append(imap_user_box);

        TextBox smtp_host_box;
        smtp_host_box.Header(box_value(L"Serveur sortant (SMTP)"));
        smtp_host_box.Text(smtp_host);
        server_panel.Children().Append(smtp_host_box);

        Grid smtp_port_grid;
        ColumnDefinition col_sp, col_ss;
        col_sp.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        col_ss.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
        smtp_port_grid.ColumnDefinitions().Append(col_sp);
        smtp_port_grid.ColumnDefinitions().Append(col_ss);

        TextBox smtp_port_box;
        smtp_port_box.Header(box_value(L"Port SMTP"));
        smtp_port_box.Text(to_hstring(smtp_port));
        smtp_port_box.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
        Grid::SetColumn(smtp_port_box, 0);
        smtp_port_grid.Children().Append(smtp_port_box);

        ComboBox smtp_sec_combo = MakeSecurityComboBox(smtp_security);
        smtp_sec_combo.Header(box_value(L"Sécurité SMTP"));
        Grid::SetColumn(smtp_sec_combo, 1);
        smtp_port_grid.Children().Append(smtp_sec_combo);
        server_panel.Children().Append(smtp_port_grid);

        TextBox smtp_user_box;
        smtp_user_box.Header(box_value(L"Nom d'utilisateur SMTP"));
        smtp_user_box.Text(smtp_user);
        server_panel.Children().Append(smtp_user_box);

        PasswordBox new_pwd_box;
        new_pwd_box.Header(box_value(L"Modifier le mot de passe"));
        new_pwd_box.PlaceholderText(L"Laisser vide pour conserver le mot de passe actuel");
        server_panel.Children().Append(new_pwd_box);

        /* Bouton de test de connectivité des serveurs modifiés */
        StackPanel test_row;
        test_row.Orientation(Orientation::Horizontal);
        test_row.Spacing(8);
        test_row.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));

        Button test_conn_btn;
        test_conn_btn.Content(box_value(L"Tester la connexion"));
        test_row.Children().Append(test_conn_btn);

        ProgressRing test_ring;
        test_ring.IsActive(false);
        test_ring.Width(18);
        test_ring.Height(18);
        test_ring.Visibility(Visibility::Collapsed);
        test_row.Children().Append(test_ring);
        server_panel.Children().Append(test_row);

        TextBlock test_status;
        test_status.TextWrapping(TextWrapping::Wrap);
        test_status.FontSize(12);
        test_status.Visibility(Visibility::Collapsed);
        server_panel.Children().Append(test_status);

        test_conn_btn.Click([this, imap_host_box, imap_port_box, imap_sec_combo,
                             smtp_host_box, smtp_port_box, smtp_sec_combo,
                             test_conn_btn, test_ring, test_status](IInspectable const&, RoutedEventArgs const&)
        {
            test_conn_btn.IsEnabled(false);
            test_ring.IsActive(true);
            test_ring.Visibility(Visibility::Visible);
            test_status.Text(L"Test de connectivité en cours…");
            test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorPrimaryBrush")).as<Brush>());
            test_status.Visibility(Visibility::Visible);

            JsonObject req;
            req.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(Trim(imap_host_box.Text())));
            int ip = _wtoi(imap_port_box.Text().c_str());
            req.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(ip > 0 ? ip : 993));
            req.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(GetSecurityMethodString(imap_sec_combo)));

            req.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(Trim(smtp_host_box.Text())));
            int sp = _wtoi(smtp_port_box.Text().c_str());
            req.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(sp > 0 ? sp : 465));
            req.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(GetSecurityMethodString(smtp_sec_combo)));

            m_core->TestConnection(req.Stringify(), [test_conn_btn, test_ring, test_status](hstring const& res_json, hstring const& error)
            {
                test_conn_btn.IsEnabled(true);
                test_ring.IsActive(false);
                test_ring.Visibility(Visibility::Collapsed);

                if (!error.empty())
                {
                    test_status.Text(L"Erreur de test : " + error);
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
                    return;
                }

                JsonObject res;
                if (!JsonObject::TryParse(res_json, res))
                {
                    test_status.Text(L"Réponse de test invalide");
                    return;
                }

                bool imap_ok = res.GetNamedBoolean(L"imap_ok", false);
                bool smtp_ok = res.GetNamedBoolean(L"smtp_ok", false);
                hstring imap_err = res.GetNamedString(L"imap_error", L"");
                hstring smtp_err = res.GetNamedString(L"smtp_error", L"");

                if (imap_ok && smtp_ok)
                {
                    test_status.Text(L"✓ Serveurs IMAP et SMTP connectés avec succès.");
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorSuccessBrush")).as<Brush>());
                }
                else
                {
                    std::wstring msg;
                    if (!imap_ok) msg += L"Échec IMAP : " + std::wstring{ imap_err } + L"  ";
                    if (!smtp_ok) msg += L"Échec SMTP : " + std::wstring{ smtp_err };
                    test_status.Text(hstring{ msg });
                    test_status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
                }
            });
        });

        server_expander.Content(server_panel);
        main_panel.Children().Append(server_expander);

        /* Section 3 : Attribution des rôles de dossiers spéciaux (CAMEL_FOLDER_TYPE_*) */
        Expander sf_expander;
        sf_expander.Header(box_value(L"Rôles des dossiers spéciaux"));
        sf_expander.IsExpanded(true);
        sf_expander.HorizontalAlignment(HorizontalAlignment::Stretch);

        StackPanel sf_panel;
        sf_panel.Spacing(10);
        sf_panel.Padding(ThicknessHelper::FromLengths(10, 8, 10, 8));

        TextBlock sf_desc;
        sf_desc.TextWrapping(TextWrapping::Wrap);
        sf_desc.Text(L"Associez les dossiers de ce compte aux fonctionnalités d'Evolution "
                     L"(messages envoyés, brouillons, corbeille, courrier indésirable et archives).");
        sf_panel.Children().Append(sf_desc);

        ComboBox sent_combo = MakeSpecialFolderRow(L"Dossier des messages envoyés (Sent)", cur_sent, folder_choices, sf_panel);
        ComboBox drafts_combo = MakeSpecialFolderRow(L"Dossier des brouillons (Drafts)", cur_drafts, folder_choices, sf_panel);
        ComboBox trash_combo = MakeSpecialFolderRow(L"Corbeille (Trash)", cur_trash, folder_choices, sf_panel);
        ComboBox junk_combo = MakeSpecialFolderRow(L"Courrier indésirable (Junk)", cur_junk, folder_choices, sf_panel);
        ComboBox archive_combo = MakeSpecialFolderRow(L"Archives (Archive)", cur_archive, folder_choices, sf_panel);

        sf_expander.Content(sf_panel);
        main_panel.Children().Append(sf_expander);

        ScrollViewer scroll;
        scroll.Content(main_panel);
        scroll.MaxHeight(560);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        /* Préparation du JSON de modification */
        JsonObject update;
        update.SetNamedValue(L"uid", JsonValue::CreateStringValue(account_uid));
        update.SetNamedValue(L"display_name", JsonValue::CreateStringValue(display_name_box.Text()));
        update.SetNamedValue(L"real_name", JsonValue::CreateStringValue(real_name_box.Text()));
        update.SetNamedValue(L"email", JsonValue::CreateStringValue(email_box.Text()));

        update.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(Trim(imap_host_box.Text())));
        int new_ip = _wtoi(imap_port_box.Text().c_str());
        update.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(new_ip > 0 ? new_ip : 993));
        update.SetNamedValue(L"imap_user", JsonValue::CreateStringValue(Trim(imap_user_box.Text())));
        update.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(GetSecurityMethodString(imap_sec_combo)));

        update.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(Trim(smtp_host_box.Text())));
        int new_sp = _wtoi(smtp_port_box.Text().c_str());
        update.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(new_sp > 0 ? new_sp : 465));
        update.SetNamedValue(L"smtp_user", JsonValue::CreateStringValue(Trim(smtp_user_box.Text())));
        update.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(GetSecurityMethodString(smtp_sec_combo)));

        hstring new_pwd = new_pwd_box.Password();
        if (!new_pwd.empty())
            update.SetNamedValue(L"password", JsonValue::CreateStringValue(new_pwd));

        JsonObject new_sf;
        new_sf.SetNamedValue(L"sent", JsonValue::CreateStringValue(GetSelectedFolderChoice(sent_combo)));
        new_sf.SetNamedValue(L"drafts", JsonValue::CreateStringValue(GetSelectedFolderChoice(drafts_combo)));
        new_sf.SetNamedValue(L"trash", JsonValue::CreateStringValue(GetSelectedFolderChoice(trash_combo)));
        new_sf.SetNamedValue(L"junk", JsonValue::CreateStringValue(GetSelectedFolderChoice(junk_combo)));
        new_sf.SetNamedValue(L"archive", JsonValue::CreateStringValue(GetSelectedFolderChoice(archive_combo)));
        update.SetNamedValue(L"special_folders", new_sf);
        update.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(is_enabled_check.IsChecked().Value()));

        bool new_default = is_default_check.IsChecked().Value();

        ShowStatus(L"Enregistrement des modifications pour « " + display_name_box.Text() + L" »…", InfoBarSeverity::Informational);

        m_core->ModifyAccount(update.Stringify(), [this, account_uid, display_name = display_name_box.Text(), new_default, is_default_initial](
            hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Modification du compte : " + error, InfoBarSeverity::Error);
                return;
            }

            if (new_default && !is_default_initial)
            {
                m_core->SetDefaultAccount(account_uid, [this, display_name](hstring const&, hstring const&)
                {
                    LoadAccounts();
                    ShowStatus(L"Compte « " + display_name + L" » mis à jour et défini par défaut.", InfoBarSeverity::Success);
                });
            }
            else
            {
                LoadAccounts();
                ShowStatus(L"Compte « " + display_name + L" » mis à jour avec succès.", InfoBarSeverity::Success);
            }
            m_core->Refresh();
        });
    }
    /**
     * @brief Affiche la boîte de dialogue d'espace de stockage et quota du compte actif.
     * @param sender Bouton émetteur.
     * @param args Arguments d'événement.
     */
    fire_and_forget MainWindow::OnRibbonAccountQuotaClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (m_account.empty() || !m_core)
        {
            ShowStatus(L"Aucun compte n'est actuellement sélectionné.", InfoBarSeverity::Warning);
            co_return;
        }

        hstring account_uid = m_account;
        hstring account_name = account_uid;
        if (m_accounts)
        {
            for (auto const& item : m_accounts)
            {
                if (auto obj = item.try_as<JsonObject>())
                {
                    if (obj.GetNamedString(L"uid", L"") == account_uid)
                    {
                        account_name = obj.GetNamedString(L"name", account_uid);
                        break;
                    }
                }
            }
        }

        ShowStatus(L"Interrogation du quota pour « " + account_name + L" »…", InfoBarSeverity::Informational);

        auto quota_json = std::make_shared<hstring>();
        auto quota_err = std::make_shared<hstring>();
        winrt::handle done{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE done_event = done.get();

        m_core->GetFolderQuota(account_uid, L"INBOX", [quota_json, quota_err, done_event](hstring const& json, hstring const& err)
        {
            *quota_json = json;
            *quota_err = err;
            SetEvent(done_event);
        });

        co_await winrt::resume_on_signal(done_event);
        co_await ui_thread;

        JsonObject res;
        bool has_quota = false;
        int64_t used = 0;
        int64_t total = 0;
        int percent = 0;
        hstring q_name = L"Espace de stockage";

        if (quota_err->empty() && JsonObject::TryParse(*quota_json, res))
        {
            has_quota = res.GetNamedBoolean(L"has_quota", false);
            if (has_quota)
            {
                used = static_cast<int64_t>(res.GetNamedNumber(L"used", 0));
                total = static_cast<int64_t>(res.GetNamedNumber(L"total", 0));
                percent = static_cast<int>(res.GetNamedNumber(L"percent", 0));
                q_name = res.GetNamedString(L"name", L"Stockage");
            }
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Espace de stockage — " + account_name));
        dialog.CloseButtonText(L"Fermer");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(400);

        if (has_quota)
        {
            TextBlock info_txt;
            info_txt.Text(q_name + L" sur le serveur :");
            info_txt.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            panel.Children().Append(info_txt);

            ProgressBar pbar;
            pbar.Minimum(0);
            pbar.Maximum(100);
            pbar.Value(percent);
            pbar.Height(8);
            panel.Children().Append(pbar);

            TextBlock usage_txt;
            hstring used_str = FormatFileSize(used);
            hstring total_str = FormatFileSize(total);
            usage_txt.Text(used_str + L" utilisés sur " + total_str + L" (" + to_hstring(percent) + L" %)");
            usage_txt.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
            panel.Children().Append(usage_txt);
        }
        else
        {
            TextBlock no_q;
            no_q.Text(L"Le serveur de messagerie pour « " + account_name + L" » ne publie pas de limite de quota ou n'utilise pas l'extension IMAP QUOTA.");
            no_q.TextWrapping(TextWrapping::Wrap);
            panel.Children().Append(no_q);
        }

        TextBlock cleanup_hdr;
        cleanup_hdr.Text(L"Outils de nettoyage rapide :");
        cleanup_hdr.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
        cleanup_hdr.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        panel.Children().Append(cleanup_hdr);

        Button empty_trash_btn;
        empty_trash_btn.Content(box_value(L"Vider la Corbeille de ce compte"));
        empty_trash_btn.Click([this, account_uid](IInspectable const&, RoutedEventArgs const&)
        {
            if (m_core)
            {
                m_core->FolderAction(account_uid, L"Trash", L"empty", [this](hstring const&, hstring const& err)
                {
                    if (err.empty())
                        ShowStatus(L"Corbeille vidée avec succès.", InfoBarSeverity::Success);
                });
            }
        });
        panel.Children().Append(empty_trash_btn);

        dialog.Content(panel);
        co_await dialog.ShowAsync();
    }
}
