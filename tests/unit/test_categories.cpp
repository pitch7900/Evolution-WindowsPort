/**
 * @file test_categories.cpp
 * @brief Tests unitaires des catégories unifiées (winui/app/Categories.h).
 *
 * Valide, sur le vrai en-tête de l'application (aucune copie) :
 * - le registre nom -> couleur (ajout, renommage, couleur, suppression, unicité insensible à la casse) ;
 * - les couleurs hexadécimales et la couleur de repli stable ;
 * - la sérialisation JSON du registre et sa résilience face aux données corrompues ;
 * - le découpage / l'assemblage des listes de catégories (champ vCard CATEGORIES) ;
 * - le filtrage par catégorie des listes (courriels, contacts, tâches, événements).
 */

#include "../include/test_framework.h"
#include "../../winui/app/Categories.h"
#include "../../winui/app/ConditionalFormat.h"

using namespace EvolutionWin::Categories;

static void test_registry_defaults_and_lookup(void)
{
    TEST_CASE("Categories: registre standard et recherche insensible a la casse");
    {
        CategoryRegistry registry;
        ASSERT_TRUE(registry.Size() >= 10);
        ASSERT_TRUE(registry.Contains(L"Travail"));
        ASSERT_TRUE(registry.Contains(L"  travail "));
        ASSERT_TRUE(registry.Contains(L"à suivre"));
        ASSERT_FALSE(registry.Contains(L"Inconnue"));

        uint8_t r = 0, g = 0, b = 0;
        ASSERT_TRUE(registry.ColorOf(L"catégorie rouge", r, g, b));
        ASSERT_EQ_INT(232, r);
        ASSERT_EQ_INT(17, g);
        ASSERT_EQ_INT(35, b);

        /* catégorie inconnue : repli stable, non noir */
        uint8_t r2 = 0, g2 = 0, b2 = 0;
        ASSERT_FALSE(registry.ColorOf(L"Projet Inconnu 123", r, g, b));
        ASSERT_FALSE(registry.ColorOf(L"projet inconnu 123", r2, g2, b2));
        ASSERT_TRUE(r == r2 && g == g2 && b == b2);
        ASSERT_TRUE(r != 0 || g != 0 || b != 0);
    }
    TEST_CASE_END();
}

static void test_registry_edit_operations(void)
{
    TEST_CASE("Categories: ajout, renommage, couleur et suppression");
    {
        CategoryRegistry registry{ std::vector<CategoryDefinition>{} };
        ASSERT_EQ_INT(0, registry.Size());

        ASSERT_TRUE(registry.Add(L"  Clients ", 10, 20, 30));
        ASSERT_TRUE(registry.Add(L"Famille", 1, 2, 3));
        ASSERT_EQ_INT(2, registry.Size());
        ASSERT_TRUE(registry.Items()[0].name == L"Clients");

        /* doublons (casse ignorée), noms vides ou avec virgule refusés */
        ASSERT_FALSE(registry.Add(L"CLIENTS", 0, 0, 0));
        ASSERT_FALSE(registry.Add(L"   ", 0, 0, 0));
        ASSERT_FALSE(registry.Add(L"A,B", 0, 0, 0));
        ASSERT_EQ_INT(2, registry.Size());

        /* renommage : couleur et position conservées */
        ASSERT_TRUE(registry.Rename(L"clients", L"Clients VIP"));
        ASSERT_TRUE(registry.Items()[0].name == L"Clients VIP");
        ASSERT_EQ_INT(10, registry.Items()[0].r);
        ASSERT_FALSE(registry.Rename(L"Clients VIP", L"famille"));
        ASSERT_FALSE(registry.Rename(L"Absente", L"X"));
        ASSERT_FALSE(registry.Rename(L"Famille", L"Vide,virgule"));
        /* simple changement de casse accepté */
        ASSERT_TRUE(registry.Rename(L"Famille", L"FAMILLE"));
        ASSERT_TRUE(registry.Items()[1].name == L"FAMILLE");

        ASSERT_TRUE(registry.SetColor(L"famille", 200, 100, 50));
        uint8_t r = 0, g = 0, b = 0;
        ASSERT_TRUE(registry.ColorOf(L"Famille", r, g, b));
        ASSERT_EQ_INT(200, r);
        ASSERT_EQ_INT(100, g);
        ASSERT_EQ_INT(50, b);
        ASSERT_FALSE(registry.SetColor(L"Absente", 1, 1, 1));

        ASSERT_TRUE(registry.Remove(L"clients vip"));
        ASSERT_FALSE(registry.Remove(L"clients vip"));
        ASSERT_EQ_INT(1, registry.Size());
    }
    TEST_CASE_END();
}

