#pragma once

#include "App.xaml.g.h"
#include "SplashWindow.xaml.h"

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Point d'entrée de l'application WinUI 3 EvolutionWin.
     */
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
        winrt::EvolutionWin::SplashWindow m_splash{ nullptr };
    };
}
