/**
 * @file test_immersive.cpp
 * @brief Tests unitaires de la logique pure du lecteur immersif (ImmersiveReader.h).
 */
#include "../../winui/app/ImmersiveReader.h"

#include <cstdio>

using namespace EvolutionWin::Immersive;

static int g_failures = 0;
static int g_tests = 0;

#define CHECK(cond) do { ++g_tests; if (!(cond)) { ++g_failures; std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond); } } while (0)

/** Les valeurs hors limites sont ramenées dans les bornes. */
static void test_normalize()
{
    Options o = Normalize({ 1, -3, 99 });
    CHECK(o.font_size == kMinFontSize && o.spacing == 0 && o.theme == kThemeCount - 1);
    o = Normalize({ 500, 12, -1 });
    CHECK(o.font_size == kMaxFontSize && o.spacing == kSpacingLevels - 1 && o.theme == 0);
    o = Normalize({});
    CHECK(o.font_size == kDefaultFontSize && o.spacing == kDefaultSpacing && o.theme == kDefaultTheme);
}

/** La taille du texte varie par pas et reste bornée. */
static void test_font_step()
{
    Options o{};
    CHECK(StepFontSize(o, 1).font_size == kDefaultFontSize + kFontSizeStep);
    CHECK(StepFontSize(o, -1).font_size == kDefaultFontSize - kFontSizeStep);
    o.font_size = kMaxFontSize;
    CHECK(StepFontSize(o, 1).font_size == kMaxFontSize);
    o.font_size = kMinFontSize;
    CHECK(StepFontSize(o, -1).font_size == kMinFontSize);
}

/** Espacement et thème tournent en boucle. */
static void test_cycles()
{
    Options o{};
    for (int i = 0; i < kSpacingLevels; ++i)
        o = CycleSpacing(o);
    CHECK(o.spacing == kDefaultSpacing);
    for (int i = 0; i < kThemeCount; ++i)
        o = CycleTheme(o);
    CHECK(o.theme == kDefaultTheme);
    CHECK(LineHeightPercent(0) < LineHeightPercent(3));
    CHECK(LineHeightPercent(-5) == LineHeightPercent(0));
}

/** Les caractères spéciaux sont échappés. */
static void test_escape()
{
    CHECK(EscapeHtml(L"<a href=\"x\">&'") == L"&lt;a href=&quot;x&quot;&gt;&amp;&#39;");
    CHECK(EscapeHtml(L"").empty());
}

/** Paragraphes : lignes vides = séparation, retours simples = <br>, CRLF accepté. */
static void test_paragraphs()
{
    CHECK(ParagraphsToHtml(L"").empty());
    CHECK(ParagraphsToHtml(L"\n\n  \n").empty());
    CHECK(ParagraphsToHtml(L"a\nb") == L"<p>a<br>b</p>\n");
    CHECK(ParagraphsToHtml(L"a\r\n\r\nb\r\n") == L"<p>a</p>\n<p>b</p>\n");
    CHECK(ParagraphsToHtml(L"<b>x</b>") == L"<p>&lt;b&gt;x&lt;/b&gt;</p>\n");
}

/** Le document contient l'objet, l'expéditeur, la taille et le thème, sans script. */
static void test_html()
{
    Options o{ 30, 2, 2 };
    std::wstring html = BuildHtml(L"Sujet <1>", L"Zoe", L"11 nov.", L"Bonjour\n\nMonde", o);
    CHECK(html.find(L"Sujet &lt;1&gt;") != std::wstring::npos);
    CHECK(html.find(L"Zoe · 11 nov.") != std::wstring::npos);
    CHECK(html.find(L"font-size:30px") != std::wstring::npos);
    CHECK(html.find(L"line-height:200%") != std::wstring::npos);
    CHECK(html.find(L"#1b1b1f") != std::wstring::npos);
    CHECK(html.find(L"<p>Bonjour</p>") != std::wstring::npos);
    CHECK(html.find(L"Content-Security-Policy") != std::wstring::npos);
    CHECK(html.find(L"p('close')") != std::wstring::npos);
    /* le texte du message ne peut pas injecter de balise */
    std::wstring piege = BuildHtml(L"<script>x</script>", L"<img src=x>", L"", L"<script>alert(1)</script>", o);
    CHECK(piege.find(L"<script>x") == std::wstring::npos && piege.find(L"<img") == std::wstring::npos);
    CHECK(piege.find(L"<script>alert") == std::wstring::npos);
    std::wstring sans_date = BuildHtml(L"S", L"E", L"", L"x", {});
    CHECK(sans_date.find(L"·") == std::wstring::npos);
}

int main()
{
    test_normalize();
    test_font_step();
    test_cycles();
    test_escape();
    test_paragraphs();
    test_html();
    std::printf("test_immersive : %d verifications, %d echec(s)\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
