#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;
import lito.book;
import lito.site;
import lito.doc.web;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::path::PathBuf;

namespace
{
auto publication_path(ref<rstd::path::Path> root, ref<str> name) -> PathBuf {
    return PathBuf::from(root).join(name);
}
void publication_write(ref<rstd::path::Path> root, ref<str> name, ref<str> text) {
    auto path = publication_path(root, name);
    rstd::fs::create_dir_all(path.as_path().parent().unwrap()).unwrap();
    rstd::fs::write(path.as_path(), text.as_bytes()).unwrap();
}
auto publication_read(ref<rstd::path::Path> root, ref<str> name) -> String {
    return rstd::fs::read_to_string(publication_path(root, name).as_path()).unwrap();
}
auto publication_book(ref<str> html = {}, ref<str> extra_book = {}) -> rstd::fs::TempDir {
    auto temporary = rstd::fs::TempDir::make("litobook-publication"_str).unwrap();
    auto root      = temporary.path();
    publication_write(root,
                      "book.toml"_str,
                      rstd::format("[book]\nname='publication'\ntitle='Book & "
                                   "Docs'\nsrc='chapters'\n{}\n[output.html]\n{}",
                                   extra_book,
                                   html)
                          .as_str());
    publication_write(
        root,
        "chapters/SUMMARY.md"_str,
        "- [Home](index.md)\n- [Install](guide/install.md)\n- [Notes](guide/安装 notes.md)\n"_str);
    publication_write(root, "chapters/index.md"_str, "# Home\n\nA small book.\n"_str);
    publication_write(root,
                      "chapters/guide/install.md"_str,
                      "# Install\n\n## Setup\n\nConfigure the project.\n"_str);
    publication_write(root, "chapters/guide/安装 notes.md"_str, "# Notes\n\nUnicode path.\n"_str);
    return temporary;
}
auto publication_prepare(ref<rstd::path::Path> root) -> Result<lito::book::PreparedBook, String> {
    return lito::book::prepare({ .directory = PathBuf::from(root) },
                               Some(lito::doc::web::load_default_frontend().unwrap()));
}
} // namespace

TEST(BookPublication, RendersConfiguredShellMetadataAndEncodedEditPaths) {
    auto book = publication_book(
        "site-url='https://example.org/manual'\n"
        "repository-url='https://forge.example.org/project'\n"
        "edit-url='https://forge.example.org/project/edit/main/doc/{path}'\n"
        "[output.html.brand]\nlabel='Example <brand>'\nhref='https://example.org/'\n"
        "[[output.html.navigation]]\nlabel='Install & "
        "setup'\nhref='guide/install/index.html#setup'\n"
        "[output.html.footer]\ntext='Copyright <contributors>'\n"
        "[[output.html.footer.links]]\nlabel='Home'\nhref='index.html'\n"
        "[output.html.pages.\"guide/install.md\"]\ndescription='Specific \"description\" & more.'\n"_str,
        "description='Default description.'\n"_str);
    auto prepared = publication_prepare(book.path());
    ASSERT_TRUE(prepared.is_ok());
    auto built = lito::book::publish(*prepared);
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(built->pages, usize(3));
    auto nested = publication_read(book.path(), "build/book/guide/install/index.html"_str);
    EXPECT_TRUE(nested.as_str().contains("Example &lt;brand&gt;"_str));
    EXPECT_TRUE(nested.as_str().contains("href=\"../../guide/install/index.html#setup\""_str));
    EXPECT_TRUE(nested.as_str().contains("Copyright &lt;contributors&gt;"_str));
    EXPECT_TRUE(
        nested.as_str().contains("content=\"Specific &quot;description&quot; &amp; more.\""_str));
    EXPECT_TRUE(nested.as_str().contains(
        "href=\"https://example.org/manual/guide/install/index.html\""_str));
    EXPECT_TRUE(nested.as_str().contains(
        "https://forge.example.org/project/edit/main/doc/chapters/guide/install.md"_str));
    EXPECT_TRUE(nested.as_str().contains("https://forge.example.org/project"_str));
    auto notes = publication_read(book.path(), "build/book/guide/安装 notes/index.html"_str);
    EXPECT_TRUE(notes.as_str().contains("chapters/guide/%E5%AE%89%E8%A3%85%20notes.md"_str));
    EXPECT_TRUE(notes.as_str().contains(
        "https://example.org/manual/guide/%E5%AE%89%E8%A3%85%20notes/index.html"_str));
    EXPECT_FALSE(notes.as_str().contains(book.path().to_str().unwrap()));
    auto root = publication_read(book.path(), "build/book/index.html"_str);
    EXPECT_TRUE(root.as_str().contains("content=\"Default description.\""_str));
    EXPECT_FALSE(root.as_str().contains("/__litobook/"_str));
    auto error = publication_read(book.path(), "build/book/404.html"_str);
    EXPECT_TRUE(error.as_str().contains("<base href=\"/manual/\">"_str));
    EXPECT_TRUE(error.as_str().contains("Page not found"_str));
    EXPECT_TRUE(error.as_str().contains("Copyright &lt;contributors&gt;"_str));
    EXPECT_FALSE(error.as_str().contains("Edit this page"_str));
    EXPECT_FALSE(error.as_str().contains("rel=\"canonical\""_str));
    EXPECT_TRUE(error.as_str().contains("name=\"robots\" content=\"noindex\""_str));
}

