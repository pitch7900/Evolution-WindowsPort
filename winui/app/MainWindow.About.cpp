#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "Policy.h"
#include "UpdateIntegrity.h"

#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#include <bcrypt.h>
#include <fstream>
#include <filesystem>
#include <memory>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "bcrypt.lib")

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
#include <winrt/Windows.UI.Text.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <functional>
#include <vector>

/* version de l'application, depuis le fichier VERSION (généré par MSBuild) */
#include "evo-version.h"
#define EVO_WIDEN_(text) L ## text
#define EVO_WIDEN(text) EVO_WIDEN_(text)
#define EVO_VERSION_W EVO_WIDEN(EVO_VERSION_STR)

/* Boîte « À propos » : version, recherche de mise à jour (GitHub Releases,
 * aussi au démarrage), composants et rapport de bug. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    /* dossier de l'exécutable (sans barre oblique finale) */
    std::wstring ExecutableDirectory()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring dir{ path };
        return dir.substr(0, dir.find_last_of(L'\\'));
    }

    /* « 24H2 (build 26100.4061) », pour les rapports de bug */
    std::wstring WindowsVersionText()
    {
        constexpr wchar_t key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        wchar_t display[64]{}, build[32]{};
        DWORD size = sizeof(display), ubr = 0, ubr_size = sizeof(ubr);

        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display, &size);
        size = sizeof(build);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"CurrentBuild", RRF_RT_REG_SZ, nullptr, build, &size);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &ubr_size);

        return std::wstring{ display } + L" (build " + build + L"." + std::to_wstring(ubr) + L")";
    }

    /* préférence : dernière version dont l'utilisateur a été prévenu au démarrage */
    constexpr wchar_t UpdateNotifiedKey[] = L"update.notified-version";

    constexpr wchar_t LatestReleaseApi[] = L"https://api.github.com/repos/pitch7900/Evolution-WindowsPort/releases/latest";

    /* « v0.9.5 » ou « 0.9.5 » en { 0, 9, 5 } (les suffixes non numériques sont ignorés) */
    std::vector<int> ParseVersion(std::wstring_view text)
    {
        std::vector<int> parts;
        if (!text.empty() && (text[0] == L'v' || text[0] == L'V'))
            text.remove_prefix(1);

        int value = 0;
        bool in_number = false;
        for (wchar_t c : text)
        {
            if (c >= L'0' && c <= L'9')
            {
                value = value * 10 + (c - L'0');
                in_number = true;
                continue;
            }
            if (in_number)
                parts.push_back(value);
            if (c != L'.')
                return parts;
            value = 0;
            in_number = false;
        }
        if (in_number)
            parts.push_back(value);
        return parts;
    }

    /* la version latest est-elle strictement plus récente que current ? */
    bool IsNewerVersion(std::wstring_view latest, std::wstring_view current)
    {
        auto a = ParseVersion(latest);
        auto b = ParseVersion(current);
        size_t size = a.size() > b.size() ? a.size() : b.size();
        a.resize(size);
        b.resize(size);
        return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
    }

    struct UpdateCheck
    {
        hstring failure;	/* non vide si la vérification a échoué */
        hstring tag;	/* dernière version publiée (« v0.9.4 ») */
        hstring page;	/* page de cette version */
        hstring msi_url;	/* installeur MSI joint à la version (vide : aucun) */
        hstring msi_name;
        hstring sha_url;	/* empreinte SHA-256 de l'installeur (fichier .sha256) */
        bool newer{ false };	/* plus récente que la version installée */
    };

    /* Compare la version locale à la dernière version publiée sur GitHub
     * (Releases). Le rappel est appelé sur le thread de l'interface.
     * Paramètres :
     *   done : reçoit le résultat (succès ou raison de l'échec) */
    fire_and_forget CheckForUpdate(std::function<void(UpdateCheck const&)> done)
    {
        apartment_context ui_thread;

        /* co_await interdit dans un catch : l'erreur est notée puis traitée après */
        UpdateCheck check;
        try
        {
            Windows::Web::Http::HttpClient http;
            auto headers = http.DefaultRequestHeaders();
            headers.UserAgent().ParseAdd(L"EvolutionWin/" EVO_VERSION_W);
            headers.Accept().ParseAdd(L"application/vnd.github+json");
            auto response = co_await http.GetAsync(Uri{ LatestReleaseApi });
            hstring body = co_await response.Content().ReadAsStringAsync();

            JsonObject release;
            if (response.StatusCode() == Windows::Web::Http::HttpStatusCode::NotFound)
                check.failure = Tr(L"aucune version publiée");
            else if (!response.IsSuccessStatusCode() || !JsonObject::TryParse(body, release))
                check.failure = Fmt(Tr(L"réponse GitHub {0}"), { to_hstring(static_cast<int>(response.StatusCode())) });
            else
            {
                check.tag = release.GetNamedString(L"tag_name", L"");
                check.page = release.GetNamedString(L"html_url", L"");
                for (auto const& value : release.GetNamedArray(L"assets", JsonArray{}))
                {
                    auto asset = value.GetObject();
                    std::wstring name{ asset.GetNamedString(L"name", L"") };
                    hstring url = asset.GetNamedString(L"browser_download_url", L"");
                    auto ends_with = [&name](std::wstring_view suffix)
                    {
                        return name.size() >= suffix.size() &&
                            ::EvolutionWin::UpdateIntegrity::EqualsNoCase(std::wstring_view{ name }.substr(name.size() - suffix.size()), suffix);
                    };
                    if (ends_with(L".msi"))
                    {
                        check.msi_url = url;
                        check.msi_name = hstring{ name };
                    }
                    else if (ends_with(L".msi.sha256"))
                        check.sha_url = url;
                }
                if (check.tag.empty())
                    check.failure = Tr(L"version inconnue");
                else
                    check.newer = IsNewerVersion(check.tag.c_str(), EVO_VERSION_W);
            }
        }
        catch (hresult_error const& e)
        {
            check.failure = e.message();
        }

        co_await ui_thread;
        done(check);
    }

    /* Affiche le résultat dans la boîte « À propos ».
     * Paramètres :
     *   status : ligne sous le numéro de version
     *   banner : bandeau mis en évidence, ouvert si une nouvelle version existe */
    void ShowUpdateResult(UpdateCheck const& check, TextBlock const& status, InfoBar const& banner,
                          std::function<void()> const& install = nullptr)
    {
        if (!check.failure.empty())
        {
            status.Text(Fmt(Tr(L"Mise à jour : vérification impossible ({0})"), { check.failure }));
            return;
        }
        if (!check.newer)
        {
            status.Text(Tr(L"Vous utilisez la dernière version."));
            return;
        }

        status.Text(Fmt(Tr(L"Nouvelle version disponible : {0}"), { check.tag }));
        status.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        status.Opacity(1);
        banner.Title(Fmt(Tr(L"Nouvelle version disponible : {0}"), { check.tag }));
        banner.Message(Fmt(Tr(L"Vous utilisez la version {0}."), { EVO_VERSION_W }));
        try
        {
            if (install && !check.msi_url.empty())
            {
                Button download;
                download.Content(box_value(Tr(L"Télécharger et installer")));
                download.Click([install](IInspectable const&, RoutedEventArgs const&) { install(); });
                banner.ActionButton(download);
            }
            else
            {
                HyperlinkButton download;
                download.Content(box_value(Tr(L"Télécharger")));
                download.NavigateUri(Uri{ check.page });
                banner.ActionButton(download);
            }
        }
        catch (hresult_error const&) {}
        banner.IsOpen(true);
    }
}

