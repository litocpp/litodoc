#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import licrypto;
import lito.doc;
import lito.doc.web;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace {

auto child(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
  return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto source_span(const PathBuf &source) -> lito::doc::DocumentationSpan {
  return {
      .path = source.clone(),
      .begin_line = usize(3),
      .begin_column = usize(1),
      .end_line = usize(3),
      .end_column = usize(18),
  };
}

auto source_span(const PathBuf &source, usize line)
    -> lito::doc::DocumentationSpan {
  return {
      .path = source.clone(),
      .begin_line = line,
      .begin_column = usize(1),
      .end_line = line,
      .end_column = usize(24),
  };
}

auto comment(const PathBuf &source, usize line, ref<str> text)
    -> Option<lito::doc::DocumentationComment> {
  return Some(lito::doc::DocumentationComment{
      .text = String::make(text),
      .span = source_span(source, line),
  });
}

auto publication_package(ref<rstd::path::Path> root, ref<str> name,
                         ref<str> module) -> lito::doc::PackageInput {
  auto source = child(root, rstd::format("src/{}.cppm", name).as_str());
  auto unit = lito::doc::DocumentationUnit{
      .source = source.clone(),
      .source_contents = rstd::format(
          "export module {};\n\nexport auto value() -> int;\n", module),
      .logical_module = String::make(module),
      .is_interface = true,
      .module_comment = Some(lito::doc::DocumentationComment{
          .text = String::make("Package documentation."_str),
      }),
      .documented = usize(1),
  };
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = rstd::format("{}::value", module),
      .kind = lito::doc::DeclarationKind::Function,
      .name = String::make("value"_str),
      .qualified_name = rstd::format("{}::value", module),
      .signature = String::make("auto value() -> int;"_str),
      .scope_signature = String::make("auto value() -> int;"_str),
      .is_definition = false,
      .exported = true,
      .spelling_span = source_span(source),
      .expansion_span = source_span(source),
  });
  auto package = lito::doc::PackageInput{
      .name = String::make(name),
      .version = String::make("1.2.3"_str),
      .source_identity =
          rstd::format("git+https://example.test/{}.git#abc", name),
      .root_module = String::make(module),
      .profile = String::make("release"_str),
      .root = PathBuf::from(root),
      .toolchain_version = String::make("clang 22"_str),
      .toolchain_target = String::make("x86_64-unknown-linux-gnu"_str),
      .language_standard = String::make("c++20"_str),
  };
  package.units.push(rstd::move(unit));
  return package;
}

auto append_module(lito::doc::PackageInput &package, ref<rstd::path::Path> root,
                   ref<str> module) -> void {
  package.units.push(lito::doc::DocumentationUnit{
      .source = child(root, "src/child.cppm"_str),
      .source_contents = rstd::format("export module {};\n", module),
      .logical_module = String::make(module),
      .is_interface = true,
      .module_comment = Some(lito::doc::DocumentationComment{
          .text = String::make("Child module documentation."_str),
      }),
  });
}

