#include "pch.h"
#include "SplashWindow.xaml.h"
#if __has_include("SplashWindow.g.cpp")
#include "SplashWindow.g.cpp"
#endif

#include "WindowIcon.h"
#include "evo-version.h"
#include "UiTranslate.h"

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.Graphics.h>
#include <microsoft.ui.xaml.window.h>
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Windowing;

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Initialise la fenêtre de démarrage sans bordures et centrée sur l'écran.
     */
    SplashWindow::SplashWindow()
    {
        InitializeComponent();

        ::EvolutionWin::UiTranslate::ApplyToWindow(*this);

        VersionText().Text(::EvolutionWin::Helpers::Fmt(::EvolutionWin::Helpers::Tr(L"Version {0}"), { to_hstring(EVO_VERSION_STR) }));

        try
        {
            wchar_t exe_path[MAX_PATH];
            if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH))
            {
                std::wstring exe_dir{ exe_path };
                auto slash = exe_dir.find_last_of(L"\\/");
                if (slash != std::wstring::npos)
                    exe_dir.resize(slash);

                std::wstring logo_path = exe_dir + L"\\Assets\\EvolutionWin.png";
                if (GetFileAttributesW(logo_path.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    AppLogo().Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{
                        Windows::Foundation::Uri{ logo_path }
                    });
                }
            }
        }
        catch (...)
        {
        }

        ExtendsContentIntoTitleBar(true);

        auto app_win = AppWindow();
        if (app_win)
        {
            ::EvolutionWin::SetWindowIcon(app_win);
            app_win.Title(L"Evolution");

            auto presenter = app_win.Presenter().try_as<OverlappedPresenter>();
            if (presenter)
            {
                presenter.IsResizable(false);
                presenter.IsMaximizable(false);
                presenter.IsMinimizable(false);
            }

            auto title_bar = app_win.TitleBar();
            if (title_bar)
            {
                title_bar.ExtendsContentIntoTitleBar(true);
                title_bar.ButtonBackgroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonInactiveBackgroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonHoverBackgroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonPressedBackgroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonForegroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonInactiveForegroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonHoverForegroundColor(Microsoft::UI::Colors::Transparent());
                title_bar.ButtonPressedForegroundColor(Microsoft::UI::Colors::Transparent());
            }

            // Resize() travaille en pixels physiques : on convertit la taille voulue
            // (en pixels indépendants du DPI) selon l'échelle de l'écran, sinon le
            // contenu est rogné sur les portables en 125 % / 150 % / 200 %.
            HWND hwnd = nullptr;
            if (auto native = try_as<::IWindowNative>())
                native->get_WindowHandle(&hwnd);
            UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
            if (dpi == 0)
                dpi = 96;
            double scale = static_cast<double>(dpi) / 96.0;

            int width = static_cast<int>(520 * scale);
            int height = static_cast<int>(340 * scale);

            auto display_area = DisplayArea::GetFromWindowId(app_win.Id(), DisplayAreaFallback::Primary);
            Windows::Graphics::RectInt32 work_area{ 0, 0, width, height };
            if (display_area)
                work_area = display_area.WorkArea();

            // Ne jamais dépasser la zone de travail (petits écrans)
            width = (std::min)(width, static_cast<int>(work_area.Width * 0.9));
            height = (std::min)(height, static_cast<int>(work_area.Height * 0.9));

            Windows::Graphics::SizeInt32 splash_size{ width, height };
            app_win.Resize(splash_size);
            app_win.Move(Windows::Graphics::PointInt32{
                work_area.X + (work_area.Width - splash_size.Width) / 2,
                work_area.Y + (work_area.Height - splash_size.Height) / 2 });
        }

        // Apparition en fondu du logo et du titre
        RootGrid().Loaded([this](auto&&, auto&&)
        {
            using namespace Microsoft::UI::Xaml::Media::Animation;
            DoubleAnimation fade;
            fade.From(0.0);
            fade.To(1.0);
            fade.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(600)));
            fade.EasingFunction(CubicEase{});
            Storyboard board;
            Storyboard::SetTarget(fade, BrandPanel());
            Storyboard::SetTargetProperty(fade, L"Opacity");
            board.Children().Append(fade);
            board.Begin();
        });
    }

    /**
     * @brief Met à jour le libellé textuel de l'étape de chargement affiché à l'utilisateur.
     * @param status Chaîne décrivant l'étape en cours.
     */
    void SplashWindow::SetStatus(hstring const& status)
    {
        StatusText().Text(status);
    }
}