namespace
{
    Style LookupAboutStyle(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<Style>();
    }
}

namespace
{
    /* Résultat du téléchargement d'une mise à jour. */
    struct UpdateDownload
    {
        std::wstring path;	/* installeur téléchargé */
        std::wstring error;	/* non vide en cas d'échec */
        bool cancelled{ false };
    };

    /* Empreinte SHA-256 incrémentale (CNG). */
    class Sha256Stream
    {
    public:
        Sha256Stream()
        {
            if (BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
                BCryptCreateHash(m_alg, &m_hash, nullptr, 0, nullptr, 0, 0);
        }
        ~Sha256Stream()
        {
            if (m_hash) BCryptDestroyHash(m_hash);
            if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
        }
        Sha256Stream(Sha256Stream const&) = delete;
        Sha256Stream& operator=(Sha256Stream const&) = delete;

        /* Ajoute des octets à l'empreinte. */
        void Append(const uint8_t* data, size_t size)
        {
            if (m_hash)
                BCryptHashData(m_hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
        }

        /* Termine le calcul ; retourne l'empreinte hexadécimale ou une chaîne vide. */
        std::wstring Finish()
        {
            unsigned char digest[32]{};
            if (!m_hash || BCryptFinishHash(m_hash, digest, sizeof digest, 0) != 0)
                return {};
            return ::EvolutionWin::UpdateIntegrity::ToHex(digest, sizeof digest);
        }

    private:
        BCRYPT_ALG_HANDLE m_alg{ nullptr };
        BCRYPT_HASH_HANDLE m_hash{ nullptr };
    };

    /* États de la signature Authenticode d'un fichier. */
    enum class SignatureState { Valid, Unsigned, Invalid };

    /* Vérifie la signature Authenticode d'un fichier (WinVerifyTrust).
     * Paramètres :
     *   path : chemin du MSI */
    SignatureState VerifyAuthenticode(std::wstring const& path)
    {
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof file;
        file.pcwszFilePath = path.c_str();

        WINTRUST_DATA data{};
        data.cbStruct = sizeof data;
        data.dwUIChoice = WTD_UI_NONE;
        data.fdwRevocationChecks = WTD_REVOKE_NONE;
        data.dwUnionChoice = WTD_CHOICE_FILE;
        data.pFile = &file;
        data.dwStateAction = WTD_STATEACTION_VERIFY;

        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG result = WinVerifyTrust(nullptr, &action, &data);
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &action, &data);

        if (result == 0)
            return SignatureState::Valid;
        if (static_cast<HRESULT>(result) == TRUST_E_NOSIGNATURE || static_cast<HRESULT>(result) == TRUST_E_PROVIDER_UNKNOWN ||
            static_cast<HRESULT>(result) == TRUST_E_SUBJECT_FORM_UNKNOWN)
            return SignatureState::Unsigned;
        return SignatureState::Invalid;
    }