auto record_package(ref<rstd::path::Path> root) -> lito::doc::PackageInput {
  auto source = child(root, "src/box.cppm"_str);
  auto unit = lito::doc::DocumentationUnit{
      .source = source.clone(),
      .source_contents =
          String::make("export module records;\n\n"
                       "export template <typename T> class Box;\n"_str),
      .logical_module = String::make("records"_str),
      .is_interface = true,
  };
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box"_str),
      .kind = lito::doc::DeclarationKind::Record,
      .name = String::make("Box"_str),
      .qualified_name = String::make("Box"_str),
      .signature = String::make("template <typename T>\nclass Box {};"_str),
      .scope_signature =
          String::make("template <typename T>\nclass Box {};"_str),
      .record_keyword = Some(String::make("class"_str)),
      .record_header =
          Some(String::make("template <typename T>\nclass Box"_str)),
      .is_definition = true,
      .exported = true,
      .comment = comment(source, usize(3), "Stores a value."_str),
      .spelling_span = source_span(source, usize(3)),
      .expansion_span = source_span(source, usize(3)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::value"_str),
      .kind = lito::doc::DeclarationKind::Field,
      .name = String::make("value"_str),
      .qualified_name = String::make("Box::value"_str),
      .signature = String::make("T Box::value{};"_str),
      .scope_signature = String::make("T value{};"_str),
      .is_definition = true,
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(0)),
      .comment = comment(source, usize(5), "Stored value."_str),
      .spelling_span = source_span(source, usize(5)),
      .expansion_span = source_span(source, usize(5)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::flags"_str),
      .kind = lito::doc::DeclarationKind::Field,
      .name = String::make("flags"_str),
      .qualified_name = String::make("Box::flags"_str),
      .signature = String::make("unsigned int Box::flags : 3 = 1;"_str),
      .scope_signature = String::make("unsigned int flags : 3 = 1;"_str),
      .is_definition = true,
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(0)),
      .spelling_span = source_span(source, usize(7)),
      .expansion_span = source_span(source, usize(7)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::get#const"_str),
      .kind = lito::doc::DeclarationKind::Function,
      .name = String::make("get"_str),
      .qualified_name = String::make("Box::get"_str),
      .signature = String::make(
          "template <typename U>\n"
          "    requires Numeric<U>\n"
          "[[nodiscard]] constexpr auto Box::get(U fallback = U{}) const & "
          "noexcept -> T;"_str),
      .scope_signature = String::make(
          "template <typename U>\n"
          "    requires Numeric<U>\n"
          "[[nodiscard]] constexpr auto get(U fallback = U{}) const & noexcept "
          "-> T;"_str),
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(0)),
      .comment = comment(source, usize(9), "Returns the value."_str),
      .spelling_span = source_span(source, usize(9)),
      .expansion_span = source_span(source, usize(9)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::get#rvalue"_str),
      .kind = lito::doc::DeclarationKind::Function,
      .name = String::make("get"_str),
      .qualified_name = String::make("Box::get"_str),
      .signature = String::make("auto Box::get() && -> Box<T>;"_str),
      .scope_signature = String::make("auto get() && -> Box<T>;"_str),
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(0)),
      .spelling_span = source_span(source, usize(11)),
      .expansion_span = source_span(source, usize(11)),
  });
  unit.declarations[usize(4)].scope_signature.references.push(
      lito::doc::DeclarationReference{
          .begin = usize(17),
          .end = usize(20),
          .semantic_identity = String::make("records::Box"_str),
      });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::Nested"_str),
      .kind = lito::doc::DeclarationKind::Record,
      .name = String::make("Nested"_str),
      .qualified_name = String::make("Box::Nested"_str),
      .signature = String::make("struct Box::Nested {};"_str),
      .scope_signature = String::make("struct Nested {};"_str),
      .record_keyword = Some(String::make("struct"_str)),
      .record_header = Some(String::make("struct Nested"_str)),
      .is_definition = true,
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(0)),
      .spelling_span = source_span(source, usize(13)),
      .expansion_span = source_span(source, usize(13)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::make_box"_str),
      .kind = lito::doc::DeclarationKind::Function,
      .name = String::make("make_box"_str),
      .qualified_name = String::make("make_box"_str),
      .signature = String::make("auto make_box() -> Box<int>;"_str),
      .scope_signature = String::make("auto make_box() -> Box<int>;"_str),
      .exported = true,
      .spelling_span = source_span(source, usize(15)),
      .expansion_span = source_span(source, usize(15)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Hidden"_str),
      .kind = lito::doc::DeclarationKind::Record,
      .name = String::make("Hidden"_str),
      .qualified_name = String::make("Hidden"_str),
      .signature = String::make("struct Hidden {};"_str),
      .scope_signature = String::make("struct Hidden {};"_str),
      .record_keyword = Some(String::make("struct"_str)),
      .record_header = Some(String::make("struct Hidden"_str)),
      .is_definition = true,
      .exported = true,
      .access = lito::doc::DeclarationAccess::Private,
      .spelling_span = source_span(source, usize(17)),
      .expansion_span = source_span(source, usize(17)),
  });
  unit.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Hidden::legacy"_str),
      .kind = lito::doc::DeclarationKind::Field,
      .name = String::make("legacy"_str),
      .qualified_name = String::make("Hidden::legacy"_str),
      .signature = String::make("int legacy;"_str),
      .scope_signature = String::make("int legacy;"_str),
      .is_definition = true,
      .is_scope_declaration = true,
      .exported = true,
      .parent = Some(usize(7)),
      .spelling_span = source_span(source, usize(18)),
      .expansion_span = source_span(source, usize(18)),
  });
  auto definition = lito::doc::DocumentationUnit{
      .source = child(root, "src/box.cpp"_str),
      .source_contents =
          String::make("auto Box<int>::get() && -> int {}\n"_str),
      .logical_module = String::make("records"_str),
  };
  definition.declarations.push(lito::doc::DeclarationOutline{
      .semantic_identity = String::make("records::Box::get#rvalue"_str),
      .kind = lito::doc::DeclarationKind::Function,
      .name = String::make("get"_str),
      .qualified_name = String::make("Box::get"_str),
      .signature = String::make("auto Box<int>::get() && -> int;"_str),
      .scope_signature = String::make("auto get() && -> int;"_str),
      .is_definition = true,
      .exported = true,
      .comment = comment(definition.source, usize(1), "Definition docs."_str),
      .spelling_span = source_span(definition.source, usize(1)),
      .expansion_span = source_span(definition.source, usize(1)),
  });
  auto package = lito::doc::PackageInput{
      .name = String::make("records"_str),
      .version = String::make("1.0.0"_str),
      .source_identity = String::make("path+records"_str),
      .root_module = String::make("records"_str),
      .profile = String::make("release"_str),
      .root = PathBuf::from(root),
      .toolchain_version = String::make("clang 22"_str),
      .toolchain_target = String::make("x86_64-unknown-linux-gnu"_str),
      .language_standard = String::make("c++20"_str),
  };
  package.units.push(rstd::move(unit));
  package.units.push(rstd::move(definition));
  return package;
}

