module;
#include <rstd/macro.hpp>

export module lito.book:project;

import rstd;
import rstd.toml;
import lito.crypto;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;
using TomlTable = rstd::toml::Table;

namespace lito::book {

template <typename T>
auto project_error(ref<rstd::path::Path> path, ref<str> message)
    -> Result<T, String> {
  return Err(rstd::format("{}: {}", path, message));
}

auto book_name_is_valid(ref<str> value) -> bool {
  if (value.is_empty())
    return false;
  for (auto character : value) {
    const auto byte = character.to_primitive();
    const auto valid =
        (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
    if (!valid)
      return false;
  }
  return true;
}

auto table_at(const Toml &value, ref<str> key, ref<rstd::path::Path> path,
              bool required) -> Result<Option<ref<TomlTable>>, String> {
  auto member = value.get(key);
  if (member.is_none()) {
    if (required)
      return project_error<Option<ref<TomlTable>>>(
          path, rstd::format("missing table [{}]", key).as_str());
    return Ok(Option<ref<TomlTable>>{});
  }
  auto table = (**member).as_table();
  if (table.is_none())
    return project_error<Option<ref<TomlTable>>>(
        path, rstd::format("[{}] must be a table", key).as_str());
  return Ok(Some(*table));
}

auto reject_keys(const TomlTable &table, slice<ref<str>> allowed,
                 ref<str> context, ref<rstd::path::Path> path)
    -> Result<empty, String> {
  auto keys = table.keys();
  for (auto key = keys.next(); key.is_some(); key = keys.next()) {
    auto known = false;
    for (auto candidate : allowed) {
      if ((**key).as_str() == candidate) {
        known = true;
        break;
      }
    }
    if (!known)
      return project_error<empty>(path,
                                  rstd::format("{}.{} is not a supported field",
                                               context, (**key).as_str())
                                      .as_str());
  }
  return Ok(empty{});
}

auto string_at(const TomlTable &table, ref<str> key, ref<str> context,
               ref<rstd::path::Path> path, bool required,
               ref<str> fallback = {}) -> Result<Option<String>, String> {
  auto value = table.get(key);
  if (value.is_none()) {
    if (!fallback.is_empty())
      return Ok(Some(String::make(fallback)));
    if (required)
      return project_error<Option<String>>(
          path, rstd::format("{}.{} is required", context, key).as_str());
    return Ok(Option<String>{});
  }
  auto text = (**value).as_str();
  if (text.is_none())
    return project_error<Option<String>>(
        path, rstd::format("{}.{} must be a string", context, key).as_str());
  if (text->is_empty())
    return project_error<Option<String>>(
        path, rstd::format("{}.{} must not be empty", context, key).as_str());
  return Ok(Some(String::make(*text)));
}

auto resolve_existing_directory(ref<rstd::path::Path> root, ref<str> value,
                                ref<rstd::path::Path> manifest, ref<str> field)
    -> Result<rstd::path::PathBuf, String> {
  auto relative = rstd::path::PathBuf::from(value);
  if (relative.is_empty() || !relative.as_path().is_safe_relative())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("{} must be a safe relative path", field).as_str());
  auto requested = rstd::path::PathBuf::from(root).join(relative.as_path());
  auto canonical = rstd::fs::canonicalize(requested.as_path());
  if (canonical.is_err())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("cannot resolve {} '{}': {}", field, requested.as_path(),
                     rstd::move(canonical).unwrap_err())
            .as_str());
  if (canonical->as_path().strip_prefix(root).is_none())
    return project_error<rstd::path::PathBuf>(
        manifest, rstd::format("{} escapes the Book root", field).as_str());
  auto metadata = rstd::fs::metadata(canonical->as_path());
  if (metadata.is_err() || !metadata->is_dir())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("{} '{}' must be a directory", field, canonical->as_path())
            .as_str());
  return Ok(rstd::move(canonical).unwrap());
}

auto resolve_existing_file(ref<rstd::path::Path> root, ref<str> value,
                           ref<rstd::path::Path> manifest, ref<str> field)
    -> Result<rstd::path::PathBuf, String> {
  auto relative = rstd::path::PathBuf::from(value);
  if (relative.is_empty() || !relative.as_path().is_safe_relative())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("{} must be a safe relative path", field).as_str());
  auto requested = rstd::path::PathBuf::from(root).join(relative.as_path());
  auto canonical = rstd::fs::canonicalize(requested.as_path());
  if (canonical.is_err())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("cannot resolve {} '{}': {}", field, requested.as_path(),
                     rstd::move(canonical).unwrap_err())
            .as_str());
  if (canonical->as_path().strip_prefix(root).is_none())
    return project_error<rstd::path::PathBuf>(
        manifest, rstd::format("{} escapes its source root", field).as_str());
  auto metadata = rstd::fs::metadata(canonical->as_path());
  if (metadata.is_err() || !metadata->is_file())
    return project_error<rstd::path::PathBuf>(
        manifest,
        rstd::format("{} '{}' must be a file", field, canonical->as_path())
            .as_str());
  return Ok(rstd::move(canonical).unwrap());
}

