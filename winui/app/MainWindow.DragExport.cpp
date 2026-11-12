#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

/**
 * @file MainWindow.DragExport.cpp
 * @brief Glisser-déposer sortant d'un événement de calendrier (.ics) ou d'un contact (.vcf)
 * vers l'Explorateur Windows, le Bureau ou une autre application.
 *
 * Le fichier est généré à la demande par le moteur (exportation filtrée sur l'élément glissé) dans le
 * dossier temporaire de glisser-déposer, nettoyé au démarrage et à la fermeture de l'application.
 */

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /**
         * @brief Ajoute le fichier généré au paquet de données du glisser-déposer puis libère le report.
         * @param deferral Report de l'opération (le système attend sa fin avant d'autoriser le dépôt).
         * @param data Paquet de données de l'opération.
         * @param path Chemin du fichier à glisser.
         * @param ok false si la génération a échoué (rien n'est ajouté).
         * @return Action asynchrone (résultat ignoré).
         */
        Windows::Foundation::IAsyncAction AttachDragFile(
            Microsoft::UI::Xaml::DragOperationDeferral deferral,
            Windows::ApplicationModel::DataTransfer::DataPackage data,
            std::wstring path,
            bool ok)
        {
            if (ok)
            {
                try
                {
                    auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path);
                    data.SetStorageItems(single_threaded_vector<Windows::Storage::IStorageItem>({ file }));
                }
                catch (...)
                {
                }
            }
            deferral.Complete();
        }
    }

    /**
     * @brief Rend un élément du calendrier (bloc de la grille, pastille du mois) glissable vers l'extérieur :
     *        le fichier .ics de l'événement est généré au début du glisser.
     * @param element Élément graphique de l'événement.
     * @param ev Événement au format JSON de la liste du moteur (calendar_uid, uid, summary).
     */
    void MainWindow::EnableEventExportDrag(UIElement const& element, JsonObject const& ev)
    {
        hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
        hstring uid = ev.GetNamedString(L"uid", L"");
        hstring summary = ev.GetNamedString(L"summary", L"");
        if (calendar_uid.empty() || uid.empty())
            return;

        element.CanDrag(true);
        element.DragStarting([this, calendar_uid, uid, summary](UIElement const&, DragStartingEventArgs const& args)
        {
            if (!m_core)
            {
                args.Cancel(true);
                return;
            }

            std::wstring path = GetDragDropTempDir() +
                std::wstring{ SanitizeFileName(summary, Tr(L"Événement")) } + L".ics";
            auto deferral = args.GetDeferral();
            auto data = args.Data();
            data.RequestedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);

            JsonObject request;
            JsonArray uids;
            uids.Append(JsonValue::CreateStringValue(uid));
            request.SetNamedValue(L"source_uid", JsonValue::CreateStringValue(calendar_uid));
            request.SetNamedValue(L"path", JsonValue::CreateStringValue(hstring{ path }));
            request.SetNamedValue(L"uids", uids);

            m_core->ExportCalendar(request.Stringify(), [this, deferral, data, path](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(Fmt(Tr(L"Impossible de glisser l'élément : {0}"), { error }), InfoBarSeverity::Error);
                AttachDragFile(deferral, data, path, error.empty());
            });
        });
    }

    /**
     * @brief Rend une ligne de la liste des contacts glissable vers l'extérieur : le fichier .vcf du contact
     *        est généré au début du glisser.
     * @param element Ligne de la liste des contacts.
     * @param contact Contact au format JSON de la liste du moteur (book_uid, uid, full_name).
     */
    void MainWindow::EnableContactExportDrag(UIElement const& element, JsonObject const& contact)
    {
        hstring book_uid = contact.GetNamedString(L"book_uid", L"");
        hstring uid = contact.GetNamedString(L"uid", L"");
        hstring full_name = contact.GetNamedString(L"full_name", L"");
        if (book_uid.empty() || uid.empty())
            return;

        element.CanDrag(true);
        element.DragStarting([this, book_uid, uid, full_name](UIElement const&, DragStartingEventArgs const& args)
        {
            if (!m_core)
            {
                args.Cancel(true);
                return;
            }

            std::wstring path = GetDragDropTempDir() +
                std::wstring{ SanitizeFileName(full_name, Tr(L"Contact")) } + L".vcf";
            auto deferral = args.GetDeferral();
            auto data = args.Data();
            data.RequestedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);

            JsonObject request;
            JsonArray uids;
            uids.Append(JsonValue::CreateStringValue(uid));
            request.SetNamedValue(L"book_uid", JsonValue::CreateStringValue(book_uid));
            request.SetNamedValue(L"path", JsonValue::CreateStringValue(hstring{ path }));
            request.SetNamedValue(L"per_contact", JsonValue::CreateBooleanValue(false));
            request.SetNamedValue(L"uids", uids);

            m_core->ExportContacts(request.Stringify(), [this, deferral, data, path](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(Fmt(Tr(L"Impossible de glisser l'élément : {0}"), { error }), InfoBarSeverity::Error);
                AttachDragFile(deferral, data, path, error.empty());
            });
        });
    }
}
