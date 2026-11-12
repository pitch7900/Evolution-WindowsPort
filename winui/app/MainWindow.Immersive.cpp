#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "ImmersiveReader.h"
#include "WindowIcon.h"

#include <memory>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** État d'une fenêtre du lecteur immersif, partagé par ses gestionnaires d'événements. */
        struct ImmersiveState
        {
            Microsoft::UI::Xaml::Window window{ nullptr };
            Microsoft::UI::Xaml::Controls::WebView2 view{ nullptr };
            ::EvolutionWin::Immersive::Options options;
            std::wstring subject;
            std::wstring sender;
            std::wstring date;
            std::wstring body;
            bool ready{ false };
        };

        /** @brief Charge les réglages enregistrés du lecteur immersif. */
        ::EvolutionWin::Immersive::Options LoadImmersiveOptions()
        {
            auto& settings = ::EvolutionWin::Settings::Instance();
            ::EvolutionWin::Immersive::Options options;
            options.font_size = settings.GetInt(L"immersive.font-size", ::EvolutionWin::Immersive::kDefaultFontSize);
            options.spacing = settings.GetInt(L"immersive.spacing", ::EvolutionWin::Immersive::kDefaultSpacing);
            options.theme = settings.GetInt(L"immersive.theme", ::EvolutionWin::Immersive::kDefaultTheme);
            return ::EvolutionWin::Immersive::Normalize(options);
        }

        /** @brief Enregistre les réglages du lecteur immersif. */
        void SaveImmersiveOptions(::EvolutionWin::Immersive::Options const& options)
        {
            auto& settings = ::EvolutionWin::Settings::Instance();
            settings.SetNumber(L"immersive.font-size", options.font_size);
            settings.SetNumber(L"immersive.spacing", options.spacing);
            settings.SetNumber(L"immersive.theme", options.theme);
        }

        /** @brief Affiche (ou réaffiche) le document dans la vue web de la fenêtre immersive. */
        void RenderImmersive(std::shared_ptr<ImmersiveState> const& state)
        {
            if (!state->ready || !state->view || !state->view.CoreWebView2())
                return;
            state->view.CoreWebView2().NavigateToString(hstring{ ::EvolutionWin::Immersive::BuildHtml(
                state->subject, state->sender, state->date, state->body, state->options) });
        }

        /** @brief Applique de nouveaux réglages : enregistrement puis nouvel affichage. */
        void ApplyImmersiveOptions(std::shared_ptr<ImmersiveState> const& state, ::EvolutionWin::Immersive::Options options)
        {
            state->options = ::EvolutionWin::Immersive::Normalize(options);
            SaveImmersiveOptions(state->options);
            RenderImmersive(state);
        }

        /**
         * @brief Crée un bouton de la barre d'outils du lecteur immersif.
         * @param glyph Glyphe Segoe Fluent Icons (ou texte court si glyph_is_text).
         * @param tooltip Info-bulle (déjà traduite).
         * @param on_click Action au clic.
         */
        Button MakeImmersiveButton(hstring const& content, hstring const& tooltip, std::function<void()> on_click)
        {
            Button button;
            button.Content(box_value(content));
            button.MinWidth(40);
            button.Height(36);
            button.Padding({ 10, 0, 10, 0 });
            ToolTipService::SetToolTip(button, box_value(tooltip));
            button.Click([on_click = std::move(on_click)](auto const&, auto const&) { on_click(); });
            return button;
        }
    }

    /**
     * @brief Ouvre le lecteur immersif sur le message affiché : fenêtre plein écran épurée avec le texte
     *        seul, taille du texte, espacement et thème réglables. Échap (ou le bouton de fermeture) quitte.
     */
    void MainWindow::OpenImmersiveReader()
    {
        if (!m_current_message)
        {
            ShowStatus(Tr(L"Sélectionnez d'abord un message à lire."), InfoBarSeverity::Informational);
            return;
        }

        if (m_immersive_window)
        {
            m_immersive_window.Close();
            m_immersive_window = nullptr;
        }

        auto state = std::make_shared<ImmersiveState>();
        state->options = LoadImmersiveOptions();
        state->subject = std::wstring{ m_current_message.GetNamedString(L"subject", L"") };
        state->sender = std::wstring{ m_current_message.GetNamedString(L"from", L"") };
        double date = m_current_message.GetNamedNumber(L"date", 0);
        if (date > 0)
            state->date = std::wstring{ FormatDate(date) };

        /* texte seul : corps texte du message, à défaut le HTML réduit à son texte */
        std::wstring text{ m_current_message.GetNamedString(L"text", L"") };
        if (text.empty())
            text = std::wstring{ m_current_message.GetNamedString(L"body", L"") };
        if (text.empty())
            text = ExtractPlainTextForSpeech(L"", L"", L"", std::wstring{ m_current_message.GetNamedString(L"html", L"") });
        state->body = text;

        Microsoft::UI::Xaml::Window window;
        state->window = window;
        window.Title(Tr(L"Lecteur immersif"));
        ::EvolutionWin::SetWindowIcon(window.AppWindow());

        Grid root;
        root.Background(Microsoft::UI::Xaml::Media::SolidColorBrush{ Microsoft::UI::Colors::Black() });

        Microsoft::UI::Xaml::Controls::WebView2 view;
        state->view = view;
        view.HorizontalAlignment(HorizontalAlignment::Stretch);
        view.VerticalAlignment(VerticalAlignment::Stretch);
        root.Children().Append(view);

        /* barre d'outils flottante en haut à droite */
        StackPanel toolbar;
        toolbar.Orientation(Orientation::Horizontal);
        toolbar.Spacing(6);
        toolbar.Padding({ 8, 8, 8, 8 });
        toolbar.HorizontalAlignment(HorizontalAlignment::Right);
        toolbar.VerticalAlignment(VerticalAlignment::Top);
        toolbar.Margin({ 0, 0, 24, 0 });

        auto close = [state]()
        {
            if (state->window)
                state->window.Close();
        };

        toolbar.Children().Append(MakeImmersiveButton(L"A−", Tr(L"Réduire le texte (touche -)"), [state]()
        {
            ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::StepFontSize(state->options, -1));
        }));
        toolbar.Children().Append(MakeImmersiveButton(L"A+", Tr(L"Agrandir le texte (touche +)"), [state]()
        {
            ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::StepFontSize(state->options, +1));
        }));
        toolbar.Children().Append(MakeImmersiveButton(L"", Tr(L"Changer l'espacement des lignes (touche L)"), [state]()
        {
            ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::CycleSpacing(state->options));
        }));
        toolbar.Children().Append(MakeImmersiveButton(L"", Tr(L"Changer le thème : clair, sépia, sombre (touche T)"), [state]()
        {
            ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::CycleTheme(state->options));
        }));
        auto quit_button = MakeImmersiveButton(L"", Tr(L"Quitter le lecteur immersif (Échap)"), close);
        toolbar.Children().Append(quit_button);

        /* les boutons d'icône utilisent la police des icônes Fluent */
        for (uint32_t i = 2; i < toolbar.Children().Size(); ++i)
            if (auto button = toolbar.Children().GetAt(i).try_as<Button>())
                button.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe Fluent Icons" });
        root.Children().Append(toolbar);

        /* Échap quand le focus est sur la barre d'outils (dans le document, c'est le script qui le gère) */
        Microsoft::UI::Xaml::Input::KeyboardAccelerator escape;
        escape.Key(Windows::System::VirtualKey::Escape);
        escape.Invoked([close](auto const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
        {
            args.Handled(true);
            close();
        });
        root.KeyboardAccelerators().Append(escape);

        window.Content(root);
        window.Closed([this, state](auto const&, auto const&)
        {
            state->window = nullptr;
            state->ready = false;
            m_immersive_window = nullptr;
        });
        m_immersive_window = window;

        window.Activate();
        window.AppWindow().SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);

        /* initialisation asynchrone de la vue web, puis premier affichage */
        [](std::shared_ptr<ImmersiveState> state) -> fire_and_forget
        {
            auto view = state->view;
            try
            {
                co_await view.EnsureCoreWebView2Async();
            }
            catch (...)
            {
                co_return;
            }
            auto core = view.CoreWebView2();
            if (!core || !state->window)
                co_return;

            core.Settings().AreDevToolsEnabled(false);
            core.Settings().AreDefaultContextMenusEnabled(false);
            core.Settings().IsStatusBarEnabled(false);
            core.Settings().IsZoomControlEnabled(false);
            view.DefaultBackgroundColor(Microsoft::UI::Colors::Black());

            /* touches du document : Échap, + / -, T, L */
            core.WebMessageReceived([state](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args)
            {
                std::wstring message{ args.TryGetWebMessageAsString() };
                if (message == L"close")
                {
                    if (state->window)
                        state->window.Close();
                }
                else if (message == L"bigger")
                    ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::StepFontSize(state->options, +1));
                else if (message == L"smaller")
                    ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::StepFontSize(state->options, -1));
                else if (message == L"theme")
                    ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::CycleTheme(state->options));
                else if (message == L"spacing")
                    ApplyImmersiveOptions(state, ::EvolutionWin::Immersive::CycleSpacing(state->options));
            });

            state->ready = true;
            RenderImmersive(state);
            view.Focus(FocusState::Programmatic);
        }(state);
    }

    /**
     * @brief Clic sur « Lecteur immersif » du ruban Affichage.
     */
    void MainWindow::OnImmersiveReaderClick(IInspectable const&, RoutedEventArgs const&)
    {
        OpenImmersiveReader();
    }
}
