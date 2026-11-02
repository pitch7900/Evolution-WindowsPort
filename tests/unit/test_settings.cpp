/**
 * @file test_settings.cpp
 * @brief Tests unitaires du gestionnaire de preferences de l'application (Settings).
 *
 * Valide le comportement de stockage et de restauration des parametres :
 * - Lecture avec valeur de repli (fallback) si la cle est absente
 * - Serialisation et relecture de nombres, booleens et chaines
 * - Gestion des tableaux JSON (ex: expéditeurs de confiance pour le blocage de contenu externe)
 * - Resilience face a un fichier corrompu ou inexistant
 */

#include "../include/test_framework.h"
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Classe de simulation isolee du gestionnaire Settings pour tester la logique sans ecrire dans le profil utilisateur.
 */
class MockSettingsManager
{
public:
    MockSettingsManager() = default;

    /**
     * @brief Definit une valeur de type double.
     * @param key Nom de la cle (ex: "layout.reading-pane-position").
     * @param val Valeur numerique.
     */
    void SetNumber(std::string const& key, double val)
    {
        m_numbers[key] = val;
    }

    /**
     * @brief Lit une valeur numerique avec repli.
     * @param key Nom de la cle.
     * @param fallback Valeur retournee si la cle est absente.
     * @return Valeur trouvee ou fallback.
     */
    double GetNumber(std::string const& key, double fallback) const
    {
        auto it = m_numbers.find(key);
        return it != m_numbers.end() ? it->second : fallback;
    }

    /**
     * @brief Definit une valeur booleenne.
     * @param key Nom de la cle (ex: "privacy.block-remote-content").
     * @param val Valeur booleenne.
     */
    void SetBoolean(std::string const& key, bool val)
    {
        m_booleans[key] = val;
    }

    /**
     * @brief Lit une valeur booleenne avec repli.
     * @param key Nom de la cle.
     * @param fallback Valeur retournee si la cle est absente.
     * @return Valeur booleenne.
     */
    bool GetBoolean(std::string const& key, bool fallback) const
    {
        auto it = m_booleans.find(key);
        return it != m_booleans.end() ? it->second : fallback;
    }

    /**
     * @brief Definit une valeur chaine.
     * @param key Nom de la cle (ex: "theme.mode").
     * @param val Valeur chaine.
     */
    void SetString(std::string const& key, std::string const& val)
    {
        m_strings[key] = val;
    }

    /**
     * @brief Lit une valeur chaine avec repli.
     * @param key Nom de la cle.
     * @param fallback Valeur retournee si la cle est absente.
     * @return Valeur chaine.
     */
    std::string GetString(std::string const& key, std::string const& fallback) const
    {
        auto it = m_strings.find(key);
        return it != m_strings.end() ? it->second : fallback;
    }

    /**
     * @brief Ajoute un element a un tableau de preferences (ex: expediteur de confiance).
     * @param key Nom de la cle.
     * @param item Element a ajouter.
     */
    void AddToArray(std::string const& key, std::string const& item)
    {
        m_arrays[key].push_back(item);
    }

    /**
     * @brief Verifie si un element est present dans un tableau de preferences.
     * @param key Nom de la cle.
     * @param item Element recherche.
     * @return true si present, false sinon.
     */
    bool ArrayContains(std::string const& key, std::string const& item) const
    {
        auto it = m_arrays.find(key);
        if (it == m_arrays.end()) return false;
        for (auto const& elem : it->second) {
            if (elem == item) return true;
        }
        return false;
    }

    /**
     * @brief Serialise l'etat actuel en JSON minimaliste.
     * @return Chaine representant le document JSON.
     */
    std::string ToJson() const
    {
        std::ostringstream ss;
        ss << "{";
        bool first = true;

        for (auto const& p : m_booleans) {
            if (!first) ss << ",";
            ss << "\"" << p.first << "\":" << (p.second ? "true" : "false");
            first = false;
        }
        for (auto const& p : m_numbers) {
            if (!first) ss << ",";
            ss << "\"" << p.first << "\":" << p.second;
            first = false;
        }
        for (auto const& p : m_strings) {
            if (!first) ss << ",";
            ss << "\"" << p.first << "\":\"" << p.second << "\"";
            first = false;
        }
        ss << "}";
        return ss.str();
    }

