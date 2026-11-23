#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.book;
import lito.doc.web;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto write_text(ref<rstd::path::Path> root, ref<str> relative, ref<str> contents)
    -> rstd::io::Result<empty> {
    auto path    = PathBuf::from(root).join(PathBuf::from(relative).as_path());
    auto parent  = path.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    return rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
}

auto child(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto make_book_project() -> rstd::io::Result<rstd::test::TempDir> {
    auto temporary = rstd::test::TempDir::make();
    if (temporary.is_err()) return temporary;
    auto root    = temporary->path();
    auto written = write_text(root,
                              "book.toml"_str,
                              "[book]\n"
                              "name = \"inline-book\"\n"
                              "title = \"Inline Book\"\n"
                              "version = \"1.2.3\"\n"
                              "language = \"en\"\n"
                              "\n"
                              "[output.html]\n"
                              "frontend = \"frontend\"\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "src/SUMMARY.md"_str,
                         "# Summary\n"
                         "\n"
                         "- [Home](index.md)\n"
                         "  - [Install](guide/install.md)\n"
                         "- [Reference](reference.md)\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "src/index.md"_str,
                         "# Welcome\n"
                         "\n"
                         "Read the **install** [instructions](guide/install.md#install).\n"
                         "\n"
                         "## Repeated\n"
                         "\n"
                         "## Repeated\n"
                         "\n"
                         "Jump [again](#repeated-2).\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "src/guide/install.md"_str,
                         "# Install\n"
                         "\n"
                         "> Keep the setup local.\n"
                         "\n"
                         "1. Build and\n"
                         "   keep the output local\n"
                         "   - Keep it local\n"
                         "2. Open the [reference](../reference.md).\n"
                         "\n"
                         "![Diagram](image.svg)\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(
        root, "src/guide/image.svg"_str, "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "src/reference.md"_str,
                         "# Reference\n"
                         "\n"
                         "Use `litobook build`.\n"
                         "\n"
                         "```cpp\n"
                         "auto value = 1 < 2;\n"
                         "```\n"
                         "\n"
                         "<script>unsafe()</script>\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "frontend/frontend.json"_str,
                         "{\n"
                         "  \"format\": \"lito-doc-frontend\",\n"
                         "  \"version\": 2,\n"
                         "  \"template-api\": 1,\n"
                         "  \"book-data-api\": 1,\n"
                         "  \"book-template-api\": 2,\n"
                         "  \"capabilities\": [\"book\"],\n"
                         "  \"templates\": {\n"
                         "    \"book-root\": \"templates/root.html\",\n"
                         "    \"book-page\": \"templates/page.html\",\n"
                         "    \"book-not-found\": \"templates/missing.html\",\n"
                         "    \"book-redirect\": \"templates/moved.html\"\n"
                         "  },\n"
                         "  \"partials\": [],\n"
                         "  \"assets\": []\n"
                         "}\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "frontend/templates/root.html"_str,
                         "<!doctype html><title>{{site.title}}</title>"
                         "{{#each page.outline}}<a class=\"outline\" "
                         "href=\"{{href}}\">{{label}}</a>{{/each}}"
                         "{{{page.content}}}"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "frontend/templates/page.html"_str,
                         "<!doctype html><title>{{page.title}}</title>{{{page.content}}}"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "frontend/templates/missing.html"_str,
                         "<!doctype html><base href=\"{{page.base_href}}\"><p>Missing</p>"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = write_text(root,
                         "frontend/templates/moved.html"_str,
                         "<!doctype html><a href=\"{{page.redirect.href}}\">Moved</a>"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(rstd::move(temporary).unwrap());
}

} // namespace

TEST(Book, ChecksAndPublishesAnInlineProject) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();

    auto checked = lito::book::check(lito::book::BookCheckInput {
        .directory = child(project.path(), "src/guide"_str),
    });
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->pages, usize(3));
    EXPECT_EQ(checked->headings, usize(5));
    EXPECT_EQ(checked->links, usize(4));
    EXPECT_FALSE(rstd::fs::exists(child(project.path(), "build/book"_str).as_path()).unwrap());

    auto built = lito::book::build(lito::book::BookBuildInput {
        .directory = PathBuf::from(project.path()),
    });
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(built->pages, usize(3));
    EXPECT_TRUE(built->site_generated);
    EXPECT_TRUE(rstd::fs::exists(child(project.path(), "build/book/data/book.json"_str).as_path())
                    .unwrap());
    auto index =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(index.is_ok());
    EXPECT_TRUE(index->as_str().contains("href=\"guide/install/index.html#install\""_str));
    EXPECT_TRUE(index->as_str().contains("href=\"#repeated-2\""_str));
    EXPECT_TRUE(index->as_str().contains("id=\"repeated-2\""_str));
    EXPECT_TRUE(index->as_str().contains("class=\"outline\" href=\"#repeated\""_str));
    EXPECT_FALSE(index->as_str().contains("class=\"outline\" href=\"#welcome\""_str));
    auto install = rstd::fs::read_to_string(
        child(project.path(), "build/book/guide/install/index.html"_str).as_path());
    ASSERT_TRUE(install.is_ok());
    EXPECT_TRUE(install->as_str().contains(
        "<ol><li>Build and keep the output local<ul><li>Keep it local</li></ul></li>"_str));
    EXPECT_TRUE(install->as_str().contains("src=\"../../assets/"_str));
    auto reference = rstd::fs::read_to_string(
        child(project.path(), "build/book/reference/index.html"_str).as_path());
    ASSERT_TRUE(reference.is_ok());
    EXPECT_TRUE(reference->as_str().contains("class=\"language-cpp\""_str));
    EXPECT_TRUE(reference->as_str().contains("&lt;script&gt;unsafe()&lt;/script&gt;"_str));

    auto loaded = lito::book::load_book_dataset(child(project.path(), "build/book"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_FALSE(loaded->identity.is_empty());
    EXPECT_EQ(loaded->title.as_str(), "Inline Book"_str);
    EXPECT_EQ(loaded->pages.len(), usize(3));

    ASSERT_TRUE(write_text(project.path(),
                           "src/index.md"_str,
                           "# Broken\n\n[missing](reference.md#missing)\n"_str)
                    .is_ok());
    auto failed = lito::book::build(lito::book::BookBuildInput {
        .directory = PathBuf::from(project.path()),
    });
    ASSERT_TRUE(failed.is_err());
    auto preserved =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(preserved.is_ok());
    EXPECT_EQ(preserved->as_str(), index->as_str());
}

TEST(Book, RendersLimarktoCommonMarkAndGfmNodes) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(write_text(project.path(),
                           "src/index.md"_str,
                           "# Welcome\n\n"
                           "_emphasis_ **outer *inner*** ~~removed~~ &amp; &#65;.\n\n"
                           "Read [**reference**][ref].\n\n"
                           "| Link | Image |\n| :--- | ---: |\n"
                           "| [reference][ref] | ![*diagram* ![nested](missing.svg)][pic] |\n\n"
                           "5. First\n6. Second\n\n"
                           "- [x] Done\n- [ ] Pending\n\n"
                           "> Quote\n>\n> > Nested\n\n"
                           "Line one  \nLine two\n\n"
                           "<https://litocpp.org/> <author@example.com>\n\n"
                           "~~~toml extra-info\n![ignored](missing.png)\n~~~\n\n"
                           "````md\n```\n````\n\n"
                           "[ref]: reference.md#reference \"A &amp; B\"\n"
                           "[pic]: guide/image.svg \"diagram title\"\n"_str)
                    .is_ok());
    auto checked = lito::book::check({ .directory = PathBuf::from(project.path()) });
    ASSERT_TRUE(checked.is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) });
    ASSERT_TRUE(built.is_ok());
    auto html =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(html.is_ok());
    EXPECT_TRUE(html->as_str().contains(
        "<em>emphasis</em> <strong>outer <em>inner</em></strong> <del>removed</del> &amp; A."_str));
    EXPECT_TRUE(html->as_str().contains(
        "href=\"reference/index.html#reference\" title=\"A &amp; B\"><strong>reference</strong></a>"_str));
    EXPECT_TRUE(
        html->as_str().contains("<table><thead><tr><th style=\"text-align: left\">Link</th>"_str));
    EXPECT_TRUE(html->as_str().contains("<tbody><tr><td style=\"text-align: left\">"_str));
    EXPECT_TRUE(html->as_str().contains("alt=\"diagram nested\" title=\"diagram title\""_str));
    EXPECT_FALSE(html->as_str().contains("src=\"missing.svg\""_str));
    EXPECT_TRUE(html->as_str().contains("<ol start=\"5\"><li>First</li><li>Second</li></ol>"_str));
    EXPECT_TRUE(html->as_str().contains("<input type=\"checkbox\" disabled checked> Done"_str));
    EXPECT_TRUE(html->as_str().contains(
        "<blockquote><p>Quote</p><blockquote><p>Nested</p></blockquote></blockquote>"_str));
    EXPECT_TRUE(html->as_str().contains("Line one<br>Line two"_str));
    EXPECT_TRUE(html->as_str().contains("href=\"mailto:author@example.com\""_str));
    EXPECT_TRUE(
        html->as_str().contains("class=\"language-toml\">![ignored](missing.png)\n</code>"_str));
    EXPECT_TRUE(html->as_str().contains("class=\"language-md\">```\n</code>"_str));
    auto assets = rstd::fs::read_dir(child(project.path(), "build/book/assets"_str).as_path());
    ASSERT_TRUE(assets.is_ok());
    auto count = usize {};
    for (auto entry = assets->next(); entry.is_some(); entry = assets->next()) {
        ASSERT_TRUE(entry->is_ok());
        ++count;
    }
    EXPECT_EQ(count, usize(1));
}