    /* Télécharge le MSI dans %LOCALAPPDATA%\EvolutionWin\updates en calculant
     * son empreinte au fil du flux, la compare à celle du fichier .sha256
     * publié, puis rend la main sur le thread de l'interface.
     * Paramètres :
     *   msi_url, sha_url : adresses des fichiers joints à la Release
     *   msi_name         : nom du MSI
     *   progress, status : barre et texte de progression
     *   cancel           : indicateur d'annulation
     *   done             : reçoit le résultat */
    fire_and_forget RunUpdateDownload(hstring msi_url, hstring sha_url, hstring msi_name, ProgressBar progress,
                                      TextBlock status, std::shared_ptr<bool> cancel,
                                      std::function<void(UpdateDownload const&)> done)
    {
        apartment_context ui_thread;
        UpdateDownload outcome;
        try
        {
            Windows::Web::Http::HttpClient http;
            http.DefaultRequestHeaders().UserAgent().ParseAdd(L"EvolutionWin/" EVO_VERSION_W);

            /* empreinte publiée : obligatoire */
            auto sha_response = co_await http.GetAsync(Uri{ sha_url });
            hstring sha_text = co_await sha_response.Content().ReadAsStringAsync();
            std::wstring expected = sha_response.IsSuccessStatusCode()
                ? ::EvolutionWin::UpdateIntegrity::ParseSha256File(sha_text.c_str(), msi_name.c_str()) : std::wstring{};
            if (expected.empty())
                outcome.error = Tr(L"empreinte SHA-256 de la version introuvable").c_str();

            if (outcome.error.empty())
            {
                wchar_t base[MAX_PATH]{};
                DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
                std::wstring dir = (length ? std::wstring{ base } : std::wstring{ L"." }) + L"\\EvolutionWin\\updates";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                outcome.path = dir + L"\\" + std::wstring{ msi_name };

                auto response = co_await http.GetAsync(Uri{ msi_url }, Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
                if (!response.IsSuccessStatusCode())
                    outcome.error = Fmt(Tr(L"réponse GitHub {0}"), { to_hstring(static_cast<int>(response.StatusCode())) }).c_str();
                else
                {
                    uint64_t total = 0;
                    if (auto length_ref = response.Content().Headers().ContentLength())
                        total = length_ref.Value();
                    auto stream = co_await response.Content().ReadAsInputStreamAsync();
                    Windows::Storage::Streams::DataReader reader{ stream };
                    reader.InputStreamOptions(Windows::Storage::Streams::InputStreamOptions::Partial);

                    std::ofstream file(outcome.path, std::ios::binary | std::ios::trunc);
                    Sha256Stream hash;
                    uint64_t received = 0;
                    std::vector<uint8_t> chunk;
                    while (!*cancel)
                    {
                        uint32_t loaded = co_await reader.LoadAsync(65536);
                        if (loaded == 0)
                            break;
                        chunk.resize(loaded);
                        reader.ReadBytes(chunk);
                        file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
                        hash.Append(chunk.data(), chunk.size());
                        received += loaded;
                        if (total > 0)
                        {
                            progress.IsIndeterminate(false);
                            progress.Value(static_cast<double>(received) * 100.0 / static_cast<double>(total));
                        }
                        status.Text(total > 0
                            ? Fmt(Tr(L"Téléchargement : {0} / {1}"), { FormatFileSize(static_cast<int64_t>(received)), FormatFileSize(static_cast<int64_t>(total)) })
                            : Fmt(Tr(L"Téléchargement : {0}"), { FormatFileSize(static_cast<int64_t>(received)) }));
                    }
                    file.close();

                    if (*cancel)
                    {
                        outcome.cancelled = true;
                        std::filesystem::remove(outcome.path, ec);
                    }
                    else if (!file || (total > 0 && received != total))
                        outcome.error = Tr(L"téléchargement incomplet").c_str();
                    else if (!::EvolutionWin::UpdateIntegrity::EqualsNoCase(hash.Finish(), expected))
                    {
                        outcome.error = Tr(L"l'empreinte SHA-256 ne correspond pas : fichier corrompu ou altéré, il a été supprimé").c_str();
                        std::filesystem::remove(outcome.path, ec);
                    }
                }
            }
        }
        catch (hresult_error const& e)
        {
            outcome.error = e.message().c_str();
        }

        co_await ui_thread;
        done(outcome);
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Remplit la page « Paramètres > À propos » avec les numéros de version,
     *        la vérification des mises à jour GitHub et les composants système.
     * @param force_check true pour forcer une nouvelle vérification en ligne.
     */
    void MainWindow::PopulateAboutSettingsView(bool force_check)
    {
        if (!SettingsCategoryAbout())
            return;

        // Numéro de version de l'application
        if (auto v = AboutVersionText())
            v.Text(Fmt(Tr(L"Version {0} — Portage Windows 11 natif d'Evolution"), { EVO_VERSION_W }));

        // Remplissage dynamique des composants du moteur et de l'environnement
        if (auto grid = AboutComponentsGrid())
        {
            grid.Children().Clear();
            grid.RowDefinitions().Clear();

            JsonObject engine;
            if (!JsonObject::TryParse(to_hstring(evo_core_version_info()), engine))
                engine = JsonObject{};

            hstring webview2;
            try
            {
                webview2 = Microsoft::Web::WebView2::Core::CoreWebView2Environment::GetAvailableBrowserVersionString();
            }
            catch (...) {}

            std::vector<std::pair<std::wstring, std::wstring>> components{
                { L"evolution-data-server", std::wstring{ engine.GetNamedString(L"evolution_data_server", L"") } },
                { L"evolution-ews", std::wstring{ engine.GetNamedString(L"evolution_ews", L"") } },
                { L"GLib", std::wstring{ engine.GetNamedString(L"glib", L"") } },
                { L"libsoup", std::wstring{ engine.GetNamedString(L"libsoup", L"") } },
                { L"Windows App SDK", EVO_WIDEN(EVO_WINDOWSAPPSDK_VERSION_STR) },
                { L"WebView2 Runtime", std::wstring{ webview2 } },
                { L"Windows NT", WindowsVersionText() },
            };

            int32_t row = 0;
            for (auto const& [name, version] : components)
            {
                grid.RowDefinitions().Append(RowDefinition{});

                TextBlock label;
                label.Text(name);
                label.Style(LookupAboutStyle(L"BodyStrongTextBlockStyle"));
                label.Foreground(::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush"));
                label.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetRow(label, row);
                Grid::SetColumn(label, 0);
                grid.Children().Append(label);

                TextBlock val;
                val.Text(version.empty() ? Tr(L"non installé") : hstring{ version });
                val.Style(LookupAboutStyle(L"BodyTextBlockStyle"));
                val.IsTextSelectionEnabled(true);
                val.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetRow(val, row);
                Grid::SetColumn(val, 1);
                grid.Children().Append(val);

                ++row;
            }
        }

        // Vérification des mises à jour en ligne
        auto status = AboutUpdateStatusText();
        auto banner = AboutUpdateInfoBar();
        if (!status || !banner)
            return;

        /* strategie de groupe : pas de recherche, bouton grise, message explicite */
        if (::EvolutionWin::Policy::AutoUpdateDisabled())
        {
            status.Text(Tr(L"Les mises à jour sont désactivées par votre administrateur."));
            status.Opacity(0.7);
            banner.IsOpen(false);
            if (auto button = AboutCheckUpdateButton())
                button.IsEnabled(false);
            return;
        }

        if (!force_check && !m_update_tag.empty())
        {
            UpdateCheck known;
            known.tag = m_update_tag;
            known.page = m_update_page;
            known.msi_url = m_update_msi_url;
            known.msi_name = m_update_msi_name;
            known.sha_url = m_update_sha_url;
            known.newer = true;
            ShowUpdateResult(known, status, banner, [weak = get_weak()]
            {
                if (auto self = weak.get())
                    self->DownloadAndInstallUpdateAsync();
            });
            return;
        }

        status.Text(Tr(L"Recherche de mises à jour en cours…"));
        status.Opacity(0.7);
        banner.IsOpen(false);

        CheckForUpdate([strong = get_strong(), status, banner](UpdateCheck const& check)
        {
            if (check.newer)
            {
                strong->m_update_tag = check.tag;
                strong->m_update_page = check.page;
                strong->m_update_msi_url = check.msi_url;
                strong->m_update_msi_name = check.msi_name;
                strong->m_update_sha_url = check.sha_url;
            }
            ShowUpdateResult(check, status, banner, [weak = winrt::make_weak(strong)]
            {
                if (auto self = weak.get())
                    self->DownloadAndInstallUpdateAsync();
            });
        });
    }

    /**
     * @brief Déclenche une recherche manuelle de mise à jour depuis la page « À propos ».
     */
    void MainWindow::OnAboutCheckUpdateClick(IInspectable const&, RoutedEventArgs const&)
    {
        PopulateAboutSettingsView(true);
    }

    /**
     * @brief Copie la configuration système et les versions de bibliothèques dans le presse-papiers.
     */
    void MainWindow::OnAboutCopySystemInfoClick(IInspectable const&, RoutedEventArgs const&)
    {
        JsonObject engine;
        if (!JsonObject::TryParse(to_hstring(evo_core_version_info()), engine))
            engine = JsonObject{};

        hstring webview2;
        try
        {
            webview2 = Microsoft::Web::WebView2::Core::CoreWebView2Environment::GetAvailableBrowserVersionString();
        }
        catch (...) {}

        std::vector<std::pair<std::wstring, std::wstring>> components{
            { L"evolution-data-server", std::wstring{ engine.GetNamedString(L"evolution_data_server", L"") } },
            { L"evolution-ews", std::wstring{ engine.GetNamedString(L"evolution_ews", L"") } },
            { L"GLib", std::wstring{ engine.GetNamedString(L"glib", L"") } },
            { L"libsoup", std::wstring{ engine.GetNamedString(L"libsoup", L"") } },
            { L"Windows App SDK", EVO_WIDEN(EVO_WINDOWSAPPSDK_VERSION_STR) },
            { L"WebView2 Runtime", std::wstring{ webview2 } },
            { L"Windows NT", WindowsVersionText() },
        };

        std::wstring report = L"Evolution pour Windows " EVO_VERSION_W L"\r\n";
        for (auto const& [name, version] : components)
            report += name + L" : " + (version.empty() ? L"non installé" : version) + L"\r\n";

        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(report);
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);

        if (auto btn_text = AboutCopySystemInfoButtonText())
            btn_text.Text(Tr(L"Informations copiées !"));
    }

    /* Télécharge le MSI de la dernière version avec une barre de progression,
     * contrôle son empreinte SHA-256 et sa signature Authenticode, puis lance
     * l'installation (msiexec). Un installeur non signé n'est installé
     * qu'après confirmation ; une signature invalide ou une empreinte
     * différente interrompt la mise à jour. */
    fire_and_forget MainWindow::DownloadAndInstallUpdateAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || m_update_msi_url.empty() || !Content() || !Content().XamlRoot())
            co_return;
        if (m_update_sha_url.empty())
        {
            ShowStatus(Tr(L"Mise à jour : empreinte SHA-256 absente de la version, installation automatique refusée."), InfoBarSeverity::Warning);
            co_return;
        }
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Fmt(Tr(L"Téléchargement de {0}"), { m_update_tag })));
        dialog.CloseButtonText(Tr(L"Annuler"));

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(380);
        TextBlock status;
        status.Text(Tr(L"Connexion à GitHub…"));
        ProgressBar progress;
        progress.IsIndeterminate(true);
        progress.Minimum(0);
        progress.Maximum(100);
        panel.Children().Append(status);
        panel.Children().Append(progress);
        dialog.Content(panel);

