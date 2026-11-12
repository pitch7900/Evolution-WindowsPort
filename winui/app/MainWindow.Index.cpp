#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"

#include <string>

/* Index plein texte local (evo-core-index.c) : réglage de Paramètres > Avancé > Index de recherche,
 * état d'avancement (événement « index-progress » du moteur) et reconstruction.
 *
 * Réglage : search.fulltext-index (booléen, activé par défaut). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Transmet au moteur le réglage d'activation de l'index plein texte.
     */
    void MainWindow::ApplyFullTextIndexSetting()
    {
        if (m_core)
            m_core->IndexSetEnabled(::EvolutionWin::Settings::Instance().GetBoolean(L"search.fulltext-index", true));
    }

    /**
     * @brief Affiche l'état de l'index dans les Paramètres (texte, barre de progression, bouton).
     * @param status Objet d'état renvoyé par le moteur (evo_core_index_status ou événement index-progress).
     */
    void MainWindow::ShowFullTextIndexStatus(JsonObject const& status)
    {
        auto text = FullTextIndexStatusText();
        if (!text)
            return;

        bool enabled = status.GetNamedBoolean(L"enabled", false);
        hstring state = status.GetNamedString(L"state", L"disabled");
        int32_t indexed = static_cast<int32_t>(status.GetNamedNumber(L"messages_indexed", 0));
        int32_t total = static_cast<int32_t>(status.GetNamedNumber(L"messages_total", 0));
        int32_t pass_done = static_cast<int32_t>(status.GetNamedNumber(L"pass_done", 0));
        int32_t pass_total = static_cast<int32_t>(status.GetNamedNumber(L"pass_total", 0));
        double size_bytes = status.GetNamedNumber(L"size_bytes", 0);
        wchar_t size[64]{};
        StrFormatByteSizeW(static_cast<LONGLONG>(size_bytes), size, 64);

        hstring line;
        if (!enabled)
            line = Tr(L"L'index est désactivé : la recherche dans le corps des messages utilise la recherche lente habituelle.");
        else if (state == L"indexing")
        {
            hstring folder = status.GetNamedString(L"current", L"");
            line = pass_total > 0
                ? Fmt(Tr(L"Indexation en cours : dossier {0} sur {1}. {2} message(s) indexé(s), {3} utilisés sur le disque."),
                    { to_hstring(pass_done + 1), to_hstring(pass_total), to_hstring(indexed), hstring{ size } })
                : Fmt(Tr(L"Indexation en cours. {0} message(s) indexé(s), {1} utilisés sur le disque."),
                    { to_hstring(indexed), hstring{ size } });
            if (!folder.empty())
                line = line + L"\n" + Fmt(Tr(L"Dossier en cours : {0}"), { folder });
        }
        else
            line = Fmt(Tr(L"Index à jour : {0} message(s) indexé(s) sur {1}, {2} utilisés sur le disque."),
                { to_hstring(indexed), to_hstring(total), hstring{ size } });
        text.Text(line);

        if (auto bar = FullTextIndexProgress())
        {
            bool busy = enabled && state == L"indexing" && pass_total > 0;
            bar.Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
            if (busy)
                bar.Value(100.0 * pass_done / pass_total);
        }
        if (auto button = FullTextIndexRebuildButton())
            button.IsEnabled(enabled);
    }

    /**
     * @brief Initialise la section de l'index dans les Paramètres : interrupteur et état courant.
     */
    void MainWindow::InitFullTextIndexSettingsView()
    {
        bool previous = m_updating_settings_view;
        m_updating_settings_view = true;
        if (auto toggle = FullTextIndexSwitch())
            toggle.IsOn(::EvolutionWin::Settings::Instance().GetBoolean(L"search.fulltext-index", true));
        m_updating_settings_view = previous;

        if (!m_core)
            return;
        m_core->IndexStatus([this](hstring const& json, hstring const& error)
        {
            JsonObject object{ nullptr };
            if (error.empty() && JsonObject::TryParse(json, object))
                ShowFullTextIndexStatus(object);
        });
    }

    /**
     * @brief Active ou désactive l'index plein texte.
     */
    void MainWindow::OnFullTextIndexToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_initialized || m_updating_settings_view)
            return;

        if (auto toggle = sender.try_as<ToggleSwitch>())
        {
            ::EvolutionWin::Settings::Instance().SetBoolean(L"search.fulltext-index", toggle.IsOn());
            ApplyFullTextIndexSetting();
            InitFullTextIndexSettingsView();
        }
    }

    /**
     * @brief Efface puis reconstruit l'index plein texte.
     */
    void MainWindow::OnFullTextIndexRebuildClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_core)
            return;

        m_core->IndexRebuild([this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Reconstruction de l'index impossible : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ShowStatus(Tr(L"Reconstruction de l'index lancée : elle se poursuit en tâche de fond."), InfoBarSeverity::Informational);
            InitFullTextIndexSettingsView();
        });
    }
}
