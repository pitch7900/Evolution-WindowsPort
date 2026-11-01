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
        /* réponse ou transfert : signature des réponses, placement par rapport au texte cité */
        bool reply{ false };
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
        void OnRecipientTextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnRecipientQuerySubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
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

        /* Signature : changement de compte expéditeur ou de signature */
        void OnFromChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSignatureChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

    private:
        void LoadSignatures();
        void SelectAccountSignature();
        Windows::Data::Json::JsonObject SelectedAccount();
        std::wstring SignatureContent(bool html);
        fire_and_forget ApplySignatureAsync();
        std::wstring RemovePlainSignature(std::wstring text);
        void ShowStatus(hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        void RebuildAttachmentPanel();
        fire_and_forget InitializeEditorAsync();
        fire_and_forget ExecuteEditorCommandAsync(hstring command, hstring argument = L"");
        fire_and_forget SwitchFormatModeAsync(bool html_mode);
        fire_and_forget SendMessageAsync();
        static hstring PlainTextToHtml(std::wstring_view text);

        std::weak_ptr<::EvolutionWin::CoreBridge> m_core;
        /* autocomplétion : requête en cours, texte inséré par suggestion */
        uint64_t m_recipient_request{ 0 };
        std::map<std::wstring, std::wstring> m_suggestion_values;
        void RememberRecipients();
        std::vector<hstring> m_account_uids;
        std::vector<hstring> m_attachments;
        ::EvolutionWin::ComposeData m_data;
        bool m_html_mode{ true };
        bool m_editor_ready{ false };
        bool m_focus_editor_on_ready{ false };
        hstring m_pending_editor_html;
        /* comptes (evo_core_list_accounts), signatures (evo_core_list_signatures)
         * et uid de chaque entrée du sélecteur de signature */
        Windows::Data::Json::JsonArray m_accounts{ nullptr };
        Windows::Data::Json::JsonArray m_signatures{ nullptr };
        std::vector<hstring> m_signature_choices;
        /* sélecteur de signature mis à jour par le code */
        bool m_updating_signature{ false };
        /* bloc de signature inséré dans le texte brut, pour le remplacer */
        std::wstring m_plain_signature;
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct ComposeWindow : ComposeWindowT<ComposeWindow, implementation::ComposeWindow>
    {
    };
}