static void test_hex_colors_and_shortcuts(void)
{
    TEST_CASE("Categories: couleurs hexadecimales et raccourcis");
    {
        ASSERT_TRUE(FormatHexColor(0, 120, 212) == L"#0078D4");
        uint8_t r = 0, g = 0, b = 0;
        ASSERT_TRUE(ParseHexColor(L"#0078d4", r, g, b));
        ASSERT_EQ_INT(0, r);
        ASSERT_EQ_INT(120, g);
        ASSERT_EQ_INT(212, b);
        ASSERT_TRUE(ParseHexColor(L"FF8C00", r, g, b));
        ASSERT_EQ_INT(255, r);
        ASSERT_FALSE(ParseHexColor(L"#12345", r, g, b));
        ASSERT_FALSE(ParseHexColor(L"#GG0000", r, g, b));
        ASSERT_FALSE(ParseHexColor(L"", r, g, b));

        ASSERT_TRUE(CategoryRegistry::ShortcutForIndex(0) == L"Ctrl+F2");
        ASSERT_TRUE(CategoryRegistry::ShortcutForIndex(10) == L"Ctrl+F12");
        ASSERT_TRUE(CategoryRegistry::ShortcutForIndex(11).empty());
    }
    TEST_CASE_END();
}

static void test_registry_serialization(void)
{
    TEST_CASE("Categories: serialisation JSON du registre et resilience");
    {
        CategoryRegistry registry{ std::vector<CategoryDefinition>{} };
        registry.Add(L"Travail \"urgent\"", 0, 99, 177);
        registry.Add(L"Été \\ plage", 255, 185, 0);
        registry.Add(L"Ça marche", 16, 124, 65);

        std::wstring json = registry.Serialize();
        CategoryRegistry loaded = CategoryRegistry::Deserialize(json);
        ASSERT_EQ_INT(3, loaded.Size());
        ASSERT_TRUE(loaded.Items()[0].name == L"Travail \"urgent\"");
        ASSERT_TRUE(loaded.Items()[1].name == L"Été \\ plage");
        ASSERT_TRUE(loaded.Items()[2].name == L"Ça marche");
        ASSERT_EQ_INT(255, loaded.Items()[1].r);
        ASSERT_EQ_INT(185, loaded.Items()[1].g);
        ASSERT_EQ_INT(124, loaded.Items()[2].g);
        ASSERT_TRUE(loaded.Serialize() == json);

        /* registre vide valide : reste vide (l'utilisateur a tout supprimé) */
        ASSERT_EQ_INT(0, CategoryRegistry::Deserialize(L"[]").Size());

        /* données corrompues : retour au registre standard */
        ASSERT_TRUE(CategoryRegistry::Deserialize(L"n'importe quoi").Size() >= 10);
        ASSERT_TRUE(CategoryRegistry::Deserialize(L"[{\"name\":\"X\"").Size() >= 10);
        ASSERT_TRUE(CategoryRegistry::Deserialize(L"").Size() >= 10);

        /* entrées partielles : doublons et noms invalides ignorés, couleur manquante = repli */
        CategoryRegistry partial = CategoryRegistry::Deserialize(
            L"[{\"name\":\"A\",\"color\":\"#010203\"},{\"name\":\"a\",\"color\":\"#FFFFFF\"},"
            L"{\"name\":\"\",\"color\":\"#000000\"},{\"name\":\"B\"},{\"name\":\"C,D\",\"color\":\"#000000\"}]");
        ASSERT_EQ_INT(2, partial.Size());
        ASSERT_EQ_INT(1, partial.Items()[0].r);
        ASSERT_TRUE(partial.Items()[1].name == L"B");
    }
    TEST_CASE_END();
}

