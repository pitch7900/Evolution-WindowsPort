#pragma once

#include "ResizeGrip.g.h"

#include <winrt/Microsoft.UI.Input.h>

namespace winrt::EvolutionWin::implementation
{
    /* Poignée d'étirement vertical du calendrier : porte le curseur Nord-Sud. */
    struct ResizeGrip : ResizeGripT<ResizeGrip>
    {
        /* Initialise la poignée et son curseur système. */
        ResizeGrip()
        {
            ProtectedCursor(Microsoft::UI::Input::InputSystemCursor::Create(
                Microsoft::UI::Input::InputSystemCursorShape::SizeNorthSouth));
        }
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct ResizeGrip : ResizeGripT<ResizeGrip, implementation::ResizeGrip>
    {
    };
}