TEST(Book, KeepsLimarktoReferenceUseLocationsAndSafeHtml) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(
        write_text(
            project.path(),
            "src/index.md"_str,
            "# Welcome\r\n\r\n> 中文 [bad][target]\r\n\r\n[target]: reference.md#missing\r\n"_str)
            .is_ok());
    auto checked = lito::book::check({ .directory = PathBuf::from(project.path()) });
    ASSERT_TRUE(checked.is_err());
    auto error = rstd::move(checked).unwrap_err();
    EXPECT_TRUE(error.as_str().contains("index.md:3:10:"_str));
    EXPECT_TRUE(error.as_str().contains("target page has no heading '#missing'"_str));
    ASSERT_TRUE(write_text(project.path(),
                           "src/index.md"_str,
                           "# Welcome\n\n<script>\n![not a resource](missing.png)\n</script>\n\n"
                           "<img src=x onerror=alert(1)>\n\n"
                           "<a href=\"javascript:alert(1)\">raw</a>\n"_str)
                    .is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) });
    ASSERT_TRUE(built.is_ok());
    auto html =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(html.is_ok());
    EXPECT_TRUE(html->as_str().contains("&lt;script&gt;"_str));
    EXPECT_TRUE(html->as_str().contains("&lt;img src=x onerror=alert(1)&gt;"_str));
    EXPECT_FALSE(html->as_str().contains("<script>"_str));
    EXPECT_FALSE(html->as_str().contains("<img src=x"_str));
    EXPECT_FALSE(html->as_str().contains("<a href=\"javascript:"_str));
}

