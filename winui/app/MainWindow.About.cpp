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
                check.failure = L"aucune version publiée";
            else if (!response.IsSuccessStatusCode() || !JsonObject::TryParse(body, release))
                check.failure = L"réponse GitHub " + to_hstring(static_cast<int>(response.StatusCode()));
            else
            {
                check.tag = release.GetNamedString(L"tag_name", L"");
                check.page = release.GetNamedString(L"html_url", L"");
                if (check.tag.empty())
                    check.failure = L"version inconnue";
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
    void ShowUpdateResult(UpdateCheck const& check, TextBlock const& status, InfoBar const& banner)
    {
        if (!check.failure.empty())
        {
            status.Text(L"Mise à jour : vérification impossible (" + check.failure + L")");
            return;
        }
        if (!check.newer)
        {
            status.Text(L"Vous utilisez la dernière version.");
            return;
        }

        status.Text(L"Nouvelle version disponible : " + check.tag);
        status.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        status.Opacity(1);
        banner.Title(L"Nouvelle version disponible : " + check.tag);
        banner.Message(L"Vous utilisez la version " EVO_VERSION_W L".");
        try
        {
            HyperlinkButton download;
            download.Content(box_value(L"Télécharger"));
            download.NavigateUri(Uri{ check.page });
            banner.ActionButton(download);
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
            v.Text(L"Version " EVO_VERSION_W L" — Portage Windows 11 natif d'Evolution");

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
                label.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
                label.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetRow(label, row);
                Grid::SetColumn(label, 0);
                grid.Children().Append(label);

                TextBlock val;
                val.Text(version.empty() ? L"non installé" : version);
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

        if (!force_check && !m_update_tag.empty())
        {
            UpdateCheck known;
            known.tag = m_update_tag;
            known.page = m_update_page;
            known.newer = true;
            ShowUpdateResult(known, status, banner);
            return;
        }

        status.Text(L"Recherche de mises à jour en cours…");
        status.Opacity(0.7);
        banner.IsOpen(false);

        CheckForUpdate([strong = get_strong(), status, banner](UpdateCheck const& check)
        {
            if (check.newer)
            {
                strong->m_update_tag = check.tag;
                strong->m_update_page = check.page;
            }
            ShowUpdateResult(check, status, banner);
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
            btn_text.Text(L"Informations copiées !");
    }

    /* Au démarrage : vérifie en arrière-plan si une version plus récente est
     * publiée ; la fenêtre d'information n'apparaît qu'une fois par version. */
    void MainWindow::CheckForUpdateOnStartup()
    {
        CheckForUpdate([strong = get_strong()](UpdateCheck const& check)
        {
            if (!check.newer)
                return;
            strong->m_update_tag = check.tag;
            strong->m_update_page = check.page;
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
        dialog.Title(box_value(L"Nouvelle version disponible"));
        dialog.Content(box_value(L"Evolution " + m_update_tag + L" est disponible. Vous utilisez la version " EVO_VERSION_W L"."));
        dialog.PrimaryButtonText(L"Voir les détails");
        dialog.CloseButtonText(L"Plus tard");
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result == ContentDialogResult::Primary)
        {
            Navigation().SelectedItem(Navigation().SettingsItem());
            SelectSettingsCategory(L"about");
        }
    }
}
