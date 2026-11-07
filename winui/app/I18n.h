#pragma once

/**
 * @file I18n.h
 * @brief Gestionnaire de ressources et d'internationalisation MRT Core / .resw.
 *
 * Ce composant permet de charger les chaînes localisées de l'application
 * via les fichiers Strings/{locale}/Resources.resw compilés dans resources.pri.
 */

#include <string>
#include <string_view>
#include <winrt/base.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

namespace EvolutionWin::I18n
{
    /**
     * @brief Obtient l'instance unique du ResourceLoader MRT Core.
     * @return Référence vers le ResourceLoader (ou instance nulle si non initialisé).
     */
    inline winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader& GetResourceLoader()
    {
        static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader s_loader{ nullptr };
        static bool s_initialized = false;
        if (!s_initialized)
        {
            try
            {
                s_loader = winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader();
            }
            catch (...)
            {
                s_loader = nullptr;
            }
            s_initialized = true;
        }
        return s_loader;
    }

    /**
     * @brief Récupère une chaîne localisée par son identifiant de ressource.
     * @param resourceId Clé de la ressource dans les fichiers .resw (ex: L"SpecialFolder_Inbox").
     * @param fallback Chaîne de repli si la ressource est introuvable ou si le loader échoue.
     * @return La chaîne localisée ou le texte de repli.
     */
    inline winrt::hstring GetString(std::wstring_view resourceId, std::wstring_view fallback = L"")
    {
        auto& loader = GetResourceLoader();
        if (loader)
        {
            try
            {
                winrt::hstring res = loader.GetString(resourceId);
                if (!res.empty())
                {
                    return res;
                }
            }
            catch (...)
            {
                // Repli gracieux
            }
        }
        return winrt::hstring{ fallback };
    }
}