TEST(Book, RejectsUnsafeLimarktoDestinationsAfterEntityDecoding) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    for (auto source : array<ref<str>, 3> {
             "[bad](jav&#x61;script:alert%281%29)\n"_str,
             "[bad][target]\n\n[target]: data:text/html,evil\n"_str,
             "![bad](javascript:alert%281%29)\n"_str,
         }) {
        ASSERT_TRUE(write_text(project.path(), "src/index.md"_str, source).is_ok());
        auto checked = lito::book::check({ .directory = PathBuf::from(project.path()) });
        ASSERT_TRUE(checked.is_err());
        EXPECT_TRUE(rstd::move(checked).unwrap_err().as_str().contains("unsafe link scheme"_str));
    }
}

TEST(Book, PreservesLegacyAnchorsAndAvoidsGeneratedCollisions) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(
        write_text(
            project.path(),
            "src/index.md"_str,
            "# Welcome\n\n## Repeat\n\n## Repeat\n\n## Repeat-2\n\n## Repeat\n\n"
            "## [**A** &amp; `B`](reference.md)\n\n"
            "[first](#repeat) [second](#repeat-2) [third](#repeat-2-2) [fourth](#repeat-3) [last](#a-b)\n"_str)
            .is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) });
    ASSERT_TRUE(built.is_ok());
    auto html =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(html.is_ok());
    EXPECT_TRUE(html->as_str().contains("id=\"repeat\""_str));
    EXPECT_TRUE(html->as_str().contains("id=\"repeat-2\""_str));
    EXPECT_TRUE(html->as_str().contains("id=\"repeat-2-2\""_str));
    EXPECT_TRUE(html->as_str().contains("id=\"repeat-3\""_str));
    EXPECT_TRUE(html->as_str().contains("id=\"a-b\""_str));
    auto loaded = lito::book::load_book_dataset(child(project.path(), "build/book"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->pages[usize {}].headings[usize(5)].text.as_str(), "A & B"_str);
}