static void test_category_lists(void)
{
    TEST_CASE("Categories: decoupage, assemblage et edition des listes");
    {
        auto names = SplitCategories(L" Travail, Important ,,travail, À suivre ");
        ASSERT_EQ_INT(3, names.size());
        ASSERT_TRUE(names[0] == L"Travail");
        ASSERT_TRUE(names[1] == L"Important");
        ASSERT_TRUE(names[2] == L"À suivre");
        ASSERT_TRUE(JoinCategories(names) == L"Travail, Important, À suivre");
        ASSERT_EQ_INT(0, SplitCategories(L"").size());
        ASSERT_TRUE(JoinCategories({}).empty());

        ASSERT_TRUE(SetCategoryPresent(names, L"Personnel", true));
        ASSERT_FALSE(SetCategoryPresent(names, L"personnel", true));
        ASSERT_FALSE(SetCategoryPresent(names, L"a,b", true));
        ASSERT_EQ_INT(4, names.size());
        ASSERT_TRUE(SetCategoryPresent(names, L"IMPORTANT", false));
        ASSERT_FALSE(SetCategoryPresent(names, L"Important", false));
        ASSERT_EQ_INT(3, names.size());

        ASSERT_TRUE(RenameInList(names, L"travail", L"Boulot"));
        ASSERT_TRUE(names[0] == L"Boulot");
        /* le nouveau nom existe déjà : pas de doublon */
        ASSERT_TRUE(RenameInList(names, L"Boulot", L"Personnel"));
        ASSERT_EQ_INT(2, names.size());
        ASSERT_FALSE(RenameInList(names, L"Absente", L"X"));
    }
    TEST_CASE_END();
}

static void test_category_filter(void)
{
    TEST_CASE("Categories: filtrage par categorie des listes");
    {
        std::vector<std::wstring> none;
        std::vector<std::wstring> work{ L"Travail" };
        std::vector<std::wstring> mixed{ L"Personnel", L"Important" };

        /* filtre vide : tout passe */
        CategoryFilter filter;
        ASSERT_TRUE(filter.IsEmpty());
        ASSERT_TRUE(MatchesCategoryFilter(none, filter));
        ASSERT_TRUE(MatchesCategoryFilter(work, filter));

        /* une catégorie (casse ignorée) */
        ToggleFilterCategory(filter, L"travail");
        ASSERT_FALSE(filter.IsEmpty());
        ASSERT_TRUE(MatchesCategoryFilter(work, filter));
        ASSERT_FALSE(MatchesCategoryFilter(mixed, filter));
        ASSERT_FALSE(MatchesCategoryFilter(none, filter));

        /* logique « ou » entre catégories retenues */
        ToggleFilterCategory(filter, L"Important");
        ASSERT_EQ_INT(2, filter.selected.size());
        ASSERT_TRUE(MatchesCategoryFilter(work, filter));
        ASSERT_TRUE(MatchesCategoryFilter(mixed, filter));

        /* éléments sans catégorie */
        filter.include_uncategorized = true;
        ASSERT_TRUE(MatchesCategoryFilter(none, filter));
        filter.selected.clear();
        ASSERT_FALSE(filter.IsEmpty());
        ASSERT_TRUE(MatchesCategoryFilter(none, filter));
        ASSERT_FALSE(MatchesCategoryFilter(work, filter));

        /* bascule : un second appel retire la catégorie */
        CategoryFilter toggled;
        ToggleFilterCategory(toggled, L"A");
        ToggleFilterCategory(toggled, L"a");
        ASSERT_TRUE(toggled.IsEmpty());
    }
    TEST_CASE_END();
}

static void test_group_by_account(void)
{
    TEST_CASE("Categories: rattachement et regroupement par comptes");
    {
        CategoryRegistry registry{ std::vector<CategoryDefinition>{} };
        registry.Add(L"Local", 1, 2, 3);
        registry.Add(L"Client", 4, 5, 6);
        registry.Add(L"Partage", 7, 8, 9);
        registry.Add(L"Orpheline", 9, 9, 9);
        ASSERT_TRUE(registry.AddAccount(L"Client", L"acc-a"));
        ASSERT_FALSE(registry.AddAccount(L"client", L"acc-a"));   /* doublon, casse ignorée */
        ASSERT_TRUE(registry.AddAccount(L"Partage", L"acc-a"));
        ASSERT_TRUE(registry.AddAccount(L"Partage", L"acc-b"));
        ASSERT_TRUE(registry.AddAccount(L"Orpheline", L"acc-supprime"));
        ASSERT_FALSE(registry.AddAccount(L"Inconnue", L"acc-a"));
        ASSERT_FALSE(registry.AddAccount(L"Local", L""));

        auto groups = GroupByAccount(registry.Items(), { L"acc-a", L"acc-b" });
        ASSERT_EQ_INT(3, groups.size());
        ASSERT_TRUE(groups[0].account.empty());                    /* locales d'abord */
        ASSERT_EQ_INT(2, groups[0].indices.size());                /* Local + Orpheline (compte inconnu) */
        ASSERT_TRUE(groups[1].account == L"acc-a");
        ASSERT_EQ_INT(2, groups[1].indices.size());                /* Client + Partage */
        ASSERT_TRUE(groups[2].account == L"acc-b");
        ASSERT_EQ_INT(1, groups[2].indices.size());                /* Partage dans les deux groupes */

        /* aucun rattachement connu : affichage à plat */
        ASSERT_EQ_INT(0, GroupByAccount(registry.Items(), {}).size());

        /* les rattachements survivent à la sérialisation */
        CategoryRegistry loaded = CategoryRegistry::Deserialize(registry.Serialize());
        ASSERT_EQ_INT(2, loaded.Find(L"Partage")->accounts.size());
        ASSERT_TRUE(loaded.Find(L"Partage")->accounts[1] == L"acc-b");
        ASSERT_EQ_INT(0, loaded.Find(L"Local")->accounts.size());
        ASSERT_TRUE(loaded.Serialize() == registry.Serialize());
    }
    TEST_CASE_END();
}

