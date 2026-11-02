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
            /* Microsoft 365 / Outlook.com */
            scope = L"https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access";
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
            /* Google / Gmail */
            scope = L"https://mail.google.com/";
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

    /* Formulaire d'ajout d'un compte, prérempli par la détection : type
     * proposé (modifiable) et paramètres à confirmer ; crée ensuite les
     * sources EDS correspondantes via evo-core.
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
        panel.Spacing(8);
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
        /* index 2 : collection Google (courrier, agendas CalDAV, contacts CardDAV) */
        provider_box.Items().Append(box_value(L"Google / Gmail (courrier, agenda, contacts)"));
        /* index 3 : compte evolution-ews (Microsoft Graph), sans IMAP/SMTP */
        provider_box.Items().Append(box_value(L"Microsoft 365 / Exchange Online (courrier, agenda, contacts)"));
        /* index 4 : compte evolution-ews (EWS) d'un serveur Exchange de l'entreprise */
        provider_box.Items().Append(box_value(L"Exchange de l'entreprise (EWS, mot de passe)"));
        provider_box.SelectedIndex(0);
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

        TextBox user_box;
        user_box.Header(box_value(L"Nom d'utilisateur"));
        user_box.PlaceholderText(L"Adresse électronique par défaut");
        user_box.Text(prefill.GetNamedString(L"user", L""));
        panel.Children().Append(user_box);

        TextBox ews_url_box;
        ews_url_box.Header(box_value(L"Adresse du serveur EWS"));
        ews_url_box.PlaceholderText(L"https://mail.exemple.fr/EWS/Exchange.asmx");
        ews_url_box.Text(prefill.GetNamedString(L"ews_url", L""));
        ews_url_box.Visibility(Visibility::Collapsed);
        panel.Children().Append(ews_url_box);

        TextBox imap_host_box;
        imap_host_box.Header(box_value(L"Serveur entrant (IMAP)"));
        imap_host_box.PlaceholderText(L"imap.exemple.fr");
        panel.Children().Append(imap_host_box);

        TextBox imap_port_box;
        imap_port_box.Header(box_value(L"Port IMAP (993 = TLS, 143 = STARTTLS)"));
        imap_port_box.Text(L"993");
        panel.Children().Append(imap_port_box);

        TextBox smtp_host_box;
        smtp_host_box.Header(box_value(L"Serveur sortant (SMTP)"));
        smtp_host_box.PlaceholderText(L"smtp.exemple.fr");
        panel.Children().Append(smtp_host_box);

        TextBox smtp_port_box;
        smtp_port_box.Header(box_value(L"Port SMTP (465 = TLS, 587 = STARTTLS)"));
        smtp_port_box.Text(L"465");
        panel.Children().Append(smtp_port_box);

        StackPanel oauth_panel;
        oauth_panel.Spacing(8);
        oauth_panel.Visibility(Visibility::Collapsed);

        wchar_t env_cid[256]{};
        wchar_t env_tid[256]{};
        GetEnvironmentVariableW(L"EVO_OAUTH2_CLIENT_ID", env_cid, 256);
        GetEnvironmentVariableW(L"EVO_OAUTH2_TENANT_ID", env_tid, 256);

        TextBox client_id_box;
        client_id_box.Header(box_value(L"Client ID OAuth2 (Application ID)"));
        client_id_box.PlaceholderText(L"Identifiant d'application Entra ID ou Google Cloud");
        client_id_box.Text(env_cid);
        oauth_panel.Children().Append(client_id_box);

        TextBox tenant_id_box;
        tenant_id_box.Header(box_value(L"Tenant ID Microsoft 365 (ex. common ou ID d'organisation)"));
        tenant_id_box.Text(env_tid[0] ? env_tid : L"common");
        oauth_panel.Children().Append(tenant_id_box);

        panel.Children().Append(oauth_panel);

        PasswordBox password_box;
        password_box.Header(box_value(L"Mot de passe (ou jeton d'accès OAuth2 si déjà obtenu)"));
        panel.Children().Append(password_box);

        provider_box.SelectionChanged([imap_host_box, imap_port_box, smtp_host_box, smtp_port_box, oauth_panel, tenant_id_box, client_id_box,
            password_box, user_box, ews_url_box](IInspectable const& sender, SelectionChangedEventArgs const&)
        {
            int idx = sender.as<ComboBox>().SelectedIndex();

            /* Microsoft Graph : ni serveurs ni mot de passe, connexion OAuth2 ensuite ;
             * Exchange : adresse EWS et mot de passe */
            auto server_visibility = idx == 2 || idx == 3 || idx == 4 ? Visibility::Collapsed : Visibility::Visible;
            imap_host_box.Visibility(server_visibility);
            imap_port_box.Visibility(server_visibility);
            smtp_host_box.Visibility(server_visibility);
            smtp_port_box.Visibility(server_visibility);
            password_box.Visibility(idx == 2 || idx == 3 ? Visibility::Collapsed : Visibility::Visible);
            user_box.Visibility(idx == 2 || idx == 3 ? Visibility::Collapsed : Visibility::Visible);
            ews_url_box.Visibility(idx == 4 ? Visibility::Visible : Visibility::Collapsed);
            client_id_box.PlaceholderText(idx == 3
                ? L"Facultatif : application d'Evolution par défaut"
                : L"Identifiant d'application Entra ID ou Google Cloud");

            if (idx == 3)
            {
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
            }
            else if (idx == 1)
            {
                imap_host_box.Text(L"outlook.office365.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.office365.com");
                smtp_port_box.Text(L"587");
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
                password_box.PlaceholderText(L"Laisser vide pour ouvrir la fenêtre OAuth2 WebView2");
            }
            else if (idx == 2)
            {
                /* connexion Google (identifiant d'application d'EDS), pas de Client ID */
                oauth_panel.Visibility(Visibility::Collapsed);
            }
            else
            {
                oauth_panel.Visibility(Visibility::Collapsed);
                password_box.PlaceholderText(idx == 4 ? L"Mot de passe du compte Windows / Exchange" : L"Mot de passe du compte");
            }
        });

        /* type et serveurs détectés */
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

        ScrollViewer scroll;
        scroll.Content(panel);
        scroll.MaxHeight(540);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        int provider_idx = provider_box.SelectedIndex();
        hstring email{ Trim(email_box.Text()) };
        hstring user = user_box.Text().empty() ? email : hstring{ Trim(user_box.Text()) };
        hstring imap_host{ Trim(imap_host_box.Text()) };
        hstring smtp_host{ Trim(smtp_host_box.Text()) };
        hstring account_name = display_name_box.Text().empty() ? email : display_name_box.Text();

        if (provider_idx == 3)
        {
            if (email.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique du compte Microsoft 365.", InfoBarSeverity::Warning);
                co_return;
            }

            JsonObject m365;
            m365.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            m365.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            m365.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));
            m365.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id_box.Text()));
            /* "common" est la valeur par défaut d'evolution-ews */
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

        if (provider_idx == 2)
        {
            if (email.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse Gmail du compte.", InfoBarSeverity::Warning);
                co_return;
            }

            JsonObject google;
            google.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            google.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            google.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));

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

        if (provider_idx == 4)
        {
            hstring ews_url{ Trim(ews_url_box.Text()) };
            if (email.empty() || ews_url.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique et l'adresse du serveur EWS.", InfoBarSeverity::Warning);
                co_return;
            }

            JsonObject ews;
            ews.SetNamedValue(L"backend", JsonValue::CreateStringValue(L"ews"));
            ews.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            ews.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            ews.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));
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

        if (email.empty() || imap_host.empty() || smtp_host.empty())
        {
            ShowStatus(L"Veuillez renseigner l'adresse électronique et les serveurs IMAP / SMTP.", InfoBarSeverity::Warning);
            co_return;
        }

        hstring secret = password_box.Password();
        if ((provider_idx == 1 || provider_idx == 2) && secret.empty())
        {
            if (client_id_box.Text().empty())
            {
                ShowStatus(L"Veuillez renseigner le Client ID OAuth2 (ou saisir un jeton d'accès).", InfoBarSeverity::Warning);
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

        /* chiffrement détecté tant que le port proposé n'a pas changé, sinon
         * déduit du port */
        auto security = [&prefill](int port, wchar_t const* port_key, wchar_t const* security_key, int starttls_port) -> hstring
        {
            if (prefill.HasKey(port_key) && static_cast<int>(prefill.GetNamedNumber(port_key)) == port)
                return prefill.GetNamedString(security_key, L"tls");
            return port == starttls_port ? L"starttls" : L"tls";
        };

        JsonObject payload;
        payload.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
            full_name_box.Text().empty() ? email : full_name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
        payload.SetNamedValue(L"user", JsonValue::CreateStringValue(user));
        payload.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(imap_host));
        payload.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(imap_port));
        payload.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(
            security(imap_port, L"imap_port", L"imap_security", 143)));
        payload.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(smtp_host));
        payload.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(smtp_port));
        payload.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(
            security(smtp_port, L"smtp_port", L"smtp_security", 587)));
        if (provider_idx == 1 || provider_idx == 2)
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
}
