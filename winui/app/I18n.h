#pragma once

/**
 * @file I18n.h
 * @brief Gestionnaire de ressources et d'internationalisation MRT Core / .resw.
 *
 * Ce composant permet de charger les chaînes localisées de l'application
 * via les fichiers Strings/{locale}/Resources.resw compilés dans resources.pri.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <winrt/base.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

namespace EvolutionWin::I18n
{
    /**
     * @brief Calcule la clé de ressource associée à un texte source français.
     *
     * Les textes de l'interface sont identifiés par leur libellé français : la clé est
     * « T_ » suivie du hachage FNV-1a 32 bits (sur les unités UTF-16) en hexadécimal
     * majuscule. Le même calcul est fait par tools\i18n-sync.py, qui génère les catalogues.
     * @param fr Texte source français (tel qu'il figure dans le code ou le XAML).
     * @return Clé de ressource (ex. L"T_1A2B3C4D").
     */
    inline std::wstring KeyFor(std::wstring_view fr)
    {
        std::uint32_t h = 2166136261u;
        for (wchar_t c : fr)
        {
            h ^= static_cast<std::uint32_t>(c) & 0xFFFFu;
            h *= 16777619u;
        }
        wchar_t buf[16];
        swprintf_s(buf, L"T_%08X", h);
        return std::wstring{ buf };
    }

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
     * @param languageOverride Code langue BCP-47 facultatif pour forcer le contexte (ex: L"en-US").
     * @return La chaîne localisée ou le texte de repli.
     */
    inline winrt::hstring GetString(std::wstring_view resourceId, std::wstring_view fallback = L"", std::wstring_view languageOverride = L"")
    {
        // 1. Si une langue est demandée, tenter de résoudre via ResourceManager + ResourceContext configuré
        if (!languageOverride.empty())
        {
            try
            {
                static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager s_manager{ nullptr };
                static bool s_mgr_init = false;
                if (!s_mgr_init)
                {
                    s_manager = winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager();
                    s_mgr_init = true;
                }

                if (s_manager)
                {
                    auto context = s_manager.CreateResourceContext();
                    context.QualifierValues().Insert(L"Language", winrt::hstring{ languageOverride });
                    auto map = s_manager.MainResourceMap();
                    if (map)
                    {
                        // Les ressources resw sont stockées sous 'Resources/<resourceId>' ou directement '<resourceId>'
                        auto candidate = map.TryGetValue(winrt::hstring{ resourceId }, context);
                        if (!candidate)
                        {
                            candidate = map.TryGetValue(winrt::hstring{ L"Resources/" } + winrt::hstring{ resourceId }, context);
                        }
                        if (candidate)
                        {
                            winrt::hstring val = candidate.ValueAsString();
                            if (!val.empty())
                            {
                                return val;
                            }
                        }
                    }
                }
            }
            catch (...)
            {
                // Repli sur le ResourceLoader standard
            }
        }

        // 2. Repli sur le ResourceLoader MRT Core standard
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