TEST(Book, ReportsSummaryAndLinkDiagnosticsWithLocations) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(write_text(project.path(),
                           "src/index.md"_str,
                           "# Welcome\n\n[missing](reference.md#missing)\n"_str)
                    .is_ok());
    auto missing = lito::book::check(lito::book::BookCheckInput {
        .directory = PathBuf::from(project.path()),
    });
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    EXPECT_TRUE(missing_error.as_str().contains("index.md:3:"_str));
    EXPECT_TRUE(missing_error.as_str().contains("has no heading"_str));

    ASSERT_TRUE(write_text(project.path(),
                           "src/SUMMARY.md"_str,
                           "- [Home](index.md)\n"
                           "    - [Reference](reference.md)\n"_str)
                    .is_ok());
    auto nesting = lito::book::check(lito::book::BookCheckInput {
        .directory = PathBuf::from(project.path()),
    });
    ASSERT_TRUE(nesting.is_err());
    auto nesting_error = rstd::move(nesting).unwrap_err();
    EXPECT_TRUE(nesting_error.as_str().contains("SUMMARY.md:2:"_str));
    EXPECT_TRUE(nesting_error.as_str().contains("skips a level"_str));

    ASSERT_TRUE(write_text(project.path(),
                           "book.toml"_str,
                           "[book]\n"
                           "name = \"inline-book\"\n"
                           "title = \"Inline Book\"\n"
                           "unexpected = true\n"_str)
                    .is_ok());
    auto manifest = lito::book::check(lito::book::BookCheckInput {
        .directory = PathBuf::from(project.path()),
    });
    ASSERT_TRUE(manifest.is_err());
    auto manifest_error = rstd::move(manifest).unwrap_err();
    EXPECT_TRUE(manifest_error.as_str().contains("book.unexpected"_str));
}

