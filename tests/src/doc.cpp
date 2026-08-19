#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
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
      .signature = String::make("auto value() -> int"_str),
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
  input.packages.push(publication_package(root, "alpha"_str, "alpha"_str));
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
    EXPECT_EQ(rstd::crypto::sha256_hex(contents->as_str()).as_str(),
              file.sha256.as_str());
    EXPECT_FALSE(file.path.as_str() == "publication.json"_str);
  }
  auto search = rstd::fs::read_to_string(
      child(root, "publication/alpha/search-index.json"_str).as_path());
  ASSERT_TRUE(search.is_ok());
  EXPECT_FALSE(search->as_str().contains("package/alpha/"_str));
  EXPECT_TRUE(search->as_str().contains("symbol/"_str));
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
