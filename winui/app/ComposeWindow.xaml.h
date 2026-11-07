#pragma once

#include "ComposeWindow.g.h"
#include "RecipientSuggestion.g.h"
#include "CoreBridge.h"

namespace EvolutionWin
{
    /* Type de réponse ou transfert */
    enum class ReplyKind { Sender, All, Forward };

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
    /* Suggestion d'adresse pour l'autocomplétion des destinataires (À, Cc, Cci).
     * Comporte le libellé affiché, l'adresse email normalisée et un indicateur
     * permettant de distinguer les adresses récentes (supprimables) des contacts. */
    struct RecipientSuggestion : RecipientSuggestionT<RecipientSuggestion>
    {
        /* Initialise une suggestion de destinataire.
         * Paramètres :
         *   displayText : libellé enrichi affiché dans la liste déroulante
         *   address     : adresse complète ou normalisée à insérer
         *   isRecent    : true s'il s'agit d'une adresse récemment utilisée */
        RecipientSuggestion(hstring const& displayText, hstring const& address, bool isRecent)
            : m_displayText(displayText), m_address(address), m_isRecent(isRecent)
        {
        }

        hstring DisplayText() const { return m_displayText; }
        hstring Address() const { return m_address; }
        bool IsRecent() const { return m_isRecent; }
        Microsoft::UI::Xaml::Visibility RecentVisibility() const
        {
            return m_isRecent ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
        }

    private:
        hstring m_displayText;
        hstring m_address;
        bool m_isRecent{ false };
    };

    struct ComposeWindow : ComposeWindowT<ComposeWindow>
    {
        ComposeWindow();

        /* Configure la fenêtre de rédaction avec le pont evo-core, la liste des
         * comptes et les données initiales du message. */
        void Setup(std::shared_ptr<::EvolutionWin::CoreBridge> const& core,
                   Windows::Data::Json::JsonArray const& accounts,
                   ::EvolutionWin::ComposeData const& data);

        void OnSendClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSaveDraftClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnAttachClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFormatModeToggle(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        /**
         * @brief Bascule l'indicateur d'importance haute pour le message en cours de rédaction.
         * @param sender Bouton émetteur de l'action.
         * @param args Arguments associés à l'événement de clic.
         */
        void OnHighImportanceClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        /**
         * @brief Bascule « Signer » ou « Chiffrer » : vérifie aussitôt les certificats et explique la situation.
         * @param sender Bouton émetteur.
         * @param args Arguments d'événement.
         */
        void OnSecurityToggleClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        /**
         * @brief Un champ de destinataires perd le focus : revérifie les certificats si « Chiffrer » est actif.
         * @param sender Champ émetteur.
         * @param args Arguments d'événement.
         */
        void OnRecipientLostFocus(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        /**
         * @brief Bascule l'indicateur d'importance basse pour le message en cours de rédaction.
         * @param sender Bouton émetteur de l'action.
         * @param args Arguments associés à l'événement de clic.
         */
        void OnLowImportanceClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDiscardClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRecipientTextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnRecipientQuerySubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
        void OnRecipientSuggestionChosen(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxSuggestionChosenEventArgs const& args);
        void OnRecipientPreviewKeyDown(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnRemoveRecentRecipientClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
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
        void RefreshSecurityStatus();
        bool m_security_notice_shown{ false };
        void RebuildAttachmentPanel();
        fire_and_forget InitializeEditorAsync();
        fire_and_forget ExecuteEditorCommandAsync(hstring command, hstring argument = L"");
        fire_and_forget SwitchFormatModeAsync(bool html_mode);
        fire_and_forget SendMessageAsync();
        fire_and_forget SaveDraftAsync(bool is_autosave);
        void StartAutoSaveTimer();
        void StopAutoSaveTimer();
        static hstring PlainTextToHtml(std::wstring_view text);

        std::weak_ptr<::EvolutionWin::CoreBridge> m_core;
        /* autocomplétion : requête en cours, texte inséré par suggestion */
        uint64_t m_recipient_request{ 0 };
        std::map<std::wstring, std::wstring> m_suggestion_values;
        /* annuaire de l'organisation (GAL) : recherche différée après la frappe */
        Microsoft::UI::Xaml::DispatcherTimer m_gal_timer{ nullptr };
        std::function<void()> m_gal_pending;
        void RememberRecipients();
        void RemoveRecentRecipient(std::wstring const& address);
        EvolutionWin::RecipientSuggestion m_focused_suggestion{ nullptr };
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
        Microsoft::UI::Xaml::DispatcherTimer m_autosave_timer{ nullptr };
        winrt::event_token m_autosave_token{};
        hstring m_saved_draft_uid;
        std::wstring m_last_saved_content;
        bool m_is_saving_draft{ false };
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct RecipientSuggestion : RecipientSuggestionT<RecipientSuggestion, implementation::RecipientSuggestion>
    {
    };

    struct ComposeWindow : ComposeWindowT<ComposeWindow, implementation::ComposeWindow>
    {
    };
}