TEST(Book, DefaultFrontendKeepsOneNavigationScrollIdentity) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(write_text(project.path(),
                           "book.toml"_str,
                           "[book]\n"
                           "name = \"inline-book\"\n"
                           "title = \"Inline Book\"\n"
                           "version = \"1.2.3\"\n"
                           "language = \"en\"\n"
                           "\n"
                           "[output.html]\n"
                           "frontend = \"default\"\n"_str)
                    .is_ok());
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto built = lito::book::build(
        lito::book::BookBuildInput {
            .directory = PathBuf::from(project.path()),
        },
        Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(built.is_ok());
    auto root_page =
        rstd::fs::read_to_string(child(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(root_page.is_ok());
    EXPECT_TRUE(root_page->as_str().contains("scroll-state-url=\"data/book.json\""_str));
    EXPECT_TRUE(root_page->as_str().contains("static/theme-icons.svg#moon"_str));
    EXPECT_TRUE(root_page->as_str().contains(
        "<link rel=\"icon\" href=\"static/favicon.svg\" type=\"image/svg+xml\">"_str));
    EXPECT_FALSE(root_page->as_str().contains("sidebar-footer"_str));
    auto nested_page = rstd::fs::read_to_string(
        child(project.path(), "build/book/guide/install/index.html"_str).as_path());
    ASSERT_TRUE(nested_page.is_ok());
    EXPECT_TRUE(nested_page->as_str().contains("scroll-state-url=\"../../data/book.json\""_str));
    EXPECT_TRUE(nested_page->as_str().contains("../../static/theme-icons.svg#sun"_str));
    EXPECT_TRUE(nested_page->as_str().contains(
        "<link rel=\"icon\" href=\"../../static/favicon.svg\" type=\"image/svg+xml\">"_str));
}

TEST(Book, PublishesConfiguredIconsForEveryPage) {
    struct IconCase {
        ref<str> source;
        ref<str> output;
        ref<str> media_type;
    };
    const auto cases = array<IconCase, 3> {
        IconCase { "src/assets/site.SVG"_str, "static/book-icon.svg"_str, "image/svg+xml"_str },
        IconCase { "src/assets/site.png"_str, "static/book-icon.png"_str, "image/png"_str },
        IconCase { "src/assets/site.ico"_str, "static/book-icon.ico"_str, "image/x-icon"_str },
    };
    const auto binary = array<u8, 4> { u8(0), u8(137), u8(255), u8(10) };
    for (const auto& item : cases) {
        auto created = make_book_project();
        ASSERT_TRUE(created.is_ok());
        auto project = rstd::move(created).unwrap();
        ASSERT_TRUE(write_text(project.path(),
                               "book.toml"_str,
                               rstd::format("[book]\nname = \"icon-book\"\ntitle = \"Icon "
                                            "Book\"\n[output.html]\nicon = \"{}\"\n",
                                            item.source)
                                   .as_str())
                        .is_ok());
        ASSERT_TRUE(write_text(project.path(),
                               item.source,
                               "<svg xmlns=\"http://www.w3.org/2000/svg\"/>"_str)
                        .is_ok());
        if (item.media_type != "image/svg+xml"_str)
            ASSERT_TRUE(rstd::fs::write_atomic(child(project.path(), item.source).as_path(),
                                               binary.as_slice())
                            .is_ok());
        else
            ASSERT_TRUE(write_text(project.path(),
                                   "src/guide/install.md"_str,
                                   "# Install\n\n![Icon](../assets/site.SVG)\n"_str)
                            .is_ok());
        if (item.media_type == "image/png"_str)
            ASSERT_TRUE(
                write_text(project.path(),
                           "src/SUMMARY.md"_str,
                           "- [Install](guide/install.md)\n- [Reference](reference.md)\n"_str)
                    .is_ok());
        auto checked = lito::book::check({ .directory = child(project.path(), "src/guide"_str) });
        ASSERT_TRUE(checked.is_ok());
        ASSERT_TRUE(checked->project.icon.is_some());
        EXPECT_EQ(checked->project.icon->asset.output.as_str(), item.output);
        auto frontend = lito::doc::web::load_default_frontend();
        ASSERT_TRUE(frontend.is_ok());
        auto output = child(project.path(), "published"_str);
        auto built  = lito::book::build(
            { .directory = child(project.path(), "src/guide"_str), .output = Some(output.clone()) },
            Some(rstd::move(frontend).unwrap()));
        ASSERT_TRUE(built.is_ok());
        auto root   = rstd::fs::read_to_string(child(output.as_path(), "index.html"_str).as_path());
        auto nested = rstd::fs::read_to_string(
            child(output.as_path(), "guide/install/index.html"_str).as_path());
        ASSERT_TRUE(root.is_ok());
        ASSERT_TRUE(nested.is_ok());
        EXPECT_TRUE(root->as_str().contains(
            rstd::format(
                "<link rel=\"icon\" href=\"{}\" type=\"{}\">", item.output, item.media_type)
                .as_str()));
        EXPECT_TRUE(nested->as_str().contains(
            rstd::format(
                "<link rel=\"icon\" href=\"../../{}\" type=\"{}\">", item.output, item.media_type)
                .as_str()));
        if (item.media_type == "image/svg+xml"_str)
            EXPECT_TRUE(nested->as_str().contains("src=\"../../static/book-icon.svg\""_str));
        auto source = rstd::fs::read(child(project.path(), item.source).as_path());
        auto copied = rstd::fs::read(child(output.as_path(), item.output).as_path());
        ASSERT_TRUE(source.is_ok());
        ASSERT_TRUE(copied.is_ok());
        EXPECT_EQ(copied->as_slice(), source->as_slice());
    }
}

TEST(Book, RejectsInvalidIconConfiguration) {
    struct InvalidIcon {
        ref<str> value;
        ref<str> message;
    };
    const auto cases = array<InvalidIcon, 7> {
        InvalidIcon { "\"\""_str, "must not be empty"_str },
        InvalidIcon { "12"_str, "must be a string"_str },
        InvalidIcon { "\"missing.svg\""_str, "cannot resolve"_str },
        InvalidIcon { "\"../outside.svg\""_str, "safe relative path"_str },
        InvalidIcon { "\"/outside.svg\""_str, "safe relative path"_str },
        InvalidIcon { "\"src\""_str, "must be a file"_str },
        InvalidIcon { "\"src/index.md\""_str, "SVG, PNG or ICO"_str },
    };
    for (const auto& item : cases) {
        auto created = make_book_project();
        ASSERT_TRUE(created.is_ok());
        auto project = rstd::move(created).unwrap();
        ASSERT_TRUE(write_text(project.path(),
                               "book.toml"_str,
                               rstd::format("[book]\nname = \"icon-book\"\ntitle = \"Icon "
                                            "Book\"\n[output.html]\nicon = {}\n",
                                            item.value)
                                   .as_str())
                        .is_ok());
        auto checked = lito::book::check({ .directory = PathBuf::from(project.path()) });
        ASSERT_TRUE(checked.is_err());
        auto error = rstd::move(checked).unwrap_err();
        EXPECT_TRUE(error.as_str().contains("book.toml:"_str));
        EXPECT_TRUE(error.as_str().contains("output.html.icon"_str));
        EXPECT_TRUE(error.as_str().contains(item.message));
        EXPECT_FALSE(rstd::fs::exists(child(project.path(), "build/book"_str).as_path()).unwrap());
    }
}

TEST(Book, RejectsIconAssetCollisionsWithoutReplacingTheSite) {
    auto created = make_book_project();
    ASSERT_TRUE(created.is_ok());
    auto project = rstd::move(created).unwrap();
    ASSERT_TRUE(
        write_text(
            project.path(),
            "book.toml"_str,
            "[book]\nname = \"icon-book\"\ntitle = \"Icon Book\"\n[output.html]\nicon = \"src/guide/image.svg\"\n"_str)
            .is_ok());
    ASSERT_TRUE(write_text(project.path(), "build/book/keep.txt"_str, "previous site"_str).is_ok());
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    frontend->assets.push(lito::site::FrontendAsset {
        .path       = String::make("static/book-icon.svg"_str),
        .media_type = String::make("image/svg+xml"_str),
        .contents   = String::make("frontend icon"_str),
    });
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) },
                                   Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(built.is_err());
    EXPECT_TRUE(
        rstd::move(built).unwrap_err().as_str().contains("conflicts with a frontend asset"_str));
    auto previous =
        rstd::fs::read_to_string(child(project.path(), "build/book/keep.txt"_str).as_path());
    ASSERT_TRUE(previous.is_ok());
    EXPECT_EQ(previous->as_str(), "previous site"_str);
}