auto symbol_page_containing(ref<rstd::path::Path> package_directory,
                            ref<str> needle) -> Result<String, String> {
  auto directory = PathBuf::from(package_directory)
                       .join(PathBuf::from("symbol"_str).as_path());
  auto opened = rstd::fs::read_dir(directory.as_path());
  if (opened.is_err())
    return Err(rstd::format("cannot enumerate '{}': {}", directory.as_path(),
                            rstd::move(opened).unwrap_err()));
  auto entries = rstd::move(opened).unwrap();
  for (auto next = entries.next(); next.is_some(); next = entries.next()) {
    auto entry = rstd::move(next).unwrap();
    if (entry.is_err())
      return Err(rstd::format("cannot enumerate '{}': {}", directory.as_path(),
                              rstd::move(entry).unwrap_err()));
    auto contents = rstd::fs::read_to_string(entry->path().as_path());
    if (contents.is_err())
      return Err(rstd::format("cannot read '{}': {}", entry->path().as_path(),
                              rstd::move(contents).unwrap_err()));
    if (contents->as_str().contains(needle))
      return Ok(rstd::move(contents).unwrap());
  }
  return Err(rstd::format("cannot find symbol page containing '{}'", needle));
}

auto occurrences(ref<str> contents, ref<str> needle) -> usize {
  auto count = usize{};
  auto remaining = contents;
  while (true) {
    auto position = remaining.find(needle);
    if (position.is_none())
      return count;
    ++count;
    remaining =
        remaining.get(*position + needle.len(), remaining.len()).unwrap();
  }
}

} // namespace