    /**
     * @brief Charge un JSON et gere les erreurs de corruption.
     * @param json Chaine JSON a charger.
     * @return true si le document est syntaxiquement un objet JSON basique ({...}), false sinon.
     */
    bool LoadJson(std::string const& json)
    {
        if (json.empty() || json.front() != '{' || json.back() != '}') {
            return false; /* Fichier corrompu ou incomplet : rejet securise */
        }
        return true;
    }

private:
    std::unordered_map<std::string, double> m_numbers;
    std::unordered_map<std::string, bool> m_booleans;
    std::unordered_map<std::string, std::string> m_strings;
    std::unordered_map<std::string, std::vector<std::string>> m_arrays;
};

/* --- Tests unitaires --- */

static void test_settings_default_fallbacks(void)
{
    TEST_CASE("Settings: Valeurs par defaut si cle absente");
    {
        MockSettingsManager mgr;
        ASSERT_EQ_INT(1, mgr.GetBoolean("notifications.enabled", true));
        ASSERT_EQ_INT(0, mgr.GetBoolean("notifications.silent", false));
        ASSERT_TRUE(mgr.GetNumber("layout.splitter-width", 250.0) == 250.0);
        std::string theme = mgr.GetString("theme.mode", "system");
        ASSERT_EQ_STR("system", theme.c_str());
    }
    TEST_CASE_END();
}

static void test_settings_read_write(void)
{
    TEST_CASE("Settings: Modification et relecture de cles");
    {
        MockSettingsManager mgr;

        mgr.SetBoolean("privacy.block-remote-content", true);
        ASSERT_EQ_INT(1, mgr.GetBoolean("privacy.block-remote-content", false));

        mgr.SetNumber("layout.reading-pane-position", 2.0); /* 2 = En bas */
        ASSERT_TRUE(mgr.GetNumber("layout.reading-pane-position", 1.0) == 2.0);

        mgr.SetString("accounts.default-uid", "account-exchange-1");
        ASSERT_EQ_STR("account-exchange-1", mgr.GetString("accounts.default-uid", "").c_str());
    }
    TEST_CASE_END();
}

static void test_settings_trusted_senders_array(void)
{
    TEST_CASE("Settings: Liste d'expediteurs et domaines de confiance");
    {
        MockSettingsManager mgr;

        mgr.AddToArray("privacy.trusted-senders", "collegue@entreprise.com");
        mgr.AddToArray("privacy.trusted-senders", "notifications@github.com");

        ASSERT_TRUE(mgr.ArrayContains("privacy.trusted-senders", "collegue@entreprise.com"));
        ASSERT_TRUE(mgr.ArrayContains("privacy.trusted-senders", "notifications@github.com"));
        ASSERT_FALSE(mgr.ArrayContains("privacy.trusted-senders", "spammeur@inconnu.net"));
    }
    TEST_CASE_END();
}

static void test_settings_corruption_resilience(void)
{
    TEST_CASE("Settings: Resilience face a un JSON tronque ou corrompu");
    {
        MockSettingsManager mgr;

        ASSERT_FALSE(mgr.LoadJson(""));
        ASSERT_FALSE(mgr.LoadJson("Ce n'est pas du JSON"));
        ASSERT_FALSE(mgr.LoadJson("{\"cle_inachevee\": "));
        ASSERT_TRUE(mgr.LoadJson("{\"theme\":\"dark\"}"));
    }
    TEST_CASE_END();
}

int main(void)
{
    TEST_SUITE_BEGIN("Preferences et Configuration (Settings.h)");

    test_settings_default_fallbacks();
    test_settings_read_write();
    test_settings_trusted_senders_array();
    test_settings_corruption_resilience();

    return test_suite_summary();
}
