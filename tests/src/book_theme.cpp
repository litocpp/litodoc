#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;
import lito.book;
import lito.site;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::path::PathBuf;
using namespace lito::site;

namespace
{
auto theme_path(ref<rstd::path::Path> root, ref<str> name) -> PathBuf {
    return PathBuf::from(root).join(name);
}
void theme_write(ref<rstd::path::Path> root, ref<str> name, ref<str> text) {
    auto path = theme_path(root, name);
    rstd::fs::create_dir_all(path.as_path().parent().unwrap()).unwrap();
    rstd::fs::write(path.as_path(), text.as_bytes()).unwrap();
}
auto theme_read(ref<rstd::path::Path> root, ref<str> name) -> String {
    return rstd::fs::read_to_string(theme_path(root, name).as_path()).unwrap();
}
auto theme_manifest(ref<str> version = "2"_str, bool complete = true) -> String {
    return rstd::format(
        "{{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,"
        "\"capabilities\":[\"book\"],\"book-data-api\":1,{}\"templates\":{{\"book-root\":\"root."
        "html\","
        "\"book-page\":\"page.html\"{}}},\"partials\":[\"end.html\"],\"assets\":[{{\"path\":\"skin."
        "css\",\"media-type\":\"text/css\"}}]}}",
        version.is_empty() ? String {} : rstd::format("\"book-template-api\":{},", version),
        complete ? ",\"book-not-found\":\"missing.html\",\"book-redirect\":\"moved.html\""_str
                 : ""_str);
}
auto make_theme_book() -> rstd::fs::TempDir {
    auto root = rstd::fs::TempDir::make("litobook-theme-test"_str).unwrap();
    theme_write(root.path(),
                "book.toml"_str,
                "[book]\nname='theme'\ntitle='Theme Book'\ndescription='Book description'\n"
                "[output.html]\nfrontend='theme'\nsite-url='https://example.org/manual/'\n"
                "repository-url='https://forge.example.org/project'\nedit-url='https://"
                "forge.example.org/edit/main/{path}'\n"
                "[[output.html.navigation]]\nlabel='Home'\nhref='index.html'\n"
                "[output.html.footer]\ntext='Footer data'\n"
                "[output.html.redirects]\n'old.html'='chapter.md#part'\n"_str);
    theme_write(
        root.path(), "src/SUMMARY.md"_str, "- [Home](index.md)\n- [Chapter](chapter.md)\n"_str);
    theme_write(root.path(), "src/index.md"_str, "# Home\n\nBody **content**.\n"_str);
    theme_write(root.path(), "src/chapter.md"_str, "# Chapter\n\n## Part\n\nSecond page.\n"_str);
    theme_write(root.path(), "theme/frontend.json"_str, theme_manifest().as_str());
    auto page =
        "<!doctype html><html><head><link rel=\"stylesheet\" "
        "href=\"{{page.asset_prefix}}skin.css\">"
        "{{#if page.description}}<meta name=\"description\" "
        "content=\"{{page.description}}\">{{/if}}"
        "{{#if page.canonical_url}}<link rel=\"canonical\" "
        "href=\"{{page.canonical_url}}\">{{/if}}"
        "</head><body data-template=\"{{page.kind}}\"><header>Independent "
        "theme</header><h1>{{page.title}}</h1>"
        "<article>{{{page.content}}}</article><small>{{page.source}}</small>{{#if page.icon_url}}has-icon{{else}}no-icon{{/if}}{{> end.html}}</body></html>"_str;
    theme_write(root.path(), "theme/root.html"_str, page);
    theme_write(root.path(), "theme/page.html"_str, page);
    theme_write(root.path(),
                "theme/end.html"_str,
                "<footer>{{#if site.brand}}{{site.brand.label}}{{else}}My brand{{/if}}"
                "{{#each site.navigation}}<a href=\"{{href}}\">{{label}}</a>{{/each}}"
                "{{#if site.repository}}<a href=\"{{site.repository.href}}\">My source</a>{{/if}}"
                "{{#if site.footer}}{{site.footer.text}}{{/if}}</footer>"_str);
    theme_write(root.path(), "theme/skin.css"_str, "body { color: navy; }"_str);
    theme_write(
        root.path(),
        "theme/missing.html"_str,
        "<!doctype html><html><head><base href=\"{{page.base_href}}\"><link rel=\"stylesheet\" "
        "href=\"skin.css\">"
        "</head><body data-template=\"{{page.kind}}\">Custom missing <a href=\"{{page.root_url}}\">Home</a>{{> end.html}}</body></html>"_str);
    theme_write(
        root.path(),
        "theme/moved.html"_str,
        "<!doctype html><meta http-equiv=\"refresh\" content=\"0;url={{page.redirect.href}}\">"
        "<p data-template=\"{{page.kind}}\">Custom moved <a href=\"{{page.redirect.href}}\">Next</a></p>"_str);
    return root;
}
auto load_test_theme(ref<rstd::path::Path> root) -> Result<FrontendBundle, String> {
    return load_frontend_directory(theme_path(root, "theme"_str).as_path());
}
} // namespace

