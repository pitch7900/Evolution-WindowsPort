/**
 * @file test_rules.cpp
 * @brief Tests unitaires des règles de messagerie (winui/app/Rules.h) et des fonctions pures
 *        ManageSieve du moteur (winui/core/evo-core-rules-util.h).
 *
 * Valide, sur les vrais en-têtes (aucune copie) :
 * - les critères (expéditeur, domaine, objet, destinataires, en-tête, pièce jointe) et leurs comparaisons ;
 * - le cumul des actions, l'arrêt du traitement, les règles serveur et hors compte ignorées ;
 * - la règle rapide préremplie ;
 * - la sérialisation des réglages et la mémoire des messages traités ;
 * - la conversion vers Microsoft Graph (messageRules) et vers un script Sieve ;
 * - les fonctions pures ManageSieve (PUTSCRIPT, AUTHENTICATE PLAIN, analyse des réponses).
 */

#include "../include/test_framework.h"
#include "../../winui/app/Rules.h"
#include "../../winui/core/evo-core-rules-util.h"

using namespace EvolutionWin::Rules;

/** @brief Message d'exemple utilisé par les tests. */
static MessageData SampleMessage()
{
    MessageData m;
    m.from = L"Éric Dupont <eric@Factures.Exemple.fr>";
    m.to = L"moi@exemple.org";
    m.subject = L"Facture n° 42 du mois";
    return m;
}

/** @brief Fabrique un critère. */
static Condition Cond(Field field, Op op, std::wstring text, std::wstring header = L"")
{
    Condition c;
    c.field = field;
    c.op = op;
    c.text = std::move(text);
    c.header = std::move(header);
    return c;
}

/** @brief Fabrique une action de déplacement. */
static Action MoveTo(std::wstring account, std::wstring folder)
{
    Action a;
    a.type = ActionType::MoveToFolder;
    a.account = std::move(account);
    a.folder = std::move(folder);
    return a;
}

/** @brief Fabrique une action sans dossier. */
static Action Simple(ActionType type, std::wstring category = L"")
{
    Action a;
    a.type = type;
    a.category = std::move(category);
    return a;
}

static void test_conditions(void)
{
    TEST_CASE("Regles: criteres et comparaisons");
    {
        MessageData m = SampleMessage();
        ASSERT_TRUE(ConditionMatches(Cond(Field::Sender, Op::Contains, L"dupont"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Sender, Op::Contains, L"  éric "), m));
        ASSERT_FALSE(ConditionMatches(Cond(Field::Sender, Op::NotContains, L"dupont"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Sender, Op::NotContains, L"martin"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Subject, Op::StartsWith, L"facture"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Subject, Op::EndsWith, L"du mois"), m));
        ASSERT_FALSE(ConditionMatches(Cond(Field::Subject, Op::Equals, L"facture"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Subject, Op::Equals, L"FACTURE n° 42 DU MOIS"), m));
        ASSERT_FALSE(ConditionMatches(Cond(Field::Subject, Op::Contains, L""), m));

        /* domaine : sous-domaines acceptés, « @ » initial toléré */
        ASSERT_TRUE(ConditionMatches(Cond(Field::SenderDomain, Op::Equals, L"exemple.fr"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::SenderDomain, Op::Equals, L"@factures.exemple.fr"), m));
        ASSERT_FALSE(ConditionMatches(Cond(Field::SenderDomain, Op::Equals, L"emple.fr"), m));

        /* destinataires : À seul, puis Cc une fois connu */
        ASSERT_TRUE(ConditionMatches(Cond(Field::Recipients, Op::Contains, L"moi@"), m));
        ASSERT_FALSE(ConditionMatches(Cond(Field::Recipients, Op::Contains, L"copie@"), m));
        m.cc = L"copie@exemple.org";
        ASSERT_TRUE(ConditionMatches(Cond(Field::Recipients, Op::Contains, L"copie@"), m));

        /* en-tête : absent -> seul « ne contient pas » est vrai */
        ASSERT_FALSE(ConditionMatches(Cond(Field::Header, Op::Contains, L"liste", L"List-Id"), m));
        ASSERT_TRUE(ConditionMatches(Cond(Field::Header, Op::NotContains, L"liste", L"List-Id"), m));
        m.headers[L"list-id"] = L"<Liste Dev.exemple.org>";
        ASSERT_TRUE(ConditionMatches(Cond(Field::Header, Op::Contains, L"liste dev", L"list-ID"), m));

        ASSERT_FALSE(ConditionMatches(Cond(Field::HasAttachment, Op::Contains, L""), m));
        m.has_attachment = true;
        ASSERT_TRUE(ConditionMatches(Cond(Field::HasAttachment, Op::Contains, L""), m));
    }
    TEST_CASE_END();
}