TEST(DocPublication, PublishesRelocatablePackageSitesFromOneDataset) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();
  auto frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(frontend.is_ok());
  auto input = lito::doc::SiteInput{
      .title = String::make("Publication fixture"_str),
      .output = child(root, "publication"_str),
      .data_output = child(root, "data"_str),
      .publication = lito::doc::PublicationKind::PackageSet,
  };
  auto alpha_package = publication_package(root, "alpha"_str, "alpha"_str);
  append_module(alpha_package, root, "alpha:child"_str);
  input.packages.push(rstd::move(alpha_package));
  input.packages.push(publication_package(root, "beta"_str, "beta"_str));

  auto generated = lito::doc::generate(rstd::move(input),
                                       Some(rstd::move(frontend).unwrap()));
  ASSERT_TRUE(generated.is_ok());
  ASSERT_TRUE(generated->publication_set.is_some());
  EXPECT_EQ(generated->publication_set->packages.len(), usize(2));
  EXPECT_TRUE(rstd::fs::exists(
                  child(root, "publication/publication-set.json"_str).as_path())
                  .unwrap());
  EXPECT_TRUE(rstd::fs::exists(
                  child(root, "publication/alpha/index.html"_str).as_path())
                  .unwrap());
  EXPECT_FALSE(rstd::fs::exists(
                   child(root, "publication/alpha/package/alpha/index.html"_str)
                       .as_path())
                   .unwrap());

  const auto &alpha = generated->publication_set->packages[usize(0)];
  ASSERT_TRUE(!alpha.files.is_empty());
  for (const auto &file : alpha.files) {
    auto path =
        child(root, rstd::format("publication/alpha/{}", file.path).as_str());
    auto contents = rstd::fs::read_to_string(path.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_EQ(contents->len(), file.size);
    EXPECT_EQ(licrypto::sha256_hex(contents->as_str()).as_str(),
              file.sha256.as_str());
    EXPECT_FALSE(file.path.as_str() == "publication.json"_str);
  }
  auto search = rstd::fs::read_to_string(
      child(root, "publication/alpha/search-index.json"_str).as_path());
  ASSERT_TRUE(search.is_ok());
  EXPECT_FALSE(search->as_str().contains("package/alpha/"_str));
  EXPECT_TRUE(search->as_str().contains("symbol/"_str));
  auto navigation = rstd::fs::read_to_string(
      child(root, "publication/alpha/navigation.json"_str).as_path());
  ASSERT_TRUE(navigation.is_ok());
  EXPECT_TRUE(
      navigation->as_str().contains("\"format\": \"lito-doc-navigation\""_str));
  EXPECT_TRUE(navigation->as_str().contains("\"package\": \"alpha\""_str));
  EXPECT_TRUE(navigation->as_str().contains("\"label\": \"child\""_str));
  EXPECT_TRUE(navigation->as_str().contains("\"url\": \"module/"_str));
  EXPECT_FALSE(navigation->as_str().contains("package/alpha/"_str));
  auto module_navigation_page = String::make();
  for (const auto &file : alpha.files) {
    if (!file.path.as_str().starts_with("module/"_str) ||
        !file.path.as_str().ends_with(".html"_str))
      continue;
    auto contents = rstd::fs::read_to_string(
        child(root, rstd::format("publication/alpha/{}", file.path).as_str())
            .as_path());
    ASSERT_TRUE(contents.is_ok());
    if (contents->as_str().contains("module-navigation-url"_str))
      module_navigation_page = rstd::move(contents).unwrap();
  }
  ASSERT_TRUE(!module_navigation_page.is_empty());
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "module-navigation-url=\"../navigation.json\""_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "module-navigation-package=\"alpha\""_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "scroll-state-url=\"../navigation.json\""_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "../static/theme-icons.svg#moon"_str));
  EXPECT_FALSE(module_navigation_page.as_str().contains("sidebar-footer"_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "<link rel=\"preload\" href=\"../navigation.json\" as=\"fetch\" "
      "crossorigin=\"anonymous\">"_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "<ul class=\"nav-list module-list\" data-module-navigation>"_str));
  EXPECT_TRUE(module_navigation_page.as_str().contains(
      "<a href=\"../index.html\">View package modules</a>"_str));
  EXPECT_FALSE(
      module_navigation_page.as_str().contains("<li><a href=\"../module/"_str));
}

TEST(DocPublication, PackageManifestDoesNotDependOnSiblingPackages) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();

  auto single_frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(single_frontend.is_ok());
  auto single = lito::doc::SiteInput{
      .title = String::make("Single package"_str),
      .output = child(root, "single"_str),
      .data_output = child(root, "single-data"_str),
      .publication = lito::doc::PublicationKind::PackageSet,
  };
  single.packages.push(publication_package(root, "alpha"_str, "alpha"_str));
  auto single_generated = lito::doc::generate(
      rstd::move(single), Some(rstd::move(single_frontend).unwrap()));
  ASSERT_TRUE(single_generated.is_ok());

  auto workspace_frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(workspace_frontend.is_ok());
  auto workspace = lito::doc::SiteInput{
      .title = String::make("Workspace"_str),
      .output = child(root, "workspace"_str),
      .data_output = child(root, "workspace-data"_str),
      .publication = lito::doc::PublicationKind::PackageSet,
  };
  workspace.packages.push(publication_package(root, "alpha"_str, "alpha"_str));
  workspace.packages.push(publication_package(root, "beta"_str, "beta"_str));
  auto workspace_generated = lito::doc::generate(
      rstd::move(workspace), Some(rstd::move(workspace_frontend).unwrap()));
  ASSERT_TRUE(workspace_generated.is_ok());

  auto single_manifest = rstd::fs::read_to_string(
      child(root, "single/alpha/publication.json"_str).as_path());
  auto workspace_manifest = rstd::fs::read_to_string(
      child(root, "workspace/alpha/publication.json"_str).as_path());
  ASSERT_TRUE(single_manifest.is_ok());
  ASSERT_TRUE(workspace_manifest.is_ok());
  EXPECT_EQ(single_manifest->as_str(), workspace_manifest->as_str());
}