TEST(Frontend, RejectsVersionOneApiFrontend) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(write_text(root,
                           "frontend.json"_str,
                           "{\n"
                           "  \"format\": \"lito-doc-frontend\",\n"
                           "  \"version\": 1,\n"
                           "  \"template-api\": 1,\n"
                           "  \"data-api\": 4,\n"
                           "  \"templates\": {\n"
                           "    \"root\": \"root.html\",\n"
                           "    \"package\": \"package.html\",\n"
                           "    \"module\": \"module.html\",\n"
                           "    \"symbol\": \"symbol.html\"\n"
                           "  },\n"
                           "  \"partials\": [],\n"
                           "  \"assets\": []\n"
                           "}\n"_str)
                    .is_ok());
    ASSERT_TRUE(write_text(root, "root.html"_str, "{{site.title}}"_str).is_ok());
    ASSERT_TRUE(write_text(root, "package.html"_str, "{{site.title}}"_str).is_ok());
    ASSERT_TRUE(write_text(root, "module.html"_str, "{{site.title}}"_str).is_ok());
    ASSERT_TRUE(write_text(root, "symbol.html"_str, "{{site.title}}"_str).is_ok());
    auto frontend = lito::site::load_frontend_directory(root);
    ASSERT_TRUE(frontend.is_err());
    EXPECT_TRUE(rstd::move(frontend).unwrap_err().as_str().contains("version"_str));
}

TEST(Frontend, RejectsUnsupportedDocDataApi) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(write_text(root,
                           "frontend.json"_str,
                           "{\n"
                           "  \"format\": \"lito-doc-frontend\",\n"
                           "  \"version\": 2,\n"
                           "  \"template-api\": 1,\n"
                           "  \"capabilities\": [\"api\"],\n"
                           "  \"doc-template-api\": 2,\n"
                           "  \"data-api\": 2\n"
                           "}\n"_str)
                    .is_ok());
    auto frontend = lito::site::load_frontend_directory(root);
    ASSERT_TRUE(frontend.is_err());
    EXPECT_TRUE(
        rstd::move(frontend).unwrap_err().as_str().contains("unsupported doc data API 2"_str));
}

