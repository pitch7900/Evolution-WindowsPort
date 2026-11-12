#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Mailto.h"

#include <winrt/Windows.System.h>
#include <commctrl.h>
#include <shlobj.h>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** Identifiant des données WM_COPYDATA d'un lien mailto: transmis entre instances (« MAIL »). */
        constexpr ULONG_PTR kMailtoCopyDataId = 0x4D41494C;
        /** Nom de la propriété posée sur la fenêtre principale pour la retrouver depuis une autre instance. */
        constexpr wchar_t kMainWindowProperty[] = L"EvolutionWin.MainWindow";
        /** Taille maximale acceptée pour une URL reçue (octets). */
        constexpr DWORD kMaxMailtoBytes = 64 * 1024;

        /** @brief Chemin complet de l'exécutable courant. */
        std::wstring CurrentExePath()
        {
            std::wstring path(MAX_PATH, L'\0');
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            while (length == path.size())
            {
                path.resize(path.size() * 2);
                length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            }
            path.resize(length);
            return path;
        }

        /**
         * @brief Indique si EvolutionWin est déclaré comme gestionnaire de mailto: pour l'utilisateur courant
         *        avec cet exécutable (clé du client de messagerie et commande du protocole).
         */
        bool IsMailtoRegistered()
        {
            std::wstring key = ::EvolutionWin::Mailto::ProtocolKey() + L"\\shell\\open\\command";
            wchar_t buffer[2048]{};
            DWORD size = sizeof(buffer) - sizeof(wchar_t);
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
                return false;
            if (!::EvolutionWin::Mailto::CommandMatchesExe(buffer, CurrentExePath()))
                return false;

            HKEY client = nullptr;
            std::wstring client_key = ::EvolutionWin::Mailto::ClientKey() + L"\\Capabilities";
            if (RegOpenKeyExW(HKEY_CURRENT_USER, client_key.c_str(), 0, KEY_READ, &client) != ERROR_SUCCESS)
                return false;
            RegCloseKey(client);
            return true;
        }

        /**
         * @brief Écrit les clés d'enregistrement de l'application de messagerie (HKEY_CURRENT_USER).
         * @return true si toutes les valeurs ont été écrites.
         */
        bool RegisterMailtoHandler()
        {
            auto values = ::EvolutionWin::Mailto::BuildRegistration(CurrentExePath(),
                std::wstring{ Tr(L"Evolution") }, std::wstring{ Tr(L"Messagerie, calendrier, contacts et tâches Evolution") });
            bool ok = true;
            for (auto const& value : values)
            {
                LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, value.key.c_str(),
                    value.value_name.empty() ? nullptr : value.value_name.c_str(), REG_SZ, value.data.c_str(),
                    static_cast<DWORD>((value.data.size() + 1) * sizeof(wchar_t)));
                if (status != ERROR_SUCCESS)
                    ok = false;
            }
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return ok;
        }

        /** @brief Supprime l'enregistrement de l'application de messagerie. */
        void UnregisterMailtoHandler()
        {
            for (auto const& key : ::EvolutionWin::Mailto::RegistrationKeysToDelete())
                RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
            RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", ::EvolutionWin::Mailto::kAppKeyName);
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }

        /** @brief Procédure de sous-classe de la fenêtre principale : reçoit les liens mailto: d'une autre instance. */
        LRESULT CALLBACK MailtoSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref)
        {
            if (message == WM_COPYDATA)
            {
                auto data = reinterpret_cast<COPYDATASTRUCT const*>(lparam);
                if (data && data->dwData == kMailtoCopyDataId && data->lpData && data->cbData >= sizeof(wchar_t)
                    && data->cbData <= kMaxMailtoBytes)
                {
                    std::wstring url(reinterpret_cast<wchar_t const*>(data->lpData), data->cbData / sizeof(wchar_t));
                    while (!url.empty() && url.back() == L'\0')
                        url.pop_back();
                    if (auto self = reinterpret_cast<MainWindow*>(ref))
                        self->HandleMailto(hstring{ url });
                    return TRUE;
                }
            }
            else if (message == WM_NCDESTROY)
            {
                RemoveWindowSubclass(hwnd, MailtoSubclassProc, id);
                RemovePropW(hwnd, kMainWindowProperty);
            }
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }

        /** @brief Contexte de recherche de la fenêtre principale d'une autre instance. */
        struct FindMainContext
        {
            DWORD own_process{ 0 };
            HWND found{ nullptr };
        };

        /** @brief Rappel d'EnumWindows : repère la fenêtre principale d'une autre instance. */
        BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lparam)
        {
            auto context = reinterpret_cast<FindMainContext*>(lparam);
            if (!GetPropW(hwnd, kMainWindowProperty))
                return TRUE;
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (process == context->own_process)
                return TRUE;
            context->found = hwnd;
            return FALSE;
        }
    }

    /**
     * @brief Transmet un lien mailto: à une instance d'EvolutionWin déjà ouverte.
     * @param url Lien mailto: complet.
     * @return true si une autre instance l'a reçu (l'instance courante peut alors se terminer).
     */
    bool MainWindow::ForwardMailtoToRunningInstance(std::wstring const& url)
    {
        FindMainContext context;
        context.own_process = GetCurrentProcessId();
        EnumWindows(FindMainWindowProc, reinterpret_cast<LPARAM>(&context));
        if (!context.found)
            return false;

        DWORD target_process = 0;
        GetWindowThreadProcessId(context.found, &target_process);
        AllowSetForegroundWindow(target_process);

        COPYDATASTRUCT data{};
        data.dwData = kMailtoCopyDataId;
        data.cbData = static_cast<DWORD>((url.size() + 1) * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(url.c_str());
        DWORD_PTR result = 0;
        LRESULT sent = SendMessageTimeoutW(context.found, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result);
        return sent != 0 && result != 0;
    }

    /**
     * @brief Prépare la réception des liens mailto: : repère la fenêtre principale pour les autres instances,
     *        installe la réception des liens transmis et mémorise le lien éventuel du lancement.
     * @param pending_url Lien mailto: reçu sur la ligne de commande (vide s'il n'y en a pas).
     */
    void MainWindow::InstallMailtoSupport(hstring const& pending_url)
    {
        if (auto native = try_as<::IWindowNative>())
        {
            HWND hwnd = nullptr;
            native->get_WindowHandle(&hwnd);
            if (hwnd)
            {
                SetPropW(hwnd, kMainWindowProperty, reinterpret_cast<HANDLE>(1));
                SetWindowSubclass(hwnd, MailtoSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
            }
        }

        if (!pending_url.empty())
            HandleMailto(pending_url);
    }

    /**
     * @brief Ouvre une fenêtre de rédaction préremplie par un lien mailto:. Si les comptes ne sont pas
     *        encore chargés (lancement), l'ouverture est différée jusqu'à leur chargement (2 minutes au plus).
     * @param url Lien mailto: complet.
     */
    void MainWindow::HandleMailto(hstring const& url)
    {
        ::EvolutionWin::Mailto::Message message;
        if (!::EvolutionWin::Mailto::Parse(std::wstring_view{ url }, message))
            return;

        m_pending_mailto = url;

        auto open = [this]() -> bool
        {
            if (!m_accounts || m_pending_mailto.empty())
                return false;

            ::EvolutionWin::Mailto::Message pending;
            ::EvolutionWin::Mailto::Parse(std::wstring_view{ m_pending_mailto }, pending);
            m_pending_mailto = L"";

            ::EvolutionWin::ComposeData data;
            data.account = m_account;
            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                if (account.GetNamedBoolean(L"default", false))
                {
                    data.account = account.GetNamedString(L"uid", m_account);
                    break;
                }
            }
            data.to = hstring{ pending.to };
            data.cc = hstring{ pending.cc };
            data.bcc = hstring{ pending.bcc };
            data.subject = hstring{ pending.subject };
            data.body = hstring{ pending.body };
            OpenCompose(data);
            return true;
        };

        if (open())
            return;

        /* comptes pas encore chargés : nouvelle tentative toutes les 500 ms */
        if (m_mailto_timer)
            m_mailto_timer.Stop();
        m_mailto_timer = DispatcherTimer();
        m_mailto_timer.Interval(std::chrono::milliseconds(500));
        auto attempts = std::make_shared<int>(0);
        m_mailto_timer.Tick([this, open, attempts](IInspectable const&, IInspectable const&)
        {
            if (open() || ++*attempts > 240)
            {
                m_mailto_timer.Stop();
                if (!m_pending_mailto.empty())
                {
                    m_pending_mailto = L"";
                    ShowStatus(Tr(L"Les comptes ne se sont pas chargés : le lien de messagerie n'a pas pu être ouvert."), InfoBarSeverity::Warning);
                }
            }
        });
        m_mailto_timer.Start();
    }

    /**
     * @brief Met à jour la section « Liens de messagerie » des Paramètres (état de l'enregistrement et boutons).
     *        Appelée chaque fois que l'on ouvre les Paramètres.
     */
    void MainWindow::InitMailtoSettingsView()
    {
        bool registered = IsMailtoRegistered();
        if (auto text = MailtoStatusText())
            text.Text(registered
                ? Tr(L"EvolutionWin est enregistré comme application de messagerie pour votre compte Windows.")
                : Tr(L"EvolutionWin n'est pas enregistré comme application de messagerie."));
        if (auto button = MailtoRegisterButton())
            button.IsEnabled(!registered);
        if (auto button = MailtoUnregisterButton())
            button.IsEnabled(registered);
        if (auto button = MailtoDefaultAppsButton())
            button.IsEnabled(registered);
    }

    /**
     * @brief Enregistre EvolutionWin comme application de messagerie puis ouvre « Applications par défaut »
     *        de Windows (le choix final de l'application par défaut revient à l'utilisateur).
     */
    void MainWindow::OnMailtoRegisterClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!RegisterMailtoHandler())
        {
            ShowStatus(Tr(L"L'enregistrement comme application de messagerie a échoué."), InfoBarSeverity::Error);
            InitMailtoSettingsView();
            return;
        }
        InitMailtoSettingsView();
        ShowStatus(Tr(L"EvolutionWin est enregistré. Choisissez-le dans « Applications par défaut » pour les liens de messagerie."),
            InfoBarSeverity::Success);
        OnMailtoDefaultAppsClick(nullptr, nullptr);
    }

    /**
     * @brief Retire l'enregistrement d'EvolutionWin comme application de messagerie.
     */
    void MainWindow::OnMailtoUnregisterClick(IInspectable const&, RoutedEventArgs const&)
    {
        UnregisterMailtoHandler();
        InitMailtoSettingsView();
        ShowStatus(Tr(L"L'enregistrement comme application de messagerie a été retiré."), InfoBarSeverity::Informational);
    }

    /**
     * @brief Ouvre la page « Applications par défaut » de Windows sur EvolutionWin.
     */
    void MainWindow::OnMailtoDefaultAppsClick(IInspectable const&, RoutedEventArgs const&)
    {
        try
        {
            Windows::System::Launcher::LaunchUriAsync(Uri{ L"ms-settings:defaultapps?registeredAppUser=EvolutionWin" });
        }
        catch (...)
        {
            ShowStatus(Tr(L"Impossible d'ouvrir les paramètres Windows."), InfoBarSeverity::Warning);
        }
    }
}