TEST(ThemeTemplates, NestedElseEachPartialAndEscaping) {
    TemplateSet templates { .identity = "test"_Str };
    templates.documents.insert(
        "root"_Str,
        parse_template(
            "root"_str,
            "{{#if enabled}}yes{{else}}{{#each items}}{{#if this}}{{this}}{{else}}empty{{/if}}{{/each}}{{> part}}{{/if}}"_str)
            .unwrap());
    templates.documents.insert(
        "part"_Str,
        parse_template("part"_str, "{{#if choice}}chosen{{else}}fallback{{/if}}"_str).unwrap());
    auto context = TemplateValue::object_value();
    context.insert("enabled"_str, TemplateValue::boolean_value(false));
    context.insert("choice"_str, TemplateValue::null());
    auto items = TemplateValue::array_value();
    items.array.push(TemplateValue::text_value("<one>"_str));
    items.array.push(TemplateValue::null());
    context.insert("items"_str, rstd::move(items));
    EXPECT_EQ(render_template(templates, "root"_str, context).unwrap(),
              "&lt;one&gt;emptyfallback"_str);
    context.insert("enabled"_str, TemplateValue::boolean_value(true));
    EXPECT_EQ(render_template(templates, "root"_str, context).unwrap(), "yes"_str);
}

TEST(ThemeTemplates, InvalidElseReportsLocationAndRawRemainsRestricted) {
    for (auto input : array<ref<str>, 5> { "{{else}}"_str,
                                           "{{#each items}}{{else}}{{/each}}"_str,
                                           "{{#if enabled}}yes{{else}}no{{else}}again{{/if}}"_str,
                                           "{{#if enabled}}{{else}}missing"_str,
                                           "{{#if enabled}}{{else}}{{/each}}"_str }) {
        auto parsed = parse_template("bad.html"_str, input);
        EXPECT_TRUE(parsed.is_err());
        EXPECT_TRUE(parsed.unwrap_err().as_str().contains("bad.html:"_str));
    }
    TemplateSet templates { .identity = "test"_Str };
    templates.documents.insert(
        "raw"_Str,
        parse_template("raw"_str, "{{#if choice}}yes{{else}}{{{text}}}{{/if}}"_str).unwrap());
    auto context = TemplateValue::object_value();
    context.insert("choice"_str, TemplateValue::null());
    context.insert("text"_str, TemplateValue::text_value("<script>bad()</script>"_str));
    EXPECT_TRUE(render_template(templates, "raw"_str, context).is_err());
}