static void test_rule_logic_and_outcome(void)
{
    TEST_CASE("Regles: tous / au moins un, actions cumulees, arret et deplacement");
    {
        MessageData m = SampleMessage();

        Rule all;
        all.name = L"Toutes";
        all.conditions = { Cond(Field::Sender, Op::Contains, L"dupont"), Cond(Field::Subject, Op::Contains, L"facture") };
        ASSERT_TRUE(RuleMatches(all, m));
        all.conditions.push_back(Cond(Field::Subject, Op::Contains, L"absent"));
        ASSERT_FALSE(RuleMatches(all, m));
        all.match_all = false;
        ASSERT_TRUE(RuleMatches(all, m));

        Rule empty;
        ASSERT_FALSE(RuleMatches(empty, m));

        /* 1re règle : lu + catégorie ; 2e : déplacement ; 3e : jamais atteinte (le message a quitté le dossier) */
        Rule r1;
        r1.conditions = { Cond(Field::Sender, Op::Contains, L"dupont") };
        r1.actions = { Simple(ActionType::MarkRead), Simple(ActionType::AssignCategory, L"Factures"), Simple(ActionType::AssignCategory, L"factures") };
        Rule r2;
        r2.conditions = { Cond(Field::Subject, Op::Contains, L"facture") };
        r2.actions = { Simple(ActionType::MarkFlagged), MoveTo(L"acc", L"INBOX/Factures"), MoveTo(L"acc", L"Autre") };
        Rule r3;
        r3.conditions = { Cond(Field::Subject, Op::Contains, L"facture") };
        r3.actions = { Simple(ActionType::Delete) };

        Outcome o = Evaluate({ r1, r2, r3 }, m, L"acc", 1000, true);
        ASSERT_TRUE(o.matched);
        ASSERT_TRUE(o.mark_read);
        ASSERT_TRUE(o.mark_flagged);
        ASSERT_EQ_INT(1, o.categories.size());
        ASSERT_TRUE(o.has_move);
        ASSERT_TRUE(o.move.folder == L"INBOX/Factures");
        ASSERT_FALSE(o.remove);

        /* arrêt demandé : la règle suivante n'ajoute rien */
        r1.stop = true;
        o = Evaluate({ r1, r2 }, m, L"acc", 1000, true);
        ASSERT_TRUE(o.mark_read);
        ASSERT_FALSE(o.has_move);
        ASSERT_FALSE(o.mark_flagged);

        /* règles ignorées : désactivée, serveur, autre compte */
        Rule off = r2;
        off.enabled = false;
        Rule server = r2;
        server.server = true;
        Rule other = r2;
        other.account = L"autre";
        o = Evaluate({ off, server, other }, m, L"acc", 1000, true);
        ASSERT_FALSE(o.matched);

        /* suppression, et copies conservées avant déplacement */
        Rule copy;
        copy.conditions = { Cond(Field::Subject, Op::Contains, L"facture") };
        Action c;
        c.type = ActionType::CopyToFolder;
        c.account = L"acc";
        c.folder = L"Archives";
        copy.actions = { c, Simple(ActionType::Delete) };
        o = Evaluate({ copy }, m, L"acc", 1000, true);
        ASSERT_TRUE(o.remove);
        ASSERT_EQ_INT(1, o.copies.size());

        /* date : un message antérieur à la création de la règle n'est traité qu'à la demande */
        Rule recent = r1;
        recent.stop = false;
        recent.since = 5000;
        ASSERT_FALSE(Evaluate({ recent }, m, L"acc", 1000, true).matched);
        ASSERT_TRUE(Evaluate({ recent }, m, L"acc", 1000, false).matched);
        ASSERT_TRUE(Evaluate({ recent }, m, L"acc", 6000, true).matched);

        ASSERT_TRUE(NeedsDetails(Rule{ L"", L"", true, true, false, false, L"", 0, { Cond(Field::Header, Op::Contains, L"x", L"X-A") }, {} }));
        ASSERT_FALSE(NeedsDetails(r1));
    }
    TEST_CASE_END();
}

