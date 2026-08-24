module;
#include <rstd/macro.hpp>

export module lito.site:frontend;

import rstd;
import rstd.json;
import :frontend_manifest;
import :templates;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site {

struct FrontendResource {
  String path;
  String media_type;
  String contents;
};

struct FrontendResourceInput {
  ref<str> path;
  ref<str> media_type;
  slice<u8> contents;
};

struct FrontendAsset {
  String path;
  String media_type;
  String contents;
};

struct FrontendBundle {
  String identity;
  String digest;
  bool supports_api{false};
  bool supports_book{false};
  String root_template;
  String package_template;
  String module_template;
  String symbol_template;
  String source_template;
  String book_root_template;
  String book_page_template;
  TemplateSet templates;
  Vec<FrontendAsset> assets;
};

inline constexpr uint64_t FRONTEND_FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FRONTEND_FNV_PRIME = 1099511628211ull;

auto frontend_hex(uint64_t value) -> String {
  static constexpr char digits[] = "0123456789abcdef";
  char result[16];
  for (size_t index = 0; index < 16; ++index) {
    result[15 - index] = digits[value & 0xfu];
    value >>= 4u;
  }
  return String::make(ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(result), usize(16)));
}

auto frontend_digest(
    const rstd::collections::BTreeMap<String, FrontendResource> &resources)
    -> String {
  auto hash = FRONTEND_FNV_OFFSET;
  auto append = [&hash](ref<str> part) {
    for (auto value : part) {
      hash ^= value.to_primitive();
      hash *= FRONTEND_FNV_PRIME;
    }
    hash ^= 0;
    hash *= FRONTEND_FNV_PRIME;
  };
  auto iterator = resources.iter();
  for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
    append((*(*item).template get<0>()).as_str());
    append((*(*item).template get<1>()).contents.as_str());
  }
  return frontend_hex(hash);
}

auto safe_frontend_path(ref<str> value) -> bool {
  if (value.is_empty() || value.starts_with("/"_str) ||
      value.contains("\\"_str) || value.contains("//"_str))
    return false;
  auto begin = usize{};
  while (begin <= value.len()) {
    auto end = begin;
    while (end < value.len() && value[end] != u8('/'))
      ++end;
    auto part = value.get(begin, end).unwrap();
    if (part.is_empty() || part == "."_str || part == ".."_str)
      return false;
    if (end == value.len())
      break;
    begin = end + usize(1);
  }
  return true;
}

auto parse_frontend_manifest(ref<str> contents)
    -> Result<FrontendManifest, String> {
  auto parsed = rstd::json::decode<FrontendManifest>(contents);
  if (parsed.is_err())
    return Err(rstd::format("invalid frontend manifest JSON: {}",
                            parsed.unwrap_err()));
  if (parsed->format.as_str() != "lito-doc-frontend"_str)
    return Err(
        rstd::format("unsupported frontend format '{}'", parsed->format));
  if (parsed->version != usize(1) && parsed->version != usize(2))
    return Err(rstd::format("unsupported frontend bundle version {}",
                            parsed->version));
  if (parsed->template_api != usize(1))
    return Err(rstd::format("frontend requires unsupported template API {}",
                            parsed->template_api));
  if (parsed->version == usize(1)) {
    if (parsed->data_api.is_none())
      return Err(String::make("frontend manifest is missing 'data-api'"_str));
    if (*parsed->data_api != usize(4))
      return Err(rstd::format("frontend requires unsupported doc data API {}",
                              *parsed->data_api));
    return Ok(rstd::move(parsed).unwrap());
  }
  if (parsed->capabilities.is_none())
    return Err(String::make("frontend manifest is missing 'capabilities'"_str));
  auto api = false;
  auto book = false;
  for (const auto &capability : *parsed->capabilities) {
    if (capability.as_str() == "api"_str) {
      if (api)
        return Err(String::make("frontend repeats capability 'api'"_str));
      api = true;
    } else if (capability.as_str() == "book"_str) {
      if (book)
        return Err(String::make("frontend repeats capability 'book'"_str));
      book = true;
    } else {
      return Err(
          rstd::format("unsupported frontend capability '{}'", capability));
    }
  }
  if (!api && !book)
    return Err(
        String::make("frontend must declare at least one capability"_str));
  if (api) {
    if (parsed->data_api.is_none())
      return Err(String::make("frontend manifest is missing 'data-api'"_str));
    if (*parsed->data_api != usize(4))
      return Err(rstd::format("frontend requires unsupported doc data API {}",
                              *parsed->data_api));
  }
  if (book) {
    if (parsed->book_data_api.is_none())
      return Err(
          String::make("frontend manifest is missing 'book-data-api'"_str));
    if (*parsed->book_data_api != usize(1))
      return Err(rstd::format("frontend requires unsupported book data API {}",
                              *parsed->book_data_api));
  }
  return Ok(rstd::move(parsed).unwrap());
}