TEST(BookTheme, CompleteThemeOwnsAllPagesAndResourcesWithoutDefaultShell) {
    auto book     = make_theme_book();
    auto prepared = lito::book::prepare({ .directory = PathBuf::from(book.path()) });
    ASSERT_TRUE(prepared.is_ok());
    EXPECT_EQ(prepared->frontend.book_template_api, usize(2));
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    auto root = theme_read(book.path(), "build/book/index.html"_str);
    EXPECT_TRUE(root.as_str().contains("Independent theme"_str));
    EXPECT_TRUE(root.as_str().contains("<strong>content</strong>"_str));
    EXPECT_TRUE(root.as_str().contains("<footer>My brand"_str));
    EXPECT_TRUE(root.as_str().contains("My source"_str));
    EXPECT_TRUE(root.as_str().contains("Footer data"_str));
    EXPECT_TRUE(root.as_str().contains("data-template=\"book-root\""_str));
    EXPECT_FALSE(root.as_str().contains("Litocpp"_str));
    EXPECT_TRUE(root.as_str().contains("no-icon"_str));
    EXPECT_FALSE(root.as_str().contains("lito-doc-shell"_str));
    EXPECT_FALSE(root.as_str().contains("Edit this page"_str));
    EXPECT_TRUE(root.as_str().contains("src/index.md"_str));
    EXPECT_TRUE(theme_read(book.path(), "build/book/chapter/index.html"_str)
                    .as_str()
                    .contains("data-template=\"book-page\""_str));
    auto missing = theme_read(book.path(), "build/book/404.html"_str);
    EXPECT_TRUE(missing.as_str().contains("Custom missing"_str));
    EXPECT_TRUE(missing.as_str().contains("<base href=\"/manual/\">"_str));
    EXPECT_FALSE(missing.as_str().contains("Return to"_str));
    auto moved = theme_read(book.path(), "build/book/old.html"_str);
    EXPECT_TRUE(moved.as_str().contains("Custom moved"_str));
    EXPECT_TRUE(moved.as_str().contains("data-template=\"redirect\""_str));
    EXPECT_TRUE(moved.as_str().contains("url=chapter/index.html#part"_str));
    EXPECT_FALSE(rstd::fs::exists(theme_path(book.path(), "build/book/static/app.js"_str).as_path())
                     .unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(theme_path(book.path(), "build/book/static/style.css"_str).as_path())
            .unwrap());
    EXPECT_TRUE(
        rstd::fs::exists(theme_path(book.path(), "build/book/skin.css"_str).as_path()).unwrap());
    EXPECT_EQ(prepared->dataset.pages.len(), usize(2));
}

TEST(BookTheme, RejectsMissingAndOldContextVersionsWithoutChangingSite) {
    auto book = make_theme_book();
    ASSERT_TRUE(lito::book::build({ .directory = PathBuf::from(book.path()) }).is_ok());
    auto original = theme_read(book.path(), "build/book/index.html"_str);
    for (auto version : array<ref<str>, 3> { ""_str, "1"_str, "99"_str }) {
        theme_write(book.path(), "theme/frontend.json"_str, theme_manifest(version).as_str());
        EXPECT_TRUE(load_test_theme(book.path()).is_err());
        EXPECT_TRUE(lito::book::check({ .directory = PathBuf::from(book.path()) }).is_err());
        EXPECT_TRUE(lito::book::build({ .directory = PathBuf::from(book.path()) }).is_err());
        EXPECT_EQ(theme_read(book.path(), "build/book/index.html"_str), original);
    }
}

TEST(BookTheme, NewThemeRejectsIncompleteOrUnsupportedContractsBeforePublication) {
    auto book = make_theme_book();
    ASSERT_TRUE(lito::book::build({ .directory = PathBuf::from(book.path()) }).is_ok());
    auto original = theme_read(book.path(), "build/book/index.html"_str);
    for (auto version : array<ref<str>, 2> { "2"_str, "99"_str }) {
        theme_write(
            book.path(), "theme/frontend.json"_str, theme_manifest(version, false).as_str());
        EXPECT_TRUE(load_test_theme(book.path()).is_err());
        EXPECT_TRUE(lito::book::prepare({ .directory = PathBuf::from(book.path()) }).is_err());
        EXPECT_EQ(theme_read(book.path(), "build/book/index.html"_str), original);
    }
    theme_write(
        book.path(),
        "theme/frontend.json"_str,
        "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,\"capabilities\":[\"api\"],\"doc-template-api\":2,\"book-template-api\":2,\"data-api\":4}"_str);
    EXPECT_TRUE(load_test_theme(book.path()).is_err());
}

