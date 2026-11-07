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
#include <winrt/Windows.Graphics.h>

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

            Windows::Graphics::SizeInt32 splash_size{ 460, 270 };
            app_win.Resize(splash_size);

            auto display_area = DisplayArea::GetFromWindowId(app_win.Id(), DisplayAreaFallback::Primary);
            if (display_area)
            {
                auto work_area = display_area.WorkArea();
                Windows::Graphics::PointInt32 pos{
                    work_area.X + (work_area.Width - splash_size.Width) / 2,
                    work_area.Y + (work_area.Height - splash_size.Height) / 2
                };
                app_win.Move(pos);
            }
        }
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
