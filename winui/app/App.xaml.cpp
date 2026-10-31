#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "ErrorLog.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EvolutionWin::implementation
{
    App::App()
    {
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
        window = make<MainWindow>();
        window.Activate();
    }
}
