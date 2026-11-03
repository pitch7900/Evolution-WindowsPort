#pragma once

#include "MainWindow.g.h"
#include "CoreBridge.h"
#include "ComposeWindow.xaml.h"
#include "MessageWindow.xaml.h"
#include "SplashWindow.xaml.h"

#include <map>

namespace winrt::EvolutionWin::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void SelectNavigationModule(winrt::hstring const& tag);
        void OnNavigationSelectionChanged(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        /* ruban */
        void OnRibbonTabChanged(
            Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& args);
        void OnRibbonSettingsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonAboutClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonQuitClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonNewFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonRenameFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonFolderMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnRibbonMarkAllReadClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonEmptyFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonDeleteFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonReadFilterClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonFolderPaneToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFolderPaneMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnRibbonResetLayoutClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonTodayClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnGoToDateClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void GoToCalendarDate(Windows::Foundation::DateTime date);
        winrt::fire_and_forget ShowGoToDateDialogAsync();
        void OnReadingPaneMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnCloseAllSecondaryWindowsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void CloseAllSecondaryWindows();
        void OnRibbonCompactDensityToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        /**
         * @brief Bascule l'affichage en conversations pour le dossier actif.
         * @param sender Bouton à bascule émetteur.
         * @param args Arguments d'événement.
         */
        void OnRibbonConversationToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ApplyCompactDensity(bool compact);
        void OnSearchAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnSearchKeyDown(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnEventItemClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void OnCalendarPeriodClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonMiniCalendarToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCalendarTimeScaleClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCalendarPeriodChanged(Microsoft::UI::Xaml::Controls::SelectorBar const& sender, Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& args);
        void OnCalendarPreviousClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCalendarNextClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnConfidentialToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnQuickAccessSizeChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void OnSortMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnSortDirectionClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnInfoBannerClosed(
            Microsoft::UI::Xaml::Controls::InfoBar const& sender,
            Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args);
        void OnNavigationItemInvoked(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& args);
        void OnFolderInvoked(
            Microsoft::UI::Xaml::Controls::TreeView const& sender,
            Microsoft::UI::Xaml::Controls::TreeViewItemInvokedEventArgs const& args);
        void OnMessageSelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnMessageListDoubleTapped(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
        void OnMessageListKeyDown(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
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
        void OnOpenInBrowserClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnViewRawMimeClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget ShowRawMimeSourceAsync(winrt::hstring uid);
        fire_and_forget OnReadAloudClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void StopSpeech();
        void OnCloseAttachmentPreviewClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPreviewSaveClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPreviewOpenClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget PreviewAttachmentAsync(uint32_t index, winrt::hstring filename);
        void CloseAttachmentPreview();
        void OnRefreshAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshFolderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnReplyClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnReplyAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForwardClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForwardAsAttachmentClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleReadClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleFlagClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDeleteClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnArchiveClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnJunkClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAddCalendarClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPrintClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnZoomInClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnZoomOutClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnZoomResetClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTogglePlainTextClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMoveFolderDialogClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMoveFolderAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        fire_and_forget ShowMoveFolderDialogAsync(std::vector<winrt::hstring> uids, bool copy);
        void ApplyReaderZoom(double zoom);
        void OnMoveMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnSearchTextChanged(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnSearchQuerySubmitted(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
        void OnSearchScopeChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

        /* Recherche rapide de contacts et accès au carnet d'adresses */
        void OnAddressBookClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRibbonContactSearchTextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnRibbonContactSearchQuerySubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
        void OnRibbonContactSearchSuggestionChosen(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxSuggestionChosenEventArgs const& args);

        /* Barre d'état permanente : indicateur d'état des connexions et hors connexion */
        void OnConnectionStatusButtonClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClearAllNotificationsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRetryConnectionsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnToggleOfflineClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void UpdateStatusBarProgress(bool active, winrt::hstring const& message, int32_t percent);
        void UpdateTaskbarProgress(bool active, int32_t percent, bool error);
        void UpdateTaskbarBadge(int32_t count);
        void UpdateConnectionStatus(winrt::hstring const& account, winrt::hstring const& status, winrt::hstring const& message);
        void RefreshConnectionAccountsList();
        void UpdateFolderStatusText();
        void RestoreSessionFolder();
        void UpdateOutOfOfficeBanner();
        Windows::Foundation::IAsyncAction ShowOutOfOfficeDialogAsync(winrt::hstring account);

        /* Écran de démarrage (Splash screen) */
        void SetSplashWindow(winrt::EvolutionWin::SplashWindow const& splash);
        void CloseSplash();
        void StartCore();

        /* Séparateurs déplaçables entre dossiers, liste et volet de lecture */
        void OnSplitterPointerPressed(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnSplitterPointerMoved(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnSplitterPointerReleased(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnSplitterPointerCaptureLost(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnSplitterDoubleTapped(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

        /* Gestionnaires du calendrier */
        void OnCalendarDateSelected(
            Microsoft::UI::Xaml::Controls::CalendarView const& sender,
            Microsoft::UI::Xaml::Controls::CalendarViewSelectedDatesChangedEventArgs const& args);
        void OnRefreshCalendarClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNewEventClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnExportCalendarClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

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
        void OnExportContactsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnComposeToContactClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDeleteContactClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Gestionnaires des tâches */
        void OnNewTaskClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNewTaskAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnRefreshTasksClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTaskListChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnTaskFilterChanged(
            Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& args);
        void OnTaskSortChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnShowCompletedTasksClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTaskItemClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void OnCreateTaskFromMessageClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCategorizeMenuOpening(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnCategorizeQuickAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnTaskSearchTextChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnTaskSearchQuerySubmitted(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);

        /* Barre latérale des tâches (To-Do bar) */
        void OnRibbonTodoBarToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTodoBarCloseClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTodoBarRefreshClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTodoBarQuickAddClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTodoBarQuickInputKeyDown(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnTodoBarItemClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void OnTodoBarViewAllClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTodoBarSplitterPointerPressed(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnTodoBarSplitterPointerMoved(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnTodoBarSplitterPointerReleased(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnTodoBarSplitterPointerCaptureLost(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnTodoBarAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

        /* Gestionnaires des paramètres / comptes */
        void OnAddAccountClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRemoteContentModeChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

        /* Signatures (Paramètres) */
        void OnNewSignatureClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSignatureFormatChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSignatureToolClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnSignatureLinkClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnSignatureImageClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSignatureEditorCancel(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnSignatureEditorSave(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSignaturePlacementToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Bandeau « Contenu externe bloqué » */
        void OnShowRemoteContentClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTrustSenderClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTrustDomainClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        /* Gestionnaires de la page Paramètres (MainWindow.Settings.cpp) */
        void OnSettingsNavSelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSettingsSearchTextChanged(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
        void OnSettingsSearchSuggestionChosen(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxSuggestionChosenEventArgs const& args);
        void OnSettingsBreadcrumbRootClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSettingsAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void SelectSettingsCategory(winrt::hstring const& category_tag);
        void InitSettingsView();
        void OnThemeChoiceChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSettingToggleChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSettingCheckChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSettingComboChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSettingTextBoxLostFocus(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCheckUpdateNowClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAboutCheckUpdateClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAboutCopySystemInfoClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnOpenDataFolderClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget OnClearWebViewCacheClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTestNotificationSoundClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnResetAllSettingsClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ApplyTheme(winrt::hstring const& theme_name);
        void OnCalendarWorkDayToggled(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ApplyCalendarPreferences();

        /* Notifications par compte */
        void OnNotificationAccountSelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnAccountNotificationSwitchToggled(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAccountSoundSwitchToggled(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void PopulateNotificationAccounts();
        void OnRibbonKeyboardShortcutsClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnKeyboardShortcutsAccelerator(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        winrt::fire_and_forget ShowKeyboardShortcutsDialogAsync();
        void UpdateAccountNotificationControls();
        void OnMessageListDragItemsStarting(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& args);
        void PrepareDragDropCache();

    private:
        using ReplyKind = ::EvolutionWin::ReplyKind;

        struct MessageState
        {
            bool seen{ true };
            bool flagged{ false };
            bool attachments{ false };
        };

        std::vector<std::wstring> m_drag_cached_files;

        void OpenCompose(::EvolutionWin::ComposeData const& data);
        void Reply(ReplyKind kind);
        void ReplyToMessages(ReplyKind kind, std::vector<hstring> const& uids);
        void ForwardMessagesAsAttachments(std::vector<hstring> const& uids);
        ::EvolutionWin::ComposeData BuildReplyComposeData(ReplyKind kind, Windows::Data::Json::JsonObject const& message);
        void LoadMessages(bool keep_selection);
        void UpdateFolderCounts(hstring const& account, Windows::Data::Json::JsonArray const& folders);
        static std::wstring NodeKey(hstring const& account, hstring const& folder);
        void Initialize();
        void RestorePaneWidths();
        Microsoft::UI::Xaml::Controls::ColumnDefinition SplitterColumn(Windows::Foundation::IInspectable const& splitter);
        void EndSplitterDrag();
        void OnCoreEvent(hstring const& event, hstring const& json);
        void LoadAccounts();
        void AddFolders(Microsoft::UI::Xaml::Controls::TreeViewNode const& parent,
                        Windows::Data::Json::JsonArray const& folders,
                        hstring const& account);
        void ShowMessages(Windows::Data::Json::JsonArray const& messages);
        void FilterLocalMessages(hstring const& query);
        void UpdateMailBadge();
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
        void CopyMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder);
        void ArchiveMessagesByUids(std::vector<hstring> const& uids);
        void MarkAsJunkByUids(std::vector<hstring> const& uids);
        void PopulateCategorizeMenuItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            std::vector<hstring> const& uids);
        void ToggleCategoryForMessages(std::vector<hstring> const& uids, std::wstring const& category_name, bool apply);
        void ClearCategoriesForMessages(std::vector<hstring> const& uids);
        void RememberRecentFolder(hstring const& account, hstring const& folder);
        void RemoveMessageFromList(hstring const& uid);
        void ClearReader();
        void PopulateMoveMenuItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            std::vector<hstring> const& uids,
            bool copy = false);
        void AppendFolderMoveItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            Windows::Data::Json::JsonArray const& folders,
            hstring const& account,
            hstring const& prefix,
            std::vector<hstring> const& uids,
            bool copy = false);
        void ShowStatus(hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        fire_and_forget InitializeReaderAsync();
        void ShowInReader(hstring const& html, bool allow_remote = false);
        void ShowCurrentMessageInReader(bool force_remote);
        void OnReaderResourceRequested(
            Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
            Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);
        fire_and_forget ServeReaderDocumentAsync(
            Microsoft::Web::WebView2::Core::CoreWebView2 sender,
            Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs args,
            hstring html);
        void UpdateRemoteContentBanner();
        void TrustCurrentSender(hstring const& key, std::wstring const& entry, hstring const& label);
        void RefreshPrivacyView();

        /* Étape 1 : Notifications Windows et pièces jointes */
        void ShowNewMailNotification(Windows::Data::Json::JsonObject const& object);
        void PopulateAttachments(Windows::Data::Json::JsonArray const& attachments);
        void UpdateSecurityBanner(Windows::Data::Json::JsonObject const& security);
        fire_and_forget SaveAttachmentAsync(uint32_t index, hstring filename);

        /* Fenêtre de lecture indépendante (Tâche 16) */
        void OpenMessageInWindow(hstring const& uid);
        bool IsDraftsFolder();
        bool IsTrashFolder();
        fire_and_forget ConfirmAndDeleteMessages(std::vector<winrt::hstring> uids, bool permanent);

        /* Portée de la recherche et localisation des messages (Tâche 20) */
        struct MessageLocation
        {
            hstring account;
            hstring folder;
            hstring folder_name;
            hstring account_name;
        };
        std::unordered_map<std::wstring, MessageLocation> m_message_locations;
        MessageLocation GetMessageLocation(hstring const& uid);
        hstring CurrentSearchScope();
        void UpdateSearchPlaceholder();

        /* Étape 2 : Gestion des comptes, certificats TLS et authentification OAuth2 */
        fire_and_forget PromptPasswordAsync(hstring source_uid, hstring name, hstring user, bool rejected);
        fire_and_forget PromptTlsCertificateAsync(Windows::Data::Json::JsonObject object);
        fire_and_forget SignInOAuth2Async(hstring source_uid, hstring name);
        Windows::Foundation::IAsyncOperation<hstring> StartOAuth2SignInAsync(
            int provider_index, hstring client_id, hstring tenant_id, hstring login_hint);
        void RefreshAccountsView();
        fire_and_forget ShowAccountDetectionDialogAsync();
        fire_and_forget ShowAddAccountDialogAsync(Windows::Data::Json::JsonObject prefill);
        fire_and_forget ShowEditAccountDialogAsync(hstring account_uid);
        fire_and_forget SignInGoogleOAuth2BrowserAsync(hstring source_uid, hstring name);
        void PopulateAboutSettingsView(bool force_check = false);
        void CheckForUpdateOnStartup();
        fire_and_forget ShowUpdateDialogAsync();
        fire_and_forget SaveMessageAsAsync();
        fire_and_forget SaveAllAttachmentsAsync(Windows::Data::Json::JsonArray attachments);
        void InitializePicker(Windows::Foundation::IInspectable const& picker);
        void ShowFolderContextMenu(
            Microsoft::UI::Xaml::UIElement const& target,
            Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);
        fire_and_forget RunFolderActionAsync(EvolutionWin::FolderItem item, hstring action);

        /* Gestion des dossiers : créer, renommer, déplacer, copier, supprimer */
        hstring NodeFolderName(Microsoft::UI::Xaml::Controls::TreeViewNode const& node);
        Windows::Data::Json::JsonObject FindFolderJson(Windows::Data::Json::JsonArray const& folders, hstring const& full_name);
        void AppendFolderManagementItems(
            Microsoft::UI::Xaml::Controls::MenuFlyout const& menu,
            EvolutionWin::FolderItem const& item,
            hstring const& full_name);
        void AppendFolderTargetItems(
            Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> const& items,
            Windows::Data::Json::JsonArray const& folders,
            hstring const& prefix,
            hstring const& excluded,
            hstring const& skipped,
            bool selectable_only,
            std::function<void(hstring const&)> const& on_pick);
        Windows::Foundation::IAsyncOperation<hstring> PromptFolderNameAsync(hstring title, hstring label, hstring initial);
        fire_and_forget CreateFolderAsync(hstring account, hstring parent, hstring parent_name);
        fire_and_forget RenameFolderAsync(hstring account, hstring full_name, hstring name);
        void MoveOrCopyFolder(hstring const& account, hstring const& full_name, hstring const& name,
                              hstring const& parent, bool copy);
        fire_and_forget DeleteFolderAsync(hstring account, hstring full_name, hstring name, int32_t total);
        void ReloadAccountFolders(hstring const& account, hstring const& old_name, hstring const& new_name);

        /* Étape 3 : Calendrier et Contacts */
        void LoadEvents();
        fire_and_forget ShowNewEventDialogAsync();
        void LoadContacts(hstring const& query);
        fire_and_forget ShowNewContactDialogAsync();

        /* Exportation vCard / iCalendar */
        fire_and_forget ExportCalendarAsync(Windows::Data::Json::JsonArray sources);
        fire_and_forget ExportContactsAsync(Windows::Data::Json::JsonArray books);

        /* Tâches (VTODO) */
        void LoadTasks();
        void RestoreTaskViewSettings();
        void ShowTasks();
        bool MatchesTaskFilter(Windows::Data::Json::JsonObject const& task, hstring const& filter);
        bool MatchesTaskSearch(Windows::Data::Json::JsonObject const& task, hstring const& query);
        Microsoft::UI::Xaml::UIElement BuildTaskRow(Windows::Data::Json::JsonObject const& task, bool show_list);
        void GetTaskAccountInfo(Windows::Data::Json::JsonObject const& task, hstring& out_name, hstring& out_email);
        void CreateTaskFromMessage(hstring const& message_uid);
        void CreateTaskFromEvent(Windows::Data::Json::JsonObject const& event_data);
        void SetTaskCompleted(Windows::Data::Json::JsonObject const& task, bool completed);
        void DeleteTaskItem(Windows::Data::Json::JsonObject const& task);
        fire_and_forget ShowTaskEditorAsync(Windows::Data::Json::JsonObject task);

        /* Volet latéral des tâches (To-Do bar façon Outlook) */
        void RestoreTodoBarSettings();
        void SetTodoBarVisible(bool visible, bool save = true);
        void UpdateTodoBarVisibility();
        void UpdateTodoBar();
        bool IsTodoBarVisible();
        Microsoft::UI::Xaml::UIElement BuildTodoBarRow(Windows::Data::Json::JsonObject const& task);

        /* Signatures (MainWindow.Signatures.cpp) */
        void RefreshSignaturesView();
        void RebuildSignatureLists();
        void OpenSignatureEditor(Windows::Data::Json::JsonObject const& signature);
        fire_and_forget EnsureSignatureEditorAsync();
        fire_and_forget SwitchSignatureFormatAsync(bool html);
        void SetSignatureEditorHtml(hstring const& html);
        Windows::Foundation::IAsyncOperation<hstring> SignatureEditorHtmlAsync();
        fire_and_forget ExecuteSignatureCommandAsync(hstring command, hstring argument);
        fire_and_forget ConfirmDeleteSignatureAsync(hstring uid, hstring name);

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
        double m_reader_zoom{ 1.0 };
        bool m_view_plain_text{ false };
        bool m_dialog_open{ false };
        bool m_startup_synced{ false };
        hstring m_update_tag;	/* version plus récente disponible (vide : aucune connue) */
        hstring m_update_page;
        /* fenêtres indépendantes de lecture ouvertes par UID */
        std::map<std::wstring, winrt::EvolutionWin::MessageWindow> m_open_message_windows;
        /* fenêtres de rédaction ouvertes */
        std::vector<winrt::EvolutionWin::ComposeWindow> m_open_compose_windows;

        /* Bandeau d'information (vues Courrier et Calendrier) : avis identifiés,
         * le dernier est affiché ; voir ShowBanner */
        struct Banner
        {
            std::wstring id;
            hstring title;
            hstring message;
            Microsoft::UI::Xaml::Controls::InfoBarSeverity severity;
            hstring action_label;
            std::function<void()> action;
        };
        std::vector<Banner> m_banners;
        hstring m_view_tag{ L"mail" };
        void ShowBanner(std::wstring const& id, hstring const& title, hstring const& message,
            Microsoft::UI::Xaml::Controls::InfoBarSeverity severity,
            hstring const& action_label, std::function<void()> action);
        void ClearBanner(std::wstring const& id);
        void RefreshBanner();
        void UpdateRibbon();
        /* tri de la liste des messages (mémorisé par dossier) */
        hstring m_sort_key{ L"date" };
        bool m_sort_descending{ true };
        void LoadFolderSort();
        void SaveFolderSort();
        bool m_conversations_enabled{ false };
        void LoadFolderConversations();
        void SaveFolderConversations(bool enabled);
        void UpdateSortHeader();
        void SortMessages(std::vector<Windows::Data::Json::JsonObject>& messages);
        void RebuildQuickAccess();
        /* mode confidentiel des notifications (préférence "notifications.confidential") */
        bool m_updating_confidential{ false };
        void SyncConfidentialControls();
        bool m_compact_density{ false };
        /* position du volet de lecture : "right" ou "bottom" (layout.reading-pane) */
        void ApplyReadingPanePosition(hstring const& position);
        void UpdateTitleBarPassthrough();
        EvolutionWin::FolderItem CurrentFolderItem();
        hstring GetFolderPaneState();
        void ApplyFolderPaneState(hstring const& state);
        void ApplyFolderPaneVisibility(bool visible);
        int64_t m_selected_day_start{ 0 };	/* jour de référence (0 : aujourd'hui) */
        hstring m_calendar_period{ L"week" };	/* "day", "week" ou "month" (calendar.period) */
        bool m_updating_period{ false };
        void SetCalendarPeriod(hstring const& period);
        fire_and_forget ShowEventDialogAsync(Windows::Data::Json::JsonObject event);
        /* grille du calendrier (MainWindow.Calendar.cpp) */
        void OpenCalendarEvent(hstring const& calendar_uid, hstring const& uid);
        void RenderCalendarGrid(Windows::Data::Json::JsonArray const& events, int64_t start_unix, int days);
        void RenderMonthGrid(Windows::Data::Json::JsonArray const& events, int64_t start_unix, int month);
        int64_t m_grid_start{ 0 };
        int m_grid_days{ 7 };
        int m_grid_month{ 0 };
        /* calendriers affichés (calendar.visible) et actif (calendar.active) */
        Windows::Data::Json::JsonArray m_calendar_sources{ nullptr };
        void LoadCalendarSources();
        fire_and_forget ShowAddCalendarDialogAsync();
        /* carnets d'adresses affichés (contacts.visible) et actif (contacts.active) */
        Windows::Data::Json::JsonArray m_contact_books{ nullptr };
        void LoadContactBooks();
        void RebuildContactBooksPanel();
        std::vector<hstring> VisibleBookUids();
        hstring ActiveBookUid();
        void RebuildCalendarSourcesPanel();
        std::vector<hstring> VisibleCalendarUids();
        hstring ActiveCalendarUid();
        void ShiftCalendarPeriod(int direction);
        /* vue Tâches : listes de tâches [{"uid", "name", "kind"}], toutes les
           tâches chargées (avant filtres) et liste affichée ("" = toutes) */
        Windows::Data::Json::JsonArray m_task_lists{ nullptr };
        Windows::Data::Json::JsonArray m_all_tasks{ nullptr };
        hstring m_task_list_uid;
        hstring m_task_search_query;
        /* préférences de la vue relues ; contrôles mis à jour par le code */
        bool m_task_view_restored{ false };
        bool m_updating_task_view{ false };
        uint64_t m_task_request{ 0 };
        /* Volet latéral des tâches (To-Do bar) */
        bool m_todobar_configured_visible{ false };
        double m_todobar_width{ 300.0 };
        bool m_todobar_dragging{ false };
        double m_todobar_drag_start_x{ 0.0 };
        double m_todobar_drag_start_width{ 0.0 };
        /* Document du lecteur : chaque affichage reçoit une adresse unique
         * (http://reader.evolution.invalid/…) servie par WebResourceRequested,
         * ce qui identifie sans ambiguïté le document chargé ; le contenu
         * distant n'est autorisé qu'une fois CE document chargé (ContentLoading). */
        hstring m_reader_uri;
        hstring m_reader_html;
        uint64_t m_reader_generation{ 0 };
        uint64_t m_reader_navigation_id{ 0 };
        bool m_reader_navigation_known{ false };
        /* décision de confiance pour le document demandé */
        bool m_reader_pending_allow{ false };
        /* le document demandé est chargé (les requêtes viennent de lui) */
        bool m_reader_document_active{ false };
        /* contenu distant autorisé pour le document chargé */
        bool m_reader_remote_allowed{ false };
        /* requêtes distantes bloquées pour le document chargé */
        uint32_t m_reader_blocked_count{ 0 };
        /* RadioButtons des préférences mis à jour par le code */
        bool m_updating_privacy_view{ false };
        /* signatures (evo_core_list_signatures), uid de la signature éditée
           ("" pour une nouvelle), éditeur HTML prêt, contrôles mis à jour par le code */
        Windows::Data::Json::JsonArray m_signatures{ nullptr };
        hstring m_editing_signature_uid;
        bool m_signature_editor_started{ false };
        bool m_signature_editor_ready{ false };
        bool m_updating_signature_view{ false };
        /* contenu à afficher dès que l'éditeur HTML est prêt */
        hstring m_signature_pending_html;
        hstring m_account;
        hstring m_folder;
        /* only the latest message request is shown */
        uint64_t m_message_request{ 0 };
        uint64_t m_search_request{ 0 };
        /* the list was reloaded and its selection restored: keep the reader */
        bool m_reselecting{ false };
        /* glisser d'un séparateur en cours : colonne redimensionnée, abscisse
           du pointeur (dans MailView) et largeur de la colonne au départ */
        Microsoft::UI::Xaml::Controls::ColumnDefinition m_drag_column{ nullptr };
        double m_drag_origin{ 0 };
        double m_drag_start_width{ 0 };
        event_token m_activated_token;

        /* État des connexions réseau et de la barre de progression permanente */
        struct AccountConnectionState
        {
            winrt::hstring name;
            winrt::hstring status;
            winrt::hstring message;
        };
        std::map<std::wstring, AccountConnectionState> m_account_states;
        bool m_is_offline{ false };
        Microsoft::UI::Xaml::DispatcherTimer m_progress_hide_timer{ nullptr };

        /* Notifications reçues dans la barre d'état (Lot 4) */
        struct NotificationItemRecord
        {
            winrt::hstring id;
            winrt::hstring title;
            winrt::hstring body;
            winrt::hstring timestamp;
        };
        std::vector<NotificationItemRecord> m_notifications;
        uint64_t m_next_notification_id{ 1 };
        void AddNotificationItem(winrt::hstring const& title, winrt::hstring const& body);
        void DismissNotificationItem(winrt::hstring const& id);
        void ClearAllNotifications();
        void UpdateNotificationsUI();

        /* Gestion automatique du statut Lu / Non lu */
        Microsoft::UI::Xaml::DispatcherTimer m_mark_read_timer{ nullptr };
        winrt::hstring m_pending_read_uid;
        winrt::hstring m_previous_selected_uid;
        void ScheduleMarkAsRead(winrt::hstring const& uid);
        void CancelMarkAsReadTimer();
        void MarkMessageAsRead(winrt::hstring const& uid);

        /* Purge automatique de la Corbeille à la fermeture */
        void EmptyAllTrashFolders();

        /* Splash screen et progression barre des tâches */
        winrt::EvolutionWin::SplashWindow m_splash{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer m_splash_timer{ nullptr };
        winrt::com_ptr<ITaskbarList3> m_taskbar_list{ nullptr };

        /* État de la page Paramètres */
        bool m_settings_initialized{ false };
        bool m_updating_settings_view{ false };
        winrt::hstring m_current_settings_category{ L"accounts" };

        /* Synthèse vocale Windows (Lecture à voix haute) */
        Windows::Media::SpeechSynthesis::SpeechSynthesizer m_speech_synth{ nullptr };
        Windows::Media::Playback::MediaPlayer m_speech_player{ nullptr };
        bool m_is_speaking{ false };

        /* Aperçu sécurisé de pièces jointes */
        winrt::hstring m_preview_current_path;
        winrt::hstring m_preview_current_filename;
        uint32_t m_preview_current_index{ 0 };
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
