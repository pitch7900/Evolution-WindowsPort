#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "DataTransfer.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

/* Assistants « Exporter des données » et « Importer des données » : courrier (mbox, EML, Maildir,
 * profil Thunderbird), contacts (vCard) et agendas / tâches (iCalendar). Les opérations tournent
 * dans le moteur (evo-core-mail-transfer.c) avec progression dans la barre d'état et annulation. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    /* Dossier de courrier d'un compte, à plat : libellé indenté et chemin complet. */
    using FolderEntry = std::pair<hstring, hstring>;

    /**
     * @brief Aplatit l'arborescence des dossiers d'un compte (ordre d'affichage).
     * @param folders tableau JSON renvoyé par ListFolders ([{full_name, name, children}])
     * @param depth   profondeur courante (indentation du libellé)
     * @param out     reçoit les dossiers : (libellé indenté, chemin complet)
     */
    void FlattenFolders(JsonArray const& folders, int depth, std::vector<FolderEntry>& out)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full = folder.GetNamedString(L"full_name", L"");
            hstring name = folder.GetNamedString(L"name", full);
            if (full.empty())
                continue;
            std::wstring label(static_cast<size_t>(depth) * 3, L' ');
            label += name.c_str();
            out.emplace_back(hstring{ label }, full);
            if (folder.HasKey(L"children") && folder.GetNamedValue(L"children").ValueType() == JsonValueType::Array)
                FlattenFolders(folder.GetNamedArray(L"children"), depth + 1, out);
        }
    }

    /**
     * @brief Secondes depuis 1970 d'une date choisie dans un CalendarDatePicker.
     * @param picker sélecteur de date
     * @return secondes depuis 1970 (début du jour), 0 si aucune date n'est choisie
     */
    int64_t PickedDay(CalendarDatePicker const& picker)
    {
        auto date = picker.Date();
        return date ? static_cast<int64_t>(winrt::clock::to_time_t(date.Value())) : 0;
    }

    /**
     * @brief Texte du rapport de fin d'une exportation ou d'une importation.
     * @param answer    réponse JSON du moteur
     * @param exporting true pour une exportation
     * @return texte multi-lignes
     */
    hstring BuildReport(JsonObject const& answer, bool exporting)
    {
        auto count = static_cast<uint32_t>(answer.GetNamedNumber(L"messages", answer.GetNamedNumber(L"count", 0)));
        auto folders = static_cast<uint32_t>(answer.GetNamedNumber(L"folders", 0));
        auto skipped = static_cast<uint32_t>(answer.GetNamedNumber(L"skipped", 0));
        auto failed = static_cast<uint32_t>(answer.GetNamedNumber(L"failed", 0));
        hstring error = answer.GetNamedString(L"error", L"");
        std::wstring text;

        if (exporting)
            text = Fmt(Tr(L"{0} message(s) exporté(s) depuis {1} dossier(s)."), { to_hstring(count), to_hstring(folders) }).c_str();
        else if (answer.HasKey(L"messages"))
            text = Fmt(Tr(L"{0} message(s) importé(s), {1} dossier(s) créé(s)."), { to_hstring(count), to_hstring(folders) }).c_str();
        else
            text = Fmt(Tr(L"{0} élément(s) importé(s)."), { to_hstring(count) }).c_str();

        if (skipped > 0)
            text += L"\n" + std::wstring{ Fmt(Tr(L"{0} élément(s) ignoré(s) (supprimés, déjà présents ou hors période)."), { to_hstring(skipped) }) };
        if (failed > 0)
            text += L"\n" + std::wstring{ Fmt(Tr(L"{0} élément(s) en échec. Première erreur : {1}"), { to_hstring(failed), error }) };
        if (answer.GetNamedBoolean(L"cancelled", false))
            text += L"\n" + std::wstring{ Tr(L"L'opération a été annulée avant la fin.") };
        return hstring{ text };
    }
}

