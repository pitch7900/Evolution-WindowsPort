#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <chrono>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace
{
    /* Segoe Fluent Icons glyphs */
    hstring FolderGlyph(hstring const& type)
    {
        if (type == L"inbox") return L"\xE8A8";
        if (type == L"outbox") return L"\xE8BD";
        if (type == L"sent") return L"\xE724";
        if (type == L"drafts") return L"\xE70F";
        if (type == L"trash") return L"\xE74D";
        if (type == L"junk") return L"\xE7BA";
        if (type == L"archive") return L"\xE7B8";
        return L"\xE8B7";
    }

    hstring FormatDate(double unix_time)
    {
        if (unix_time <= 0)
            return L"";

        auto time = winrt::clock::from_time_t(static_cast<time_t>(unix_time));
        Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"shortdate shorttime" };

        return formatter.Format(time);
    }

    /* display name of "Name <address>" */
    hstring SenderName(hstring const& from)
    {
        std::wstring_view view{ from };
        auto pos = view.find(L" <");

        if (pos != std::wstring_view::npos && pos > 0)
        {
            auto name = view.substr(0, pos);
            if (name.size() >= 2 && name.front() == L'"' && name.back() == L'"')
                name = name.substr(1, name.size() - 2);
            return hstring{ name };
        }

        return from;
    }
}

namespace winrt::EvolutionWin::implementation
{
    MainWindow::MainWindow()
    {
        /* named elements exist once the XAML is loaded: initialize on the
           first activation */
        m_activated_token = Activated([this](IInspectable const&, WindowActivatedEventArgs const&)
        {
            if (!m_initialized)
            {
                m_initialized = true;
                Initialize();
            }
        });
    }

    MainWindow::~MainWindow()
    {
        m_core.reset();
    }