TEST(BookPublication, RedirectsResolveFinalPagesWithoutChangingDataset) {
    auto book = publication_book(
        "site-url='https://example.org/manual/'\n"
        "[output.html.redirects]\n'old.html'='guide/install.md#setup'\n'legacy/install/'='index.md'\n"_str);
    auto prepared = publication_prepare(book.path());
    ASSERT_TRUE(prepared.is_ok());
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    EXPECT_EQ(prepared->dataset.pages.len(), usize(3));
    auto redirect = publication_read(book.path(), "build/book/old.html"_str);
    EXPECT_TRUE(redirect.as_str().contains(
        "http-equiv=\"refresh\" content=\"0;url=guide/install/index.html#setup\""_str));
    EXPECT_TRUE(redirect.as_str().contains(
        "rel=\"canonical\" href=\"https://example.org/manual/guide/install/index.html\""_str));
    auto nested = publication_read(book.path(), "build/book/legacy/install/index.html"_str);
    EXPECT_TRUE(nested.as_str().contains("href=\"../../index.html\""_str));
    EXPECT_FALSE(publication_read(book.path(), "build/book/data/book.json"_str)
                     .as_str()
                     .contains("old.html"_str));
}

TEST(BookPublication, RejectsInvalidConfigurationAndUnknownTargetsInCheckAndBuild) {
    const auto invalid = array<ref<str>, 15> {
        "repository-url='javascript:alert(1)'\n"_str,
        "repository-url='https://user:pass@example.org/'\n"_str,
        "site-url='https://example.org/manual/?q=1'\n"_str,
        "site-url='https://example.org/#fragment'\n"_str,
        "edit-url='https://example.org/edit'\n"_str,
        "edit-url='https://example.org/{path}/{path}'\n"_str,
        "edit-url='https://example.org/?path={path}'\n"_str,
        "edit-url='https://example.org/{branch}/{path}'\n"_str,
        "[[output.html.navigation]]\nlabel='Bad'\nhref='../index.html'\n"_str,
        "[[output.html.navigation]]\nlabel='Bad'\nhref='%2e%2e/index.html'\n"_str,
        "[[output.html.navigation]]\nlabel='Bad'\nhref='guide%2Finstall/index.html'\n"_str,
        "[[output.html.navigation]]\nlabel='Bad'\nhref='missing/index.html'\n"_str,
        "[[output.html.navigation]]\nlabel='Bad'\nhref='guide/install/index.html#missing'\n"_str,
        "[output.html.pages.'missing.md']\ndescription='Missing'\n"_str,
        "[output.html.footer]\nhtml='<script>bad()</script>'\n"_str,
    };
    for (auto config : invalid) {
        auto book    = publication_book(config);
        auto checked = lito::book::check({ .directory = PathBuf::from(book.path()) },
                                         Some(lito::doc::web::load_default_frontend().unwrap()));
        EXPECT_TRUE(checked.is_err());
        EXPECT_TRUE(publication_prepare(book.path()).is_err());
        EXPECT_FALSE(
            rstd::fs::exists(publication_path(book.path(), "build/book"_str).as_path()).unwrap());
    }
}

TEST(BookPublication, RejectsRedirectCollisionsAndInvalidTargetsBeforePublication) {
    const auto entries = array<ref<str>, 12> {
        "'index.html'='guide/install.md'"_str,
        "'404.html'='index.md'"_str,
        "'data/page.html'='index.md'"_str,
        "'static/old.html'='index.md'"_str,
        "'guide/install/'='index.md'"_str,
        "'../outside.html'='index.md'"_str,
        "'alias.html'='missing.md'"_str,
        "'alias.html'='index.md#missing'"_str,
        "'alias.html'='https://example.org/'"_str,
        "'alias.html'='other.html'\n'other.html'='alias.html'"_str,
        "'alias.html'='index.md'\n'alias.html/child.html'='index.md'"_str,
        "'alias/'='index.md'\n'alias/index.html'='index.md'"_str,
    };
    for (auto entries_text : entries) {
        auto book  = publication_book();
        auto valid = publication_prepare(book.path());
        ASSERT_TRUE(valid.is_ok());
        ASSERT_TRUE(lito::book::publish(*valid).is_ok());
        auto original = publication_read(book.path(), "build/book/index.html"_str);
        publication_write(book.path(),
                          "book.toml"_str,
                          rstd::format("[book]\nname='publication'\ntitle='Book'\nsrc='chapters'\n["
                                       "output.html.redirects]\n{}\n",
                                       entries_text)
                              .as_str());
        EXPECT_TRUE(publication_prepare(book.path()).is_err());
        EXPECT_EQ(publication_read(book.path(), "build/book/index.html"_str), original);
    }
}

