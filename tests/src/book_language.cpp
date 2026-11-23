#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.book;
import lito.doc;
import lito.doc.web;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto language_path(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto language_write(ref<rstd::path::Path> root, ref<str> relative, ref<str> contents)
    -> rstd::io::Result<empty> {
    auto path    = language_path(root, relative);
    auto created = rstd::fs::create_dir_all(path.as_path().parent().unwrap());
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    return rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
}

auto language_book(ref<str> configuration) -> rstd::io::Result<rstd::test::TempDir> {
    auto temporary = rstd::test::TempDir::make();
    if (temporary.is_err()) return Err(rstd::move(temporary).unwrap_err());
    auto root     = temporary->path();
    auto manifest = rstd::format("[book]\nname = \"language-book\"\ntitle = \"Language Book\"\n{}",
                                 configuration);
    auto written  = language_write(root, "book.toml"_str, manifest.as_str());
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = language_write(
        root, "src/SUMMARY.md"_str, "- [Home](index.md)\n  - [Install](guide/install.md)\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = language_write(root, "src/index.md"_str, "# Home\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    written = language_write(root, "src/guide/install.md"_str, "# Install\n"_str);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(rstd::move(temporary).unwrap());
}

auto language_api_package(ref<rstd::path::Path> root) -> lito::doc::PackageInput {
    auto source = language_path(root, "src/api.cpp"_str);
    auto unit   = lito::doc::DocumentationUnit {
        .source          = source.clone(),
        .source_contents = String::make("int value();\n"_str),
        .logical_module  = String::make("api"_str),
    };
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("api-value"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("value"_str),
        .qualified_name    = String::make("value"_str),
        .signature         = String::make("int value();"_str),
        .scope_signature   = String::make("int value();"_str),
        .spelling_span     = { .path         = source.clone(),
                               .begin_line   = usize(1),
                               .begin_column = usize(1),
                               .end_line     = usize(1),
                               .end_column   = usize(13) },
        .expansion_span    = { .path         = source.clone(),
                               .begin_line   = usize(1),
                               .begin_column = usize(1),
                               .end_line     = usize(1),
                               .end_column   = usize(13) },
    });
    auto package = lito::doc::PackageInput {
        .name              = String::make("api"_str),
        .version           = String::make("1.0.0"_str),
        .source_identity   = String::make("path+api"_str),
        .root_module       = String::make("api"_str),
        .profile           = String::make("debug"_str),
        .root              = PathBuf::from(root),
        .toolchain_version = String::make("test"_str),
        .toolchain_target  = String::make("test"_str),
        .language_standard = String::make("c++20"_str),
    };
    package.units.push(rstd::move(unit));
    return package;
}

} // namespace

TEST(BookLanguage, UsesConfiguredLanguageOnRootAndNestedPages) {
    auto created = language_book("language = \"zh-CN\"\n"_str);
    ASSERT_TRUE(created.is_ok());
    auto project  = rstd::move(created).unwrap();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) },
                                   Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(built.is_ok());
    for (auto page : array<ref<str>, 2> { "build/book/index.html"_str,
                                          "build/book/guide/install/index.html"_str }) {
        auto html = rstd::fs::read_to_string(language_path(project.path(), page).as_path());
        ASSERT_TRUE(html.is_ok());
        EXPECT_TRUE(html->as_str().contains("<html lang=\"zh-CN\">"_str));
    }
}

TEST(BookLanguage, DefaultsToEnglishWithoutConfiguration) {
    auto created = language_book({});
    ASSERT_TRUE(created.is_ok());
    auto project  = rstd::move(created).unwrap();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) },
                                   Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(built.is_ok());
    for (auto page : array<ref<str>, 2> { "build/book/index.html"_str,
                                          "build/book/guide/install/index.html"_str }) {
        auto html = rstd::fs::read_to_string(language_path(project.path(), page).as_path());
        ASSERT_TRUE(html.is_ok());
        EXPECT_TRUE(html->as_str().contains("<html lang=\"en\">"_str));
    }
}

TEST(BookLanguage, EscapesTheLanguageAttribute) {
    auto created = language_book("language = 'zh-CN\" data-test=\"unsafe&<value>'\n"_str);
    ASSERT_TRUE(created.is_ok());
    auto project  = rstd::move(created).unwrap();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto built = lito::book::build({ .directory = PathBuf::from(project.path()) },
                                   Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(built.is_ok());
    auto html = rstd::fs::read_to_string(
        language_path(project.path(), "build/book/index.html"_str).as_path());
    ASSERT_TRUE(html.is_ok());
    EXPECT_TRUE(html->as_str().contains(
        "<html lang=\"zh-CN&quot; data-test=&quot;unsafe&amp;&lt;value&gt;\">"_str));
    EXPECT_FALSE(html->as_str().contains("lang=\"zh-CN\" data-test="_str));
}

TEST(BookLanguage, ApiLayoutsKeepEnglishAsTheDefault) {
    for (auto publication : array<lito::doc::PublicationKind, 2> {
             lito::doc::PublicationKind::Site, lito::doc::PublicationKind::PackageSet }) {
        auto temporary = rstd::test::TempDir::make();
        ASSERT_TRUE(temporary.is_ok());
        auto root     = temporary->path();
        auto frontend = lito::doc::web::load_default_frontend();
        ASSERT_TRUE(frontend.is_ok());
        auto input = lito::doc::SiteInput {
            .title       = String::make("API Language"_str),
            .output      = language_path(root, "site"_str),
            .data_output = language_path(root, "data"_str),
            .publication = publication,
        };
        input.packages.push(language_api_package(root));
        auto generated =
            lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
        ASSERT_TRUE(generated.is_ok());
        auto package_page = publication == lito::doc::PublicationKind::Site
                                ? "site/package/api/index.html"_str
                                : "site/api/index.html"_str;
        auto html         = rstd::fs::read_to_string(language_path(root, package_page).as_path());
        ASSERT_TRUE(html.is_ok());
        EXPECT_TRUE(html->as_str().contains("<html lang=\"en\">"_str));
        if (publication == lito::doc::PublicationKind::Site) {
            auto index =
                rstd::fs::read_to_string(language_path(root, "site/index.html"_str).as_path());
            ASSERT_TRUE(index.is_ok());
            EXPECT_TRUE(index->as_str().contains("<html lang=\"en\">"_str));
        }
    }
}
