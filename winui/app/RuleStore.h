#pragma once

/**
 * @file RuleStore.h
 * @brief Persistance des règles de messagerie et de la mémoire des messages traités
 *        (settings.json, clés « mail.rules » et « mail.rules.processed »).
 *
 * Fait le lien entre la logique pure (Rules.h) et les préférences (Settings.h).
 * À utiliser depuis le thread de l'interface, comme Settings.
 */

#include "Rules.h"
#include "Settings.h"

#include <chrono>

namespace EvolutionWin
{
    /** @brief Accès singleton aux règles de messagerie. */
    class RuleStore
    {
    public:
        /**
         * @brief Règles chargées depuis les réglages au premier accès.
         * @return Référence modifiable ; appeler Save() après toute modification.
         */
        static std::vector<Rules::Rule>& Rules()
        {
            auto& state = State();
            if (!state.loaded)
            {
                state.loaded = true;
                state.rules = Rules::Deserialize(std::wstring{ Settings::Instance().GetString(L"mail.rules", L"") });
            }
            return state.rules;
        }

        /** @brief Enregistre les règles dans les réglages. */
        static void Save()
        {
            Settings::Instance().SetString(L"mail.rules", winrt::hstring{ Rules::Serialize(Rules()) });
        }

        /**
         * @brief Mémoire des messages déjà traités à la réception.
         * @return Référence modifiable ; appeler SaveProcessed() après modification.
         */
        static Rules::ProcessedSet& Processed()
        {
            auto& state = State();
            if (!state.processed_loaded)
            {
                state.processed_loaded = true;
                state.processed = Rules::ProcessedSet::Deserialize(std::wstring{ Settings::Instance().GetString(L"mail.rules.processed", L"") });
            }
            return state.processed;
        }

        /** @brief Enregistre la mémoire des messages traités. */
        static void SaveProcessed()
        {
            Settings::Instance().SetString(L"mail.rules.processed", winrt::hstring{ Processed().Serialize() });
        }

        /**
         * @brief Identifiant unique pour une nouvelle règle (date en millisecondes + compteur).
         * @return Identifiant.
         */
        static std::wstring NewId()
        {
            static unsigned counter = 0;
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            return L"r" + std::to_wstring(now) + L"-" + std::to_wstring(++counter);
        }

        /**
         * @brief Instant présent en secondes Unix.
         * @return Secondes depuis 1970.
         */
        static int64_t NowUnix()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

    private:
        struct StateData
        {
            bool loaded{ false };
            std::vector<Rules::Rule> rules;
            bool processed_loaded{ false };
            Rules::ProcessedSet processed;
        };

        static StateData& State()
        {
            static StateData state;
            return state;
        }
    };
}
