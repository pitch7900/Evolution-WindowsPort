#pragma once

#include "PaneSplitter.g.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace winrt::EvolutionWin::implementation
{
    /* Contrôle séparateur redimensionnable entre les panneaux de l'interface principale.
     * Prend en charge le curseur Est-Ouest ainsi qu'un retour visuel accentué au survol
     * et lors des opérations de glissement à la souris. */
    struct PaneSplitter : PaneSplitterT<PaneSplitter>
    {
        /* Initialise le séparateur, son curseur système et ses gestionnaires de survol. */
        PaneSplitter()
        {
            ProtectedCursor(Microsoft::UI::Input::InputSystemCursor::Create(
                Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));

            PointerEntered([this](auto const&, auto const&) { SetHoverVisual(true); });
            PointerExited([this](auto const&, auto const&) { if (!m_is_dragging) SetHoverVisual(false); });
            PointerPressed([this](auto const&, auto const&) { m_is_dragging = true; SetHoverVisual(true); });
            PointerReleased([this](auto const&, auto const&) { m_is_dragging = false; SetHoverVisual(false); });
            PointerCaptureLost([this](auto const&, auto const&) { m_is_dragging = false; SetHoverVisual(false); });
        }

    private:
        /* Indicateur d'un glissement de redimensionnement en cours */
        bool m_is_dragging{ false };

        /* Met à jour l'apparence visuelle du séparateur lors du survol ou du glissement.
         * Paramètre :
         *   active : true pour accentuer le séparateur (survol ou glissement), false pour l'état au repos. */
        void SetHoverVisual(bool active)
        {
            try
            {
                for (auto const& child : Children())
                {
                    if (auto rect = child.try_as<Microsoft::UI::Xaml::Shapes::Rectangle>())
                    {
                        if (active)
                        {
                            rect.Width(3);
                            rect.RadiusX(1.5);
                            rect.RadiusY(1.5);
                            rect.Fill(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
                        }
                        else
                        {
                            rect.Width(1);
                            rect.RadiusX(0);
                            rect.RadiusY(0);
                            rect.Fill(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(box_value(L"DividerStrokeColorDefaultBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct PaneSplitter : PaneSplitterT<PaneSplitter, implementation::PaneSplitter>
    {
    };
}