auto resolve_override_directory(ref<rstd::path::Path> root,
                                ref<rstd::path::Path> value, ref<str> field)
    -> Result<rstd::path::PathBuf, String> {
  if (value.is_empty())
    return Err(rstd::format("{} must not be empty", field));
  auto requested = value.is_absolute()
                       ? rstd::path::PathBuf::from(value)
                       : rstd::path::PathBuf::from(root).join(value);
  auto canonical = rstd::fs::canonicalize(requested.as_path());
  if (canonical.is_err())
    return Err(rstd::format("cannot resolve {} '{}': {}", field,
                            requested.as_path(),
                            rstd::move(canonical).unwrap_err()));
  auto metadata = rstd::fs::metadata(canonical->as_path());
  if (metadata.is_err() || !metadata->is_dir())
    return Err(rstd::format("{} '{}' must be a directory", field,
                            canonical->as_path()));
  return Ok(rstd::move(canonical).unwrap());
}

auto output_path(ref<rstd::path::Path> root, ref<rstd::path::Path> value)
    -> Result<rstd::path::PathBuf, String> {
  if (value.is_empty())
    return Err(String::make("Book output must not be empty"_str));
  return Ok(value.is_absolute() ? rstd::path::PathBuf::from(value)
                                : rstd::path::PathBuf::from(root).join(value));
}

} // namespace lito::book