    void MainWindow::Initialize()
    {
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1500, 950 });

        InitializeReaderAsync();

        m_core = std::make_shared<::EvolutionWin::CoreBridge>(
            DispatcherQueue(),
            [this](hstring const& event, hstring const& json) { OnCoreEvent(event, json); });

        Closed([this](IInspectable const&, WindowEventArgs const&)
        {
            /* stops the engine before the window goes away */
            m_core.reset();
        });
    }

    std::wstring MainWindow::NodeKey(hstring const& account, hstring const& folder)
    {
        return std::wstring{ account } + L'\x1F' + std::wstring{ folder };
    }

    void MainWindow::OnCoreEvent(hstring const& event, hstring const& json)
    {
        hstring message;
        JsonObject object;

        if (!JsonObject::TryParse(json, object))
            object = JsonObject{};

        message = object.GetNamedString(L"message", L"");

        if (event == L"ready")
        {
            StatusBar().IsOpen(false);
            LoadAccounts();
        }
        else if (event == L"folder-changed")
        {
            hstring account = object.GetNamedString(L"account", L"");
            hstring folder = object.GetNamedString(L"folder", L"");

            if (auto node = m_nodes.find(NodeKey(account, folder)); node != m_nodes.end())
            {
                if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                    item.Unread(static_cast<int32_t>(object.GetNamedNumber(L"unread", 0)));
            }

            /* new or removed messages in the folder on screen */
            if (account == m_account && folder == m_folder &&
                (object.GetNamedNumber(L"added", 0) > 0 || object.GetNamedNumber(L"removed", 0) > 0))
                LoadMessages(true);
        }
        else if (event == L"folders-updated")
        {
            hstring account = object.GetNamedString(L"account", L"");

            m_core->ListFolders(account, [this, account](hstring const& folders, hstring const& error)
            {
                if (error.empty())
                {
                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ account }] = parsed;
                    UpdateFolderCounts(account, parsed);
                }
            });
        }
        else if (event == L"sending")
        {
            ShowStatus(L"Envoi de « " + object.GetNamedString(L"subject", L"") + L" »…", InfoBarSeverity::Informational);
        }
        else if (event == L"sent")
        {
            ShowStatus(L"Message envoyé : « " + object.GetNamedString(L"subject", L"") + L" »", InfoBarSeverity::Success);
        }
        else if (event == L"error")
        {
            ShowStatus(L"Le moteur de messagerie n'a pas pu démarrer : " + message, InfoBarSeverity::Error);
        }
        else if (event == L"status")
        {
            ShowStatus(message, InfoBarSeverity::Informational);
        }
        else if (event == L"new-mail")
        {
            ShowNewMailNotification(object);
        }
        else if (event == L"auth-required")
        {
            PromptPasswordAsync(
                object.GetNamedString(L"source_uid", L""),
                object.GetNamedString(L"name", L""),
                object.GetNamedString(L"user", L""),
                object.GetNamedBoolean(L"rejected", false));
        }
        else if (event == L"tls-trust-required")
        {
            PromptTlsCertificateAsync(object);
        }
        else if (event == L"oauth2-required")
        {
            SignInOAuth2Async(object.GetNamedString(L"source_uid", L""), object.GetNamedString(L"name", L""));
        }
    }

    void MainWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        StatusBar().Message(message);
        StatusBar().Severity(severity);
        StatusBar().IsOpen(!message.empty());
    }

    void MainWindow::LoadAccounts()
    {
        m_core->ListAccounts([this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Comptes : " + error, InfoBarSeverity::Error);
                return;
            }

            FolderTree().RootNodes().Clear();
            m_nodes.clear();
            m_account_folders.clear();
            m_accounts = JsonArray::Parse(json);
            RefreshAccountsView();

            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                hstring uid = account.GetNamedString(L"uid");
                bool local = account.GetNamedBoolean(L"local", false);

                TreeViewNode node;
                node.Content(EvolutionWin::FolderItem(account.GetNamedString(L"name", uid),
                    local ? L"\xE7F8" : L"\xE715", 0, uid, L"", true));
                node.IsExpanded(true);
                FolderTree().RootNodes().Append(node);
                m_nodes[NodeKey(uid, L"")] = node;

                m_core->ListFolders(uid, [this, node, uid](hstring const& folders, hstring const& folders_error)
                {
                    if (!folders_error.empty())
                    {
                        ShowStatus(L"Dossiers : " + folders_error, InfoBarSeverity::Warning);
                        return;
                    }

                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ uid }] = parsed;
                    AddFolders(node, parsed, uid);
                });
            }
        });
    }

    void MainWindow::AddFolders(TreeViewNode const& parent, JsonArray const& folders, hstring const& account)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring type = folder.GetNamedString(L"type", L"");

            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(folder.GetNamedString(L"name", full_name), FolderGlyph(type),
                static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)), account,
                folder.GetNamedBoolean(L"selectable", true) ? full_name : hstring{}, false));
            node.IsExpanded(type == L"inbox");
            parent.Children().Append(node);
            m_nodes[NodeKey(account, full_name)] = node;

            AddFolders(node, folder.GetNamedArray(L"children", JsonArray{}), account);
        }
    }

    void MainWindow::UpdateFolderCounts(hstring const& account, JsonArray const& folders)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");

            if (auto node = m_nodes.find(NodeKey(account, full_name)); node != m_nodes.end())
            {
                if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                    item.Unread(static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)));
            }

            UpdateFolderCounts(account, folder.GetNamedArray(L"children", JsonArray{}));
        }
    }

    void MainWindow::OnNavigationSelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        if (args.IsSettingsSelected())
        {
            MailView().Visibility(Visibility::Collapsed);
            CalendarViewPane().Visibility(Visibility::Collapsed);
            ContactsViewPane().Visibility(Visibility::Collapsed);
            SettingsViewPane().Visibility(Visibility::Visible);
            RefreshAccountsView();
            return;
        }

        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        hstring tag = item ? unbox_value_or<hstring>(item.Tag(), L"") : hstring{};

        MailView().Visibility(tag == L"mail" ? Visibility::Visible : Visibility::Collapsed);
        CalendarViewPane().Visibility(tag == L"calendar" ? Visibility::Visible : Visibility::Collapsed);
        ContactsViewPane().Visibility(tag == L"contacts" ? Visibility::Visible : Visibility::Collapsed);
        SettingsViewPane().Visibility(Visibility::Collapsed);

        if (tag == L"calendar")
            LoadEvents();
        else if (tag == L"contacts")
            LoadContacts(ContactSearchBox().Text());
    }

    void MainWindow::OnFolderInvoked(TreeView const&, TreeViewItemInvokedEventArgs const& args)
    {
        auto node = args.InvokedItem().try_as<TreeViewNode>();
        auto item = node ? node.Content().try_as<EvolutionWin::FolderItem>() : nullptr;
        if (!item)
            return;

        hstring account = item.Account();
        hstring folder = item.Folder();

        /* accounts and non-selectable folders only expand */
        if (folder.empty())
        {
            node.IsExpanded(!node.IsExpanded());
            return;
        }

        m_account = account;
        m_folder = folder;
        ++m_search_request;

        FolderTitle().Text(item.Name());
        SearchBox().Text(L"");

        LoadMessages(false);
    }

    void MainWindow::LoadMessages(bool keep_selection)
    {
        hstring account = m_account;
        hstring folder = m_folder;
        hstring selected;

        if (account.empty() || folder.empty())
            return;

        if (keep_selection)
        {
            selected = SelectedMessageUid();
        }
        else
        {
            MessageList().Items().Clear();
            m_message_states.clear();
            ListProgress().IsActive(true);
        }

        m_core->ListMessages(account, folder, 500, [this, account, folder, selected](hstring const& json, hstring const& error)
        {
            /* another folder was chosen meanwhile */
            if (account != m_account || folder != m_folder)
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(L"Messages : " + error, InfoBarSeverity::Error);
                return;
            }

            m_all_messages = JsonArray::Parse(json);

            if (!SearchBox().Text().empty())
                FilterLocalMessages(SearchBox().Text());
            else
                ShowMessages(m_all_messages);

            /* the reader keeps its message: reselect it silently */
            if (!selected.empty())
            {
                if (auto grid = FindMessageItem(selected))
                {
                    m_reselecting = true;
                    MessageList().SelectedItem(grid);
                    m_reselecting = false;
                }
            }
        });
    }

    /* Construit et affiche les éléments de la liste de messages avec leurs
     * indicateurs (non lu, suivi, pièce jointe) et leur menu contextuel.
     * Paramètres :
     *   messages : tableau JSON des résumés de messages à afficher */
    void MainWindow::ShowMessages(JsonArray const& messages)
    {
        auto items = MessageList().Items();

        items.Clear();
        m_message_states.clear();

        for (auto const& value : messages)
        {
            auto message = value.GetObject();
            hstring uid = message.GetNamedString(L"uid");
            bool seen = message.GetNamedBoolean(L"seen", true);
            bool flagged = message.GetNamedBoolean(L"flagged", false);
            bool attachments = message.GetNamedBoolean(L"attachments", false);

            m_message_states[std::wstring{ uid }] = MessageState{ seen, flagged, attachments };

            Grid item;
            item.Padding(ThicknessHelper::FromLengths(8, 8, 8, 8));
            item.HorizontalAlignment(HorizontalAlignment::Stretch);
            /* Fond transparent explicite pour que le survol (PointerEntered/PointerExited)
             * soit détecté sur toute la surface de la ligne et pas uniquement sur le texte */
            item.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            item.Tag(box_value(uid));
            item.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition date_column;
            date_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(date_column);
            ColumnDefinition action_column;
            action_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(action_column);
            item.RowDefinitions().Append(RowDefinition{});
            item.RowDefinitions().Append(RowDefinition{});

            TextBlock from;
            from.Text(SenderName(message.GetNamedString(L"from", L"")));
            from.TextTrimming(TextTrimming::CharacterEllipsis);
            item.Children().Append(from);

            StackPanel meta;
            meta.Orientation(Orientation::Horizontal);
            meta.Spacing(6);
            meta.VerticalAlignment(VerticalAlignment::Center);
            meta.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
            Grid::SetColumn(meta, 1);

            FontIcon flag_icon;
            flag_icon.Glyph(L"\xE7C1");
            flag_icon.FontSize(12);
            flag_icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, 247, 99, 12 } });
            flag_icon.Visibility(flagged ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(flag_icon);

            FontIcon attachment_icon;
            attachment_icon.Glyph(L"\xE723");
            attachment_icon.FontSize(12);
            attachment_icon.Visibility(attachments ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(attachment_icon);

            TextBlock date;
            date.Text(FormatDate(message.GetNamedNumber(L"date", 0)));
            date.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            meta.Children().Append(date);

            item.Children().Append(meta);

            TextBlock subject;
            subject.Text(message.GetNamedString(L"subject", L""));
            subject.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetRow(subject, 1);
            Grid::SetColumnSpan(subject, 2);
            item.Children().Append(subject);

            /* Bouton corbeille de suppression rapide au survol (style Outlook) */
            Button delete_button;
            FontIcon trash_icon;
            trash_icon.Glyph(L"\xE74D");
            trash_icon.FontSize(14);
            delete_button.Content(trash_icon);
            delete_button.Width(32);
            delete_button.Height(32);
            delete_button.Padding(ThicknessHelper::FromUniformLength(0));
            delete_button.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
            delete_button.VerticalAlignment(VerticalAlignment::Center);
            delete_button.Visibility(Visibility::Collapsed);
            ToolTipService::SetToolTip(delete_button, box_value(L"Supprimer"));
            Grid::SetColumn(delete_button, 2);
            Grid::SetRow(delete_button, 0);
            Grid::SetRowSpan(delete_button, 2);
            delete_button.Click([this, uid](IInspectable const&, RoutedEventArgs const&)
            {
                DeleteMessagesByUids(TargetMessageUids(uid));
            });
            item.Children().Append(delete_button);

            item.PointerEntered([delete_button](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                delete_button.Visibility(Visibility::Visible);
            });
            item.PointerExited([delete_button](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                delete_button.Visibility(Visibility::Collapsed);
            });

            UpdateMessageItemVisual(item, seen, flagged);

            /* Menu contextuel par clic droit sur chaque message (gère un ou plusieurs messages sélectionnés) */
            MenuFlyout context_menu;
            context_menu.Opening([this, uid, context_menu](IInspectable const&, IInspectable const&)
            {
                auto menu_items = context_menu.Items();
                menu_items.Clear();

                auto target_uids = TargetMessageUids(uid);
                bool multi = target_uids.size() > 1;
                std::wstring count_suffix = multi ? L" (" + std::to_wstring(target_uids.size()) + L" messages)" : L"";

                MenuFlyoutItem reply_item;
                reply_item.Text(multi
                    ? hstring{ L"Répondre (1er mail + " + std::to_wstring(target_uids.size() - 1) + L" en PJ)" }
                    : L"Répondre");
                reply_item.Icon(FontIcon{});
                reply_item.Icon().as<FontIcon>().Glyph(L"\xE97A");
                reply_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Sender, target_uids);
                });
                menu_items.Append(reply_item);

                MenuFlyoutItem reply_all_item;
                reply_all_item.Text(multi
                    ? hstring{ L"Répondre à tous (1er mail + " + std::to_wstring(target_uids.size() - 1) + L" en PJ)" }
                    : L"Répondre à tous");
                reply_all_item.Icon(FontIcon{});
                reply_all_item.Icon().as<FontIcon>().Glyph(L"\xEE35");
                reply_all_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::All, target_uids);
                });
                menu_items.Append(reply_all_item);

                MenuFlyoutItem forward_item;
                forward_item.Text(multi
                    ? hstring{ L"Transférer en pièces jointes" + count_suffix }
                    : L"Transférer");
                forward_item.Icon(FontIcon{});
                forward_item.Icon().as<FontIcon>().Glyph(L"\xE72D");
                forward_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Forward, target_uids);
                });
                menu_items.Append(forward_item);

                if (!multi)
                {
                    MenuFlyoutItem forward_att_item;
                    forward_att_item.Text(L"Transférer en pièce jointe (.eml)");
                    forward_att_item.Icon(FontIcon{});
                    forward_att_item.Icon().as<FontIcon>().Glyph(L"\xE723");
                    forward_att_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                    {
                        ForwardMessagesAsAttachments(target_uids);
                    });
                    menu_items.Append(forward_att_item);
                }

                menu_items.Append(MenuFlyoutSeparator{});

                auto state_it = m_message_states.find(std::wstring{ target_uids.front() });
                bool is_seen = state_it != m_message_states.end() ? state_it->second.seen : true;
                bool is_flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;

                MenuFlyoutItem read_item;
                read_item.Text(hstring{ (is_seen ? L"Marquer comme non lu" : L"Marquer comme lu") + count_suffix });
                read_item.Icon(FontIcon{});
                read_item.Icon().as<FontIcon>().Glyph(is_seen ? L"\xE715" : L"\xE8C3");
                read_item.Click([this, target_uids, is_seen](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"seen", !is_seen);
                });
                menu_items.Append(read_item);

                MenuFlyoutItem flag_item;
                flag_item.Text(hstring{ (is_flagged ? L"Retirer le drapeau" : L"Ajouter un drapeau") + count_suffix });
                flag_item.Icon(FontIcon{});
                flag_item.Icon().as<FontIcon>().Glyph(L"\xE7C1");
                flag_item.Click([this, target_uids, is_flagged](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"flagged", !is_flagged);
                });
                menu_items.Append(flag_item);

                MenuFlyoutSubItem move_sub;
                move_sub.Text(hstring{ L"Déplacer vers" + count_suffix });
                move_sub.Icon(FontIcon{});
                move_sub.Icon().as<FontIcon>().Glyph(L"\xE8DE");
                PopulateMoveMenuItems(move_sub.Items(), target_uids);
                menu_items.Append(move_sub);

                menu_items.Append(MenuFlyoutSeparator{});

                MenuFlyoutItem delete_item;
                delete_item.Text(hstring{ L"Supprimer" + count_suffix });
                delete_item.Icon(FontIcon{});
                delete_item.Icon().as<FontIcon>().Glyph(L"\xE74D");
                delete_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    DeleteMessagesByUids(target_uids);
                });
                menu_items.Append(delete_item);
            });
            item.ContextFlyout(context_menu);

            items.Append(item);
        }

        if (messages.Size() == 0)
        {
            if (!SearchBox().Text().empty())
                ShowStatus(L"Aucun message ne correspond à « " + SearchBox().Text() + L" ».", InfoBarSeverity::Informational);
            else
                ShowStatus(L"Ce dossier est vide.", InfoBarSeverity::Informational);
        }
    }

    void MainWindow::OnMessageSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_reselecting)
            return;

        auto item = MessageList().SelectedItem().try_as<Grid>();
        if (!item)
        {
            ClearReader();
            return;
        }

        hstring uid = unbox_value<hstring>(item.Tag());
        if (uid == m_current_message_uid && m_current_message)
        {
            UpdateActionButtons(uid);
            return;
        }

        uint64_t request = ++m_message_request;

        auto state_it = m_message_states.find(std::wstring{ uid });
        bool flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;
        m_message_states[std::wstring{ uid }].seen = true;
        UpdateMessageItemVisual(item, true, flagged);
        UpdateActionButtons(uid);

        m_core->FetchMessage(m_account, m_folder, uid, [this, request, uid](hstring const& json, hstring const& error)
        {
            if (request != m_message_request)
                return;

            if (!error.empty())
            {
                ShowStatus(L"Message : " + error, InfoBarSeverity::Error);
                return;
            }

            auto message = JsonObject::Parse(json);
            m_current_message = message;
            m_current_message_uid = uid;
            hstring to = message.GetNamedString(L"to", L"");
            hstring cc = message.GetNamedString(L"cc", L"");

            MessageSubject().Text(message.GetNamedString(L"subject", L""));
            MessageFrom().Text(message.GetNamedString(L"from", L""));
            MessageTo().Text(L"À : " + to + (cc.empty() ? hstring{} : L"   Cc : " + cc));
            MessageDate().Text(FormatDate(message.GetNamedNumber(L"date", 0)));
            UpdateSecurityBanner(message.GetNamedObject(L"security", JsonObject{}));
            PopulateAttachments(message.GetNamedArray(L"attachments", JsonArray{}));
            MessageHeader().Visibility(Visibility::Visible);
            ReaderPlaceholder().Visibility(Visibility::Collapsed);
            ReaderContainer().Visibility(Visibility::Visible);
            UpdateActionButtons(uid);

            ShowInReader(message.GetNamedString(L"html", L""));
        });
    }

    fire_and_forget MainWindow::InitializeReaderAsync()
    {
        auto strong = get_strong();

        /* writable data folder: next to the executable is not (Program Files) */
        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        /* messages are designed for a light background (like Outlook's
           default reading pane), whatever the Windows theme */
        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        co_await Reader().EnsureCoreWebView2Async();

        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = Reader().CoreWebView2();
        /* Force le schéma de couleurs clair dans Chromium pour éviter que le
         * mode sombre de Windows 11 n'active un fond noir sous un texte noir */
        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);

        auto settings = core.Settings();

        /* messages are documents: no scripts, no remote content (privacy,
           as in Evolution), links open in the default browser */
        settings.IsScriptEnabled(false);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(true);

        core.AddWebResourceRequestedFilter(L"*", Microsoft::Web::WebView2::Core::CoreWebView2WebResourceContext::All);
        core.WebResourceRequested([](auto const& sender, Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args)
        {
            std::wstring_view uri{ args.Request().Uri() };

            if (uri.rfind(L"http:", 0) == 0 || uri.rfind(L"https:", 0) == 0)
                args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 403, L"Blocked", L""));
        });

        core.NavigationStarting([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring_view uri{ args.Uri() };

            if (args.IsUserInitiated() && uri.rfind(L"data:", 0) != 0 && uri.rfind(L"about:", 0) != 0)
            {
                args.Cancel(true);
                Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
            }
        });

        core.NewWindowRequested([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& args)
        {
            args.Handled(true);
            Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
        });

        m_reader_ready = true;

        if (!m_pending_html.empty())
        {
            ShowInReader(m_pending_html);
            m_pending_html = L"";
        }
    }

    void MainWindow::ShowInReader(hstring const& html)
    {
        if (!m_reader_ready)
        {
            m_pending_html = html;
            return;
        }

        try
        {
            Reader().NavigateToString(html);
        }
        catch (hresult_error const& e)
        {
            /* NavigateToString is limited to 2 MB */
            ShowStatus(L"Ce message est trop volumineux pour l'aperçu : " + e.message(), InfoBarSeverity::Warning);
        }
    }

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowStatus(L"Relève du courrier…", InfoBarSeverity::Informational);
        m_core->Refresh();
    }

    void MainWindow::OnNewMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        ::EvolutionWin::ComposeData data;

        data.account = m_account;
        OpenCompose(data);
    }

    void MainWindow::OpenCompose(::EvolutionWin::ComposeData const& data)
    {
        if (!m_accounts)
        {
            ShowStatus(L"Les comptes ne sont pas encore chargés.", InfoBarSeverity::Warning);
            return;
        }

        auto window = make<ComposeWindow>();
        get_self<ComposeWindow>(window)->Setup(m_core, m_accounts, data);
        window.Activate();
    }

    void MainWindow::OnReplyClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Sender);
    }

    void MainWindow::OnReplyAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::All);
    }

    void MainWindow::OnForwardClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Forward);
    }

    namespace
    {
        bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
        {
            return text.size() >= prefix.size() &&
                CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        }

        /* Échappe les caractères spéciaux HTML d'une chaîne brute. */
        std::wstring EscapeHtml(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size() + 32);

            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'&': out += L"&amp;"; break;
                case L'<': out += L"&lt;"; break;
                case L'>': out += L"&gt;"; break;
                case L'"': out += L"&quot;"; break;
                case L'\r': break;
                case L'\n': out += L"<br>"; break;
                default: out += c; break;
                }
            }

            return out;
        }

        /* "> " before each line */
        std::wstring Quote(std::wstring_view text)
        {
            std::wstring quoted = L"> ";

            for (auto c : text)
            {
                if (c == L'\r')
                    continue;
                quoted += c;
                if (c == L'\n')
                    quoted += L"> ";
            }

            return quoted;
        }

        /* comma-separated addresses without those containing own_address */
        std::wstring WithoutAddress(std::wstring_view addresses, std::wstring_view own_address)
        {
            std::wstring result;
            size_t start = 0;

            while (start <= addresses.size())
            {
                size_t end = addresses.find(L',', start);
                if (end == std::wstring_view::npos)
                    end = addresses.size();

                std::wstring part{ addresses.substr(start, end - start) };
                part.erase(0, part.find_first_not_of(L' '));

                std::wstring lower_part = part, lower_own{ own_address };
                CharLowerBuffW(lower_part.data(), static_cast<DWORD>(lower_part.size()));
                CharLowerBuffW(lower_own.data(), static_cast<DWORD>(lower_own.size()));

                if (!part.empty() && (lower_own.empty() || lower_part.find(lower_own) == std::wstring::npos))
                {
                    if (!result.empty())
                        result += L", ";
                    result += part;
                }

                start = end + 1;
            }

            return result;
        }
    }

    /* Construit la structure ComposeData (destinataires, objet, corps cité) à
     * partir d'un objet JSON de message pour une réponse ou un transfert simple.
     * Paramètres :
     *   kind    : type d'action (Sender, All ou Forward)
     *   message : objet JSON complet du message source
     * Retourne : la structure ComposeData prête pour OpenCompose. */
    ::EvolutionWin::ComposeData MainWindow::BuildReplyComposeData(ReplyKind kind, JsonObject const& message)
    {
        hstring subject = message.GetNamedString(L"subject", L"");
        hstring from = message.GetNamedString(L"from", L"");
        hstring to = message.GetNamedString(L"to", L"");
        hstring cc = message.GetNamedString(L"cc", L"");
        hstring date = FormatDate(message.GetNamedNumber(L"date", 0));
        hstring text = message.GetNamedString(L"text", L"");
        hstring message_id = message.GetNamedString(L"message_id", L"");

        ::EvolutionWin::ComposeData data;
        data.account = m_account;

        if (kind == ReplyKind::Forward)
        {
            data.subject = StartsWithNoCase(subject, L"TR:") || StartsWithNoCase(subject, L"Fwd:") ? subject : L"TR: " + subject;
            data.body = L"\r\r-------- Message transféré --------\rDe : " + from + L"\rDate : " + date +
                L"\rObjet : " + subject + L"\rÀ : " + to + (cc.empty() ? hstring{} : L"\rCc : " + cc) +
                L"\r\r" + text;
            data.html_body = hstring{
                L"<div><br></div><hr style=\"border:none;border-top:1px solid #ccc;\">"
                L"<div style=\"color:#444;font-size:10pt;\">"
                L"<b>-------- Message transféré --------</b><br>"
                L"<b>De :</b> " + EscapeHtml(from) + L"<br>"
                L"<b>Date :</b> " + EscapeHtml(date) + L"<br>"
                L"<b>Objet :</b> " + EscapeHtml(subject) + L"<br>"
                L"<b>À :</b> " + EscapeHtml(to) +
                (cc.empty() ? std::wstring{} : L"<br><b>Cc :</b> " + EscapeHtml(cc)) +
                L"</div><br><div>" + EscapeHtml(text) + L"</div>" };
        }
        else
        {
            hstring reply_to = message.GetNamedString(L"reply_to", L"");

            data.to = reply_to.empty() ? from : reply_to;
            data.subject = StartsWithNoCase(subject, L"Re:") ? subject : L"Re: " + subject;
            data.in_reply_to = message_id;

            hstring references = message.GetNamedString(L"references", L"");
            data.references = references.empty() ? message_id : references + L" " + message_id;

            if (kind == ReplyKind::All)
            {
                hstring own_address;
                for (auto const& value : m_accounts)
                {
                    auto account = value.GetObject();
                    if (account.GetNamedString(L"uid") == m_account)
                        own_address = account.GetNamedString(L"address", L"");
                }

                std::wstring others = std::wstring{ to } + (cc.empty() ? L"" : L", " + std::wstring{ cc });
                data.cc = hstring{ WithoutAddress(others, own_address) };
            }

            /* the text box uses \r line ends */
            std::wstring quoted = Quote(text);
            for (auto& c : quoted)
                if (c == L'\n')
                    c = L'\r';

            data.body = L"\r\rLe " + date + L", " + from + L" a écrit :\r" + hstring{ quoted };
            data.html_body = hstring{
                L"<div><br></div><div>Le " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" a écrit :</div>"
                L"<blockquote>" + EscapeHtml(text) + L"</blockquote>" };
        }

        return data;
    }

    /* Transfère un ou plusieurs messages en les exportant sous forme de pièces
     * jointes .eml attachées à un nouveau courriel.
     * Paramètres :
     *   uids : identifiants des messages à joindre en .eml */
    void MainWindow::ForwardMessagesAsAttachments(std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        hstring first_subject;
        if (m_current_message && m_current_message_uid == uids.front())
        {
            first_subject = m_current_message.GetNamedString(L"subject", L"");
        }
        else if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                if (obj.GetNamedString(L"uid", L"") == uids.front())
                {
                    first_subject = obj.GetNamedString(L"subject", L"");
                    break;
                }
            }
        }

        JsonObject req;
        req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uid_array;
        for (auto const& u : uids)
            uid_array.Append(JsonValue::CreateStringValue(u));
        req.SetNamedValue(L"uids", uid_array);

        hstring account = m_account;
        ShowStatus(L"Préparation des courriels en pièces jointes…", InfoBarSeverity::Informational);

        m_core->ExportMessagesEml(req.Stringify(),
            [this, account, first_subject](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Transfert : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonArray paths;
            if (!JsonArray::TryParse(json, paths))
                return;

            ::EvolutionWin::ComposeData data;
            data.account = account;
            if (!first_subject.empty())
            {
                data.subject = StartsWithNoCase(first_subject, L"TR:") || StartsWithNoCase(first_subject, L"Fwd:")
                    ? first_subject
                    : L"TR: " + first_subject;
            }
            else
            {
                data.subject = L"TR: Messages transférés";
            }

            for (auto const& val : paths)
                data.attachments.push_back(val.GetString());

            StatusBar().IsOpen(false);
            OpenCompose(data);
        });
    }

    /* Répond ou transfère un ou plusieurs messages :
     * - Pour un transfert (Forward), joint tous les courriels sélectionnés en
     *   pièces jointes (.eml) à un nouveau message ;
     * - Pour une réponse (Sender / All), répond au premier courriel sélectionné
     *   et joint tous les autres courriels sélectionnés en pièces jointes (.eml).
     * Paramètres :
     *   kind : type d'action (Sender, All ou Forward)
     *   uids : liste ordonnée des identifiants des messages sélectionnés */
    void MainWindow::ReplyToMessages(ReplyKind kind, std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        /* Pour le transfert, on joint le ou les mails sélectionnés en pièces jointes (.eml) */
        if (kind == ReplyKind::Forward)
        {
            ForwardMessagesAsAttachments(uids);
            return;
        }

        hstring first_uid = uids.front();
        std::vector<hstring> extra_uids(uids.begin() + 1, uids.end());

        auto proceed_with_first = [this, kind, extra_uids](JsonObject const& first_msg)
        {
            auto data = BuildReplyComposeData(kind, first_msg);

            if (extra_uids.empty())
            {
                OpenCompose(data);
                return;
            }

            JsonObject req;
            req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
            req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
            JsonArray uid_array;
            for (auto const& u : extra_uids)
                uid_array.Append(JsonValue::CreateStringValue(u));
            req.SetNamedValue(L"uids", uid_array);

            ShowStatus(L"Ajout des autres courriels sélectionnés en pièces jointes…", InfoBarSeverity::Informational);

            m_core->ExportMessagesEml(req.Stringify(),
                [this, data](hstring const& json, hstring const& error) mutable
            {
                if (!error.empty())
                {
                    ShowStatus(L"Pièces jointes : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonArray paths;
                if (JsonArray::TryParse(json, paths))
                {
                    for (auto const& val : paths)
                        data.attachments.push_back(val.GetString());
                }

                StatusBar().IsOpen(false);
                OpenCompose(data);
            });
        };

        if (m_current_message && m_current_message_uid == first_uid)
        {
            proceed_with_first(m_current_message);
        }
        else
        {
            m_core->FetchMessage(m_account, m_folder, first_uid,
                [this, proceed_with_first](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Message : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonObject msg;
                if (JsonObject::TryParse(json, msg))
                    proceed_with_first(msg);
            });
        }
    }

    void MainWindow::Reply(ReplyKind kind)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty() && !m_current_message_uid.empty())
            uids.push_back(m_current_message_uid);

        ReplyToMessages(kind, uids);
    }

    /* Retourne l'UID du premier message sélectionné dans la liste, ou ""
     * s'il n'y a aucune sélection. */
    hstring MainWindow::SelectedMessageUid()
    {
        if (auto item = MessageList().SelectedItem().try_as<Grid>())
            return unbox_value_or<hstring>(item.Tag(), L"");
        return L"";
    }

    /* Retourne la liste ordonnée des UID de tous les messages actuellement
     * sélectionnés dans MessageList. */
    std::vector<hstring> MainWindow::SelectedMessageUids()
    {
        std::vector<hstring> result;
        for (auto const& obj : MessageList().SelectedItems())
        {
            if (auto grid = obj.try_as<Grid>())
            {
                hstring u = unbox_value_or<hstring>(grid.Tag(), L"");
                if (!u.empty())
                    result.push_back(u);
            }
        }
        return result;
    }

    /* Détermine la liste des UID ciblés par une action (barre d'outils ou menu
     * contextuel) : si context_uid fait partie de la sélection courante (ou est
     * vide), retourne toute la sélection ; sinon retourne uniquement context_uid. */
    std::vector<hstring> MainWindow::TargetMessageUids(hstring const& context_uid)
    {
        auto selected = SelectedMessageUids();
        if (context_uid.empty())
            return selected;

        for (auto const& u : selected)
        {
            if (u == context_uid)
                return selected;
        }
        return { context_uid };
    }

    /* Recherche dans la liste affichée l'élément Grid correspondant à un UID.
     * Paramètres :
     *   uid : identifiant du message recherché
     * Retourne : l'élément Grid trouvé, ou nullptr. */
    Grid MainWindow::FindMessageItem(hstring const& uid)
    {
        for (auto const& entry : MessageList().Items())
        {
            auto grid = entry.try_as<Grid>();
            if (grid && unbox_value_or<hstring>(grid.Tag(), L"") == uid)
                return grid;
        }
        return nullptr;
    }

    /* Met à jour l'apparence visuelle d'un élément de la liste de messages
     * (graisse, couleur d'accentuation et icône de suivi).
     * Paramètres :
     *   item    : élément Grid du message dans MessageList
     *   seen    : true si le message est lu, false s'il est non lu
     *   flagged : true si le message porte un indicateur de suivi */
    void MainWindow::UpdateMessageItemVisual(Grid const& item, bool seen, bool flagged)
    {
        if (!item)
            return;

        auto weight = seen ? Microsoft::UI::Text::FontWeights::Normal() : Microsoft::UI::Text::FontWeights::SemiBold();

        for (auto const& child : item.Children())
        {
            if (auto text = child.try_as<TextBlock>(); text && Grid::GetColumn(text) == 0)
            {
                text.FontWeight(weight);
                if (!seen && Grid::GetRow(text) == 1)
                {
                    bool dark = (item.ActualTheme() == ElementTheme::Dark);
                    text.Foreground(SolidColorBrush{ dark
                        ? Windows::UI::Color{ 255, 96, 205, 255 }
                        : Windows::UI::Color{ 255, 0, 95, 184 } });
                }
                else
                {
                    text.ClearValue(TextBlock::ForegroundProperty());
                }
            }
            else if (auto meta = child.try_as<StackPanel>(); meta && Grid::GetColumn(meta) == 1 && meta.Children().Size() > 0)
            {
                if (auto icon = meta.Children().GetAt(0).try_as<FontIcon>())
                    icon.Visibility(flagged ? Visibility::Visible : Visibility::Collapsed);
            }
        }
    }

    /* Met à jour le libellé et l'icône des boutons d'action du volet de lecture
     * selon l'état du message sélectionné.
     * Paramètres :
     *   uid : identifiant du message affiché */
    void MainWindow::UpdateActionButtons(hstring const& uid)
    {
        auto it = m_message_states.find(std::wstring{ uid });
        bool seen = it != m_message_states.end() ? it->second.seen : true;
        bool flagged = it != m_message_states.end() ? it->second.flagged : false;

        ToggleReadButton().Label(seen ? L"Non lu" : L"Lu");
        ToggleReadIcon().Glyph(seen ? L"\xE715" : L"\xE8C3");

        ToggleFlagButton().Label(flagged ? L"Retirer suivi" : L"Suivi");
    }

    /* Modifie un indicateur ("seen" ou "flagged") sur un message côté interface
     * et côté moteur evo-core.
     * Paramètres :
     *   uid   : identifiant du message
     *   flag  : nom de l'indicateur ("seen" ou "flagged")
     *   value : nouvelle valeur booléenne de l'indicateur */
    void MainWindow::SetMessageFlag(hstring const& uid, hstring const& flag, bool value)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty())
            return;

        auto& state = m_message_states[std::wstring{ uid }];
        if (flag == L"seen")
            state.seen = value;
        else if (flag == L"flagged")
            state.flagged = value;

        if (auto item = FindMessageItem(uid))
            UpdateMessageItemVisual(item, state.seen, state.flagged);

        if (uid == SelectedMessageUid())
            UpdateActionButtons(uid);

        /* Met aussi à jour la copie en mémoire pour que la recherche locale reste cohérente */
        if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                if (obj.GetNamedString(L"uid", L"") == uid)
                {
                    obj.SetNamedValue(flag, JsonValue::CreateBooleanValue(value));
                    break;
                }
            }
        }

        hstring account = m_account;
        hstring folder = m_folder;
        m_core->SetMessageFlags(account, folder, uid, flag, value,
            [this, account, folder, uid](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Indicateur : " + error, InfoBarSeverity::Error);
                return;
            }

            if (account != m_account || folder != m_folder)
                return;

            JsonObject result;
            if (JsonObject::TryParse(json, result))
            {
                auto& current = m_message_states[std::wstring{ uid }];
                current.seen = result.GetNamedBoolean(L"seen", current.seen);
                current.flagged = result.GetNamedBoolean(L"flagged", current.flagged);
                if (auto item = FindMessageItem(uid))
                    UpdateMessageItemVisual(item, current.seen, current.flagged);
                if (uid == SelectedMessageUid())
                    UpdateActionButtons(uid);
            }
        });
    }

    /* Modifie un indicateur ("seen" ou "flagged") sur une liste de messages.
     * Paramètres :
     *   uids  : identifiants des messages
     *   flag  : nom de l'indicateur ("seen" ou "flagged")
     *   value : nouvelle valeur booléenne */
    void MainWindow::SetMessagesFlag(std::vector<hstring> const& uids, hstring const& flag, bool value)
    {
        for (auto const& u : uids)
            SetMessageFlag(u, flag, value);
    }

    /* Vide le volet de lecture lorsqu'aucun message n'est sélectionné. */
    void MainWindow::ClearReader()
    {
        ++m_message_request;
        m_current_message = nullptr;
        m_current_message_uid = L"";
        SecurityBar().IsOpen(false);
        AttachmentBar().Children().Clear();
        AttachmentBar().Visibility(Visibility::Collapsed);
        MessageHeader().Visibility(Visibility::Collapsed);
        ReaderContainer().Visibility(Visibility::Collapsed);
        ReaderPlaceholder().Visibility(Visibility::Visible);
        ShowInReader(L"<!DOCTYPE html><html><body style=\"background:#fff;color:#1a1a1a;\"></body></html>");
    }

    /* Retire un message de la liste affichée et de la liste en mémoire, puis
     * sélectionne le message voisin s'il en reste.
     * Paramètres :
     *   uid : identifiant du message à retirer */
    void MainWindow::RemoveMessageFromList(hstring const& uid)
    {
        auto items = MessageList().Items();
        uint32_t removed_index = 0;
        bool found = false;
        bool was_selected = (SelectedMessageUid() == uid);

        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            auto grid = items.GetAt(i).try_as<Grid>();
            if (grid && unbox_value_or<hstring>(grid.Tag(), L"") == uid)
            {
                removed_index = i;
                found = true;
                items.RemoveAt(i);
                break;
            }
        }

        m_message_states.erase(std::wstring{ uid });

        if (m_all_messages)
        {
            for (uint32_t i = 0; i < m_all_messages.Size(); ++i)
            {
                if (m_all_messages.GetAt(i).GetObject().GetNamedString(L"uid", L"") == uid)
                {
                    m_all_messages.RemoveAt(i);
                    break;
                }
            }
        }

        if (was_selected && MessageList().SelectedItems().Size() == 0)
        {
            if (found && items.Size() > 0)
            {
                uint32_t next_index = removed_index < items.Size() ? removed_index : items.Size() - 1;
                MessageList().SelectedIndex(static_cast<int32_t>(next_index));
            }
            else
            {
                ClearReader();
            }
        }
    }

    /* Supprime un message identifié par son UID (déplacement vers la corbeille
     * ou suppression définitive).
     * Paramètres :
     *   uid : identifiant du message à supprimer */
    void MainWindow::DeleteMessageByUid(hstring const& uid)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty())
            return;

        hstring account = m_account;
        hstring folder = m_folder;

        RemoveMessageFromList(uid);

        m_core->DeleteMessage(account, folder, uid, [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression : " + error, InfoBarSeverity::Error);
                LoadMessages(false);
            }
        });
    }

    /* Supprime un ensemble de messages identifiés par leurs UID.
     * Paramètres :
     *   uids : liste des identifiants des messages à supprimer */
    void MainWindow::DeleteMessagesByUids(std::vector<hstring> const& uids)
    {
        for (auto const& u : uids)
            DeleteMessageByUid(u);
    }

    /* Déplace un message identifié par son UID vers un autre dossier.
     * Paramètres :
     *   uid          : identifiant du message à déplacer
     *   dest_account : identifiant du compte de destination
     *   dest_folder  : chemin complet du dossier de destination */
    void MainWindow::MoveMessageByUid(hstring const& uid, hstring const& dest_account, hstring const& dest_folder)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty() || dest_account.empty() || dest_folder.empty())
            return;

        if (dest_account == m_account && dest_folder == m_folder)
            return;

        hstring src_account = m_account;
        hstring src_folder = m_folder;

        RemoveMessageFromList(uid);

        m_core->MoveMessage(src_account, src_folder, uid, dest_account, dest_folder,
            [this, dest_folder](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Déplacement : " + error, InfoBarSeverity::Error);
                LoadMessages(false);
                return;
            }

            ShowStatus(L"Message déplacé vers « " + dest_folder + L" ».", InfoBarSeverity::Informational);
        });
    }

    /* Déplace un ensemble de messages identifiés par leurs UID vers un autre dossier.
     * Paramètres :
     *   uids         : liste des identifiants des messages à déplacer
     *   dest_account : identifiant du compte de destination
     *   dest_folder  : chemin complet du dossier de destination */
    void MainWindow::MoveMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder)
    {
        for (auto const& u : uids)
            MoveMessageByUid(u, dest_account, dest_folder);
    }

    /* Ajoute récursivement les dossiers d'un compte dans un menu « Déplacer vers ».
     * Paramètres :
     *   items   : collection d'éléments du menu à remplir
     *   folders : tableau JSON des dossiers à parcourir
     *   account : identifiant du compte propriétaire des dossiers
     *   prefix  : préfixe d'affichage pour les sous-dossiers
     *   uids    : identifiants des messages à déplacer */
    void MainWindow::AppendFolderMoveItems(
        Collections::IVector<MenuFlyoutItemBase> const& items,
        JsonArray const& folders,
        hstring const& account,
        hstring const& prefix,
        std::vector<hstring> const& uids)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring name = folder.GetNamedString(L"name", full_name);
            hstring label = prefix.empty() ? name : prefix + L" / " + name;
            bool selectable = folder.GetNamedBoolean(L"selectable", true);

            if (selectable && !(account == m_account && full_name == m_folder))
            {
                MenuFlyoutItem item;
                item.Text(label);
                item.Icon(FontIcon{});
                item.Icon().as<FontIcon>().Glyph(FolderGlyph(folder.GetNamedString(L"type", L"")));
                item.Click([this, uids, account, full_name](IInspectable const&, RoutedEventArgs const&)
                {
                    MoveMessagesByUids(uids, account, full_name);
                });
                items.Append(item);
            }

            AppendFolderMoveItems(items, folder.GetNamedArray(L"children", JsonArray{}), account, label, uids);
        }
    }

    /* Remplit un menu « Déplacer vers » avec les dossiers du compte courant.
     * Paramètres :
     *   items : collection d'éléments du menu à remplir
     *   uids  : identifiants des messages à déplacer */
    void MainWindow::PopulateMoveMenuItems(Collections::IVector<MenuFlyoutItemBase> const& items, std::vector<hstring> const& uids)
    {
        items.Clear();

        auto it = m_account_folders.find(std::wstring{ m_account });
        if (it != m_account_folders.end() && it->second)
            AppendFolderMoveItems(items, it->second, m_account, L"", uids);

        if (items.Size() == 0)
        {
            MenuFlyoutItem empty_item;
            empty_item.Text(L"Aucun autre dossier disponible");
            empty_item.IsEnabled(false);
            items.Append(empty_item);
        }
    }

    /* Gestionnaire du bouton « Lu / Non lu » du volet de lecture. */
    void MainWindow::OnToggleReadClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
            return;

        auto it = m_message_states.find(std::wstring{ uids.front() });
        bool seen = it != m_message_states.end() ? it->second.seen : true;
        SetMessagesFlag(uids, L"seen", !seen);
    }

    /* Gestionnaire du bouton « Suivi » (drapeau) du volet de lecture. */
    void MainWindow::OnToggleFlagClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
            return;

        auto it = m_message_states.find(std::wstring{ uids.front() });
        bool flagged = it != m_message_states.end() ? it->second.flagged : false;
        SetMessagesFlag(uids, L"flagged", !flagged);
    }

    /* Gestionnaire du bouton « Supprimer » du volet de lecture. */
    void MainWindow::OnDeleteClick(IInspectable const&, RoutedEventArgs const&)
    {
        DeleteMessagesByUids(SelectedMessageUids());
    }

    /* Gestionnaire du bouton « Imprimer » (Ctrl+P) du volet de lecture :
     * ouvre la boîte de dialogue d'impression native de WebView2 pour le
     * message actuellement affiché. */
    void MainWindow::OnPrintClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message || !m_reader_ready || !Reader().CoreWebView2())
        {
            ShowStatus(L"Sélectionnez d'abord un message à imprimer.", InfoBarSeverity::Informational);
            return;
        }

        try
        {
            Reader().CoreWebView2().ShowPrintUI(Microsoft::Web::WebView2::Core::CoreWebView2PrintDialogKind::Browser);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'ouvrir l'impression : " + e.message(), InfoBarSeverity::Error);
        }
    }

    /* Gestionnaire d'ouverture du menu « Déplacer » du volet de lecture. */
    void MainWindow::OnMoveMenuOpening(IInspectable const&, IInspectable const&)
    {
        PopulateMoveMenuItems(MoveMenu().Items(), SelectedMessageUids());
    }

    /* Filtre instantanément en mémoire les messages chargés du dossier selon
     * l'objet et l'expéditeur.
     * Paramètres :
     *   query : texte saisi dans la zone de recherche */
    void MainWindow::FilterLocalMessages(hstring const& query)
    {
        if (!m_all_messages)
            return;

        if (query.empty())
        {
            ShowMessages(m_all_messages);
            return;
        }

        std::wstring needle{ query };
        CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));

        JsonArray filtered;
        for (auto const& value : m_all_messages)
        {
            auto msg = value.GetObject();
            std::wstring haystack = std::wstring{ msg.GetNamedString(L"subject", L"") } + L" " +
                std::wstring{ msg.GetNamedString(L"from", L"") };
            CharLowerBuffW(haystack.data(), static_cast<DWORD>(haystack.size()));

            if (haystack.find(needle) != std::wstring::npos)
                filtered.Append(msg);
        }

        ShowMessages(filtered);
    }

    /* Gestionnaire de modification du texte de recherche : filtre la liste en
     * direct ou la restaure lorsque le champ est vidé. */
    void MainWindow::OnSearchTextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput)
        {
            ++m_search_request;
            FilterLocalMessages(sender.Text());
        }
    }

    /* Gestionnaire de validation de la recherche (touche Entrée ou icône
     * loupe) : lance une recherche complète dans le dossier (en-têtes et corps
     * des messages) via evo-core. */
    void MainWindow::OnSearchQuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        hstring query = args.QueryText();
        if (query.empty() || m_account.empty() || m_folder.empty())
        {
            FilterLocalMessages(query);
            return;
        }

        hstring account = m_account;
        hstring folder = m_folder;
        uint64_t request = ++m_search_request;

        ListProgress().IsActive(true);

        m_core->SearchMessages(account, folder, query, 500,
            [this, account, folder, request](hstring const& json, hstring const& error)
        {
            if (request != m_search_request || account != m_account || folder != m_folder)
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(L"Recherche : " + error, InfoBarSeverity::Error);
                return;
            }

            ShowMessages(JsonArray::Parse(json));
        });
    }

    /* -------------------------------------------------------------------- */
    /* Étape 1 : Notifications Windows (toasts) et pièces jointes            */
    /* -------------------------------------------------------------------- */

    /* Affiche une notification système Windows (toast) ainsi qu'un message
     * dans la barre d'état à l'arrivée de nouveaux courriers non lus.
     * Paramètres :
     *   object : objet JSON décrivant les nouveaux messages ("from", "subject", "count") */
    void MainWindow::ShowNewMailNotification(JsonObject const& object)
    {
        int32_t count = static_cast<int32_t>(object.GetNamedNumber(L"count", 1));
        hstring from = SenderName(object.GetNamedString(L"from", L"Nouveau message"));
        hstring subject = object.GetNamedString(L"subject", L"(Sans objet)");

        hstring title = count > 1
            ? hstring{ std::to_wstring(count) + L" nouveaux messages" }
            : L"Nouveau message de " + from;
        hstring body = count > 1
            ? from + L" — " + subject
            : subject;

        ShowStatus(title + L" : " + body, InfoBarSeverity::Informational);

        try
        {
            Microsoft::Windows::AppNotifications::Builder::AppNotificationBuilder builder;
            builder.AddText(title);
            builder.AddText(body);
            Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(builder.BuildNotification());
        }
        catch (...)
        {
            /* Si l'application non empaquetée n'a pas pu enregistrer de canal toast,
               l'InfoBar reste visible pour prévenir l'utilisateur. */
        }
    }

    /* Remplit la barre des pièces jointes du volet de lecture avec un bouton
     * de téléchargement pour chaque pièce jointe du message affiché.
     * Paramètres :
     *   attachments : tableau JSON des pièces jointes ("index", "filename", "size") */
    void MainWindow::PopulateAttachments(JsonArray const& attachments)
    {
        auto children = AttachmentBar().Children();
        children.Clear();

        if (!attachments || attachments.Size() == 0)
        {
            AttachmentBar().Visibility(Visibility::Collapsed);
            return;
        }

        for (auto const& item : attachments)
        {
            auto att = item.GetObject();
            uint32_t index = static_cast<uint32_t>(att.GetNamedNumber(L"index", 0));
            hstring filename = att.GetNamedString(L"filename", L"piece-jointe");
            int64_t size = static_cast<int64_t>(att.GetNamedNumber(L"size", 0));

            hstring size_label;
            if (size >= 1024 * 1024)
                size_label = L" (" + to_hstring(size / (1024 * 1024)) + L" Mo)";
            else if (size >= 1024)
                size_label = L" (" + to_hstring(size / 1024) + L" Ko)";
            else if (size > 0)
                size_label = L" (" + to_hstring(size) + L" o)";

            Button button;
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(6);

            FontIcon icon;
            icon.Glyph(L"\xE723");
            icon.FontSize(12);
            content.Children().Append(icon);

            TextBlock label;
            label.Text(filename + size_label);
            content.Children().Append(label);

            button.Content(content);
            ToolTipService::SetToolTip(button, box_value(L"Enregistrer « " + filename + L" »"));
            button.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                SaveAttachmentAsync(index, filename);
            });

            children.Append(button);
        }

        AttachmentBar().Visibility(Visibility::Visible);
    }

    /* Met à jour le bandeau de sécurité cryptographique (OpenPGP / S/MIME) du
     * volet de lecture selon l'objet JSON "security" renvoyé par evo-core.
     * Paramètres :
     *   security : objet JSON ("encrypted", "signed", "valid", "protocol", "signer", "description") */
    void MainWindow::UpdateSecurityBanner(JsonObject const& security)
    {
        bool encrypted = security.GetNamedBoolean(L"encrypted", false);
        bool signed_msg = security.GetNamedBoolean(L"signed", false);

        if (!encrypted && !signed_msg)
        {
            SecurityBar().IsOpen(false);
            return;
        }

        bool valid = security.GetNamedBoolean(L"valid", false);
        hstring protocol = security.GetNamedString(L"protocol", L"");
        hstring signer = security.GetNamedString(L"signer", L"");
        hstring desc = security.GetNamedString(L"description", L"");

        std::wstring title = protocol.empty() ? L"Sécurité" : std::wstring{ protocol } + L" : ";
        if (encrypted && signed_msg)
            title += valid ? L"Message déchiffré et signature vérifiée" : L"Message chiffré / signé (vérification incomplète)";
        else if (encrypted)
            title += valid ? L"Message déchiffré" : L"Échec du déchiffrement";
        else
            title += valid ? L"Signature numérique valide" : L"Signature numérique non vérifiée ou invalide";

        std::wstring msg;
        if (!signer.empty())
            msg = L"Signataire : " + std::wstring{ signer };
        if (!desc.empty())
        {
            if (!msg.empty())
                msg += L" — ";
            msg += desc;
        }

        SecurityBar().Title(hstring{ title });
        SecurityBar().Message(hstring{ msg });
        SecurityBar().Severity(valid ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        SecurityBar().IsOpen(true);
    }

    /* Ouvre un sélecteur d'enregistrement de fichier Windows et sauvegarde la
     * pièce jointe sélectionnée sur le disque via evo-core.
     * Paramètres :
     *   index    : indice de la pièce jointe (base 0) dans le message courant
     *   filename : nom de fichier suggéré pour l'enregistrement */
    fire_and_forget MainWindow::SaveAttachmentAsync(uint32_t index, hstring filename)
    {
        auto strong = get_strong();
        hstring uid = SelectedMessageUid();
        hstring account = m_account;
        hstring folder = m_folder;

        if (uid.empty() || account.empty() || folder.empty())
            co_return;

        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

        picker.SuggestedFileName(filename);

        std::wstring_view view{ filename };
        auto dot = view.rfind(L'.');
        hstring ext = (dot != std::wstring_view::npos && dot + 1 < view.size())
            ? hstring{ view.substr(dot) }
            : L".bin";
        picker.FileTypeChoices().Insert(L"Fichier", single_threaded_vector<hstring>({ ext }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;

        hstring dest_path = file.Path();
        m_core->SaveAttachment(account, folder, uid, index, dest_path,
            [this, filename](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Enregistrement de la pièce jointe : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Pièce jointe « " + filename + L" » enregistrée.", InfoBarSeverity::Success);
        });
    }

    /* -------------------------------------------------------------------- */
    /* Étape 2 : Gestion des comptes et boîtes de dialogue d'authentification */
    /* -------------------------------------------------------------------- */

    /* Affiche une boîte de dialogue modale demandant le mot de passe d'une
     * source EDS lorsque celui-ci est absent ou refusé par le serveur, puis
     * l'enregistre et relance la synchronisation.
     * Paramètres :
     *   source_uid : identifiant de la source EDS concernée
     *   name       : nom d'affichage du compte ou du serveur
     *   user       : identifiant utilisateur associé
     *   rejected   : true si le mot de passe précédent a été refusé */
    fire_and_forget MainWindow::PromptPasswordAsync(hstring source_uid, hstring name, hstring user, bool rejected)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Authentification requise"));
        dialog.PrimaryButtonText(L"Valider");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);

        TextBlock description;
        description.TextWrapping(TextWrapping::Wrap);
        description.Text((rejected
            ? L"Le serveur a refusé le mot de passe pour « "
            : L"Veuillez saisir le mot de passe pour « ")
            + name + (user.empty() ? L" » :" : L" » (" + user + L") :"));
        panel.Children().Append(description);

        PasswordBox password_box;
        password_box.PlaceholderText(L"Mot de passe");
        panel.Children().Append(password_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::Primary)
        {
            hstring password = password_box.Password();
            if (!password.empty())
            {
                m_core->SavePassword(source_uid, password, [this, name](hstring const&, hstring const& error)
                {
                    if (!error.empty())
                    {
                        ShowStatus(L"Mot de passe : " + error, InfoBarSeverity::Error);
                        return;
                    }
                    ShowStatus(L"Mot de passe enregistré pour « " + name + L" ».", InfoBarSeverity::Success);
                    m_core->Refresh();
                });
            }
        }
    }

    /* Affiche une boîte de dialogue modale permettant d'inspecter et d'accepter
     * (pour la session ou de façon permanente) ou de refuser un certificat TLS
     * présentant des erreurs de validation.
     * Paramètres :
     *   object : objet JSON de l'événement "tls-trust-required" */
    fire_and_forget MainWindow::PromptTlsCertificateAsync(JsonObject object)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;
        hstring trust_id = object.GetNamedString(L"id", L"");
        if (trust_id.empty())
            co_return;

        /* Si une autre boîte de dialogue est ouverte, attend sa fermeture sans perdre la demande */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }

        if (!Content() || !Content().XamlRoot() || !m_core)
        {
            if (m_core)
                m_core->RespondTlsTrust(trust_id, 0);
            co_return;
        }

        m_dialog_open = true;

        hstring name = object.GetNamedString(L"name", L"Serveur");
        hstring host = object.GetNamedString(L"host", L"");
        hstring subject = object.GetNamedString(L"subject", host);
        hstring issuer = object.GetNamedString(L"issuer", L"(inconnu)");
        hstring fingerprint = object.GetNamedString(L"fingerprint", L"");
        hstring errors = object.GetNamedString(L"errors", L"");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Certificat TLS non vérifié"));
        dialog.PrimaryButtonText(L"Toujours approuver");
        dialog.SecondaryButtonText(L"Accepter (session)");
        dialog.CloseButtonText(L"Refuser");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(8);
        panel.MaxWidth(480);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(L"Le serveur « " + host + L" » (" + name + L") a présenté un certificat TLS qui n'a pas pu être validé automatiquement :");
        panel.Children().Append(intro);

        TextBlock err_block;
        err_block.TextWrapping(TextWrapping::Wrap);
        err_block.Text(errors);
        err_block.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        panel.Children().Append(err_block);

        TextBlock details;
        details.TextWrapping(TextWrapping::Wrap);
        details.IsTextSelectionEnabled(true);
        details.Text(
            L"Objet : " + subject + L"\n" +
            L"Émetteur : " + issuer + L"\n" +
            L"Empreinte SHA-256 :\n" + fingerprint);
        details.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Cascadia Mono, Consolas" });
        details.FontSize(12);
        panel.Children().Append(details);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (!m_core)
            co_return;

        if (result == ContentDialogResult::Primary)
        {
            m_core->RespondTlsTrust(trust_id, 2);
            ShowStatus(L"Certificat TLS approuvé de façon permanente pour « " + host + L" ».", InfoBarSeverity::Success);
            m_core->Refresh();
        }
        else if (result == ContentDialogResult::Secondary)
        {
            m_core->RespondTlsTrust(trust_id, 1);
            ShowStatus(L"Certificat TLS accepté pour cette session (« " + host + L" »).", InfoBarSeverity::Informational);
            m_core->Refresh();
        }
        else
        {
            m_core->RespondTlsTrust(trust_id, 0);
            ShowStatus(L"Certificat TLS refusé pour « " + host + L" ».", InfoBarSeverity::Warning);
        }
    }

    /* Exécute un flux d'authentification OAuth2 interactif avec PKCE dans un
     * contrôle WebView2 intégré pour Microsoft 365 / Outlook.com ou Google / Gmail,
     * puis échange le code d'autorisation contre un jeton d'accès et un jeton de
     * rafraîchissement.
     * Paramètres :
     *   provider_index : 1 = Microsoft 365 / Outlook.com, 2 = Google / Gmail
     *   client_id      : identifiant d'application OAuth2
     *   tenant_id      : locataire Entra ID ("common" par défaut)
     *   login_hint     : adresse électronique de l'utilisateur
     * Retourne : chaîne JSON contenant access_token, refresh_token, etc., ou "" si annulé. */
    IAsyncOperation<hstring> MainWindow::StartOAuth2SignInAsync(
        int provider_index, hstring client_id, hstring tenant_id, hstring login_hint)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (!Content() || !Content().XamlRoot() || client_id.empty())
            co_return L"";

        /* Génération du code_verifier et du code_challenge PKCE (S256) */
        auto rnd_buf = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(32);
        std::wstring verifier{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(rnd_buf) };
        for (auto& c : verifier)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        verifier.erase(std::remove(verifier.begin(), verifier.end(), L'='), verifier.end());

        auto utf8_ver = Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(
            verifier, Windows::Security::Cryptography::BinaryStringEncoding::Utf8);
        auto sha256 = Windows::Security::Cryptography::Core::HashAlgorithmProvider::OpenAlgorithm(
            Windows::Security::Cryptography::Core::HashAlgorithmNames::Sha256());
        auto hash_buf = sha256.HashData(utf8_ver);
        std::wstring challenge{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(hash_buf) };
        for (auto& c : challenge)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        challenge.erase(std::remove(challenge.begin(), challenge.end(), L'='), challenge.end());

        std::wstring redirect_uri = L"http://localhost/oauth2callback";
        std::wstring tenant = tenant_id.empty() ? L"common" : std::wstring{ tenant_id };
        std::wstring auth_url;
        std::wstring token_endpoint;
        std::wstring scope;

        if (provider_index == 1)
        {
            /* Microsoft 365 / Outlook.com */
            scope = L"https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access";
            auth_url = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/authorize"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&response_mode=query"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/token";
        }
        else
        {
            /* Google / Gmail */
            scope = L"https://mail.google.com/";
            auth_url = L"https://accounts.google.com/o/oauth2/v2/auth"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&access_type=offline&prompt=consent"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://oauth2.googleapis.com/token";
        }

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(provider_index == 1
            ? L"Connexion OAuth2 — Microsoft 365"
            : L"Connexion OAuth2 — Google / Gmail"));
        dialog.CloseButtonText(L"Annuler");

        WebView2 oauth_web;
        oauth_web.MinWidth(480);
        oauth_web.MinHeight(540);
        dialog.Content(oauth_web);

        auto captured_code = std::make_shared<std::wstring>();
        auto captured_err = std::make_shared<std::wstring>();

        co_await oauth_web.EnsureCoreWebView2Async();
        auto core_wv = oauth_web.CoreWebView2();

        core_wv.NavigationStarting([dialog, captured_code, captured_err, redirect_uri](
            auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring uri{ args.Uri() };
            if (uri.rfind(redirect_uri, 0) == 0)
            {
                args.Cancel(true);
                try
                {
                    Uri parsed{ uri };
                    auto decoder = parsed.QueryParsed();
                    for (uint32_t i = 0; i < decoder.Size(); ++i)
                    {
                        auto entry = decoder.GetAt(i);
                        if (entry.Name() == L"code")
                            *captured_code = entry.Value();
                        else if (entry.Name() == L"error_description" || entry.Name() == L"error")
                            *captured_err = entry.Value();
                    }
                }
                catch (...) {}
                dialog.Hide();
            }
        });

        oauth_web.Source(Uri{ auth_url });
        co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (captured_code->empty())
        {
            if (!captured_err->empty())
                ShowStatus(L"Authentification OAuth2 refusée : " + hstring{ *captured_err }, InfoBarSeverity::Error);
            co_return L"";
        }

        /* Échange du code d'autorisation contre access_token + refresh_token
         * (co_await interdit dans un catch : l'erreur est affichée après) */
        hstring network_error;
        try
        {
            Windows::Web::Http::HttpClient http;
            std::map<hstring, hstring> form_fields{
                { L"client_id", client_id },
                { L"grant_type", L"authorization_code" },
                { L"code", hstring{ *captured_code } },
                { L"redirect_uri", hstring{ redirect_uri } },
                { L"code_verifier", hstring{ verifier } }
            };
            Windows::Web::Http::HttpFormUrlEncodedContent form_content{ form_fields };
            auto response = co_await http.PostAsync(Uri{ token_endpoint }, form_content);
            hstring body = co_await response.Content().ReadAsStringAsync();
            co_await ui_thread;

            JsonObject tok_obj;
            if (JsonObject::TryParse(body, tok_obj))
            {
                hstring access_tok = tok_obj.GetNamedString(L"access_token", L"");
                if (!access_tok.empty())
                {
                    int64_t exp_in = static_cast<int64_t>(tok_obj.GetNamedNumber(L"expires_in", 3600));
                    int64_t now_s = static_cast<int64_t>(std::time(nullptr));
                    tok_obj.SetNamedValue(L"expires_at", JsonValue::CreateNumberValue(static_cast<double>(now_s + exp_in)));
                    tok_obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id));
                    tok_obj.SetNamedValue(L"token_endpoint", JsonValue::CreateStringValue(token_endpoint));
                    co_return tok_obj.Stringify();
                }
                hstring err_desc = tok_obj.GetNamedString(L"error_description", tok_obj.GetNamedString(L"error", L"Jeton invalide"));
                ShowStatus(L"Échange du jeton OAuth2 : " + err_desc, InfoBarSeverity::Error);
            }
        }
        catch (hresult_error const& e)
        {
            network_error = e.message();
        }

        if (!network_error.empty())
        {
            co_await ui_thread;
            ShowStatus(L"Erreur réseau OAuth2 : " + network_error, InfoBarSeverity::Error);
        }

        co_return L"";
    }

    /* Connexion OAuth2 d'un compte géré par EDS (Microsoft 365...), à l'ajout
     * du compte ou sur l'événement "oauth2-required" : la page de connexion du
     * fournisseur s'ouvre dans un WebView2 ; la navigation vers l'adresse de
     * redirection (code d'autorisation) est interceptée et transmise au
     * moteur, qui enregistre les jetons (renouvelés ensuite par EDS).
     * Paramètres :
     *   source_uid : uid du compte ou de sa collection
     *   name       : nom du compte, pour le titre et les messages */
    fire_and_forget MainWindow::SignInOAuth2Async(hstring source_uid, hstring name)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (source_uid.empty() || !m_core)
            co_return;

        /* une autre boîte de dialogue est ouverte : attendre sa fermeture */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }
        if (!Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        /* étape 1 : adresse de connexion (rappel reçu sur ce thread) */
        auto begin_json = std::make_shared<hstring>();
        auto begin_error = std::make_shared<hstring>();
        winrt::handle begun{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE begun_event = begun.get();
        m_core->OAuth2Begin(source_uid, [begin_json, begin_error, begun_event](hstring const& json, hstring const& error)
        {
            *begin_json = json;
            *begin_error = error;
            SetEvent(begun_event);
        });
        co_await winrt::resume_on_signal(begun_event);
        co_await ui_thread;

        JsonObject begin;
        if (!begin_error->empty() || !JsonObject::TryParse(*begin_json, begin))
        {
            m_dialog_open = false;
            ShowStatus(L"Connexion à « " + name + L" » : " + *begin_error, InfoBarSeverity::Error);
            co_return;
        }

        hstring credentials_uid = begin.GetNamedString(L"source_uid", source_uid);
        Uri redirect{ begin.GetNamedString(L"redirect_uri", L"") };
        hstring service = begin.GetNamedString(L"service", L"OAuth2");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Connexion " + service + L" — " + name));
        dialog.CloseButtonText(L"Annuler");

        WebView2 web;
        web.MinWidth(480);
        web.MinHeight(600);
        dialog.Content(web);

        /* même hôte et même chemin que l'adresse de redirection : fin */
        auto reached = std::make_shared<hstring>();
        web.NavigationStarting([dialog, reached, redirect](WebView2 const&,
            Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            try
            {
                Uri target{ args.Uri() };
                if (_wcsicmp(target.Host().c_str(), redirect.Host().c_str()) == 0 &&
                    _wcsicmp(target.Path().c_str(), redirect.Path().c_str()) == 0)
                {
                    args.Cancel(true);
                    *reached = args.Uri();
                    dialog.Hide();
                }
            }
            catch (...) {}
        });
        web.Source(Uri{ begin.GetNamedString(L"uri", L"") });

        co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (reached->empty())
        {
            ShowStatus(L"Connexion à « " + name + L" » annulée.", InfoBarSeverity::Warning);
            co_return;
        }

        /* étape 2 : échange du code et enregistrement des jetons */
        ShowStatus(L"Connexion à « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->OAuth2Finish(credentials_uid, *reached, [this, name](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Connexion à « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Connecté à « " + name + L" ».", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* Met à jour la liste des comptes affichée dans la vue Paramètres. */
    void MainWindow::RefreshAccountsView()
    {
        auto items = AccountList().Items();
        items.Clear();

        if (!m_accounts)
            return;

        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            hstring uid = account.GetNamedString(L"uid", L"");
            hstring name = account.GetNamedString(L"name", uid);
            hstring address = account.GetNamedString(L"address", L"");
            hstring backend = account.GetNamedString(L"backend", L"");
            bool local = account.GetNamedBoolean(L"local", false);

            Grid row;
            row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition action_col;
            action_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(action_col);

            StackPanel info;
            info.Spacing(2);

            TextBlock title;
            title.Text(name);
            title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            info.Children().Append(title);

            TextBlock subtitle;
            subtitle.Text((address.empty() ? L"Compte local" : address) +
                (backend.empty() ? hstring{} : L"  •  Protocole : " + backend));
            subtitle.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            info.Children().Append(subtitle);

            row.Children().Append(info);

            if (!local)
            {
                Button remove_btn;
                remove_btn.Content(box_value(L"Supprimer"));
                remove_btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(remove_btn, 1);
                remove_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->RemoveAccount(uid, [this, name](hstring const&, hstring const& error)
                    {
                        if (!error.empty())
                        {
                            ShowStatus(L"Suppression du compte : " + error, InfoBarSeverity::Error);
                            return;
                        }
                        ShowStatus(L"Compte « " + name + L" » supprimé.", InfoBarSeverity::Informational);
                        LoadAccounts();
                    });
                });
                row.Children().Append(remove_btn);
            }

            items.Append(row);
        }
    }

    /* Gestionnaire du bouton « Ajouter un compte IMAP / SMTP ». */
    void MainWindow::OnAddAccountClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAddAccountDialogAsync();
    }

    /* Affiche l'assistant d'ajout d'un compte IMAP / SMTP sous forme de boîte
     * de dialogue et crée les sources EDS correspondantes via evo-core. */
    fire_and_forget MainWindow::ShowAddAccountDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un compte"));
        dialog.PrimaryButtonText(L"Ajouter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(440);

        ComboBox provider_box;
        provider_box.Header(box_value(L"Type de compte"));
        provider_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        provider_box.Items().Append(box_value(L"IMAP / SMTP standard (mot de passe)"));
        provider_box.Items().Append(box_value(L"Microsoft 365 / Outlook.com (OAuth2 XOAUTH2)"));
        provider_box.Items().Append(box_value(L"Google / Gmail (OAuth2 XOAUTH2)"));
        /* index 3 : compte evolution-ews (Microsoft Graph), sans IMAP/SMTP */
        provider_box.Items().Append(box_value(L"Microsoft 365 / Exchange Online (courrier, agenda, contacts)"));
        provider_box.SelectedIndex(0);
        panel.Children().Append(provider_box);

        TextBox display_name_box;
        display_name_box.Header(box_value(L"Nom du compte"));
        display_name_box.PlaceholderText(L"Personnel, Travail…");
        panel.Children().Append(display_name_box);

        TextBox full_name_box;
        full_name_box.Header(box_value(L"Votre nom complet"));
        full_name_box.PlaceholderText(L"Pierre Dupont");
        panel.Children().Append(full_name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"pierre@exemple.fr");
        panel.Children().Append(email_box);

        TextBox imap_host_box;
        imap_host_box.Header(box_value(L"Serveur entrant (IMAP)"));
        imap_host_box.PlaceholderText(L"imap.exemple.fr");
        panel.Children().Append(imap_host_box);

        TextBox imap_port_box;
        imap_port_box.Header(box_value(L"Port IMAP (993 = TLS, 143 = STARTTLS)"));
        imap_port_box.Text(L"993");
        panel.Children().Append(imap_port_box);

        TextBox smtp_host_box;
        smtp_host_box.Header(box_value(L"Serveur sortant (SMTP)"));
        smtp_host_box.PlaceholderText(L"smtp.exemple.fr");
        panel.Children().Append(smtp_host_box);

        TextBox smtp_port_box;
        smtp_port_box.Header(box_value(L"Port SMTP (465 = TLS, 587 = STARTTLS)"));
        smtp_port_box.Text(L"465");
        panel.Children().Append(smtp_port_box);

        StackPanel oauth_panel;
        oauth_panel.Spacing(8);
        oauth_panel.Visibility(Visibility::Collapsed);

        wchar_t env_cid[256]{};
        wchar_t env_tid[256]{};
        GetEnvironmentVariableW(L"EVO_OAUTH2_CLIENT_ID", env_cid, 256);
        GetEnvironmentVariableW(L"EVO_OAUTH2_TENANT_ID", env_tid, 256);

        TextBox client_id_box;
        client_id_box.Header(box_value(L"Client ID OAuth2 (Application ID)"));
        client_id_box.PlaceholderText(L"Identifiant d'application Entra ID ou Google Cloud");
        client_id_box.Text(env_cid);
        oauth_panel.Children().Append(client_id_box);

        TextBox tenant_id_box;
        tenant_id_box.Header(box_value(L"Tenant ID Microsoft 365 (ex. common ou ID d'organisation)"));
        tenant_id_box.Text(env_tid[0] ? env_tid : L"common");
        oauth_panel.Children().Append(tenant_id_box);

        panel.Children().Append(oauth_panel);

        PasswordBox password_box;
        password_box.Header(box_value(L"Mot de passe (ou jeton d'accès OAuth2 si déjà obtenu)"));
        panel.Children().Append(password_box);

        provider_box.SelectionChanged([imap_host_box, imap_port_box, smtp_host_box, smtp_port_box, oauth_panel, tenant_id_box, client_id_box, password_box](
            IInspectable const& sender, SelectionChangedEventArgs const&)
        {
            int idx = sender.as<ComboBox>().SelectedIndex();

            /* Microsoft Graph : ni serveurs ni mot de passe, connexion OAuth2 ensuite */
            auto server_visibility = idx == 3 ? Visibility::Collapsed : Visibility::Visible;
            imap_host_box.Visibility(server_visibility);
            imap_port_box.Visibility(server_visibility);
            smtp_host_box.Visibility(server_visibility);
            smtp_port_box.Visibility(server_visibility);
            password_box.Visibility(server_visibility);
            client_id_box.PlaceholderText(idx == 3
                ? L"Facultatif : application d'Evolution par défaut"
                : L"Identifiant d'application Entra ID ou Google Cloud");

            if (idx == 3)
            {
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
            }
            else if (idx == 1)
            {
                imap_host_box.Text(L"outlook.office365.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.office365.com");
                smtp_port_box.Text(L"587");
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
                password_box.PlaceholderText(L"Laisser vide pour ouvrir la fenêtre OAuth2 WebView2");
            }
            else if (idx == 2)
            {
                imap_host_box.Text(L"imap.gmail.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.gmail.com");
                smtp_port_box.Text(L"465");
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Collapsed);
                password_box.PlaceholderText(L"Laisser vide pour ouvrir la fenêtre OAuth2 WebView2");
            }
            else
            {
                oauth_panel.Visibility(Visibility::Collapsed);
                password_box.PlaceholderText(L"Mot de passe du compte");
            }
        });

        ScrollViewer scroll;
        scroll.Content(panel);
        scroll.MaxHeight(540);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        int provider_idx = provider_box.SelectedIndex();
        hstring email = email_box.Text();
        hstring imap_host = imap_host_box.Text();
        hstring smtp_host = smtp_host_box.Text();

        if (provider_idx == 3)
        {
            if (email.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique du compte Microsoft 365.", InfoBarSeverity::Warning);
                co_return;
            }

            hstring account_name = display_name_box.Text().empty() ? email : display_name_box.Text();
            JsonObject m365;
            m365.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            m365.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            m365.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));
            m365.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id_box.Text()));
            /* "common" est la valeur par défaut d'evolution-ews */
            m365.SetNamedValue(L"tenant", JsonValue::CreateStringValue(
                tenant_id_box.Text() == L"common" ? L"" : tenant_id_box.Text()));

            m_core->AddMicrosoft365Account(m365.Stringify(), [this, account_name](hstring const& json, hstring const& error)
            {
                JsonObject created;
                if (!error.empty() || !JsonObject::TryParse(json, created))
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                LoadAccounts();
                SignInOAuth2Async(created.GetNamedString(L"collection_uid", L""), account_name);
            });
            co_return;
        }

        if (email.empty() || imap_host.empty() || smtp_host.empty())
        {
            ShowStatus(L"Veuillez renseigner l'adresse électronique et les serveurs IMAP / SMTP.", InfoBarSeverity::Warning);
            co_return;
        }

        hstring secret = password_box.Password();
        if ((provider_idx == 1 || provider_idx == 2) && secret.empty())
        {
            if (client_id_box.Text().empty())
            {
                ShowStatus(L"Veuillez renseigner le Client ID OAuth2 (ou saisir un jeton d'accès).", InfoBarSeverity::Warning);
                co_return;
            }
            secret = co_await StartOAuth2SignInAsync(provider_idx, client_id_box.Text(), tenant_id_box.Text(), email);
            if (secret.empty())
                co_return;
        }

        int imap_port = _wtoi(imap_port_box.Text().c_str());
        if (imap_port <= 0) imap_port = 993;
        int smtp_port = _wtoi(smtp_port_box.Text().c_str());
        if (smtp_port <= 0) smtp_port = 465;

        JsonObject payload;
        payload.SetNamedValue(L"display_name", JsonValue::CreateStringValue(
            display_name_box.Text().empty() ? email : display_name_box.Text()));
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
            full_name_box.Text().empty() ? email : full_name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
        payload.SetNamedValue(L"user", JsonValue::CreateStringValue(email));
        payload.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(imap_host));
        payload.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(imap_port));
        payload.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(
            imap_port == 143 ? L"STARTTLS-on-standard-port" : L"tls"));
        payload.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(smtp_host));
        payload.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(smtp_port));
        payload.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(
            smtp_port == 587 ? L"STARTTLS-on-standard-port" : L"tls"));
        if (provider_idx == 1 || provider_idx == 2)
            payload.SetNamedValue(L"auth_mechanism", JsonValue::CreateStringValue(L"XOAUTH2"));
        payload.SetNamedValue(L"password", JsonValue::CreateStringValue(secret));

        m_core->AddAccount(payload.Stringify(), [this, email](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Compte « " + email + L" » ajouté.", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* -------------------------------------------------------------------- */
    /* Étape 3 : Calendrier (ECalClient) et Contacts (EBookClient)          */
    /* -------------------------------------------------------------------- */

    /* Charge et affiche les événements des calendriers EDS autour de la date
     * sélectionnée (ou les 30 prochains jours par défaut). */
    void MainWindow::LoadEvents()
    {
        int64_t start_unix = m_selected_day_start;
        int64_t end_unix = 0;

        if (start_unix <= 0)
        {
            start_unix = static_cast<int64_t>(std::time(nullptr)) - 86400;
            end_unix = start_unix + 30LL * 86400;
            CalendarHeader().Text(L"Événements (30 prochains jours)");
        }
        else
        {
            end_unix = start_unix + 86400;
            CalendarHeader().Text(L"Événements du " + FormatDate(static_cast<double>(start_unix)));
        }

        m_core->ListEvents(start_unix, end_unix, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Calendrier : " + error, InfoBarSeverity::Error);
                return;
            }

            auto items = EventList().Items();
            items.Clear();

            auto events = JsonArray::Parse(json);
            for (auto const& value : events)
            {
                auto ev = value.GetObject();
                hstring uid = ev.GetNamedString(L"uid", L"");
                hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
                hstring summary = ev.GetNamedString(L"summary", L"(Sans titre)");
                hstring location = ev.GetNamedString(L"location", L"");
                hstring description = ev.GetNamedString(L"description", L"");
                double start_t = ev.GetNamedNumber(L"start", 0);
                double end_t = ev.GetNamedNumber(L"end", 0);

                Grid row;
                row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
                row.ColumnDefinitions().Append(ColumnDefinition{});
                ColumnDefinition del_col;
                del_col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(del_col);

                StackPanel details;
                details.Spacing(2);

                TextBlock title;
                title.Text(summary);
                title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                details.Children().Append(title);

                hstring time_str = FormatDate(start_t) + (end_t > start_t ? L" → " + FormatDate(end_t) : hstring{});
                if (!location.empty())
                    time_str = time_str + L"  •  Lieu : " + location;

                TextBlock sub;
                sub.Text(time_str);
                sub.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                details.Children().Append(sub);

                if (!description.empty())
                {
                    TextBlock desc;
                    desc.Text(description);
                    desc.TextWrapping(TextWrapping::Wrap);
                    details.Children().Append(desc);
                }

                row.Children().Append(details);

                Button del_btn;
                FontIcon del_icon;
                del_icon.Glyph(L"\xE74D");
                del_icon.FontSize(14);
                del_btn.Content(del_icon);
                del_btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(del_btn, 1);
                ToolTipService::SetToolTip(del_btn, box_value(L"Supprimer l'événement"));
                del_btn.Click([this, calendar_uid, uid](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->DeleteEvent(calendar_uid, uid, [this](hstring const&, hstring const& err)
                    {
                        if (!err.empty())
                        {
                            ShowStatus(L"Suppression de l'événement : " + err, InfoBarSeverity::Error);
                            return;
                        }
                        LoadEvents();
                    });
                });
                row.Children().Append(del_btn);

                items.Append(row);
            }

            if (events.Size() == 0)
            {
                TextBlock empty;
                empty.Text(L"Aucun événement sur cette période.");
                empty.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
                items.Append(empty);
            }
        });
    }

    /* Gestionnaire de sélection d'une date sur le calendrier mensuel. */
    void MainWindow::OnCalendarDateSelected(CalendarView const& sender, CalendarViewSelectedDatesChangedEventArgs const&)
    {
        auto dates = sender.SelectedDates();
        if (dates.Size() == 0)
        {
            m_selected_day_start = 0;
        }
        else
        {
            time_t raw = winrt::clock::to_time_t(dates.GetAt(0));
            struct tm local_tm{};
            localtime_s(&local_tm, &raw);
            local_tm.tm_hour = 0;
            local_tm.tm_min = 0;
            local_tm.tm_sec = 0;
            m_selected_day_start = static_cast<int64_t>(mktime(&local_tm));
        }
        LoadEvents();
    }

    /* Gestionnaire du bouton d'actualisation du calendrier. */
    void MainWindow::OnRefreshCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_selected_day_start = 0;
        MonthCalendar().SelectedDates().Clear();
        LoadEvents();
    }

    /* Gestionnaire du bouton « Nouvel événement ». */
    void MainWindow::OnNewEventClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewEventDialogAsync();
    }

    /* Affiche une boîte de dialogue permettant de créer un nouvel événement
     * dans le calendrier EDS et l'enregistre via evo-core. */
    fire_and_forget MainWindow::ShowNewEventDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Nouvel événement"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        TextBox summary_box;
        summary_box.Header(box_value(L"Titre"));
        summary_box.PlaceholderText(L"Réunion, Rendez-vous…");
        panel.Children().Append(summary_box);

        TextBox location_box;
        location_box.Header(box_value(L"Lieu"));
        panel.Children().Append(location_box);

        CalendarDatePicker date_picker;
        date_picker.Header(box_value(L"Date"));
        date_picker.Date(m_selected_day_start > 0
            ? winrt::clock::from_time_t(static_cast<time_t>(m_selected_day_start + 3600))
            : winrt::clock::now());
        panel.Children().Append(date_picker);

        TimePicker start_picker;
        start_picker.Header(box_value(L"Heure de début"));
        start_picker.Time( std::chrono::hours(9) );
        panel.Children().Append(start_picker);

        TimePicker end_picker;
        end_picker.Header(box_value(L"Heure de fin"));
        end_picker.Time( std::chrono::hours(10) );
        panel.Children().Append(end_picker);

        TextBox desc_box;
        desc_box.Header(box_value(L"Description"));
        desc_box.AcceptsReturn(true);
        desc_box.Height(80);
        panel.Children().Append(desc_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary || summary_box.Text().empty())
            co_return;

        auto picked_date = date_picker.Date() ? date_picker.Date().Value() : winrt::clock::now();
        time_t raw_date = winrt::clock::to_time_t(picked_date);
        struct tm day_tm{};
        localtime_s(&day_tm, &raw_date);
        day_tm.tm_hour = 0;
        day_tm.tm_min = 0;
        day_tm.tm_sec = 0;
        int64_t day_base = static_cast<int64_t>(mktime(&day_tm));

        int64_t start_secs = std::chrono::duration_cast<std::chrono::seconds>(start_picker.Time()).count();
        int64_t end_secs = std::chrono::duration_cast<std::chrono::seconds>(end_picker.Time()).count();
        if (end_secs <= start_secs)
            end_secs = start_secs + 3600;

        JsonObject payload;
        payload.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary_box.Text()));
        payload.SetNamedValue(L"location", JsonValue::CreateStringValue(location_box.Text()));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(desc_box.Text()));
        payload.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(day_base + start_secs)));
        payload.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(day_base + end_secs)));

        m_core->CreateEvent(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création de l'événement : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Événement enregistré.", InfoBarSeverity::Success);
            LoadEvents();
        });
    }

    /* Charge la liste des contacts depuis les carnets d'adresses EDS.
     * Paramètres :
     *   query : texte de filtrage optionnel sur le nom ou l'adresse e-mail */
    void MainWindow::LoadContacts(hstring const& query)
    {
        m_core->ListContacts(query, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Contacts : " + error, InfoBarSeverity::Error);
                return;
            }

            auto items = ContactList().Items();
            items.Clear();
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);

            auto contacts = JsonArray::Parse(json);
            for (auto const& value : contacts)
            {
                auto contact = value.GetObject();
                hstring full_name = contact.GetNamedString(L"full_name", L"(Sans nom)");
                hstring email = contact.GetNamedString(L"email", L"");

                StackPanel row;
                row.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
                row.Tag(box_value(contact.Stringify()));

                TextBlock name_text;
                name_text.Text(full_name);
                name_text.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                row.Children().Append(name_text);

                if (!email.empty())
                {
                    TextBlock email_text;
                    email_text.Text(email);
                    email_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    row.Children().Append(email_text);
                }

                items.Append(row);
            }
        });
    }

    /* Gestionnaire de modification du champ de recherche de contacts. */
    void MainWindow::OnContactSearchChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput)
            LoadContacts(sender.Text());
    }

    /* Gestionnaire de validation de la recherche de contacts. */
    void MainWindow::OnContactSearchSubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        LoadContacts(args.QueryText());
    }

    /* Gestionnaire de sélection d'un contact dans la liste : affiche sa fiche détaillée. */
    void MainWindow::OnContactSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto panel = ContactList().SelectedItem().try_as<StackPanel>();
        if (!panel)
        {
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);
            return;
        }

        hstring raw = unbox_value_or<hstring>(panel.Tag(), L"");
        JsonObject contact;
        if (!JsonObject::TryParse(raw, contact))
            return;

        m_selected_contact = contact;
        ContactNameText().Text(contact.GetNamedString(L"full_name", L"(Sans nom)"));
        ContactEmailText().Text(contact.GetNamedString(L"email", L"—"));
        ContactPhoneText().Text(contact.GetNamedString(L"phone", L"—"));
        ContactOrgText().Text(contact.GetNamedString(L"org", L"—"));
        ContactNotesText().Text(contact.GetNamedString(L"note", L""));

        ContactPlaceholder().Visibility(Visibility::Collapsed);
        ContactDetailPane().Visibility(Visibility::Visible);
    }

    /* Gestionnaire du bouton « Nouveau contact ». */
    void MainWindow::OnNewContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewContactDialogAsync();
    }

    /* Affiche une boîte de dialogue permettant d'ajouter un nouveau contact
     * dans le carnet d'adresses EDS et l'enregistre via evo-core. */
    fire_and_forget MainWindow::ShowNewContactDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Nouveau contact"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        TextBox name_box;
        name_box.Header(box_value(L"Nom complet"));
        name_box.PlaceholderText(L"Jean Martin");
        panel.Children().Append(name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"jean.martin@exemple.fr");
        panel.Children().Append(email_box);

        TextBox phone_box;
        phone_box.Header(box_value(L"Téléphone"));
        panel.Children().Append(phone_box);

        TextBox org_box;
        org_box.Header(box_value(L"Organisation"));
        panel.Children().Append(org_box);

        TextBox note_box;
        note_box.Header(box_value(L"Notes"));
        note_box.AcceptsReturn(true);
        note_box.Height(72);
        panel.Children().Append(note_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary || name_box.Text().empty())
            co_return;

        JsonObject payload;
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email_box.Text()));
        payload.SetNamedValue(L"phone", JsonValue::CreateStringValue(phone_box.Text()));
        payload.SetNamedValue(L"org", JsonValue::CreateStringValue(org_box.Text()));
        payload.SetNamedValue(L"note", JsonValue::CreateStringValue(note_box.Text()));

        m_core->CreateContact(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact enregistré.", InfoBarSeverity::Success);
            LoadContacts(ContactSearchBox().Text());
        });
    }

    /* Gestionnaire du bouton « Écrire un message » sur la fiche d'un contact :
     * ouvre la fenêtre de rédaction pré-remplie avec l'adresse du contact. */
    void MainWindow::OnComposeToContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring email = m_selected_contact.GetNamedString(L"email", L"");
        hstring name = m_selected_contact.GetNamedString(L"full_name", L"");
        if (email.empty())
        {
            ShowStatus(L"Ce contact n'a pas d'adresse électronique.", InfoBarSeverity::Warning);
            return;
        }

        ::EvolutionWin::ComposeData data;
        data.account = m_account;
        data.to = name.empty() ? email : name + L" <" + email + L">";
        OpenCompose(data);
    }

    /* Gestionnaire du bouton « Supprimer le contact » sur la fiche d'un contact. */
    void MainWindow::OnDeleteContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring book_uid = m_selected_contact.GetNamedString(L"book_uid", L"");
        hstring uid = m_selected_contact.GetNamedString(L"uid", L"");
        if (book_uid.empty() || uid.empty())
            return;

        m_core->DeleteContact(book_uid, uid, [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact supprimé.", InfoBarSeverity::Informational);
            LoadContacts(ContactSearchBox().Text());
        });
    }
}