namespace winrt::EvolutionWin::implementation
{
    /* Bouton « Exporter des données » du ruban Fichier : liste les comptes puis ouvre l'assistant. */
    void MainWindow::OnExportDataClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ListAccounts([this](hstring const& json, hstring const& error)
        {
            JsonArray accounts;
            if (!error.empty() || !JsonArray::TryParse(json, accounts))
            {
                ShowStatus(Fmt(Tr(L"Exportation : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ExportDataAsync(accounts);
        });
    }

    /* Bouton « Importer des données » des rubans Fichier : liste les comptes, les carnets d'adresses et
     * les agendas puis ouvre l'assistant. Le Tag du bouton présélectionne la source ("mail", "contacts",
     * "calendar"). */
    void MainWindow::OnImportDataClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        hstring preselect;
        if (auto element = sender.try_as<FrameworkElement>())
            if (auto tag = element.Tag())
                preselect = unbox_value_or<hstring>(tag, L"");

        m_core->ListAccounts([this, preselect](hstring const& json, hstring const& error)
        {
            JsonArray accounts;
            if (!error.empty() || !JsonArray::TryParse(json, accounts))
            {
                ShowStatus(Fmt(Tr(L"Importation : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            m_core->ListSources(L"contacts", [this, accounts, preselect](hstring const& books_json, hstring const&)
            {
                JsonArray books;
                if (!JsonArray::TryParse(books_json, books))
                    books = JsonArray();
                m_core->ListSources(L"calendar,tasks", [this, accounts, books, preselect](hstring const& cal_json, hstring const&)
                {
                    JsonArray calendars;
                    if (!JsonArray::TryParse(cal_json, calendars))
                        calendars = JsonArray();
                    ImportDataAsync(accounts, books, calendars, preselect);
                });
            });
        });
    }

    /* Affiche le rapport de fin d'une exportation ou d'une importation.
     * Paramètres :
     *   title   : titre de la boîte de dialogue
     *   summary : texte du rapport
     *   dest    : dossier de destination à proposer d'ouvrir (vide : aucun bouton) */
    fire_and_forget MainWindow::ShowTransferReportAsync(hstring title, hstring summary, hstring dest)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
        {
            ShowStatus(summary, InfoBarSeverity::Success);
            co_return;
        }
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(title));
        TextBlock text;
        text.Text(summary);
        text.TextWrapping(TextWrapping::Wrap);
        text.IsTextSelectionEnabled(true);
        dialog.Content(text);
        if (!dest.empty())
            dialog.PrimaryButtonText(Tr(L"Ouvrir le dossier"));
        dialog.CloseButtonText(Tr(L"Fermer"));
        dialog.DefaultButton(ContentDialogButton::Close);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result == ContentDialogResult::Primary && !dest.empty())
            co_await Windows::System::Launcher::LaunchFolderPathAsync(dest);
    }

    /* Assistant d'exportation : choix du type de données puis, pour le courrier, du compte, des
     * dossiers, du format et de la période ; les contacts et les agendas passent par leurs assistants.
     * Paramètres :
     *   accounts : comptes [{uid, name, ...}] */
    fire_and_forget MainWindow::ExportDataAsync(JsonArray accounts)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Exporter des données")));
        dialog.PrimaryButtonText(Tr(L"Exporter"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(460);

        ComboBox kind_box;
        kind_box.Header(box_value(Tr(L"Données à exporter")));
        kind_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        kind_box.Items().Append(box_value(Tr(L"Courrier (mbox ou fichiers .eml)")));
        kind_box.Items().Append(box_value(Tr(L"Contacts (vCard)")));
        kind_box.Items().Append(box_value(Tr(L"Agendas et listes de tâches (iCalendar)")));
        kind_box.SelectedIndex(0);
        panel.Children().Append(kind_box);

        StackPanel mail_panel;
        mail_panel.Spacing(10);

        ComboBox account_box;
        account_box.Header(box_value(Tr(L"Compte")));
        account_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : accounts)
            account_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        mail_panel.Children().Append(account_box);

        TextBlock folder_title;
        folder_title.Text(Tr(L"Dossiers à exporter"));
        mail_panel.Children().Append(folder_title);

        ListView folder_list;
        folder_list.SelectionMode(ListViewSelectionMode::Multiple);
        folder_list.Height(190);
        mail_panel.Children().Append(folder_list);

        Button select_all;
        select_all.Content(box_value(Tr(L"Tout sélectionner")));
        select_all.Click([folder_list](IInspectable const&, RoutedEventArgs const&) { folder_list.SelectAll(); });
        mail_panel.Children().Append(select_all);

        CheckBox recursive;
        recursive.Content(box_value(Tr(L"Inclure aussi les sous-dossiers des dossiers choisis")));
        recursive.IsChecked(true);
        mail_panel.Children().Append(recursive);

        RadioButtons format;
        format.Header(box_value(Tr(L"Format")));
        format.Items().Append(box_value(Tr(L"mbox : un fichier par dossier")));
        format.Items().Append(box_value(Tr(L"EML : un fichier par message")));
        format.SelectedIndex(0);
        mail_panel.Children().Append(format);

        CheckBox limit;
        limit.Content(box_value(Tr(L"Limiter à une période (date de réception)")));
        mail_panel.Children().Append(limit);

        StackPanel dates;
        dates.Orientation(Orientation::Horizontal);
        dates.Spacing(10);
        CalendarDatePicker date_from, date_to;
        date_from.Header(box_value(Tr(L"Du")));
        date_to.Header(box_value(Tr(L"Au (inclus)")));
        date_from.IsEnabled(false);
        date_to.IsEnabled(false);
        dates.Children().Append(date_from);
        dates.Children().Append(date_to);
        mail_panel.Children().Append(dates);
        limit.Checked([date_from, date_to](IInspectable const&, RoutedEventArgs const&) { date_from.IsEnabled(true); date_to.IsEnabled(true); });
        limit.Unchecked([date_from, date_to](IInspectable const&, RoutedEventArgs const&) { date_from.IsEnabled(false); date_to.IsEnabled(false); });
        panel.Children().Append(mail_panel);

        kind_box.SelectionChanged([kind_box, mail_panel](IInspectable const&, SelectionChangedEventArgs const&)
        {
            mail_panel.Visibility(kind_box.SelectedIndex() == 0 ? Visibility::Visible : Visibility::Collapsed);
        });

        /* arborescence du compte choisi */
        auto folder_names = std::make_shared<std::vector<FolderEntry>>();
        auto load_folders = [this, folder_list, folder_names, accounts, account_box]()
        {
            int index = account_box.SelectedIndex();
            folder_list.Items().Clear();
            if (index < 0 || static_cast<uint32_t>(index) >= accounts.Size())
                return;
            hstring uid = accounts.GetObjectAt(static_cast<uint32_t>(index)).GetNamedString(L"uid", L"");
            m_core->ListFolders(uid, [folder_list, folder_names](hstring const& json, hstring const&)
            {
                JsonArray tree;
                if (!JsonArray::TryParse(json, tree))
                    return;
                folder_names->clear();
                FlattenFolders(tree, 0, *folder_names);
                folder_list.Items().Clear();
                for (auto const& [label, full] : *folder_names)
                {
                    ListViewItem item;
                    item.Content(box_value(label));
                    item.Tag(box_value(full));
                    folder_list.Items().Append(item);
                }
            });
        };
        account_box.SelectionChanged([load_folders](IInspectable const&, SelectionChangedEventArgs const&) { load_folders(); });
        if (accounts.Size() > 0)
            account_box.SelectedIndex(0);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        if (kind_box.SelectedIndex() == 1)
        {
            OnExportContactsClick(nullptr, nullptr);
            co_return;
        }
        if (kind_box.SelectedIndex() == 2)
        {
            OnExportCalendarClick(nullptr, nullptr);
            co_return;
        }

        int account_index = account_box.SelectedIndex();
        JsonArray folders;
        for (auto const& item : folder_list.SelectedItems())
            if (auto entry = item.try_as<ListViewItem>())
                folders.Append(JsonValue::CreateStringValue(unbox_value<hstring>(entry.Tag())));
        if (account_index < 0 || folders.Size() == 0)
        {
            ShowStatus(Tr(L"Exportation : choisissez un compte et au moins un dossier."), InfoBarSeverity::Warning);
            co_return;
        }

        int64_t from = 0, to = 0;
        if (limit.IsChecked() && limit.IsChecked().Value())
        {
            from = PickedDay(date_from);
            to = PickedDay(date_to);
            if (to > 0)
                to = ::EvolutionWin::DataTransfer::EndOfDay(to);
        }

        Windows::Storage::Pickers::FolderPicker picker;
        InitializePicker(picker);
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
        picker.FileTypeFilter().Append(L"*");
        auto destination = co_await picker.PickSingleFolderAsync();
        if (!destination)
            co_return;
        hstring dest = destination.Path();

        JsonObject request;
        request.SetNamedValue(L"account", JsonValue::CreateStringValue(
            accounts.GetObjectAt(static_cast<uint32_t>(account_index)).GetNamedString(L"uid", L"")));
        request.SetNamedValue(L"folders", folders);
        request.SetNamedValue(L"recursive", JsonValue::CreateBooleanValue(recursive.IsChecked() && recursive.IsChecked().Value()));
        request.SetNamedValue(L"dest", JsonValue::CreateStringValue(dest));
        request.SetNamedValue(L"format", JsonValue::CreateStringValue(format.SelectedIndex() == 1 ? L"eml" : L"mbox"));
        request.SetNamedValue(L"date_from", JsonValue::CreateNumberValue(static_cast<double>(from)));
        request.SetNamedValue(L"date_to", JsonValue::CreateNumberValue(static_cast<double>(to)));

        ShowStatus(Tr(L"Exportation du courrier en cours…"), InfoBarSeverity::Informational);
        m_core->ExportMail(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(Fmt(Tr(L"Exportation du courrier : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ShowTransferReportAsync(Tr(L"Exportation terminée"), BuildReport(answer, true), dest);
        });
    }

    /* Assistant d'importation : choix de la source (mbox / .eml, dossier, profil Thunderbird, vCard,
     * iCalendar, Outlook .pst), de la destination, puis du fichier ou du dossier à importer.
     * Paramètres :
     *   accounts   : comptes de messagerie [{uid, name, ...}]
     *   books      : carnets d'adresses [{uid, name, kind}]
     *   calendars  : agendas et listes de tâches [{uid, name, kind}]
     *   preselect  : source présélectionnée ("mail", "contacts", "calendar", ou vide) */
    fire_and_forget MainWindow::ImportDataAsync(JsonArray accounts, JsonArray books, JsonArray calendars, hstring preselect)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Importer des données")));
        dialog.PrimaryButtonText(Tr(L"Importer"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(460);

        ComboBox source_box;
        source_box.Header(box_value(Tr(L"Source")));
        source_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        source_box.Items().Append(box_value(Tr(L"Courrier : fichier mbox ou message .eml")));
        source_box.Items().Append(box_value(Tr(L"Courrier : dossier (mbox, .eml ou Maildir)")));
        source_box.Items().Append(box_value(Tr(L"Courrier : profil Thunderbird")));
        source_box.Items().Append(box_value(Tr(L"Contacts : fichier vCard (.vcf)")));
        source_box.Items().Append(box_value(Tr(L"Agenda ou tâches : fichier iCalendar (.ics)")));
        source_box.Items().Append(box_value(Tr(L"Outlook : fichier de données (.pst ou .ost)")));
        source_box.SelectedIndex(preselect == L"contacts" ? 3 : (preselect == L"calendar" || preselect == L"tasks") ? 4 : 0);
        panel.Children().Append(source_box);

        /* courrier : compte et dossier de destination */
        StackPanel mail_panel;
        mail_panel.Spacing(10);
        ComboBox account_box;
        account_box.Header(box_value(Tr(L"Compte de destination")));
        account_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : accounts)
            account_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        if (accounts.Size() > 0)
            account_box.SelectedIndex(0);
        mail_panel.Children().Append(account_box);
        TextBox dest_folder;
        dest_folder.Header(box_value(Tr(L"Dossier de destination (créé s'il n'existe pas ; « / » sépare les niveaux)")));
        dest_folder.Text(Tr(L"Importé"));
        mail_panel.Children().Append(dest_folder);
        panel.Children().Append(mail_panel);

        /* profil Thunderbird : dossiers détectés */
        StackPanel tb_panel;
        tb_panel.Spacing(6);
        ComboBox tb_box;
        tb_box.Header(box_value(Tr(L"Dossier de courrier Thunderbird détecté")));
        tb_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        std::vector<::EvolutionWin::DataTransfer::SourceDir> tb_dirs;
        {
            wchar_t appdata[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0)
                tb_dirs = ::EvolutionWin::DataTransfer::FindThunderbirdMailDirs(
                    std::filesystem::path{ appdata } / L"Thunderbird" / L"Profiles");
        }
        for (auto const& dir : tb_dirs)
            tb_box.Items().Append(box_value(hstring{ dir.label }));
        TextBlock tb_note;
        tb_note.TextWrapping(TextWrapping::Wrap);
        tb_note.Opacity(0.7);
        if (tb_dirs.empty())
            tb_note.Text(Tr(L"Aucun profil Thunderbird détecté : vous pourrez choisir le dossier à la main (Mail ou ImapMail du profil)."));
        else
        {
            tb_box.SelectedIndex(0);
            tb_note.Text(Tr(L"Les dossiers et sous-dossiers de ce compte seront recréés sous le dossier de destination."));
        }
        if (!tb_dirs.empty())
            tb_panel.Children().Append(tb_box);
        tb_panel.Children().Append(tb_note);
        panel.Children().Append(tb_panel);

        /* contacts : carnet d'adresses */
        StackPanel book_panel;
        ComboBox book_box;
        book_box.Header(box_value(Tr(L"Carnet d'adresses de destination")));
        book_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : books)
            book_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        if (books.Size() > 0)
            book_box.SelectedIndex(0);
        book_panel.Children().Append(book_box);
        panel.Children().Append(book_panel);

        /* iCalendar : agenda ou liste de tâches */
        StackPanel cal_panel;
        ComboBox cal_box;
        cal_box.Header(box_value(Tr(L"Agenda ou liste de tâches de destination")));
        cal_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : calendars)
            cal_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        if (calendars.Size() > 0)
            cal_box.SelectedIndex(0);
        cal_panel.Children().Append(cal_box);
        panel.Children().Append(cal_panel);

        /* Outlook : explication */
        TextBlock pst_note;
        pst_note.TextWrapping(TextWrapping::Wrap);
        pst_note.Text(Tr(L"Les fichiers .pst et .ost d'Outlook sont un format propriétaire que EvolutionWin ne sait pas lire directement. Convertissez d'abord le fichier en dossiers mbox (par exemple avec l'outil libre readpst, de libpst : « readpst -r archive.pst »), puis importez le dossier obtenu avec « Courrier : dossier ». Pour les contacts et les agendas, exportez-les d'Outlook en .vcf / .ics et importez-les ici."));
        panel.Children().Append(pst_note);

        auto update = [=]()
        {
            int kind = source_box.SelectedIndex();
            mail_panel.Visibility(kind <= 2 ? Visibility::Visible : Visibility::Collapsed);
            tb_panel.Visibility(kind == 2 ? Visibility::Visible : Visibility::Collapsed);
            book_panel.Visibility(kind == 3 ? Visibility::Visible : Visibility::Collapsed);
            cal_panel.Visibility(kind == 4 ? Visibility::Visible : Visibility::Collapsed);
            pst_note.Visibility(kind == 5 ? Visibility::Visible : Visibility::Collapsed);
            dialog.IsPrimaryButtonEnabled(kind != 5);
        };
        source_box.SelectionChanged([update](IInspectable const&, SelectionChangedEventArgs const&) { update(); });
        update();

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        int kind = source_box.SelectedIndex();
        if (kind == 5)
            co_return;

        /* source : fichier ou dossier */
        hstring path;
        if (kind == 2 && tb_box.SelectedIndex() >= 0 && static_cast<size_t>(tb_box.SelectedIndex()) < tb_dirs.size())
        {
            path = hstring{ tb_dirs[static_cast<size_t>(tb_box.SelectedIndex())].path };
        }
        else if (kind == 1 || kind == 2)
        {
            Windows::Storage::Pickers::FolderPicker picker;
            InitializePicker(picker);
            picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");
            auto folder = co_await picker.PickSingleFolderAsync();
            if (!folder)
                co_return;
            path = folder.Path();
        }
        else
        {
            Windows::Storage::Pickers::FileOpenPicker picker;
            InitializePicker(picker);
            picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            if (kind == 0)
            {
                for (auto ext : { L".mbox", L".mbx", L".eml" })
                    picker.FileTypeFilter().Append(ext);
                picker.FileTypeFilter().Append(L"*");
            }
            else if (kind == 3)
                picker.FileTypeFilter().Append(L".vcf");
            else
            {
                picker.FileTypeFilter().Append(L".ics");
                picker.FileTypeFilter().Append(L".ical");
            }
            auto file = co_await picker.PickSingleFileAsync();
            if (!file)
                co_return;
            path = file.Path();
        }

        if (::EvolutionWin::DataTransfer::IsOutlookDataFile(path.c_str()))
        {
            ShowStatus(Tr(L"Les fichiers .pst et .ost ne peuvent pas être lus directement : convertissez-les d'abord en mbox (readpst)."), InfoBarSeverity::Warning);
            co_return;
        }

        JsonObject request;
        request.SetNamedValue(L"path", JsonValue::CreateStringValue(path));

        if (kind <= 2)
        {
            int account_index = account_box.SelectedIndex();
            hstring folder = hstring{ ::EvolutionWin::DataTransfer::NormalizeFolderPath(dest_folder.Text().c_str()) };
            if (account_index < 0 || folder.empty())
            {
                ShowStatus(Tr(L"Importation : choisissez un compte et un dossier de destination."), InfoBarSeverity::Warning);
                co_return;
            }
            request.SetNamedValue(L"account", JsonValue::CreateStringValue(
                accounts.GetObjectAt(static_cast<uint32_t>(account_index)).GetNamedString(L"uid", L"")));
            request.SetNamedValue(L"dest_folder", JsonValue::CreateStringValue(folder));
            ShowStatus(Tr(L"Importation du courrier en cours…"), InfoBarSeverity::Informational);
            m_core->ImportMail(request.Stringify(), [this](hstring const& json, hstring const& error)
            {
                JsonObject answer;
                if (!error.empty() || !JsonObject::TryParse(json, answer))
                {
                    ShowStatus(Fmt(Tr(L"Importation du courrier : {0}"), { error }), InfoBarSeverity::Error);
                    return;
                }
                ShowTransferReportAsync(Tr(L"Importation terminée"), BuildReport(answer, false), L"");
            });
            co_return;
        }

        auto finish = [this](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(Fmt(Tr(L"Importation : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ShowTransferReportAsync(Tr(L"Importation terminée"), BuildReport(answer, false), L"");
        };
        if (kind == 3)
        {
            int index = book_box.SelectedIndex();
            if (index < 0)
            {
                ShowStatus(Tr(L"Aucun carnet d'adresses de destination."), InfoBarSeverity::Warning);
                co_return;
            }
            request.SetNamedValue(L"book_uid", JsonValue::CreateStringValue(
                books.GetObjectAt(static_cast<uint32_t>(index)).GetNamedString(L"uid", L"")));
            ShowStatus(Tr(L"Importation des contacts en cours…"), InfoBarSeverity::Informational);
            m_core->ImportContacts(request.Stringify(), finish);
        }
        else
        {
            int index = cal_box.SelectedIndex();
            if (index < 0)
            {
                ShowStatus(Tr(L"Aucun agenda de destination."), InfoBarSeverity::Warning);
                co_return;
            }
            request.SetNamedValue(L"source_uid", JsonValue::CreateStringValue(
                calendars.GetObjectAt(static_cast<uint32_t>(index)).GetNamedString(L"uid", L"")));
            ShowStatus(Tr(L"Importation de l'agenda en cours…"), InfoBarSeverity::Informational);
            m_core->ImportCalendar(request.Stringify(), finish);
        }
    }
}
