#pragma once

#include "MainWindow.g.h"
#include "CoreBridge.h"
#include "ComposeWindow.xaml.h"

#include <map>

namespace winrt::EvolutionWin::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void OnNavigationSelectionChanged(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        void OnNavigationItemInvoked(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& args);
        void OnFolderInvoked(
            Microsoft::UI::Xaml::Controls::TreeView const& sender,
            Microsoft::UI::Xaml::Controls::TreeViewItemInvokedEventArgs const& args);
        void OnMessageSelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnNewMessageClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshSplitClick(
            Microsoft::UI::Xaml::Controls::SplitButton const& sender,
            Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs const& args);
        void OnQuickFilterChanged(
            Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& args);
        void OnQuickFilterToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSaveMessageClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnReplyClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnReplyAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForwardClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleReadClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleFlagClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDeleteClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPrintClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMoveMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnSearchTextChanged(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnSearchQuerySubmitted(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);

        /* Gestionnaires du calendrier */
        void OnCalendarDateSelected(
            Microsoft::UI::Xaml::Controls::CalendarView const& sender,
            Microsoft::UI::Xaml::Controls::CalendarViewSelectedDatesChangedEventArgs const& args);
        void OnRefreshCalendarClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNewEventClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Gestionnaires des contacts */
        void OnContactSearchChanged(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnContactSearchSubmitted(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
        void OnContactSelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnNewContactClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnComposeToContactClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDeleteContactClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Gestionnaires des paramètres / comptes */
        void OnAddAccountClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        enum class ReplyKind { Sender, All, Forward };

        struct MessageState
        {
            bool seen{ true };
            bool flagged{ false };
            bool attachments{ false };
        };

        void OpenCompose(::EvolutionWin::ComposeData const& data);
        void Reply(ReplyKind kind);
        void ReplyToMessages(ReplyKind kind, std::vector<hstring> const& uids);
        void ForwardMessagesAsAttachments(std::vector<hstring> const& uids);
        ::EvolutionWin::ComposeData BuildReplyComposeData(ReplyKind kind, Windows::Data::Json::JsonObject const& message);
        void LoadMessages(bool keep_selection);
        void UpdateFolderCounts(hstring const& account, Windows::Data::Json::JsonArray const& folders);
        static std::wstring NodeKey(hstring const& account, hstring const& folder);
        void Initialize();
        void OnCoreEvent(hstring const& event, hstring const& json);
        void LoadAccounts();
        void AddFolders(Microsoft::UI::Xaml::Controls::TreeViewNode const& parent,
                        Windows::Data::Json::JsonArray const& folders,
                        hstring const& account);
        void ShowMessages(Windows::Data::Json::JsonArray const& messages);
        void FilterLocalMessages(hstring const& query);
        bool MatchesQuickFilters(Windows::Data::Json::JsonObject const& message);
        void ReapplyMessageFilters();
        hstring SelectedMessageUid();
        std::vector<hstring> SelectedMessageUids();
        std::vector<hstring> TargetMessageUids(hstring const& context_uid = L"");
        Microsoft::UI::Xaml::Controls::Grid FindMessageItem(hstring const& uid);
        void UpdateMessageItemVisual(Microsoft::UI::Xaml::Controls::Grid const& item, bool seen, bool flagged);
        void UpdateActionButtons(hstring const& uid);
        void SetMessageFlag(hstring const& uid, hstring const& flag, bool value);
        void SetMessagesFlag(std::vector<hstring> const& uids, hstring const& flag, bool value);
        void DeleteMessageByUid(hstring const& uid);
        void DeleteMessagesByUids(std::vector<hstring> const& uids);
        void MoveMessageByUid(hstring const& uid, hstring const& dest_account, hstring const& dest_folder);
        void MoveMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder);
        void RemoveMessageFromList(hstring const& uid);
        void ClearReader();
        void PopulateMoveMenuItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            std::vector<hstring> const& uids);
        void AppendFolderMoveItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            Windows::Data::Json::JsonArray const& folders,
            hstring const& account,
            hstring const& prefix,
            std::vector<hstring> const& uids);
        void ShowStatus(hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        fire_and_forget InitializeReaderAsync();
        void ShowInReader(hstring const& html);

        /* Étape 1 : Notifications Windows et pièces jointes */
        void ShowNewMailNotification(Windows::Data::Json::JsonObject const& object);
        void PopulateAttachments(Windows::Data::Json::JsonArray const& attachments);
        void UpdateSecurityBanner(Windows::Data::Json::JsonObject const& security);
        fire_and_forget SaveAttachmentAsync(uint32_t index, hstring filename);

        /* Étape 2 : Gestion des comptes, certificats TLS et authentification OAuth2 */
        fire_and_forget PromptPasswordAsync(hstring source_uid, hstring name, hstring user, bool rejected);
        fire_and_forget PromptTlsCertificateAsync(Windows::Data::Json::JsonObject object);
        fire_and_forget SignInOAuth2Async(hstring source_uid, hstring name);
        Windows::Foundation::IAsyncOperation<hstring> StartOAuth2SignInAsync(
            int provider_index, hstring client_id, hstring tenant_id, hstring login_hint);
        void RefreshAccountsView();
        fire_and_forget ShowAddAccountDialogAsync();
        fire_and_forget ShowAboutDialogAsync();
        fire_and_forget SaveMessageAsAsync();
        fire_and_forget SaveAllAttachmentsAsync(Windows::Data::Json::JsonArray attachments);
        void InitializePicker(Windows::Foundation::IInspectable const& picker);
        void ShowFolderContextMenu(
            Microsoft::UI::Xaml::UIElement const& target,
            Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);
        fire_and_forget RunFolderActionAsync(EvolutionWin::FolderItem item, hstring action);

        /* Étape 3 : Calendrier et Contacts */
        void LoadEvents();
        fire_and_forget ShowNewEventDialogAsync();
        void LoadContacts(hstring const& query);
        fire_and_forget ShowNewContactDialogAsync();

        /* shared with the compose windows */
        std::shared_ptr<::EvolutionWin::CoreBridge> m_core;
        Windows::Data::Json::JsonArray m_accounts{ nullptr };
        /* liste complète des messages du dossier courant (avant filtrage local) */
        Windows::Data::Json::JsonArray m_all_messages{ nullptr };
        /* message shown in the reader, for replies */
        Windows::Data::Json::JsonObject m_current_message{ nullptr };
        hstring m_current_message_uid;
        /* contact sélectionné dans la vue Contacts */
        Windows::Data::Json::JsonObject m_selected_contact{ nullptr };
        /* folder tree nodes by NodeKey() */
        std::map<std::wstring, Microsoft::UI::Xaml::Controls::TreeViewNode> m_nodes;
        /* arborescence JSON des dossiers par identifiant de compte */
        std::map<std::wstring, Windows::Data::Json::JsonArray> m_account_folders;
        /* état courant (lu, suivi, pièces jointes) des messages affichés par UID */
        std::map<std::wstring, MessageState> m_message_states;
        bool m_initialized{ false };
        bool m_reader_ready{ false };
        bool m_dialog_open{ false };
        int64_t m_selected_day_start{ 0 };
        hstring m_pending_html;
        hstring m_account;
        hstring m_folder;
        /* only the latest message request is shown */
        uint64_t m_message_request{ 0 };
        uint64_t m_search_request{ 0 };
        /* the list was reloaded and its selection restored: keep the reader */
        bool m_reselecting{ false };
        event_token m_activated_token;
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
