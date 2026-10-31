#pragma once

/* Identifiant de l'icône de l'application dans les ressources (EvolutionWin.rc) */
#define IDI_EVOLUTIONWIN 1

#ifndef RC_INVOKED
#include <winrt/Microsoft.UI.Interop.h>

namespace EvolutionWin
{
    /* Icône de l'application sur une fenêtre (barre des tâches, Alt+Tab),
     * depuis les ressources de l'exécutable.
     * Paramètres :
     *   app_window : AppWindow de la fenêtre WinUI à décorer */
    inline void SetWindowIcon(winrt::Microsoft::UI::Windowing::AppWindow const& app_window)
    {
        HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_EVOLUTIONWIN), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
        if (icon)
            app_window.SetIcon(winrt::Microsoft::UI::GetIconIdFromIcon(icon));
    }
}
#endif