static void test_conditional_format(void)
{
    TEST_CASE("Mise en forme conditionnelle: regles, domaine, importance, serialisation");
    {
        using namespace EvolutionWin::ConditionalFormat;
        ASSERT_TRUE(DomainOf(L"Jean Dupont <Jean@Exemple.FR>") == L"exemple.fr");
        ASSERT_TRUE(DomainOf(L"jean@exemple.fr") == L"exemple.fr");
        ASSERT_TRUE(DomainOf(L"sans adresse").empty());

        RuleContext mail;
        mail.sender = L"Jean Dupont <jean@mail.exemple.fr>";
        mail.subject = L"Facture de Mars";
        mail.importance = L"high";

        FormatRule r;
        r.field = RuleField::Sender;     r.text = L"dupont";
        ASSERT_TRUE(RuleMatches(r, mail));
        r.text = L"  ";                  ASSERT_FALSE(RuleMatches(r, mail));          /* texte vide */
        r.field = RuleField::SenderDomain; r.text = L"@exemple.fr";
        ASSERT_TRUE(RuleMatches(r, mail));                                              /* sous-domaine */
        r.text = L"emple.fr";            ASSERT_FALSE(RuleMatches(r, mail));          /* pas une frontiere */
        r.field = RuleField::Subject;    r.text = L"FACTURE";
        ASSERT_TRUE(RuleMatches(r, mail));                                              /* casse ignoree */
        r.field = RuleField::Importance; r.text = L"high";
        ASSERT_TRUE(RuleMatches(r, mail));
        r.text = L"low";                 ASSERT_FALSE(RuleMatches(r, mail));
        r.field = RuleField::Organizer;  r.text = L"jean";
        ASSERT_FALSE(RuleMatches(r, mail));                                             /* organisateur absent */
        r.field = RuleField::Subject;    r.text = L"mars"; r.enabled = false;
        ASSERT_FALSE(RuleMatches(r, mail));                                             /* regle inactive */

        /* la premiere regle active qui correspond fixe la couleur */
        std::vector<FormatRule> rules;
        FormatRule a; a.field = RuleField::Subject; a.text = L"absent"; a.r = 1; a.g = 1; a.b = 1;
        FormatRule b; b.field = RuleField::Subject; b.text = L"facture"; b.r = 10; b.g = 20; b.b = 30;
        FormatRule c; c.field = RuleField::Sender;  c.text = L"jean";    c.r = 99; c.g = 99; c.b = 99;
        rules = { a, b, c };
        uint8_t cr = 0, cg = 0, cb = 0;
        ASSERT_TRUE(FindRuleColor(rules, mail, cr, cg, cb));
        ASSERT_EQ_INT(10, cr);
        ASSERT_EQ_INT(30, cb);
        ASSERT_FALSE(FindRuleColor({}, mail, cr, cg, cb));

        /* serialisation : aller-retour, tabulations neutralisees, lignes illisibles ignorees */
        b.text = L"a\tb";
        std::wstring text = Serialize({ a, b });
        auto loaded = Deserialize(text + L"n'importe quoi\n9\t9\t#000000\tx\n");
        ASSERT_EQ_INT(2, loaded.size());
        ASSERT_TRUE(loaded[1].text == L"a b");
        ASSERT_EQ_INT(20, loaded[1].g);
        ASSERT_TRUE(loaded[0].field == RuleField::Subject);
    }
    TEST_CASE_END();
}

int main(void)
{
    TEST_SUITE_BEGIN("Categories unifiees (Categories.h)");

    test_registry_defaults_and_lookup();
    test_registry_edit_operations();
    test_hex_colors_and_shortcuts();
    test_registry_serialization();
    test_category_lists();
    test_category_filter();
    test_group_by_account();
    test_conditional_format();

    return test_suite_summary();
}