TEST(Frontend, RejectsMissingOldAndMismatchedContextDeclarationsForAllSources) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    for (
        auto declaration : array<ref<str>, 10> {
            "\"capabilities\":[\"api\"],\"data-api\":4"_str,
            "\"capabilities\":[\"api\"],\"data-api\":4,\"doc-template-api\":1"_str,
            "\"capabilities\":[\"api\"],\"data-api\":4,\"doc-template-api\":99"_str,
            "\"capabilities\":[\"book\"],\"book-data-api\":1"_str,
            "\"capabilities\":[\"book\"],\"book-data-api\":1,\"book-template-api\":1"_str,
            "\"capabilities\":[\"book\"],\"book-data-api\":1,\"book-template-api\":99"_str,
            "\"capabilities\":[\"api\",\"book\"],\"data-api\":4,\"book-data-api\":1,\"doc-template-api\":2"_str,
            "\"capabilities\":[\"api\",\"book\"],\"data-api\":4,\"book-data-api\":1,\"book-template-api\":2"_str,
            "\"capabilities\":[\"api\"],\"data-api\":4,\"doc-template-api\":2,\"book-template-api\":2"_str,
            "\"capabilities\":[\"book\"],\"book-data-api\":1,\"book-template-api\":2,\"doc-template-api\":2"_str }) {
        auto manifest =
            rstd::format("{{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,{},"
                         "\"templates\":{{}},\"partials\":[],\"assets\":[]}}",
                         declaration);
        ASSERT_TRUE(write_text(root, "frontend.json"_str, manifest.as_str()).is_ok());
        auto directory = lito::site::load_frontend_directory(root);
        ASSERT_TRUE(directory.is_err());
        auto directory_error = rstd::move(directory).unwrap_err();
        EXPECT_TRUE(directory_error.as_str().contains("template-api"_str));
        const lito::site::FrontendResourceInput resources[] = {
            { .path       = "frontend.json"_str,
              .media_type = "application/json"_str,
              .contents   = manifest.as_str().as_bytes() },
        };
        auto embedded = lito::site::load_frontend_resources(
            "embedded:invalid"_str,
            slice<lito::site::FrontendResourceInput>::from_raw_parts(resources, usize(1)));
        ASSERT_TRUE(embedded.is_err());
        EXPECT_EQ(embedded.unwrap_err().as_str(), directory_error.as_str());
    }
}

TEST(Frontend, RejectsDeprecatedSourceTemplateEntry) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    ASSERT_TRUE(
        write_text(
            temporary->path(),
            "frontend.json"_str,
            "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,"
            "\"capabilities\":[\"api\"],\"doc-template-api\":2,\"data-api\":4,\"templates\":{"
            "\"root\":\"root.html\",\"package\":\"package.html\",\"module\":\"module.html\","
            "\"symbol\":\"symbol.html\",\"source\":\"source.html\"},\"partials\":[],\"assets\":[]}"_str)
            .is_ok());
    auto frontend = lito::site::load_frontend_directory(temporary->path());
    ASSERT_TRUE(frontend.is_err());
    EXPECT_TRUE(frontend.unwrap_err().as_str().contains("source"_str));
}

TEST(Frontend, DirectConstructionValidatesParsedManifestBeforeReadingOptionalVersions) {
    for (
        auto source : array<ref<str>, 2> {
            "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,\"capabilities\":[\"book\"],\"book-data-api\":1,\"book-template-api\":2}"_str,
            "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,\"capabilities\":[\"api\"],\"data-api\":4,\"doc-template-api\":2}"_str }) {
        auto manifest = lito::site::parse_frontend_manifest(source);
        ASSERT_TRUE(manifest.is_ok());
        if (lito::site::frontend_capability(*manifest, "book"_str))
            manifest->book_template_api = None();
        else
            manifest->doc_template_api = None();
        auto valid = lito::site::validate_frontend_manifest(*manifest);
        ASSERT_TRUE(valid.is_err());
        auto validation_error = rstd::move(valid).unwrap_err();
        auto paths            = lito::site::frontend_manifest_paths(*manifest);
        ASSERT_TRUE(paths.is_err());
        EXPECT_EQ(paths.unwrap_err().as_str(), validation_error.as_str());
        auto bundle = lito::site::make_frontend_bundle(
            "direct"_Str,
            "digest"_Str,
            rstd::collections::BTreeMap<String, lito::site::FrontendResource>::make(),
            rstd::move(manifest).unwrap());
        ASSERT_TRUE(bundle.is_err());
        EXPECT_EQ(bundle.unwrap_err().as_str(), validation_error.as_str());
    }
}

