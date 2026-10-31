#pragma once

#include "PaneSplitter.g.h"

#include <winrt/Microsoft.UI.Input.h>

namespace winrt::EvolutionWin::implementation
{
    struct PaneSplitter : PaneSplitterT<PaneSplitter>
    {
        PaneSplitter()
        {
            ProtectedCursor(Microsoft::UI::Input::InputSystemCursor::Create(
                Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
        }
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct PaneSplitter : PaneSplitterT<PaneSplitter, implementation::PaneSplitter>
    {
    };
}
