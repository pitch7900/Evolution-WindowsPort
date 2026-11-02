#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"

#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <vector>

/* Mise en page : volets redimensionnables (séparateurs), volet des dossiers,
 * position du volet de lecture, zone déplaçable de la barre de titre. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    /* Panneaux redimensionnables de la vue Courrier : clé de la largeur
     * mémorisée, largeur par défaut et largeur minimale (pixels). */
    struct PaneLayout
    {
        wchar_t const* key;
        double default_width;
        double min_width;
    };

    constexpr PaneLayout FoldersPane{ L"layout.folders-width", 260, 180 };

    constexpr PaneLayout ListPane{ L"layout.list-width", 420, 280 };

    /* le volet de lecture prend la place restante, au moins cette largeur */
    constexpr double ReaderMinWidth = 320;

    /* borne d'une largeur relue, contre un fichier de préférences aberrant */
    constexpr double RestoredMaxWidth = 1200;
}

namespace winrt::EvolutionWin::implementation
{
    /* Rétablit les largeurs des dossiers et de la liste de messages
     * mémorisées lors d'une session précédente (layout.folders-width,
     * layout.list-width). */
    void MainWindow::RestorePaneWidths()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();

        for (auto [column, pane] : { std::pair{ FoldersColumn(), &FoldersPane }, std::pair{ ListColumn(), &ListPane } })
        {
            double width = settings.GetNumber(pane->key, pane->default_width);
            width = std::clamp(width, pane->min_width, RestoredMaxWidth);
            column.Width(GridLengthHelper::FromPixels(width));
        }
    }

    /* Colonne redimensionnée par un séparateur.
     * Paramètres :
     *   splitter : FoldersSplitter ou ListSplitter */
    ColumnDefinition MainWindow::SplitterColumn(IInspectable const& splitter)
    {
        return splitter == FoldersSplitter() ? FoldersColumn() : ListColumn();
    }

    /* Début du glisser : capture le pointeur pour suivre le mouvement même
     * hors du séparateur (au-dessus de la liste ou du volet de lecture). */
    void MainWindow::OnSplitterPointerPressed(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        auto splitter = sender.as<UIElement>();

        if (!args.GetCurrentPoint(splitter).Properties().IsLeftButtonPressed())
            return;
        if (!splitter.CapturePointer(args.Pointer()))
            return;

        m_drag_column = SplitterColumn(sender);
        m_drag_origin = args.GetCurrentPoint(MailView()).Position().X;
        m_drag_start_width = m_drag_column.ActualWidth();
    }

    /* Pendant le glisser : nouvelle largeur de la colonne, bornée par son
     * minimum et par la place à laisser aux autres colonnes et au volet de
     * lecture. Les positions sont lues dans MailView, qui ne bouge pas. */
    void MainWindow::OnSplitterPointerMoved(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_drag_column || SplitterColumn(sender) != m_drag_column)
            return;

        PaneLayout const& pane = m_drag_column == FoldersColumn() ? FoldersPane : ListPane;
        auto columns = MailView().ColumnDefinitions();

        /* largeur occupée par les autres colonnes fixes (autre panneau et
           séparateurs) ; la dernière est le volet de lecture */
        double others = 0;
        for (uint32_t i = 0; i + 1 < columns.Size(); ++i)
        {
            if (columns.GetAt(i) != m_drag_column)
                others += columns.GetAt(i).ActualWidth();
        }

        double max_width = (std::max)(pane.min_width, MailView().ActualWidth() - others - ReaderMinWidth);
        double x = args.GetCurrentPoint(MailView()).Position().X;
        double width = std::clamp(m_drag_start_width + x - m_drag_origin, pane.min_width, max_width);

        m_drag_column.Width(GridLengthHelper::FromPixels(std::round(width)));
    }

    void MainWindow::OnSplitterPointerReleased(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_drag_column)
            return;

        EndSplitterDrag();
        sender.as<UIElement>().ReleasePointerCapture(args.Pointer());
    }

    /* capture perdue (Échap, perte du focus de la fenêtre…) : la largeur
       atteinte est conservée */
    void MainWindow::OnSplitterPointerCaptureLost(IInspectable const&, Input::PointerRoutedEventArgs const&)
    {
        EndSplitterDrag();
    }

    /* Fin du glisser : mémorise la largeur de la colonne pour la prochaine
     * session. */
    void MainWindow::EndSplitterDrag()
    {
        if (!m_drag_column)
            return;

        auto column = m_drag_column;
        m_drag_column = nullptr;

        PaneLayout const& pane = column == FoldersColumn() ? FoldersPane : ListPane;
        ::EvolutionWin::Settings::Instance().SetNumber(pane.key, column.Width().Value);
    }

    /* Double-clic sur un séparateur : rétablit la largeur par défaut du
     * panneau. Le glisser éventuellement commencé par le second appui est
     * abandonné, sans quoi le prochain mouvement remettrait l'ancienne
     * largeur. */
    void MainWindow::OnSplitterDoubleTapped(IInspectable const& sender, Input::DoubleTappedRoutedEventArgs const& args)
    {
        auto column = SplitterColumn(sender);
        PaneLayout const& pane = column == FoldersColumn() ? FoldersPane : ListPane;

        m_drag_column = nullptr;
        sender.as<UIElement>().ReleasePointerCaptures();

        column.Width(GridLengthHelper::FromPixels(pane.default_width));
        ::EvolutionWin::Settings::Instance().SetNumber(pane.key, pane.default_width);
        args.Handled(true);
    }

    void MainWindow::OnRibbonFolderPaneToggled(IInspectable const&, RoutedEventArgs const&)
    {
        bool visible = RibbonFolderPaneToggle().IsChecked().GetBoolean();
        ::EvolutionWin::Settings::Instance().SetBoolean(L"layout.folders-visible", visible);
        ApplyFolderPaneVisibility(visible);
    }

    /* Volet des dossiers affiché ou masqué (largeur mémorisée conservée).
     * Paramètres :
     *   visible : true pour afficher le volet */
    void MainWindow::ApplyFolderPaneVisibility(bool visible)
    {
        RibbonFolderPaneToggle().IsChecked(visible);
        FoldersSplitter().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        if (visible)
            RestorePaneWidths();
        else
            FoldersColumn().Width(GridLengthHelper::FromPixels(0));
    }

    /* largeurs par défaut des panneaux, volet des dossiers réaffiché */
    void MainWindow::OnRibbonResetLayoutClick(IInspectable const&, RoutedEventArgs const&)
    {
        for (PaneLayout const* pane : { &FoldersPane, &ListPane })
            ::EvolutionWin::Settings::Instance().SetNumber(pane->key, pane->default_width);
        ::EvolutionWin::Settings::Instance().SetBoolean(L"layout.folders-visible", true);
        ApplyFolderPaneVisibility(true);
        ::EvolutionWin::Settings::Instance().SetString(L"layout.reading-pane", L"right");
        ApplyReadingPanePosition(L"right");
        ::EvolutionWin::Settings::Instance().SetNumber(L"layout.todobar-width", 300.0);
        m_todobar_width = 300.0;
        if (IsTodoBarVisible())
            TodoBarColumn().Width(GridLengthHelper::FromPixels(300.0));
    }

    /* La barre de titre (SetTitleBar) sert à déplacer la fenêtre : la zone de
     * la barre d'accès rapide y est déclarée « passthrough » pour que ses
     * boutons reçoivent les clics (pixels physiques, relatifs à la fenêtre). */
    void MainWindow::UpdateTitleBarPassthrough()
    {
        if (!QuickAccessBar().XamlRoot())
            return;

        double scale = QuickAccessBar().XamlRoot().RasterizationScale();
        std::vector<Windows::Graphics::RectInt32> rects;

        for (FrameworkElement element : { QuickAccessBar().as<FrameworkElement>(), SearchContainer().as<FrameworkElement>() })
        {
            if (element.ActualWidth() <= 0)
                continue;

            auto bounds = element.TransformToVisual(nullptr).TransformBounds(
                Rect{ 0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight()) });

            rects.push_back({
                static_cast<int32_t>(bounds.X * scale), static_cast<int32_t>(bounds.Y * scale),
                static_cast<int32_t>(bounds.Width * scale), static_cast<int32_t>(bounds.Height * scale) });
        }

        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id());
        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough, rects);
    }

    /* Volet de lecture à droite de la liste (colonne 4, comme Outlook par
     * défaut) ou en dessous (liste et lecture sur toute la largeur restante,
     * l'une au-dessus de l'autre) ; le séparateur liste / lecture n'a de
     * sens qu'à droite. */
    void MainWindow::ApplyReadingPanePosition(hstring const& position)
    {
        bool off = position == L"off";
        bool bottom = position == L"bottom";
        auto list = MessageListPane();
        auto reader = ReaderPane();

        if (off)
        {
            /* Volet de lecture désactivé : la liste de messages s'étend sur toute la largeur */
            Grid::SetRow(list, 0);
            Grid::SetRowSpan(list, 3);
            Grid::SetColumn(list, 2);
            Grid::SetColumnSpan(list, 3);

            reader.Visibility(Visibility::Collapsed);
            ReaderDivider().Visibility(Visibility::Collapsed);
            ListSplitter().Visibility(Visibility::Collapsed);
            ReaderRow().Height(GridLengthHelper::FromPixels(0));
        }
        else
        {
            reader.Visibility(Visibility::Visible);

            Grid::SetRow(list, 0);
            Grid::SetRowSpan(list, bottom ? 1 : 3);
            Grid::SetColumn(list, 2);
            Grid::SetColumnSpan(list, bottom ? 3 : 1);

            Grid::SetRow(reader, bottom ? 2 : 0);
            Grid::SetRowSpan(reader, bottom ? 1 : 3);
            Grid::SetColumn(reader, bottom ? 2 : 4);
            Grid::SetColumnSpan(reader, bottom ? 3 : 1);

            ReaderRow().Height(bottom ? GridLengthHelper::FromValueAndType(1.3, GridUnitType::Star)
                                      : GridLengthHelper::FromPixels(0));
            ReaderDivider().Visibility(bottom ? Visibility::Visible : Visibility::Collapsed);
            ListSplitter().Visibility(bottom ? Visibility::Collapsed : Visibility::Visible);

            if (!bottom)
            {
                RestorePaneWidths();
            }

            /* Si un message était déjà sélectionné, le recharger dans le lecteur */
            if (!m_current_message_uid.empty() && !m_current_message)
            {
                OnMessageSelectionChanged(nullptr, nullptr);
            }
        }
    }

    /* Menu Affichage › Volet de lecture : à droite / en bas / désactivé (mémorisé). */
    void MainWindow::OnReadingPaneMenuOpening(IInspectable const& sender, IInspectable const&)
    {
        auto menu = sender.as<MenuFlyout>();
        menu.Items().Clear();

        hstring current = ::EvolutionWin::Settings::Instance().GetString(L"layout.reading-pane", L"right");
        for (auto const& [key, label] : { std::pair{ L"right", L"À droite" }, std::pair{ L"bottom", L"En bas" }, std::pair{ L"off", L"Désactivé" } })
        {
            RadioMenuFlyoutItem entry;
            entry.Text(label);
            entry.GroupName(L"reading-pane");
            entry.IsChecked(current == key);
            hstring position = key;
            entry.Click([this, position](IInspectable const&, RoutedEventArgs const&)
            {
                ::EvolutionWin::Settings::Instance().SetString(L"layout.reading-pane", position);
                ApplyReadingPanePosition(position);
            });
            menu.Items().Append(entry);
        }
    }
}