static void test_quick_rule(void)
{
    TEST_CASE("Regles: regle rapide preremplie et objet nettoye");
    {
        ASSERT_TRUE(AddressOf(L"Éric <eric@exemple.fr>") == L"eric@exemple.fr");
        ASSERT_TRUE(AddressOf(L"eric@exemple.fr") == L"eric@exemple.fr");
        ASSERT_TRUE(AddressOf(L"Sans adresse").empty());
        ASSERT_TRUE(DomainOf(L"X <a@Exemple.FR>") == L"exemple.fr");

        ASSERT_TRUE(CleanSubject(L"RE: TR : Fwd:  Bonjour") == L"Bonjour");
        ASSERT_TRUE(CleanSubject(L"Reponse sans prefixe") == L"Reponse sans prefixe");
        ASSERT_TRUE(CleanSubject(L"Re:") == L"Re:");

        Rule rule = MakeQuickRule(L"Éric <eric@exemple.fr>", L"Re: Rapport", L"acc");
        ASSERT_EQ_INT(2, rule.conditions.size());
        ASSERT_TRUE(rule.conditions[0].field == Field::Sender);
        ASSERT_TRUE(rule.conditions[0].text == L"eric@exemple.fr");
        ASSERT_TRUE(rule.conditions[1].text == L"Rapport");
        ASSERT_TRUE(rule.account == L"acc");
        ASSERT_FALSE(rule.name.empty());
        ASSERT_TRUE(rule.actions.empty());
    }
    TEST_CASE_END();
}

static void test_serialization(void)
{
    TEST_CASE("Regles: serialisation des reglages et messages traites");
    {
        Rule a;
        a.id = L"r1";
        a.name = L"Nom\tavec\ttab\net \\ antislash";
        a.account = L"compte-1";
        a.stop = true;
        a.since = 1700000000;
        a.match_all = false;
        a.conditions = { Cond(Field::Header, Op::EndsWith, L"v\talue", L"X-Spam"), Cond(Field::HasAttachment, Op::Contains, L"") };
        Action cat = Simple(ActionType::AssignCategory, L"Travail");
        a.actions = { MoveTo(L"compte-1", L"INBOX/Sous dossier"), cat };
        Rule b;
        b.id = L"r2";
        b.enabled = false;
        b.server = true;

        std::wstring text = Serialize({ a, b });
        ASSERT_FALSE(text.find(L"\t\t\t\t\t\t\t\t\t") != std::wstring::npos);
        auto loaded = Deserialize(text + L"n'importe quoi\nC\t99\t0\tx\ty\nA\t9\tx\ty\tz\n");
        ASSERT_EQ_INT(2, loaded.size());
        ASSERT_TRUE(loaded[0].name == a.name);
        ASSERT_TRUE(loaded[0].account == L"compte-1");
        ASSERT_TRUE(loaded[0].stop && !loaded[0].match_all && !loaded[0].server);
        ASSERT_TRUE(loaded[0].since == 1700000000);
        ASSERT_EQ_INT(2, loaded[0].conditions.size());
        ASSERT_TRUE(loaded[0].conditions[0].text == L"v\talue");
        ASSERT_TRUE(loaded[0].conditions[0].op == Op::EndsWith);
        ASSERT_EQ_INT(2, loaded[0].actions.size());
        ASSERT_TRUE(loaded[0].actions[0].folder == L"INBOX/Sous dossier");
        ASSERT_TRUE(loaded[0].actions[1].category == L"Travail");
        ASSERT_FALSE(loaded[1].enabled);
        ASSERT_TRUE(loaded[1].server);
        ASSERT_TRUE(Deserialize(L"").empty());

        ProcessedSet set;
        auto key = ProcessedSet::Key(L"acc", L"INBOX", L"12");
        ASSERT_TRUE(set.Add(key));
        ASSERT_FALSE(set.Add(key));
        ASSERT_TRUE(set.Contains(key));
        auto restored = ProcessedSet::Deserialize(set.Serialize());
        ASSERT_TRUE(restored.Contains(key));
        for (int i = 0; i < 3100; ++i)
            set.Add(ProcessedSet::Key(L"a", L"f", std::to_wstring(i)));
        ASSERT_EQ_INT(ProcessedSet::Capacity, set.Size());
        ASSERT_FALSE(set.Contains(key));
        ASSERT_TRUE(set.Contains(ProcessedSet::Key(L"a", L"f", L"3099")));
    }
    TEST_CASE_END();
}