        auto cancel = std::make_shared<bool>(false);
        auto outcome = std::make_shared<UpdateDownload>();
        hstring msi_url = m_update_msi_url, sha_url = m_update_sha_url, msi_name = m_update_msi_name;
        dialog.CloseButtonClick([cancel](ContentDialog const&, ContentDialogButtonClickEventArgs const&) { *cancel = true; });
        dialog.Opened([=](ContentDialog const& sender, ContentDialogOpenedEventArgs const&)
        {
            RunUpdateDownload(msi_url, sha_url, msi_name, progress, status, cancel,
                [outcome, sender](UpdateDownload const& result)
                {
                    *outcome = result;
                    sender.Hide();
                });
        });

        co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (outcome->cancelled || (*cancel && outcome->path.empty()))
            co_return;
        if (!outcome->error.empty())
        {
            ShowStatus(Fmt(Tr(L"Mise à jour impossible : {0}"), { hstring{ outcome->error } }), InfoBarSeverity::Error);
            co_return;
        }
        if (outcome->path.empty())
            co_return;

        SignatureState signature = VerifyAuthenticode(outcome->path);
        if (signature == SignatureState::Invalid)
        {
            std::error_code ec;
            std::filesystem::remove(outcome->path, ec);
            ShowStatus(Tr(L"Mise à jour refusée : la signature Authenticode de l'installeur n'est pas valide."), InfoBarSeverity::Error);
            co_return;
        }
        if (signature == SignatureState::Unsigned)
        {
            ContentDialog confirm;
            confirm.XamlRoot(Content().XamlRoot());
            confirm.Title(box_value(Tr(L"Installeur non signé")));
            confirm.Content(box_value(Tr(L"L'empreinte SHA-256 est correcte, mais l'installeur n'est pas signé (Authenticode). Installer quand même ?")));
            confirm.PrimaryButtonText(Tr(L"Installer"));
            confirm.CloseButtonText(Tr(L"Annuler"));
            m_dialog_open = true;
            auto answer = co_await confirm.ShowAsync();
            m_dialog_open = false;
            if (answer != ContentDialogResult::Primary)
                co_return;
        }

