#pragma once

#include "MessageWindow.g.h"
#include "CoreBridge.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace winrt::EvolutionWin::implementation
{
    struct MessageWindow : MessageWindowT<MessageWindow>
    {
        MessageWindow();

        /* Initialise la fenêtre avec le pont vers le moteur, les comptes, le
         * dossier et le message cible.
         * Paramètres :
         *   core             : pont vers evo-core
         *   accounts         : liste JSON des comptes configurés
         *   account          : identifiant du compte du message
         *   folder           : nom complet du dossier du message
         *   uid              : identifiant unique du message
         *   all_uids         : liste ordonnée des UID des messages affichés dans la vue
         *   account_folders  : arborescence des dossiers par compte (pour Déplacer)
         *   on_seen_changed  : rappel lorsque l'état lu/non lu change
         *   on_deleted       : rappel lorsque le message est supprimé
         *   on_moved         : rappel lorsque le message est déplacé */
        void Setup(std::shared_ptr<::EvolutionWin::CoreBridge> const& core,
                   Windows::Data::Json::JsonArray const& accounts,
                   winrt::hstring const& account,
                   winrt::hstring const& folder,
                   winrt::hstring const& uid,
                   std::vector<winrt::hstring> const& all_uids,
                   std::map<std::wstring, Windows::Data::Json::JsonArray> const& account_folders,
                   std::function<void(winrt::hstring const&, bool)> on_seen_changed = nullptr,
                   std::function<void(winrt::hstring const&)> on_deleted = nullptr,
                   std::function<void(winrt::hstring const&, winrt::hstring const&)> on_moved = nullptr);

        /* Navigation entre messages */
        void OnPrevClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNextClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Actions sur le message courant */
        void OnReplyClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnReplyAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForwardClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForwardAsAttachmentClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleReadClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMoveMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnDeleteClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPrintClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Gestion du contenu externe distant */
        void OnAllowRemoteForMessageClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTrustSenderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTrustDomainClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Pièces jointes */
        fire_and_forget OnSaveAllAttachmentsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void LoadCurrentMessage();
        void UpdateNavigationButtons();
        fire_and_forget InitializeReaderAsync();
        void ShowInReader(winrt::hstring const& html, bool allow_remote);
        void ShowCurrentMessageInReader(bool force_remote);
        void OnReaderResourceRequested(Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
                                       Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);
        fire_and_forget ServeReaderDocumentAsync(Microsoft::Web::WebView2::Core::CoreWebView2 sender,
                                                 Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs args,
                                                 winrt::hstring html);
        void PopulateAttachments(Windows::Data::Json::JsonArray const& attachments);
        void UpdateSecurityBanner(Windows::Data::Json::JsonObject const& security);
        void UpdateRemoteContentBanner();
        void SaveWindowBounds();
        void RestoreWindowBounds();
        void ShowStatus(winrt::hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);

        std::weak_ptr<::EvolutionWin::CoreBridge> m_core;
        Windows::Data::Json::JsonArray m_accounts{ nullptr };
        winrt::hstring m_account;
        winrt::hstring m_folder;
        winrt::hstring m_uid;
        std::vector<winrt::hstring> m_all_uids;
        size_t m_current_index{ 0 };

        Windows::Data::Json::JsonObject m_current_message{ nullptr };
        std::map<std::wstring, Windows::Data::Json::JsonArray> m_account_folders;
        std::function<void(winrt::hstring const&, bool)> m_on_seen_changed;
        std::function<void(winrt::hstring const&)> m_on_deleted;
        std::function<void(winrt::hstring const&, winrt::hstring const&)> m_on_moved;

        bool m_seen{ false };
        bool m_reader_ready{ false };
        winrt::hstring m_reader_uri;
        winrt::hstring m_reader_html;
        uint64_t m_reader_generation{ 0 };
        uint64_t m_reader_navigation_id{ 0 };
        bool m_reader_navigation_known{ false };
        bool m_reader_pending_allow{ false };
        bool m_reader_document_active{ false };
        bool m_reader_remote_allowed{ false };
        uint32_t m_reader_blocked_count{ 0 };
        uint64_t m_message_request{ 0 };
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct MessageWindow : MessageWindowT<MessageWindow, implementation::MessageWindow>
    {
    };
}
