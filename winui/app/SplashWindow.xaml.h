#pragma once

#include "SplashWindow.g.h"

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Fenêtre d'écran de démarrage (Splash Screen) affichée au lancement.
     * 
     * Présente une interface épurée sans bordures système avec le logo, le nom
     * de l'application, la version et l'étape de démarrage en cours.
     */
    struct SplashWindow : SplashWindowT<SplashWindow>
    {
        SplashWindow();

        /**
         * @brief Met à jour le message d'état affiché sous l'indicateur d'activité.
         * @param status Nouveau libellé d'état (ex. "Connexion aux services…").
         */
        void SetStatus(hstring const& status);
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct SplashWindow : SplashWindowT<SplashWindow, implementation::SplashWindow>
    {
    };
}