export namespace lito::book {

auto discover_book_manifest(ref<rstd::path::Path> requested)
    -> Result<rstd::path::PathBuf, String> {
  auto directory = rstd::fs::canonicalize(requested);
  if (directory.is_err())
    return Err(rstd::format("cannot resolve Book working directory '{}': {}",
                            requested, rstd::move(directory).unwrap_err()));
  auto metadata = rstd::fs::metadata(directory->as_path());
  if (metadata.is_err() || !metadata->is_dir())
    return Err(rstd::format("Book working directory '{}' is not a directory",
                            directory->as_path()));
  auto current = rstd::move(directory).unwrap();
  while (true) {
    auto candidate =
        current.join(rstd::path::PathBuf::from("book.toml"_str).as_path());
    auto exists = rstd::fs::exists(candidate.as_path());
    if (exists.is_err())
      return Err(rstd::format("cannot inspect Book manifest '{}': {}",
                              candidate.as_path(),
                              rstd::move(exists).unwrap_err()));
    if (*exists)
      return Ok(rstd::move(candidate));
    auto parent = current.as_path().parent();
    if (parent.is_none() || *parent == current.as_path())
      break;
    current = rstd::path::PathBuf::from(*parent);
  }
  return Err(rstd::format("cannot find book.toml from '{}' or its ancestors",
                          requested));
}

auto load_book_project(
    ref<rstd::path::Path> requested,
    const Option<rstd::path::PathBuf> &output_override = {},
    const Option<rstd::path::PathBuf> &frontend_override = {})
    -> Result<BookProject, String> {
  auto manifest = rstd_try(discover_book_manifest(requested));
  auto root = rstd::path::PathBuf::from(manifest.as_path().parent().unwrap());
  auto contents = rstd::fs::read_to_string(manifest.as_path());
  if (contents.is_err())
    return project_error<BookProject>(
        manifest.as_path(), rstd::format("cannot read manifest: {}",
                                         rstd::move(contents).unwrap_err())
                                .as_str());
  auto parsed = rstd::toml::from_str(contents->as_str());
  if (parsed.is_err())
    return project_error<BookProject>(
        manifest.as_path(),
        rstd::format("invalid TOML: {}", rstd::move(parsed).unwrap_err())
            .as_str());
  auto root_table = parsed->as_table();
  if (root_table.is_none())
    return project_error<BookProject>(manifest.as_path(),
                                      "manifest must be a table"_str);
  const auto root_keys =
      array<ref<str>, 3>{"book"_str, "build"_str, "output"_str};
  rstd_try(reject_keys(**root_table, root_keys.as_slice(), "manifest"_str,
                       manifest.as_path()));
  auto book = rstd_try(table_at(*parsed, "book"_str, manifest.as_path(), true));
  const auto book_keys =
      array<ref<str>, 6>{"name"_str,     "title"_str, "version"_str,
                         "language"_str, "src"_str,   "summary"_str};
  rstd_try(reject_keys(**book, book_keys.as_slice(), "book"_str,
                       manifest.as_path()));
  auto name = rstd_try(
      string_at(**book, "name"_str, "book"_str, manifest.as_path(), true));
  auto title = rstd_try(
      string_at(**book, "title"_str, "book"_str, manifest.as_path(), true));
  auto version = rstd_try(
      string_at(**book, "version"_str, "book"_str, manifest.as_path(), false));
  auto language = rstd_try(
      string_at(**book, "language"_str, "book"_str, manifest.as_path(), false));
  auto source = rstd_try(string_at(**book, "src"_str, "book"_str,
                                   manifest.as_path(), false, "src"_str));
  auto summary =
      rstd_try(string_at(**book, "summary"_str, "book"_str, manifest.as_path(),
                         false, "SUMMARY.md"_str));
  if (!book_name_is_valid(name->as_str()))
    return project_error<BookProject>(
        manifest.as_path(), "book.name must be a path-safe identifier"_str);

  auto output_text = String::make("build/book"_str);
  auto build =
      rstd_try(table_at(*parsed, "build"_str, manifest.as_path(), false));
  if (build.is_some()) {
    const auto build_keys = array<ref<str>, 1>{"output"_str};
    rstd_try(reject_keys(**build, build_keys.as_slice(), "build"_str,
                         manifest.as_path()));
    auto configured = rstd_try(string_at(**build, "output"_str, "build"_str,
                                         manifest.as_path(), false));
    if (configured.is_some())
      output_text = rstd::move(configured).unwrap();
  }
  auto frontend_text = String::make("default"_str);
  auto output =
      rstd_try(table_at(*parsed, "output"_str, manifest.as_path(), false));
  if (output.is_some()) {
    const auto output_keys = array<ref<str>, 1>{"html"_str};
    rstd_try(reject_keys(**output, output_keys.as_slice(), "output"_str,
                         manifest.as_path()));
    auto html = (**output).get("html"_str);
    if (html.is_some()) {
      auto table = (**html).as_table();
      if (table.is_none())
        return project_error<BookProject>(manifest.as_path(),
                                          "output.html must be a table"_str);
      const auto html_keys = array<ref<str>, 1>{"frontend"_str};
      rstd_try(reject_keys(**table, html_keys.as_slice(), "output.html"_str,
                           manifest.as_path()));
      auto configured =
          rstd_try(string_at(**table, "frontend"_str, "output.html"_str,
                             manifest.as_path(), false));
      if (configured.is_some())
        frontend_text = rstd::move(configured).unwrap();
    }
  }

  auto source_root = rstd_try(resolve_existing_directory(
      root.as_path(), source->as_str(), manifest.as_path(), "book.src"_str));
  auto summary_path =
      rstd_try(resolve_existing_file(source_root.as_path(), summary->as_str(),
                                     manifest.as_path(), "book.summary"_str));
  auto resolved_output =
      output_override.is_some()
          ? rstd_try(output_path(root.as_path(), output_override->as_path()))
          : rstd_try(
                output_path(root.as_path(),
                            rstd::path::PathBuf::from(output_text).as_path()));
  auto resolved_frontend = Option<rstd::path::PathBuf>{};
  if (frontend_override.is_some()) {
    resolved_frontend = Some(rstd_try(
        resolve_override_directory(root.as_path(), frontend_override->as_path(),
                                   "frontend override"_str)));
  } else if (frontend_text.as_str() != "default"_str) {
    resolved_frontend = Some(rstd_try(resolve_existing_directory(
        root.as_path(), frontend_text.as_str(), manifest.as_path(),
        "output.html.frontend"_str)));
  }
  auto identity = lito::crypto::sha256_hex(
      rstd::format("lito-book-project-v1\n{}", name->as_str()).as_str());
  return Ok(BookProject{
      .root = rstd::move(root),
      .manifest = rstd::move(manifest),
      .identity = rstd::move(identity),
      .name = rstd::move(name).unwrap(),
      .title = rstd::move(title).unwrap(),
      .version = rstd::move(version),
      .language = rstd::move(language),
      .source_root = rstd::move(source_root),
      .summary = rstd::move(summary_path),
      .output = rstd::move(resolved_output),
      .frontend = rstd::move(resolved_frontend),
  });
}

} // namespace lito::book