static void test_graph_conversion(void)
{
    TEST_CASE("Regles: conversion vers Microsoft Graph (messageRules)");
    {
        Rule r;
        r.name = L"Factures \"pro\"";
        r.account = L"acc";
        r.server = true;
        r.stop = true;
        r.conditions = { Cond(Field::Sender, Op::Contains, L"compta@"), Cond(Field::Subject, Op::Contains, L"facture"),
                         Cond(Field::SenderDomain, Op::Equals, L"exemple.fr") };
        r.actions = { MoveTo(L"acc", L"INBOX/Factures"), Simple(ActionType::AssignCategory, L"Pro"), Simple(ActionType::MarkRead) };

        /* deux critères « expéditeur » en « tous » : non convertible */
        auto bad = ToGraphRules(r, 1);
        ASSERT_TRUE(bad.rules.empty());
        ASSERT_FALSE(bad.reason.empty());

        r.conditions.pop_back();
        auto ok = ToGraphRules(r, 3);
        ASSERT_EQ_INT(1, ok.rules.size());
        const std::wstring& j = ok.rules[0];
        ASSERT_TRUE(j.find(L"\"displayName\":\"[Evolution] Factures \\\"pro\\\"\"") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"sequence\":3") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"senderContains\":[\"compta@\"]") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"subjectContains\":[\"facture\"]") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"moveToFolder\":\"INBOX/Factures\"") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"assignCategories\":[\"Pro\"]") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"markAsRead\":true") != std::wstring::npos);
        ASSERT_TRUE(j.find(L"\"stopProcessingRules\":true") != std::wstring::npos);

        /* « ne contient pas » -> exceptions */
        r.conditions.push_back(Cond(Field::Subject, Op::NotContains, L"rappel"));
        auto with_exception = ToGraphRules(r, 1);
        ASSERT_EQ_INT(1, with_exception.rules.size());
        ASSERT_TRUE(with_exception.rules[0].find(L"\"exceptions\":{\"subjectContains\":[\"rappel\"]}") != std::wstring::npos);

        /* « au moins un » : une règle serveur par critère */
        Rule any = r;
        any.match_all = false;
        any.conditions = { Cond(Field::Sender, Op::Equals, L"a@b.fr"), Cond(Field::SenderDomain, Op::Contains, L"c.fr") };
        auto split = ToGraphRules(any, 5);
        ASSERT_EQ_INT(2, split.rules.size());
        ASSERT_TRUE(split.rules[0].find(L"\"fromAddresses\":[{\"emailAddress\":{\"address\":\"a@b.fr\"}}]") != std::wstring::npos);
        ASSERT_TRUE(split.rules[1].find(L"\"senderContains\":[\"@c.fr\"]") != std::wstring::npos);
        ASSERT_TRUE(split.rules[1].find(L"\"sequence\":6") != std::wstring::npos);
        ASSERT_TRUE(split.rules[1].find(L"(2/2)") != std::wstring::npos);

        /* limites de Graph */
        Rule flag = r;
        flag.actions = { Simple(ActionType::MarkFlagged) };
        ASSERT_TRUE(ToGraphRules(flag, 1).rules.empty());
        Rule foreign = r;
        foreign.actions = { MoveTo(L"autre", L"INBOX") };
        ASSERT_TRUE(ToGraphRules(foreign, 1).rules.empty());
        Rule starts = r;
        starts.conditions = { Cond(Field::Subject, Op::StartsWith, L"x") };
        ASSERT_TRUE(ToGraphRules(starts, 1).rules.empty());
        Rule none;
        ASSERT_TRUE(ToGraphRules(none, 1).rules.empty());

        /* plan d'un compte : séquences consécutives, règles locales et d'un autre compte ignorées */
        Rule local = r;
        local.server = false;
        Rule elsewhere = r;
        elsewhere.account = L"autre";
        Rule header;
        header.name = L"Liste";
        header.account = L"acc";
        header.server = true;
        header.conditions = { Cond(Field::Header, Op::Contains, L"dev", L"List-Id"), Cond(Field::HasAttachment, Op::Contains, L"") };
        header.actions = { Simple(ActionType::Delete) };
        auto plan = PlanGraphSync({ local, elsewhere, any, flag, header }, L"acc");
        ASSERT_EQ_INT(3, plan.rules.size());
        ASSERT_EQ_INT(1, plan.skipped.size());
        ASSERT_TRUE(plan.rules[2].find(L"\"sequence\":3") != std::wstring::npos);
        ASSERT_TRUE(plan.rules[2].find(L"\"headerContains\":[\"List-Id: dev\"]") != std::wstring::npos);
        ASSERT_TRUE(plan.rules[2].find(L"\"hasAttachments\":true") != std::wstring::npos);
        ASSERT_TRUE(plan.rules[2].find(L"\"delete\":true") != std::wstring::npos);
        std::wstring array = GraphRulesArray(plan);
        ASSERT_TRUE(array.front() == L'[' && array.back() == L']');
    }
    TEST_CASE_END();
}

