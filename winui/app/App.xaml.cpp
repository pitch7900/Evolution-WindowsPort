#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"
#include "ErrorLog.h"

#include <winrt/Windows.Globalization.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EvolutionWin::implementation
{
    App::App()
    {
        /* Encapsulation du processus et des futurs sous-processus EDS dans un Job Object Windows */
        ::EvolutionWin::Helpers::InitializeJobObject();

        /* Application du choix de langue forcé de l'utilisateur (PrimaryLanguageOverride) */
        {
            auto saved_lang = ::EvolutionWin::Settings::Instance().GetString(L"general.language", L"system");
            if (saved_lang == L"system")
            {
                // Si l'utilisateur n'a pas défini de langue dans les Paramètres, vérifier si le MSI a défini UILanguage dans HKLM
                std::wstring msi_lang = ::EvolutionWin::Helpers::GetMsiMachineLanguage();
                if (!msi_lang.empty())
                {
                    saved_lang = winrt::hstring{ msi_lang };
                }
                else
                {
                    saved_lang = L"";
                }
            }
            try
            {
                Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(saved_lang);
            }
            catch (...) {}
        }

        /* exception non gérée dans un gestionnaire d'événement : journalisée
           (errors.log) au lieu d'arrêter l'application */
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            ::EvolutionWin::LogError(L"XAML", e.Exception(), e.Message());
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
#endif
            e.Handled(true);
        });
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        /* Affichage instantané de l'écran de démarrage avant le chargement de CoreBridge */
        m_splash = make<SplashWindow>();
        m_splash.Activate();

        auto main_window = make<MainWindow>();
        winrt::get_self<MainWindow>(main_window)->SetSplashWindow(m_splash);
        window = main_window;
    }
}