auto frontend_capability(const FrontendManifest &manifest, ref<str> requested)
    -> bool {
  if (manifest.version == usize(1))
    return requested == "api"_str;
  for (const auto &capability : *manifest.capabilities) {
    if (capability.as_str() == requested)
      return true;
  }
  return false;
}

auto frontend_manifest_paths(const FrontendManifest &manifest)
    -> Result<Vec<String>, String> {
  if (manifest.templates.is_none())
    return Err(String::make("frontend manifest is missing 'templates'"_str));
  if (manifest.partials.is_none())
    return Err(String::make("frontend manifest is missing 'partials'"_str));
  if (manifest.assets.is_none())
    return Err(String::make("frontend manifest is missing 'assets'"_str));
  auto paths = Vec<String>::make();
  auto append_template = [&paths](const Option<String> &path,
                                  ref<str> name) -> Result<empty, String> {
    if (path.is_none())
      return Err(rstd::format("frontend templates is missing '{}'", name));
    if (!safe_frontend_path(path->as_str()))
      return Err(
          rstd::format("invalid frontend template path '{}'", path->as_str()));
    paths.push(path->clone());
    return Ok(empty{});
  };
  const auto &templates = *manifest.templates;
  auto api = frontend_capability(manifest, "api"_str);
  auto book = frontend_capability(manifest, "book"_str);
  if (api) {
    auto appended = append_template(templates.root, "root"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
    appended = append_template(templates.package, "package"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
    appended = append_template(templates.module, "module"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
    appended = append_template(templates.symbol, "symbol"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
    appended = append_template(templates.source, "source"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
  }
  if (book) {
    auto appended = append_template(templates.book_root, "book-root"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
    appended = append_template(templates.book_page, "book-page"_str);
    if (appended.is_err())
      return Err(rstd::move(appended).unwrap_err());
  }
  for (const auto &partial : *manifest.partials) {
    if (!safe_frontend_path(partial.as_str()))
      return Err(rstd::format("invalid frontend partial path '{}'", partial));
    paths.push(partial.clone());
  }
  for (const auto &asset : *manifest.assets) {
    if (!safe_frontend_path(asset.path.as_str()))
      return Err(rstd::format("invalid frontend asset path '{}'", asset.path));
    paths.push(asset.path.clone());
  }
  return Ok(rstd::move(paths));
}

struct FrontendManifestDescription {
  FrontendManifest manifest;
  Vec<String> paths;
  rstd::collections::BTreeSet<String> declared;
};

auto describe_frontend_manifest(ref<str> contents)
    -> Result<FrontendManifestDescription, String> {
  auto manifest = rstd_try(parse_frontend_manifest(contents));
  auto paths = rstd_try(frontend_manifest_paths(manifest));
  auto declared = rstd::collections::BTreeSet<String>::make();
  declared.insert(String::make("frontend.json"_str));
  for (const auto &path : paths) {
    if (!declared.insert(path.clone()))
      return Err(rstd::format("frontend manifest repeats resource '{}'",
                              path.as_str()));
  }
  return Ok(FrontendManifestDescription{
      .manifest = rstd::move(manifest),
      .paths = rstd::move(paths),
      .declared = rstd::move(declared),
  });
}

auto make_frontend_bundle(
    String identity, String digest,
    rstd::collections::BTreeMap<String, FrontendResource> resources,
    FrontendManifest manifest) -> Result<FrontendBundle, String> {
  if (manifest.templates.is_none())
    return Err(String::make("frontend manifest is missing 'templates'"_str));
  if (manifest.partials.is_none())
    return Err(String::make("frontend manifest is missing 'partials'"_str));
  if (manifest.assets.is_none())
    return Err(String::make("frontend manifest is missing 'assets'"_str));
  auto supports_api = frontend_capability(manifest, "api"_str);
  auto supports_book = frontend_capability(manifest, "book"_str);
  auto root_template = String::make();
  auto package_template = String::make();
  auto module_template = String::make();
  auto symbol_template = String::make();
  auto source_template = String::make();
  auto book_root_template = String::make();
  auto book_page_template = String::make();
  auto read_template = [](Option<String> &value,
                          ref<str> name) -> Result<String, String> {
    if (value.is_none())
      return Err(rstd::format("frontend templates is missing '{}'", name));
    return Ok(rstd::move(value).unwrap());
  };
  auto &templates = *manifest.templates;
  if (supports_api) {
    root_template = rstd_try(read_template(templates.root, "root"_str));
    package_template =
        rstd_try(read_template(templates.package, "package"_str));
    module_template = rstd_try(read_template(templates.module, "module"_str));
    symbol_template = rstd_try(read_template(templates.symbol, "symbol"_str));
    source_template = rstd_try(read_template(templates.source, "source"_str));
  }
  if (supports_book) {
    book_root_template =
        rstd_try(read_template(templates.book_root, "book-root"_str));
    book_page_template =
        rstd_try(read_template(templates.book_page, "book-page"_str));
  }

  auto set = TemplateSet{
      .identity = identity.clone(),
      .documents =
          rstd::collections::BTreeMap<String, TemplateDocument>::make(),
  };
  auto template_paths = Vec<String>::make();
  if (supports_api) {
    template_paths.push(root_template.clone());
    template_paths.push(package_template.clone());
    template_paths.push(module_template.clone());
    template_paths.push(symbol_template.clone());
    template_paths.push(source_template.clone());
  }
  if (supports_book) {
    template_paths.push(book_root_template.clone());
    template_paths.push(book_page_template.clone());
  }
  for (const auto &partial : *manifest.partials) {
    template_paths.push(partial.clone());
  }
  auto seen = rstd::collections::BTreeSet<String>::make();
  for (const auto &path : template_paths) {
    if (!seen.insert(path.clone()))
      continue;
    auto resource = resources.get(path.as_str());
    if (resource.is_none())
      return Err(rstd::format("frontend '{}' is missing template '{}'",
                              identity, path));
    auto parsed = parse_template(path.as_str(), (**resource).contents.as_str());
    if (parsed.is_err())
      return Err(rstd::move(parsed).unwrap_err());
    set.documents.insert(path.clone(), rstd::move(parsed).unwrap());
  }

  auto frontend_assets = Vec<FrontendAsset>::make();
  for (auto &asset : *manifest.assets) {
    auto resource = resources.get(asset.path.as_str());
    if (resource.is_none())
      return Err(rstd::format("frontend '{}' is missing asset '{}'",
                              identity.as_str(), asset.path));
    if ((**resource).media_type.as_str() != asset.media_type.as_str())
      return Err(
          rstd::format("frontend asset '{}' media type mismatch", asset.path));
    frontend_assets.push(FrontendAsset{
        .path = rstd::move(asset.path),
        .media_type = rstd::move(asset.media_type),
        .contents = (**resource).contents.clone(),
    });
  }
  return Ok(FrontendBundle{
      .identity = rstd::move(identity),
      .digest = rstd::move(digest),
      .supports_api = supports_api,
      .supports_book = supports_book,
      .root_template = rstd::move(root_template),
      .package_template = rstd::move(package_template),
      .module_template = rstd::move(module_template),
      .symbol_template = rstd::move(symbol_template),
      .source_template = rstd::move(source_template),
      .book_root_template = rstd::move(book_root_template),
      .book_page_template = rstd::move(book_page_template),
      .templates = rstd::move(set),
      .assets = rstd::move(frontend_assets),
  });
}

auto finish_frontend_resources(
    String identity,
    rstd::collections::BTreeMap<String, FrontendResource> resources,
    FrontendManifestDescription description) -> Result<FrontendBundle, String> {
  auto iterator = resources.iter();
  for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
    auto path = (*(*item).template get<0>()).as_str();
    if (!description.declared.contains(path))
      return Err(rstd::format(
          "frontend resource '{}' is not declared by frontend.json", path));
  }
  auto declared = description.declared.iter();
  for (auto path = declared.next(); path.is_some(); path = declared.next()) {
    if (!resources.contains_key((**path).as_str()))
      return Err(rstd::format("frontend '{}' is missing resource '{}'",
                              identity.as_str(), (**path).as_str()));
  }
  auto digest = frontend_digest(resources);
  return make_frontend_bundle(rstd::move(identity), rstd::move(digest),
                              rstd::move(resources),
                              rstd::move(description.manifest));
}

auto read_frontend_file(ref<rstd::path::Path> root, ref<str> relative,
                        ref<str> media_type)
    -> Result<FrontendResource, String> {
  if (!safe_frontend_path(relative))
    return Err(rstd::format("invalid frontend resource path '{}'", relative));
  auto path = rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path());
  auto metadata = rstd::fs::symlink_metadata(path.as_path());
  if (metadata.is_err())
    return Err(rstd::format("cannot inspect frontend resource '{}': {}",
                            path.as_path(), rstd::move(metadata).unwrap_err()));
  if (metadata->is_symlink() || !metadata->is_file())
    return Err(rstd::format("frontend resource '{}' must be a regular file",
                            path.as_path()));
  auto contents = rstd::fs::read_to_string(path.as_path());
  if (contents.is_err())
    return Err(rstd::format("cannot read frontend resource '{}': {}",
                            path.as_path(), rstd::move(contents).unwrap_err()));
  return Ok(FrontendResource{
      .path = String::make(relative),
      .media_type = String::make(media_type),
      .contents = rstd::move(contents).unwrap(),
  });
}

auto validate_frontend_directory(
    ref<rstd::path::Path> root, ref<rstd::path::Path> directory,
    const rstd::collections::BTreeSet<String> &declared)
    -> Result<empty, String> {
  auto opened = rstd::fs::read_dir(directory);
  if (opened.is_err())
    return Err(rstd::format("cannot enumerate frontend directory '{}': {}",
                            directory, rstd::move(opened).unwrap_err()));
  auto entries = rstd::move(opened).unwrap();
  for (auto next = entries.next(); next.is_some(); next = entries.next()) {
    auto item = rstd::move(next).unwrap();
    if (item.is_err())
      return Err(rstd::format("cannot enumerate frontend directory '{}': {}",
                              directory, rstd::move(item).unwrap_err()));
    auto entry = rstd::move(item).unwrap();
    auto type = entry.file_type();
    if (type.is_err())
      return Err(rstd::format("cannot inspect frontend entry '{}': {}",
                              entry.path().as_path(),
                              rstd::move(type).unwrap_err()));
    auto path = entry.path();
    if (type->is_symlink())
      return Err(rstd::format("frontend entry '{}' must not be a symlink",
                              path.as_path()));
    if (type->is_dir()) {
      auto nested = validate_frontend_directory(root, path.as_path(), declared);
      if (nested.is_err())
        return nested;
      continue;
    }
    if (!type->is_file())
      return Err(rstd::format("frontend entry '{}' must be a regular file",
                              path.as_path()));
    auto relative = path.as_path().strip_prefix(root);
    if (relative.is_none())
      return Err(
          rstd::format("frontend entry '{}' escapes its root", path.as_path()));
    auto text = (*relative).to_str();
    if (text.is_none())
      return Err(rstd::format("frontend entry '{}' is not valid UTF-8",
                              path.as_path()));
    if (!declared.contains(*text))
      return Err(rstd::format(
          "frontend resource '{}' is not declared by frontend.json", *text));
  }
  return Ok(empty{});
}

auto load_frontend_directory(ref<rstd::path::Path> root)
    -> Result<FrontendBundle, String> {
  auto manifest_resource =
      read_frontend_file(root, "frontend.json"_str, "application/json"_str);
  if (manifest_resource.is_err())
    return Err(rstd::move(manifest_resource).unwrap_err());
  auto description =
      describe_frontend_manifest(manifest_resource->contents.as_str());
  if (description.is_err())
    return Err(rstd::move(description).unwrap_err());
  auto valid_directory =
      validate_frontend_directory(root, root, description->declared);
  if (valid_directory.is_err())
    return Err(rstd::move(valid_directory).unwrap_err());
  auto resources =
      rstd::collections::BTreeMap<String, FrontendResource>::make();
  resources.insert(String::make("frontend.json"_str),
                   rstd::move(manifest_resource).unwrap());
  if (description->manifest.assets.is_none())
    return Err(String::make("frontend manifest is missing 'assets'"_str));
  auto media = rstd::collections::BTreeMap<String, String>::make();
  for (const auto &asset : *description->manifest.assets) {
    media.insert(asset.path.clone(), asset.media_type.clone());
  }
  for (const auto &path : description->paths) {
    if (resources.contains_key(path.as_str()))
      continue;
    auto media_type = media.get(path.as_str());
    auto resource = read_frontend_file(
        root, path.as_str(),
        media_type.is_some() ? (**media_type).as_str() : "text/html"_str);
    if (resource.is_err())
      return Err(rstd::move(resource).unwrap_err());
    resources.insert(path.clone(), rstd::move(resource).unwrap());
  }
  auto root_text = root.to_str();
  if (root_text.is_none())
    return Err(String::make("frontend root is not valid UTF-8"_str));
  return finish_frontend_resources(rstd::format("directory:{}", *root_text),
                                   rstd::move(resources),
                                   rstd::move(description).unwrap());
}

auto load_frontend_resources(ref<str> identity,
                             slice<FrontendResourceInput> inputs)
    -> Result<FrontendBundle, String> {
  if (identity.is_empty())
    return Err(
        String::make("frontend resource identity must not be empty"_str));
  auto resources =
      rstd::collections::BTreeMap<String, FrontendResource>::make();
  for (const auto &input : inputs) {
    if (!safe_frontend_path(input.path))
      return Err(
          rstd::format("invalid frontend resource path '{}'", input.path));
    if (input.media_type.is_empty())
      return Err(
          rstd::format("frontend resource '{}' has no media type", input.path));
    auto text = rstd::str_::from_utf8(input.contents);
    if (text.is_err())
      return Err(rstd::format("frontend resource '{}' is not valid UTF-8",
                              input.path));
    auto previous =
        resources.insert(String::make(input.path),
                         FrontendResource{
                             .path = String::make(input.path),
                             .media_type = String::make(input.media_type),
                             .contents = String::make(*text),
                         });
    if (previous.is_some())
      return Err(
          rstd::format("frontend resource '{}' is repeated", input.path));
  }
  auto manifest_resource = resources.get("frontend.json"_str);
  if (manifest_resource.is_none())
    return Err(
        String::make("frontend resources are missing frontend.json"_str));
  if ((**manifest_resource).media_type.as_str() != "application/json"_str)
    return Err(String::make(
        "frontend.json must use media type 'application/json'"_str));
  auto description = rstd_try(
      describe_frontend_manifest((**manifest_resource).contents.as_str()));
  return finish_frontend_resources(
      String::make(identity), rstd::move(resources), rstd::move(description));
}

} // namespace lito::site