static void test_sieve_generation(void)
{
    TEST_CASE("Regles: generation du script Sieve");
    {
        Rule r;
        r.name = L"Factures";
        r.account = L"acc";
        r.server = true;
        r.conditions = { Cond(Field::Sender, Op::Contains, L"compta@"), Cond(Field::Subject, Op::StartsWith, L"Facture \"n\""),
                         Cond(Field::Recipients, Op::NotContains, L"x@y") };
        Action copy;
        copy.type = ActionType::CopyToFolder;
        copy.folder = L"Archives";
        r.actions = { Simple(ActionType::MarkRead), Simple(ActionType::AssignCategory, L"Pro"), copy, MoveTo(L"acc", L"INBOX.Factures") };

        auto script = ToSieve({ r }, L"acc");
        ASSERT_EQ_INT(1, script.rule_count);
        const std::wstring& t = script.text;
        ASSERT_TRUE(t.find(L"require [\"copy\", \"fileinto\", \"imap4flags\"];") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"if allof (header :contains \"from\" \"compta@\"") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"header :matches \"subject\" \"Facture \\\"n\\\"*\"") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"not header :contains [\"to\", \"cc\"] \"x@y\"") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"addflag \"\\\\Seen\";") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"addflag \"Pro\";") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"fileinto :copy \"Archives\";") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"fileinto \"INBOX.Factures\";") != std::wstring::npos);
        ASSERT_TRUE(t.find(L"stop;") != std::wstring::npos);
        /* les indicateurs précèdent le classement */
        ASSERT_TRUE(t.find(L"addflag \"Pro\"") < t.find(L"fileinto \"INBOX.Factures\""));

        Rule any = r;
        any.match_all = false;
        any.name = L"Autre";
        any.conditions = { Cond(Field::SenderDomain, Op::Equals, L"@exemple.fr"), Cond(Field::Header, Op::Equals, L"oui", L"X-Test") };
        any.actions = { Simple(ActionType::Delete) };
        Rule bad = r;
        bad.actions = { MoveTo(L"autre", L"INBOX") };
        Rule local = r;
        local.server = false;
        auto two = ToSieve({ local, any, bad }, L"acc");
        ASSERT_EQ_INT(1, two.rule_count);
        ASSERT_EQ_INT(1, two.skipped.size());
        ASSERT_TRUE(two.text.find(L"if anyof (address :domain :is \"from\" \"exemple.fr\"") != std::wstring::npos);
        ASSERT_TRUE(two.text.find(L"header :is \"X-Test\" \"oui\"") != std::wstring::npos);
        ASSERT_TRUE(two.text.find(L"fileinto \"Trash\";") != std::wstring::npos);
        ASSERT_TRUE(two.text.find(L"\"imap4flags\"") == std::wstring::npos);

        ASSERT_TRUE(ToSieve({ local }, L"acc").text.empty());
    }
    TEST_CASE_END();
}