TEST(DocPublication, InlinesRecordFunctionsAndFieldsOnTheRecordPage) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();
  auto frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(frontend.is_ok());
  auto input = lito::doc::SiteInput{
      .title = String::make("Record fixture"_str),
      .output = child(root, "publication"_str),
      .data_output = child(root, "data"_str),
      .publication = lito::doc::PublicationKind::PackageSet,
  };
  input.packages.push(record_package(root));

  auto generated = lito::doc::generate(rstd::move(input),
                                       Some(rstd::move(frontend).unwrap()));
  ASSERT_TRUE(generated.is_ok());
  ASSERT_TRUE(generated->publication_set.is_some());
  const auto &publication = generated->publication_set->packages[usize(0)];
  auto symbol_pages = usize{};
  auto record_html = String::make();
  for (const auto &file : publication.files) {
    if (!file.path.as_str().starts_with("symbol/"_str) ||
        !file.path.as_str().ends_with(".html"_str))
      continue;
    ++symbol_pages;
    auto contents = rstd::fs::read_to_string(
        child(root, rstd::format("publication/records/{}", file.path).as_str())
            .as_path());
    ASSERT_TRUE(contents.is_ok());
    if (contents->as_str().contains("<h1 class=\"page-title\">Box</h1>"_str))
      record_html = rstd::move(contents).unwrap();
  }
  EXPECT_EQ(symbol_pages, usize(4));
  ASSERT_TRUE(!record_html.is_empty());
  EXPECT_TRUE(record_html.as_str().contains("Stored value."_str));
  EXPECT_TRUE(record_html.as_str().contains("Returns the value."_str));
  EXPECT_TRUE(record_html.as_str().contains("Definition docs."_str));
  EXPECT_TRUE(record_html.as_str().contains("public:"_str));
  EXPECT_TRUE(record_html.as_str().contains(
      "template &lt;typename T&gt;\nclass Box {"_str));
  EXPECT_TRUE(record_html.as_str().contains("T value{};"_str));
  EXPECT_TRUE(record_html.as_str().contains("unsigned int flags : 3 = 1;"_str));
  EXPECT_TRUE(record_html.as_str().contains(
      "auto get() &amp;&amp; -&gt; <a class=\"declaration-reference\""_str));
  EXPECT_TRUE(record_html.as_str().contains(
      "class=\"declaration-reference\" href=\"../symbol/"_str));
  EXPECT_TRUE(record_html.as_str().contains("\">Box</a>&lt;T&gt;;"_str));
  EXPECT_TRUE(record_html.as_str().contains(
      "template &lt;typename U&gt;\n    requires Numeric&lt;U&gt;\n"
      "[[nodiscard]] constexpr auto get"_str));
  EXPECT_EQ(occurrences(record_html.as_str(), "class=\"member-summary\""_str),
            usize(2));
  EXPECT_EQ(occurrences(record_html.as_str(),
                        "class=\"record-member method-detail\""_str),
            usize(2));
  EXPECT_FALSE(record_html.as_str().contains("<h2><a href=\"#method-"_str));
  EXPECT_EQ(occurrences(record_html.as_str(), "id=\"method-"_str), usize(2));
  EXPECT_EQ(occurrences(record_html.as_str(), "id=\"field-"_str), usize(2));
  EXPECT_FALSE(record_html.as_str().contains("Box&lt;int&gt;::get"_str));
  EXPECT_FALSE(record_html.as_str().contains("box.cpp:1"_str));

  auto search = rstd::fs::read_to_string(
      child(root, "publication/records/search-index.json"_str).as_path());
  ASSERT_TRUE(search.is_ok());
  EXPECT_EQ(occurrences(search->as_str(), "#method-"_str), usize(2));
  EXPECT_EQ(occurrences(search->as_str(), "#field-"_str), usize(2));
}

TEST(DocPublication, RejectsInvalidDeclarationReferenceRanges) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();
  auto package = record_package(root);
  package.units[usize{}]
      .declarations[usize(4)]
      .scope_signature.references[usize{}]
      .end = usize(1024);
  auto input = lito::doc::SiteInput{
      .title = String::make("Invalid declaration reference"_str),
      .output = child(root, "publication"_str),
      .data_output = child(root, "data"_str),
      .data_only = true,
  };
  input.packages.push(rstd::move(package));

  auto generated = lito::doc::generate(rstd::move(input));
  ASSERT_TRUE(generated.is_err());
  EXPECT_TRUE(generated.unwrap_err().as_str().contains(
      "invalid declaration reference [17, 1024)"_str));
}