TEST(BookTheme, NewContextDoesNotExposeDefaultLayoutFlagsAndFailedRenderKeepsSite) {
    auto book = make_theme_book();
    ASSERT_TRUE(lito::book::build({ .directory = PathBuf::from(book.path()) }).is_ok());
    auto original = theme_read(book.path(), "build/book/index.html"_str);
    for (auto template_source :
         array<ref<str>, 5> { "{{#if site.has_default_brand}}forced{{/if}}"_str,
                              "{{{book.content}}}"_str,
                              "{{#if page.has_edit_url}}edit{{/if}}"_str,
                              "{{#if navigation.has_scroll_state}}scroll{{/if}}"_str,
                              "{{{site.title}}}"_str }) {
        theme_write(book.path(), "theme/root.html"_str, template_source);
        auto prepared = lito::book::prepare({ .directory = PathBuf::from(book.path()) });
        ASSERT_TRUE(prepared.is_ok());
        EXPECT_TRUE(lito::book::publish(*prepared).is_err());
        EXPECT_EQ(theme_read(book.path(), "build/book/index.html"_str), original);
    }
}

TEST(BookTheme, ContextSnapshotDoesNotReopenManifestOrTemplateFiles) {
    auto book     = make_theme_book();
    auto prepared = lito::book::prepare({ .directory = PathBuf::from(book.path()) });
    ASSERT_TRUE(prepared.is_ok());
    theme_write(book.path(), "theme/root.html"_str, "{{#if broken}}"_str);
    theme_write(book.path(), "theme/frontend.json"_str, "not JSON"_str);
    theme_write(book.path(), "book.toml"_str, "not TOML ["_str);
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    EXPECT_TRUE(theme_read(book.path(), "build/book/index.html"_str)
                    .as_str()
                    .contains("Independent theme"_str));
}

TEST(BookTheme, ThemeOwnsErrorAndRedirectWording) {
    auto book = make_theme_book();
    theme_write(book.path(), "theme/missing.html"_str, "<p>Own error: {{page.title}}</p>"_str);
    theme_write(
        book.path(), "theme/moved.html"_str, "<p>Own redirect: {{page.redirect.href}}</p>"_str);
    ASSERT_TRUE(lito::book::build({ .directory = PathBuf::from(book.path()) }).is_ok());
    EXPECT_TRUE(theme_read(book.path(), "build/book/404.html"_str)
                    .as_str()
                    .contains("Own error: Theme Book"_str));
    EXPECT_TRUE(theme_read(book.path(), "build/book/old.html"_str)
                    .as_str()
                    .contains("Own redirect: chapter/index.html#part"_str));
}

TEST(BookTheme, MutatedOldContractOrIncompleteBundleIsRejectedBeforeReplacingSite) {
    auto book     = make_theme_book();
    auto prepared = lito::book::prepare({ .directory = PathBuf::from(book.path()) });
    ASSERT_TRUE(prepared.is_ok());
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    auto original                        = theme_read(book.path(), "build/book/index.html"_str);
    prepared->frontend.book_template_api = usize(1);
    EXPECT_TRUE(lito::book::publish(*prepared).is_err());
    EXPECT_EQ(theme_read(book.path(), "build/book/index.html"_str), original);
    prepared->frontend.book_template_api      = usize(2);
    prepared->frontend.book_redirect_template = String {};
    EXPECT_TRUE(lito::book::publish(*prepared).is_err());
    EXPECT_EQ(theme_read(book.path(), "build/book/index.html"_str), original);
}
