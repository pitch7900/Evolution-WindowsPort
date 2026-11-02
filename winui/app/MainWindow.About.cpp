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

/* version de l'application, depuis le fichier VERSION (généré par MSBuild) */
#include "evo-version.h"
#define EVO_WIDEN_(text) L ## text
#define EVO_WIDEN(text) EVO_WIDEN_(text)
#define EVO_VERSION_W EVO_WIDEN(EVO_VERSION_STR)

/* Boîte « À propos » : version, composants et rapport de bug. */

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
}

namespace winrt::EvolutionWin::implementation
{
    /* Boîte « À propos » : version de l'application (fichier VERSION, comme
     * l'exécutable et l'installeur), licence, liens, versions des composants
     * et bouton de copie de ces informations pour les rapports de bug. */
    fire_and_forget MainWindow::ShowAboutDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        std::wstring exe_dir = ExecutableDirectory();

        /* versions : moteur (evo-core), Windows App SDK, WebView2, Windows */
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
            { L"WebView2", std::wstring{ webview2 } },
            { L"Windows", WindowsVersionText() },
        };

        std::wstring report = L"Evolution pour Windows " EVO_VERSION_W L"\r\n";
        for (auto const& [name, version] : components)
            report += name + L" : " + (version.empty() ? L"non installé" : version) + L"\r\n";

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"À propos d'Evolution"));
        dialog.CloseButtonText(L"Fermer");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(420);

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(16);
        Image logo;
        logo.Width(64);
        logo.Height(64);
        logo.Source(Imaging::BitmapImage{ Uri{ hstring{ exe_dir + L"\\Assets\\EvolutionWin.png" } } });
        header.Children().Append(logo);

        StackPanel title;
        title.VerticalAlignment(VerticalAlignment::Center);
        TextBlock name;
        name.Text(L"Evolution pour Windows");
        name.FontSize(20);
        name.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        title.Children().Append(name);
        TextBlock version;
        version.Text(L"Version " EVO_VERSION_W);
        title.Children().Append(version);
        header.Children().Append(title);
        panel.Children().Append(header);

        TextBlock about;
        about.TextWrapping(TextWrapping::Wrap);
        about.Text(L"Client de messagerie, calendrier et contacts natif Windows 11, "
            L"bâti sur le moteur de GNOME Evolution (evolution-data-server, Camel). "
            L"Logiciel libre sous licence GNU LGPL 2.1 ou ultérieure.");
        panel.Children().Append(about);

        StackPanel links;
        links.Orientation(Orientation::Horizontal);
        HyperlinkButton repository;
        repository.Content(box_value(L"Dépôt GitHub"));
        repository.NavigateUri(Uri{ L"https://github.com/pitch7900/Evolution-WindowsPort" });
        links.Children().Append(repository);
        HyperlinkButton changelog;
        changelog.Content(box_value(L"Nouveautés (CHANGELOG)"));
        changelog.NavigateUri(Uri{ L"https://github.com/pitch7900/Evolution-WindowsPort/blob/main/CHANGELOG.md" });
        links.Children().Append(changelog);
        panel.Children().Append(links);

        Grid table;
        table.ColumnSpacing(16);
        table.ColumnDefinitions().Append(ColumnDefinition{});
        table.ColumnDefinitions().Append(ColumnDefinition{});
        int32_t row = 0;
        for (auto const& [component, component_version] : components)
        {
            table.RowDefinitions().Append(RowDefinition{});
            TextBlock label;
            label.Text(component);
            label.Opacity(0.7);
            Grid::SetRow(label, row);
            table.Children().Append(label);
            TextBlock value;
            value.Text(component_version.empty() ? L"non installé" : component_version);
            value.IsTextSelectionEnabled(true);
            Grid::SetRow(value, row);
            Grid::SetColumn(value, 1);
            table.Children().Append(value);
            ++row;
        }
        panel.Children().Append(table);

        Button copy;
        copy.Content(box_value(L"Copier les informations"));
        copy.Click([report](IInspectable const& sender, RoutedEventArgs const&)
        {
            Windows::ApplicationModel::DataTransfer::DataPackage package;
            package.SetText(report);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
            sender.as<Button>().Content(box_value(L"Informations copiées"));
        });
        panel.Children().Append(copy);

        dialog.Content(panel);
        co_await dialog.ShowAsync();
        m_dialog_open = false;
    }
}
