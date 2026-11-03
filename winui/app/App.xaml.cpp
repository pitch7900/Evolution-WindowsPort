#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Settings.h"
#include "ErrorLog.h"

#include <winrt/Windows.Globalization.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EvolutionWin::implementation
{
    App::App()
    {
        /* Application du choix de langue forcé de l'utilisateur (PrimaryLanguageOverride) */
        {
            auto saved_lang = ::EvolutionWin::Settings::Instance().GetString(L"general.language", L"system");
            if (saved_lang == L"system")
                saved_lang = L"";
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
