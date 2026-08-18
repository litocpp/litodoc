#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.book;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace {

auto write_text(ref<rstd::path::Path> root, ref<str> relative,
                ref<str> contents) -> rstd::io::Result<empty> {
  auto path = PathBuf::from(root).join(PathBuf::from(relative).as_path());
  auto parent = path.as_path().parent().unwrap();
  auto created = rstd::fs::create_dir_all(parent);
  if (created.is_err())
    return Err(rstd::move(created).unwrap_err());
  return rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
}

auto child(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
  return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto make_book_project() -> rstd::io::Result<rstd::test::TempDir> {
  auto temporary = rstd::test::TempDir::make();
  if (temporary.is_err())
    return temporary;
  auto root = temporary->path();
  auto written = write_text(root, "book.toml"_str,
                            "[book]\n"
                            "name = \"inline-book\"\n"
                            "title = \"Inline Book\"\n"
                            "version = \"1.2.3\"\n"
                            "language = \"en\"\n"
                            "\n"
                            "[output.html]\n"
                            "frontend = \"frontend\"\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(root, "src/SUMMARY.md"_str,
                       "# Summary\n"
                       "\n"
                       "- [Home](index.md)\n"
                       "  - [Install](guide/install.md)\n"
                       "- [Reference](reference.md)\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(
      root, "src/index.md"_str,
      "# Welcome\n"
      "\n"
      "Read the **install** [instructions](guide/install.md#install).\n"
      "\n"
      "## Repeated\n"
      "\n"
      "## Repeated\n"
      "\n"
      "Jump [again](#repeated-2).\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(root, "src/guide/install.md"_str,
                       "# Install\n"
                       "\n"
                       "> Keep the setup local.\n"
                       "\n"
                       "1. Build\n"
                       "  - Keep it local\n"
                       "2. Open the [reference](../reference.md).\n"
                       "\n"
                       "![Diagram](image.svg)\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written =
      write_text(root, "src/guide/image.svg"_str,
                 "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(root, "src/reference.md"_str,
                       "# Reference\n"
                       "\n"
                       "Use `litobook build`.\n"
                       "\n"
                       "```cpp\n"
                       "auto value = 1 < 2;\n"
                       "```\n"
                       "\n"
                       "<script>unsafe()</script>\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(root, "frontend/frontend.json"_str,
                       "{\n"
                       "  \"format\": \"lito-doc-frontend\",\n"
                       "  \"version\": 2,\n"
                       "  \"template-api\": 1,\n"
                       "  \"book-data-api\": 1,\n"
                       "  \"capabilities\": [\"book\"],\n"
                       "  \"templates\": {\n"
                       "    \"book-root\": \"templates/root.html\",\n"
                       "    \"book-page\": \"templates/page.html\"\n"
                       "  },\n"
                       "  \"partials\": [],\n"
                       "  \"assets\": []\n"
                       "}\n"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(
      root, "frontend/templates/root.html"_str,
      "<!doctype html><title>{{site.title}}</title>{{{book.content}}}"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  written = write_text(
      root, "frontend/templates/page.html"_str,
      "<!doctype html><title>{{book.title}}</title>{{{book.content}}}"_str);
  if (written.is_err())
    return Err(rstd::move(written).unwrap_err());
  return Ok(rstd::move(temporary).unwrap());
}

} // namespace

TEST(Book, ChecksAndPublishesAnInlineProject) {
  auto created = make_book_project();
  ASSERT_TRUE(created.is_ok());
  auto project = rstd::move(created).unwrap();

  auto checked = lito::book::check(lito::book::BookCheckInput{
      .directory = child(project.path(), "src/guide"_str),
  });
  ASSERT_TRUE(checked.is_ok());
  EXPECT_EQ(checked->pages, usize(3));
  EXPECT_EQ(checked->headings, usize(5));
  EXPECT_EQ(checked->links, usize(4));
  EXPECT_FALSE(
      rstd::fs::exists(child(project.path(), "build/book"_str).as_path())
          .unwrap());

  auto built = lito::book::build(lito::book::BookBuildInput{
      .directory = PathBuf::from(project.path()),
  });
  ASSERT_TRUE(built.is_ok());
  EXPECT_EQ(built->pages, usize(3));
  EXPECT_TRUE(built->site_generated);
  EXPECT_TRUE(
      rstd::fs::exists(
          child(project.path(), "build/book/data/book.json"_str).as_path())
          .unwrap());
  auto index = rstd::fs::read_to_string(
      child(project.path(), "build/book/index.html"_str).as_path());
  ASSERT_TRUE(index.is_ok());
  EXPECT_TRUE(index->as_str().contains(
      "href=\"guide/install/index.html#install\""_str));
  EXPECT_TRUE(index->as_str().contains("href=\"#repeated-2\""_str));
  EXPECT_TRUE(index->as_str().contains("id=\"repeated-2\""_str));
  auto install = rstd::fs::read_to_string(
      child(project.path(), "build/book/guide/install/index.html"_str)
          .as_path());
  ASSERT_TRUE(install.is_ok());
  EXPECT_TRUE(install->as_str().contains(
      "<ol><li>Build<ul><li>Keep it local</li></ul></li>"_str));
  EXPECT_TRUE(install->as_str().contains("src=\"../../assets/"_str));
  auto reference = rstd::fs::read_to_string(
      child(project.path(), "build/book/reference/index.html"_str).as_path());
  ASSERT_TRUE(reference.is_ok());
  EXPECT_TRUE(reference->as_str().contains("class=\"language-cpp\""_str));
  EXPECT_TRUE(reference->as_str().contains(
      "&lt;script&gt;unsafe()&lt;/script&gt;"_str));

  auto loaded = lito::book::load_book_dataset(
      child(project.path(), "build/book"_str).as_path());
  ASSERT_TRUE(loaded.is_ok());
  EXPECT_FALSE(loaded->identity.is_empty());
  EXPECT_EQ(loaded->title.as_str(), "Inline Book"_str);
  EXPECT_EQ(loaded->pages.len(), usize(3));

  ASSERT_TRUE(write_text(project.path(), "src/index.md"_str,
                         "# Broken\n\n[missing](reference.md#missing)\n"_str)
                  .is_ok());
  auto failed = lito::book::build(lito::book::BookBuildInput{
      .directory = PathBuf::from(project.path()),
  });
  ASSERT_TRUE(failed.is_err());
  auto preserved = rstd::fs::read_to_string(
      child(project.path(), "build/book/index.html"_str).as_path());
  ASSERT_TRUE(preserved.is_ok());
  EXPECT_EQ(preserved->as_str(), index->as_str());
}

TEST(Book, ReportsSummaryAndLinkDiagnosticsWithLocations) {
  auto created = make_book_project();
  ASSERT_TRUE(created.is_ok());
  auto project = rstd::move(created).unwrap();
  ASSERT_TRUE(write_text(project.path(), "src/index.md"_str,
                         "# Welcome\n\n[missing](reference.md#missing)\n"_str)
                  .is_ok());
  auto missing = lito::book::check(lito::book::BookCheckInput{
      .directory = PathBuf::from(project.path()),
  });
  ASSERT_TRUE(missing.is_err());
  auto missing_error = rstd::move(missing).unwrap_err();
  EXPECT_TRUE(missing_error.as_str().contains("index.md:3:"_str));
  EXPECT_TRUE(missing_error.as_str().contains("has no heading"_str));

  ASSERT_TRUE(write_text(project.path(), "src/SUMMARY.md"_str,
                         "- [Home](index.md)\n"
                         "    - [Reference](reference.md)\n"_str)
                  .is_ok());
  auto nesting = lito::book::check(lito::book::BookCheckInput{
      .directory = PathBuf::from(project.path()),
  });
  ASSERT_TRUE(nesting.is_err());
  auto nesting_error = rstd::move(nesting).unwrap_err();
  EXPECT_TRUE(nesting_error.as_str().contains("SUMMARY.md:2:"_str));
  EXPECT_TRUE(nesting_error.as_str().contains("skips a level"_str));

  ASSERT_TRUE(write_text(project.path(), "book.toml"_str,
                         "[book]\n"
                         "name = \"inline-book\"\n"
                         "title = \"Inline Book\"\n"
                         "unexpected = true\n"_str)
                  .is_ok());
  auto manifest = lito::book::check(lito::book::BookCheckInput{
      .directory = PathBuf::from(project.path()),
  });
  ASSERT_TRUE(manifest.is_err());
  auto manifest_error = rstd::move(manifest).unwrap_err();
  EXPECT_TRUE(manifest_error.as_str().contains("book.unexpected"_str));
}

TEST(Book, PreservesVersionOneApiFrontendCompatibility) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();
  ASSERT_TRUE(write_text(root, "frontend.json"_str,
                         "{\n"
                         "  \"format\": \"lito-doc-frontend\",\n"
                         "  \"version\": 1,\n"
                         "  \"template-api\": 1,\n"
                         "  \"data-api\": 2,\n"
                         "  \"templates\": {\n"
                         "    \"root\": \"root.html\",\n"
                         "    \"package\": \"package.html\",\n"
                         "    \"module\": \"module.html\",\n"
                         "    \"symbol\": \"symbol.html\",\n"
                         "    \"source\": \"source.html\"\n"
                         "  },\n"
                         "  \"partials\": [],\n"
                         "  \"assets\": []\n"
                         "}\n"_str)
                  .is_ok());
  ASSERT_TRUE(write_text(root, "root.html"_str, "{{site.title}}"_str).is_ok());
  ASSERT_TRUE(
      write_text(root, "package.html"_str, "{{site.title}}"_str).is_ok());
  ASSERT_TRUE(
      write_text(root, "module.html"_str, "{{site.title}}"_str).is_ok());
  ASSERT_TRUE(
      write_text(root, "symbol.html"_str, "{{site.title}}"_str).is_ok());
  ASSERT_TRUE(
      write_text(root, "source.html"_str, "{{site.title}}"_str).is_ok());

  auto frontend = lito::site::load_frontend_directory(root);
  ASSERT_TRUE(frontend.is_ok());
  EXPECT_TRUE(frontend->supports_api);
  EXPECT_FALSE(frontend->supports_book);
}