        /* l'installeur remplace les fichiers de l'application : elle doit être fermée. Avertissement, puis fermeture
           automatique dès que l'installation est lancée */
        {
            ContentDialog warning;
            warning.XamlRoot(Content().XamlRoot());
            warning.Title(box_value(Tr(L"Installer la mise à jour")));
            TextBlock warning_text;
            warning_text.TextWrapping(TextWrapping::Wrap);
            warning_text.MaxWidth(420);
            warning_text.Text(Fmt(Tr(L"L'application va se fermer pour installer {0}. Enregistrez votre travail en cours : les messages en cours de rédaction non enregistrés seront perdus."),
                { m_update_tag }));
            warning.Content(warning_text);
            warning.PrimaryButtonText(Tr(L"Installer et fermer"));
            warning.CloseButtonText(Tr(L"Annuler"));
            warning.DefaultButton(ContentDialogButton::Primary);
            m_dialog_open = true;
            auto answer = co_await warning.ShowAsync();
            m_dialog_open = false;
            if (answer != ContentDialogResult::Primary)
                co_return;
        }

        std::wstring arguments = L"/i \"" + outcome->path + L"\"";
        auto launched = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"msiexec.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
        if (launched <= 32)
        {
            ShowStatus(Tr(L"Impossible de lancer l'installeur."), InfoBarSeverity::Error);
            co_return;
        }