TEST(DocPublication, ResolvesOnlyAvailableUnambiguousTypePages) {
  auto temporary = rstd::test::TempDir::make();
  ASSERT_TRUE(temporary.is_ok());
  auto root = temporary->path();
  auto owner_package = [root]() mutable {
    auto package = record_package(root);
    package.name = String::make("owner"_str);
    package.source_identity = String::make("path+owner"_str);
    auto &holder = package.units[usize{}].declarations[usize{}];
    holder.semantic_identity = String::make("owner::Holder"_str);
    holder.name = String::make("Holder"_str);
    holder.qualified_name = String::make("Holder"_str);
    holder.signature =
        String::make("template <typename T>\nclass Holder {};"_str);
    holder.scope_signature =
        String::make("template <typename T>\nclass Holder {};"_str);
    holder.record_header =
        Some(String::make("template <typename T>\nclass Holder"_str));
    append_module(package, root, "records:child"_str);
    return package;
  };
  auto target_package = [root](ref<str> name) mutable {
    auto package = record_package(root);
    package.name = String::make(name);
    package.source_identity = rstd::format("path+{}", name);
    return package;
  };

  auto frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(frontend.is_ok());
  auto workspace = lito::doc::SiteInput{
      .title = String::make("Workspace links"_str),
      .output = child(root, "workspace"_str),
      .data_output = child(root, "workspace-data"_str),
  };
  workspace.packages.push(owner_package());
  workspace.packages.push(target_package("target"_str));
  auto generated = lito::doc::generate(rstd::move(workspace),
                                       Some(rstd::move(frontend).unwrap()));
  ASSERT_TRUE(generated.is_ok());
  auto workspace_page =
      symbol_page_containing(generated->packages[usize{}].directory.as_path(),
                             "<h1 class=\"page-title\">Holder</h1>"_str);
  ASSERT_TRUE(workspace_page.is_ok());
  EXPECT_TRUE(
      workspace_page->as_str().contains("href=\"../../target/symbol/"_str));
  EXPECT_TRUE(workspace_page->as_str().contains(
      "module-navigation-url=\"../../../package/owner/navigation.json\""_str));
  EXPECT_TRUE(workspace_page->as_str().contains(
      "scroll-state-url=\"../../../package/owner/navigation.json\""_str));
  EXPECT_TRUE(workspace_page->as_str().contains(
      "href=\"../../../package/owner/index.html\">View package modules"_str));

  frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(frontend.is_ok());
  auto publications = lito::doc::SiteInput{
      .title = String::make("Package links"_str),
      .output = child(root, "publications"_str),
      .data_output = child(root, "publications-data"_str),
      .publication = lito::doc::PublicationKind::PackageSet,
  };
  publications.packages.push(owner_package());
  publications.packages.push(target_package("target"_str));
  generated = lito::doc::generate(rstd::move(publications),
                                  Some(rstd::move(frontend).unwrap()));
  ASSERT_TRUE(generated.is_ok());
  ASSERT_TRUE(generated->publication_set.is_some());
  auto package_page = symbol_page_containing(
      generated->publication_set->packages[usize{}].directory.as_path(),
      "<h1 class=\"page-title\">Holder</h1>"_str);
  ASSERT_TRUE(package_page.is_ok());
  EXPECT_FALSE(
      package_page->as_str().contains("href=\"../../target/symbol/"_str));
  EXPECT_TRUE(package_page->as_str().contains(
      "module-navigation-url=\"../navigation.json\""_str));

  frontend = lito::doc::web::load_default_frontend();
  ASSERT_TRUE(frontend.is_ok());
  auto ambiguous = lito::doc::SiteInput{
      .title = String::make("Ambiguous links"_str),
      .output = child(root, "ambiguous"_str),
      .data_output = child(root, "ambiguous-data"_str),
  };
  ambiguous.packages.push(owner_package());
  ambiguous.packages.push(target_package("target-a"_str));
  ambiguous.packages.push(target_package("target-b"_str));
  generated = lito::doc::generate(rstd::move(ambiguous),
                                  Some(rstd::move(frontend).unwrap()));
  ASSERT_TRUE(generated.is_ok());
  auto ambiguous_page =
      symbol_page_containing(generated->packages[usize{}].directory.as_path(),
                             "<h1 class=\"page-title\">Holder</h1>"_str);
  ASSERT_TRUE(ambiguous_page.is_ok());
  EXPECT_FALSE(
      ambiguous_page->as_str().contains("class=\"declaration-reference\""_str));
}