TEST(Frontend, DirectoryAndEmbeddedResourcesShareValidation) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto           root             = temporary->path();
    constexpr auto manifest         = R"json({
  "format": "lito-doc-frontend",
  "version": 2,
  "template-api": 1,
  "book-data-api": 1,
  "book-template-api": 2,
  "capabilities": ["book"],
  "templates": {
    "book-root": "templates/root.html",
    "book-page": "templates/page.html",
    "book-not-found": "templates/missing.html",
    "book-redirect": "templates/moved.html"
  },
  "partials": [],
  "assets": []
}
)json"_str;
    constexpr auto root_template    = "{{site.title}}"_str;
    constexpr auto page_template    = "{{page.title}}"_str;
    constexpr auto missing_template = "<base href=\"{{page.base_href}}\">Missing"_str;
    constexpr auto moved_template   = "<a href=\"{{page.redirect.href}}\">Moved</a>"_str;
    ASSERT_TRUE(write_text(root, "frontend.json"_str, manifest).is_ok());
    ASSERT_TRUE(write_text(root, "templates/root.html"_str, root_template).is_ok());
    ASSERT_TRUE(write_text(root, "templates/page.html"_str, page_template).is_ok());
    ASSERT_TRUE(write_text(root, "templates/missing.html"_str, missing_template).is_ok());
    ASSERT_TRUE(write_text(root, "templates/moved.html"_str, moved_template).is_ok());

    auto directory = lito::site::load_frontend_directory(root);
    ASSERT_TRUE(directory.is_ok());
    const lito::site::FrontendResourceInput resources[] = {
        { .path       = "frontend.json"_str,
          .media_type = "application/json"_str,
          .contents   = manifest.as_bytes() },
        { .path       = "templates/page.html"_str,
          .media_type = "text/html"_str,
          .contents   = page_template.as_bytes() },
        { .path       = "templates/root.html"_str,
          .media_type = "text/html"_str,
          .contents   = root_template.as_bytes() },
        { .path       = "templates/missing.html"_str,
          .media_type = "text/html"_str,
          .contents   = missing_template.as_bytes() },
        { .path       = "templates/moved.html"_str,
          .media_type = "text/html"_str,
          .contents   = moved_template.as_bytes() },
    };
    auto embedded = lito::site::load_frontend_resources(
        "embedded:test"_str,
        slice<lito::site::FrontendResourceInput>::from_raw_parts(
            resources, usize(sizeof(resources) / sizeof(resources[0]))));
    ASSERT_TRUE(embedded.is_ok());
    EXPECT_EQ(embedded->digest.as_str(), directory->digest.as_str());
    EXPECT_FALSE(embedded->supports_api);
    EXPECT_TRUE(embedded->supports_book);
}

TEST(Frontend, LoadsEmbeddedDefaultBundle) {
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    EXPECT_TRUE(frontend->supports_api);
    EXPECT_TRUE(frontend->supports_book);
    EXPECT_EQ(frontend->doc_template_api, usize(2));
    EXPECT_EQ(frontend->book_template_api, usize(2));
    EXPECT_EQ(frontend->assets.len(), usize(7));
    EXPECT_FALSE(frontend->digest.is_empty());
    auto bootstrap_gates_document        = false;
    auto application_reveals_document    = false;
    auto application_persists_navigation = false;
    auto stylesheet_gates_document       = false;
    auto theme_icons_are_embedded        = false;
    for (const auto& asset : frontend->assets) {
        if (asset.path.as_str() == "static/theme-bootstrap.js"_str)
            bootstrap_gates_document =
                asset.contents.as_str().contains("data-document-pending"_str);
        if (asset.path.as_str() == "static/app.js"_str)
            application_reveals_document =
                asset.contents.as_str().contains("data-document-pending"_str);
        if (asset.path.as_str() == "static/app.js"_str)
            application_persists_navigation =
                asset.contents.as_str().contains("lito-doc-navigation-scroll:"_str);
        if (asset.path.as_str() == "static/style.css"_str)
            stylesheet_gates_document =
                asset.contents.as_str().contains("data-document-pending"_str);
        if (asset.path.as_str() == "static/theme-icons.svg"_str)
            theme_icons_are_embedded = asset.contents.as_str().contains("<symbol id=\"sun\""_str) &&
                                       asset.contents.as_str().contains("<symbol id=\"moon\""_str);
    }
    EXPECT_TRUE(bootstrap_gates_document);
    EXPECT_TRUE(application_reveals_document);
    EXPECT_TRUE(application_persists_navigation);
    EXPECT_TRUE(stylesheet_gates_document);
    EXPECT_TRUE(theme_icons_are_embedded);
}
