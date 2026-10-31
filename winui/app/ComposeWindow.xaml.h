#pragma once

#include "ComposeWindow.g.h"
#include "CoreBridge.h"

namespace EvolutionWin
{
    /* Contenu initial d'un message en cours de rédaction (nouveau, réponse, transfert) */
    struct ComposeData
    {
        winrt::hstring account;
        winrt::hstring to;
        winrt::hstring cc;
        winrt::hstring subject;
        winrt::hstring body;
        winrt::hstring html_body;
        winrt::hstring in_reply_to;
        winrt::hstring references;
        std::vector<winrt::hstring> attachments;
    };
}

namespace winrt::EvolutionWin::implementation
{
    struct ComposeWindow : ComposeWindowT<ComposeWindow>
    {
        ComposeWindow();

        /* Configure la fenêtre de rédaction avec le pont evo-core, la liste des
         * comptes et les données initiales du message. */
        void Setup(std::shared_ptr<::EvolutionWin::CoreBridge> const& core,
                   Windows::Data::Json::JsonArray const& accounts,
                   ::EvolutionWin::ComposeData const& data);

        void OnSendClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnAttachClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFormatModeToggle(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDiscardClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSubjectChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

        /* Gestionnaires de la barre de mise en forme HTML */
        void OnBoldClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnItalicClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnderlineClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnStrikeClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnBulletsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNumberingClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnQuoteClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnInsertLinkClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClearFormatClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ShowStatus(hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        void RebuildAttachmentPanel();
        fire_and_forget InitializeEditorAsync();
        fire_and_forget ExecuteEditorCommandAsync(hstring command, hstring argument = L"");
        fire_and_forget SwitchFormatModeAsync(bool html_mode);
        fire_and_forget SendMessageAsync();
        static hstring PlainTextToHtml(std::wstring_view text);

        std::weak_ptr<::EvolutionWin::CoreBridge> m_core;
        std::vector<hstring> m_account_uids;
        std::vector<hstring> m_attachments;
        ::EvolutionWin::ComposeData m_data;
        bool m_html_mode{ true };
        bool m_editor_ready{ false };
        bool m_focus_editor_on_ready{ false };
        hstring m_pending_editor_html;
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct ComposeWindow : ComposeWindowT<ComposeWindow, implementation::ComposeWindow>
    {
    };
}