static void test_managesieve_helpers(void)
{
    TEST_CASE("Regles: fonctions pures ManageSieve (evo-core-rules-util.h)");
    {
        char buffer[512];
        ASSERT_TRUE(rules_sieve_quote("a\"b\\c", buffer, sizeof (buffer)) == 9);
        ASSERT_EQ_STR("\"a\\\"b\\\\c\"", buffer);
        ASSERT_TRUE(rules_sieve_quote("trop long pour le tampon", buffer, 8) == 0);

        size_t length = rules_sieve_putscript_command("evolution", "keep;", buffer, sizeof (buffer));
        ASSERT_TRUE(length == strlen(buffer));
        ASSERT_EQ_STR("PUTSCRIPT \"evolution\" {5+}\r\nkeep;\r\n", buffer);
        ASSERT_TRUE(rules_sieve_putscript_command("evolution", "keep;", buffer, 10) == 0);
        ASSERT_TRUE(rules_sieve_putscript_command(NULL, "keep;", buffer, sizeof (buffer)) == 0);

        /* base64 : vecteurs de la RFC 4648 */
        ASSERT_TRUE(rules_base64_encode((const unsigned char *) "", 0, buffer, sizeof (buffer)) == 0 || buffer[0] == '\0');
        rules_base64_encode((const unsigned char *) "f", 1, buffer, sizeof (buffer));
        ASSERT_EQ_STR("Zg==", buffer);
        rules_base64_encode((const unsigned char *) "fo", 2, buffer, sizeof (buffer));
        ASSERT_EQ_STR("Zm8=", buffer);
        rules_base64_encode((const unsigned char *) "foobar", 6, buffer, sizeof (buffer));
        ASSERT_EQ_STR("Zm9vYmFy", buffer);
        ASSERT_TRUE(rules_base64_encode((const unsigned char *) "foobar", 6, buffer, 4) == 0);

        /* SASL PLAIN : \0user\0password */
        rules_sieve_authenticate_plain("tim", "tanstaaftanstaaf", buffer, sizeof (buffer));
        ASSERT_EQ_STR("AUTHENTICATE \"PLAIN\" \"AHRpbQB0YW5zdGFhZnRhbnN0YWFm\"\r\n", buffer);
        ASSERT_TRUE(rules_sieve_authenticate_plain("", "x", buffer, sizeof (buffer)) == 0);

        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_OK, rules_sieve_response_class("OK"));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_OK, rules_sieve_response_class("ok \"Authentication complete\""));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_NO, rules_sieve_response_class("NO (QUOTA) \"trop gros\""));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_BYE, rules_sieve_response_class("BYE"));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_OTHER, rules_sieve_response_class("\"IMPLEMENTATION\" \"Dovecot\""));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_OTHER, rules_sieve_response_class("NOTE"));
        ASSERT_EQ_INT(RULES_SIEVE_RESPONSE_OTHER, rules_sieve_response_class(NULL));

        ASSERT_EQ_INT(123, rules_sieve_literal_size("NO {123}"));
        ASSERT_EQ_INT(7, rules_sieve_literal_size("\"SIEVE\" {7+}"));
        ASSERT_EQ_INT(-1, rules_sieve_literal_size("OK"));
        ASSERT_EQ_INT(-1, rules_sieve_literal_size("{}"));
        ASSERT_EQ_INT(-1, rules_sieve_literal_size("abc{12"));
    }
    TEST_CASE_END();
}

int main(void)
{
    TEST_SUITE_BEGIN("Regles de messagerie (Rules.h, evo-core-rules-util.h)");

    test_conditions();
    test_rule_logic_and_outcome();
    test_quick_rule();
    test_serialization();
    test_graph_conversion();
    test_sieve_generation();
    test_managesieve_helpers();

    return test_suite_summary();
}