        /* fermeture normale de l'application (moteur, fenêtres secondaires) pour libérer ses fichiers */
        Close();
    }

    /* Au démarrage : vérifie en arrière-plan si une version plus récente est
     * publiée ; la fenêtre d'information n'apparaît qu'une fois par version. */
    void MainWindow::CheckForUpdateOnStartup()
    {
        /* strategie de groupe : mises a jour desactivees par l'administrateur */
        if (::EvolutionWin::Policy::AutoUpdateDisabled())
            return;
        CheckForUpdate([strong = get_strong()](UpdateCheck const& check)
        {
            if (!check.newer)
                return;
            strong->m_update_tag = check.tag;
            strong->m_update_page = check.page;
            strong->m_update_msi_url = check.msi_url;
            strong->m_update_msi_name = check.msi_name;
            strong->m_update_sha_url = check.sha_url;
            if (::EvolutionWin::Settings::Instance().GetString(UpdateNotifiedKey, L"") != check.tag)
                strong->ShowUpdateDialogAsync();
        });
    }

    /* Fenêtre « nouvelle version disponible » ; « Voir les détails » ouvre
     * la section « Paramètres > À propos » (avec bandeau et lien de téléchargement). */
    fire_and_forget MainWindow::ShowUpdateDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || m_update_tag.empty() || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;
        ::EvolutionWin::Settings::Instance().SetString(UpdateNotifiedKey, m_update_tag);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Nouvelle version disponible")));
        dialog.Content(box_value(Fmt(Tr(L"Evolution {0} est disponible. Vous utilisez la version {1}."), { m_update_tag, EVO_VERSION_W })));
        bool can_install = !m_update_msi_url.empty();
        if (can_install)
        {
            dialog.PrimaryButtonText(Tr(L"Télécharger et installer"));
            dialog.SecondaryButtonText(Tr(L"Voir les détails"));
        }
        else
            dialog.PrimaryButtonText(Tr(L"Voir les détails"));
        dialog.CloseButtonText(Tr(L"Plus tard"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (can_install && result == ContentDialogResult::Primary)
        {
            DownloadAndInstallUpdateAsync();
            co_return;
        }
        if ((can_install && result == ContentDialogResult::Secondary) || (!can_install && result == ContentDialogResult::Primary))
        {
            Navigation().SelectedItem(Navigation().SettingsItem());
            SelectSettingsCategory(L"about");
        }
    }

    /**
     * @brief Ouvre la boîte de dialogue de consultation des raccourcis clavier depuis le ruban ou les paramètres.
     * @param sender Élément graphique déclencheur.
     * @param args Arguments d'événement de clic.
     */
    void MainWindow::OnRibbonKeyboardShortcutsClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ShowKeyboardShortcutsDialogAsync();
    }

    /**
     * @brief Gestionnaire de l'accélérateur universel F1 pour afficher instantanément la liste des raccourcis.
     * @param sender Accélérateur clavier déclencheur.
     * @param args Arguments d'invocation de l'accélérateur.
     */
    void MainWindow::OnKeyboardShortcutsAccelerator(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        ShowKeyboardShortcutsDialogAsync();
    }

    /**
     * @brief Présente la boîte de dialogue complète des raccourcis clavier avec recherche interactive et choix de profil.
     */
    fire_and_forget MainWindow::ShowKeyboardShortcutsDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Raccourcis clavier d'Evolution")));
        dialog.CloseButtonText(Tr(L"Fermer"));
        dialog.DefaultButton(ContentDialogButton::Close);

        Grid root;
        root.Width(620);
        root.MaxHeight(540);
        RowDefinition top_row;
        top_row.Height(GridLengthHelper::Auto());
        root.RowDefinitions().Append(top_row);
        RowDefinition list_row;
        list_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        root.RowDefinitions().Append(list_row);

        /* Barre d'outils supérieure : recherche et profil (Outlook / Evolution) */
        Grid top_bar;
        top_bar.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));
        top_bar.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition col_spacer;
        col_spacer.Width(GridLengthHelper::FromPixels(16));
        top_bar.ColumnDefinitions().Append(col_spacer);
        ColumnDefinition col_combo;
        col_combo.Width(GridLengthHelper::Auto());
        top_bar.ColumnDefinitions().Append(col_combo);

        AutoSuggestBox search_box;
        search_box.PlaceholderText(Tr(L"Rechercher une action ou un raccourci..."));
        FontIcon search_icon;
        search_icon.Glyph(L"\xE721");
        search_box.QueryIcon(search_icon);
        top_bar.Children().Append(search_box);
        Grid::SetColumn(search_box, 0);

        ComboBox profile_combo;
        profile_combo.MinWidth(180);
        ComboBoxItem item_outlook;
        item_outlook.Content(box_value(Tr(L"Profil Outlook (défaut)")));
        item_outlook.Tag(box_value(L"outlook"));
        ComboBoxItem item_evo;
        item_evo.Content(box_value(Tr(L"Profil Evolution GNOME")));
        item_evo.Tag(box_value(L"evolution"));
        profile_combo.Items().Append(item_outlook);
        profile_combo.Items().Append(item_evo);

        hstring saved_profile = ::EvolutionWin::Settings::Instance().GetString(L"general.shortcuts-profile", L"outlook");
        profile_combo.SelectedIndex(saved_profile == L"evolution" ? 1 : 0);
        top_bar.Children().Append(profile_combo);
        Grid::SetColumn(profile_combo, 2);

        root.Children().Append(top_bar);
        Grid::SetRow(top_bar, 0);

        ScrollViewer scroller;
        scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);

        StackPanel list_panel;
        list_panel.Spacing(8);
        scroller.Content(list_panel);

        root.Children().Append(scroller);
        Grid::SetRow(scroller, 1);

        auto refresh_list = [this, list_panel, search_box, profile_combo]()
        {
            list_panel.Children().Clear();
            std::wstring query{ search_box.Text() };
            bool is_evo = profile_combo.SelectedIndex() == 1;

            auto all = ::EvolutionWin::Helpers::GetKeyboardShortcuts();
            auto filtered = ::EvolutionWin::Helpers::FilterKeyboardShortcuts(all, query);

            if (filtered.empty())
            {
                TextBlock empty_txt;
                empty_txt.Text(Tr(L"Aucun raccourci ne correspond à votre recherche."));
                empty_txt.Margin(ThicknessHelper::FromLengths(0, 24, 0, 24));
                empty_txt.HorizontalAlignment(HorizontalAlignment::Center);
                empty_txt.Foreground(::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush"));
                list_panel.Children().Append(empty_txt);
                return;
            }

            std::wstring current_cat;
            for (auto const& sc : filtered)
            {
                if (sc.category != current_cat)
                {
                    current_cat = sc.category;
                    TextBlock cat_hdr;
                    cat_hdr.Text(current_cat);
                    cat_hdr.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                    cat_hdr.Margin(ThicknessHelper::FromLengths(0, list_panel.Children().Size() == 0 ? 0 : 8, 0, 4));
                    cat_hdr.Foreground(::EvolutionWin::Helpers::ThemedBrush(L"AccentFillColorDefaultBrush"));
                    list_panel.Children().Append(cat_hdr);
                }

                Border row_card;
                row_card.Background(::EvolutionWin::Helpers::ThemedBrush(L"CardBackgroundFillColorDefaultBrush"));
                row_card.BorderBrush(::EvolutionWin::Helpers::ThemedBrush(L"CardStrokeColorDefaultBrush"));
                row_card.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
                row_card.CornerRadius(CornerRadiusHelper::FromRadii(4, 4, 4, 4));
                row_card.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));

                Grid row_grid;
                row_grid.ColumnDefinitions().Append(ColumnDefinition{});
                ColumnDefinition key_col;
                key_col.Width(GridLengthHelper::Auto());
                row_grid.ColumnDefinitions().Append(key_col);

                TextBlock action_txt;
                action_txt.Text(sc.action);
                action_txt.VerticalAlignment(VerticalAlignment::Center);
                row_grid.Children().Append(action_txt);
                Grid::SetColumn(action_txt, 0);

                Border key_badge;
                key_badge.Background(::EvolutionWin::Helpers::ThemedBrush(L"SubtleFillColorSecondaryBrush"));
                key_badge.BorderBrush(::EvolutionWin::Helpers::ThemedBrush(L"ControlStrokeColorDefaultBrush"));
                key_badge.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
                key_badge.CornerRadius(CornerRadiusHelper::FromRadii(4, 4, 4, 4));
                key_badge.Padding(ThicknessHelper::FromLengths(8, 2, 8, 2));

                TextBlock key_txt;
                key_txt.Text(is_evo ? sc.keys_evolution : sc.keys_outlook);
                key_txt.FontFamily(FontFamily{ L"Segoe UI Variable Display, Cascadia Mono, Consolas" });
                key_txt.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                key_txt.FontSize(12);
                key_badge.Child(key_txt);

                row_grid.Children().Append(key_badge);
                Grid::SetColumn(key_badge, 1);

                row_card.Child(row_grid);
                list_panel.Children().Append(row_card);
            }
        };

        search_box.TextChanged([refresh_list](AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
        {
            refresh_list();
        });

        profile_combo.SelectionChanged([refresh_list](IInspectable const&, SelectionChangedEventArgs const&)
        {
            refresh_list();
        });

        refresh_list();

        dialog.Content(root);
        co_await dialog.ShowAsync();

        /* Persistance du profil sélectionné */
        hstring chosen_profile = profile_combo.SelectedIndex() == 1 ? L"evolution" : L"outlook";
        ::EvolutionWin::Settings::Instance().SetString(L"general.shortcuts-profile", chosen_profile);

        m_dialog_open = false;
    }
}