TEST(BookPublication, PreservesDefaultsAndUsesSafeLegacyNotFoundFallback) {
    auto book     = publication_book();
    auto prepared = publication_prepare(book.path());
    ASSERT_TRUE(prepared.is_ok());
    prepared->frontend.book_not_found_template = String {};
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    auto root = publication_read(book.path(), "build/book/index.html"_str);
    EXPECT_TRUE(root.as_str().contains("href=\"https://litocpp.org/\""_str));
    EXPECT_FALSE(root.as_str().contains("name=\"description\""_str));
    EXPECT_FALSE(root.as_str().contains("rel=\"canonical\""_str));
    EXPECT_FALSE(root.as_str().contains("class=\"site-footer\""_str));
    EXPECT_FALSE(root.as_str().contains("class=\"site-mobile-links\""_str));
    auto error = publication_read(book.path(), "build/book/404.html"_str);
    EXPECT_TRUE(error.as_str().contains("<base href=\"/\">"_str));
    EXPECT_TRUE(error.as_str().contains("Book &amp; Docs"_str));
    EXPECT_FALSE(error.as_str().contains("rel=\"canonical\""_str));
}

TEST(BookPublication, PreparedModelPublishesWithoutReopeningConfiguration) {
    auto book     = publication_book("site-url='https://example.org/'\n"_str);
    auto prepared = publication_prepare(book.path());
    ASSERT_TRUE(prepared.is_ok());
    publication_write(book.path(), "book.toml"_str, "not TOML ["_str);
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    EXPECT_TRUE(publication_read(book.path(), "build/book/index.html"_str)
                    .as_str()
                    .contains("rel=\"canonical\" href=\"https://example.org/index.html\""_str));
}

TEST(BookPublication, SyntheticIndexHasNoEditTargetAndFlatSourceUsesLogicalPaths) {
    auto book = publication_book("edit-url='https://forge.example.org/edit/main/{path}'\n"_str);
    publication_write(
        book.path(), "chapters/SUMMARY.md"_str, "- [Install](guide/install.md)\n"_str);
    auto prepared = publication_prepare(book.path());
    ASSERT_TRUE(prepared.is_ok());
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    EXPECT_FALSE(publication_read(book.path(), "build/book/index.html"_str)
                     .as_str()
                     .contains("Edit this page"_str));
    publication_write(
        book.path(),
        "book.toml"_str,
        "[book]\nname='publication'\ntitle='Book'\nsrc='.'\n[output.html]\nedit-url='https://forge.example.org/edit/main/{path}'\n"_str);
    publication_write(
        book.path(), "SUMMARY.md"_str, "- [Install](chapters/guide/install.md)\n"_str);
    auto flat = publication_prepare(book.path());
    ASSERT_TRUE(flat.is_ok());
    ASSERT_TRUE(lito::book::publish(*flat).is_ok());
    auto html = publication_read(book.path(), "build/book/chapters/guide/install/index.html"_str);
    EXPECT_TRUE(html.as_str().contains(
        "https://forge.example.org/edit/main/chapters/guide/install.md"_str));
    EXPECT_FALSE(html.as_str().contains("edit/main/./"_str));
}

TEST(BookPublication, IsDeterministicAcrossRootsAndValidatesFrontendFileParents) {
    auto first = publication_book(
        "site-url='https://example.org/manual/'\n[output.html.redirects]\n'old.html'='index.md'\n"_str);
    auto second = publication_book(
        "site-url='https://example.org/manual/'\n[output.html.redirects]\n'old.html'='index.md'\n"_str);
    auto a = publication_prepare(first.path());
    auto b = publication_prepare(second.path());
    ASSERT_TRUE(a.is_ok());
    ASSERT_TRUE(b.is_ok());
    ASSERT_TRUE(lito::book::publish(*a).is_ok());
    ASSERT_TRUE(lito::book::publish(*b).is_ok());
    for (auto file : array<ref<str>, 4> { "build/book/index.html"_str,
                                          "build/book/old.html"_str,
                                          "build/book/404.html"_str,
                                          "build/book/data/book.json"_str })
        EXPECT_EQ(publication_read(first.path(), file), publication_read(second.path(), file));
    auto original = publication_read(first.path(), "build/book/index.html"_str);
    a->frontend.assets.push(lito::site::FrontendAsset {
        .path = "guide/install"_Str, .media_type = "text/plain"_Str, .contents = "conflict"_Str });
    EXPECT_TRUE(lito::book::publish(*a).is_err());
    EXPECT_EQ(publication_read(first.path(), "build/book/index.html"_str), original);
}
