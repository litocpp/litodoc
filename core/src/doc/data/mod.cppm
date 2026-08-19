export module lito.doc:data;

import rstd;
import rstd.json;
import :model;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;
using JsonMap = rstd::json::Map;
using JsonArray = rstd::json::Array;
using namespace lito::site;

namespace lito::doc {

inline constexpr uint64_t DATA_FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t DATA_FNV_PRIME = 1099511628211ull;

auto data_hex(uint64_t value) -> String {
  static constexpr char digits[] = "0123456789abcdef";
  char result[16];
  for (size_t index = 0; index < 16; ++index) {
    result[15 - index] = digits[value & 0xfu];
    value >>= 4u;
  }
  return String::make(ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(result), usize(16)));
}

auto data_digest(ref<str> value) -> String {
  auto hash = DATA_FNV_OFFSET;
  for (auto byte : value) {
    hash ^= byte.to_primitive();
    hash *= DATA_FNV_PRIME;
  }
  return data_hex(hash);
}

auto data_identity(ref<str> recipe, ref<str> value) -> String {
  auto hash = DATA_FNV_OFFSET;
  auto append = [&hash](ref<str> part) {
    for (auto byte : part) {
      hash ^= byte.to_primitive();
      hash *= DATA_FNV_PRIME;
    }
    hash ^= 0;
    hash *= DATA_FNV_PRIME;
  };
  append(recipe);
  append(value);
  return data_hex(hash);
}

auto json_string(ref<str> value) -> Json {
  return Json::String(String::make(value));
}

auto json_usize(usize value) -> Json {
  return Json::Number(rstd::json::Number::from_u64(
      u64(static_cast<rstd::uint64_t>(value.to_primitive()))));
}

auto json_text(const Json &value) -> String {
  auto result = rstd::json::to_string(
      value, rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  result.push_ascii('\n');
  return result;
}

auto source_data_path(ref<str> package, ref<str> source) -> String {
  return rstd::format(
      "packages/{}/sources/{}.json", package,
      data_identity("lito-doc-source-data-v1"_str, source).as_str());
}

auto package_data_path(ref<str> package) -> String {
  return rstd::format("packages/{}/doc.json", package);
}

auto safe_data_path(ref<str> value) -> bool {
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

auto data_path(ref<rstd::path::Path> root, ref<str> relative)
    -> Result<rstd::path::PathBuf, String> {
  if (!safe_data_path(relative))
    return Err(rstd::format("invalid doc data path '{}'", relative));
  return Ok(rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path()));
}

auto write_data_file(ref<rstd::path::Path> root, ref<str> relative,
                     ref<str> contents) -> Result<empty, String> {
  auto path = data_path(root, relative);
  if (path.is_err())
    return Err(rstd::move(path).unwrap_err());
  auto parent = path->as_path().parent();
  if (parent.is_none())
    return Err(rstd::format("doc data '{}' has no parent", path->as_path()));
  auto created = rstd::fs::create_dir_all(*parent);
  if (created.is_err())
    return Err(rstd::format("cannot create doc data directory '{}': {}",
                            *parent, rstd::move(created).unwrap_err()));
  auto written = rstd::fs::write_atomic(path->as_path(), contents.as_bytes());
  if (written.is_err())
    return Err(rstd::format("cannot write doc data '{}': {}", path->as_path(),
                            rstd::move(written).unwrap_err()));
  return Ok(empty{});
}

auto encode_source_fragment(ref<str> package, const Source &source) -> Json {
  auto root = JsonMap::make();
  root.insert(String::make("format"_str), json_string("lito-doc-source"_str));
  root.insert(String::make("version"_str), json_usize(usize(1)));
  root.insert(String::make("package"_str), json_string(package));
  root.insert(String::make("path"_str), json_string(source.path.as_str()));
  root.insert(String::make("page"_str), json_string(source.page.as_str()));
  root.insert(String::make("contents"_str),
              json_string(source.contents.as_str()));
  return Json::Object(rstd::move(root));
}

auto encode_package(const Package &package) -> Json {
  auto root = JsonMap::make();
  root.insert(String::make("format"_str), json_string("lito-doc"_str));
  root.insert(String::make("version"_str), json_usize(usize(3)));
  auto generator = JsonMap::make();
  generator.insert(String::make("frontend"_str),
                   json_string("lito-native-frontend-v1"_str));
  generator.insert(String::make("parser"_str),
                   json_string("lito-doc-outline-v1"_str));
  generator.insert(String::make("dataset"_str),
                   json_string("lito-doc-data-v3"_str));
  generator.insert(String::make("toolchain-version"_str),
                   json_string(package.toolchain_version.as_str()));
  generator.insert(String::make("toolchain-target"_str),
                   json_string(package.toolchain_target.as_str()));
  generator.insert(String::make("language-standard"_str),
                   json_string(package.language_standard.as_str()));
  root.insert(String::make("generator"_str),
              Json::Object(rstd::move(generator)));

  auto metadata = JsonMap::make();
  metadata.insert(String::make("name"_str), json_string(package.name.as_str()));
  metadata.insert(String::make("version"_str),
                  json_string(package.version.as_str()));
  metadata.insert(String::make("root-module"_str),
                  json_string(package.root_module.as_str()));
  metadata.insert(String::make("profile"_str),
                  json_string(package.profile.as_str()));
  root.insert(String::make("package"_str), Json::Object(rstd::move(metadata)));

  auto modules = JsonArray::make();
  for (const auto &module : package.modules) {
    auto object = JsonMap::make();
    object.insert(String::make("name"_str), json_string(module.name.as_str()));
    object.insert(String::make("page"_str), json_string(module.page.as_str()));
    object.insert(String::make("comment"_str),
                  module.comment.is_some()
                      ? json_string(module.comment->as_str())
                      : Json::Null());
    auto reexports = JsonArray::make();
    for (const auto &reexport : module.reexports)
      reexports.push(json_string(reexport.as_str()));
    object.insert(String::make("reexports"_str),
                  Json::Array(rstd::move(reexports)));
    modules.push(Json::Object(rstd::move(object)));
  }
  root.insert(String::make("modules"_str), Json::Array(rstd::move(modules)));

  auto symbols = JsonArray::make();
  for (const auto &symbol : package.symbols) {
    auto object = JsonMap::make();
    object.insert(String::make("key"_str), json_string(symbol.key.as_str()));
    object.insert(String::make("page"_str), json_string(symbol.page.as_str()));
    object.insert(String::make("module"_str),
                  json_string(symbol.module.as_str()));
    object.insert(String::make("module-page"_str),
                  json_string(symbol.module_page.as_str()));
    object.insert(String::make("kind"_str),
                  json_string(declaration_kind_name(symbol.kind)));
    object.insert(String::make("name"_str), json_string(symbol.name.as_str()));
    object.insert(String::make("qualified-name"_str),
                  json_string(symbol.qualified_name.as_str()));
    object.insert(String::make("namespace"_str),
                  json_string(symbol.namespace_name.as_str()));
    object.insert(String::make("signature"_str),
                  json_string(symbol.signature.as_str()));
    object.insert(String::make("scope-signature"_str),
                  json_string(symbol.scope_signature.as_str()));
    object.insert(String::make("record-keyword"_str),
                  symbol.record_keyword.is_some()
                      ? json_string(symbol.record_keyword->as_str())
                      : Json::Null());
    object.insert(String::make("record-header"_str),
                  symbol.record_header.is_some()
                      ? json_string(symbol.record_header->as_str())
                      : Json::Null());
    object.insert(String::make("is-definition"_str),
                  Json::Bool(symbol.is_definition));
    object.insert(String::make("placement"_str),
                  json_string(symbol.placement == SymbolPlacement::RecordMember
                                  ? "record-member"_str
                                  : "standalone"_str));
    object.insert(String::make("anchor"_str),
                  symbol.anchor.is_some() ? json_string(symbol.anchor->as_str())
                                          : Json::Null());
    object.insert(String::make("declaration-order"_str),
                  json_usize(symbol.declaration_order));
    object.insert(String::make("parent"_str),
                  symbol.parent_key.is_some()
                      ? json_string(symbol.parent_key->as_str())
                      : Json::Null());
    object.insert(String::make("group"_str),
                  symbol.group.is_some() ? json_string(symbol.group->as_str())
                                         : Json::Null());
    object.insert(String::make("comment"_str),
                  symbol.comment.is_some()
                      ? json_string(symbol.comment->as_str())
                      : Json::Null());
    auto source = JsonMap::make();
    source.insert(String::make("path"_str),
                  json_string(symbol.source_path.as_str()));
    source.insert(String::make("page"_str),
                  json_string(symbol.source_page.as_str()));
    source.insert(String::make("line"_str), json_usize(symbol.source_line));
    source.insert(String::make("column"_str), json_usize(symbol.source_column));
    source.insert(String::make("end-line"_str),
                  json_usize(symbol.source_end_line));
    source.insert(String::make("end-column"_str),
                  json_usize(symbol.source_end_column));
    object.insert(String::make("source"_str), Json::Object(rstd::move(source)));
    symbols.push(Json::Object(rstd::move(object)));
  }
  root.insert(String::make("symbols"_str), Json::Array(rstd::move(symbols)));

  auto sources = JsonArray::make();
  for (const auto &source : package.sources) {
    auto fragment =
        json_text(encode_source_fragment(package.name.as_str(), source));
    auto fragment_path =
        source_data_path(package.name.as_str(), source.path.as_str());
    auto object = JsonMap::make();
    object.insert(String::make("path"_str), json_string(source.path.as_str()));
    object.insert(String::make("page"_str), json_string(source.page.as_str()));
    object.insert(String::make("data"_str),
                  json_string(fragment_path.as_str()));
    object.insert(String::make("digest"_str),
                  json_string(data_digest(fragment.as_str()).as_str()));
    sources.push(Json::Object(rstd::move(object)));
  }
  root.insert(String::make("sources"_str), Json::Array(rstd::move(sources)));

  auto diagnostics = JsonArray::make();
  for (const auto &diagnostic : package.diagnostics) {
    auto object = JsonMap::make();
    object.insert(
        String::make("severity"_str),
        json_string(diagnostic.severity == DocumentationSeverity::Error
                        ? "error"_str
                        : "warning"_str));
    object.insert(String::make("code"_str),
                  json_string(diagnostic.code.as_str()));
    object.insert(String::make("message"_str),
                  json_string(diagnostic.message.as_str()));
    object.insert(String::make("path"_str),
                  json_string(diagnostic.path.as_str()));
    object.insert(String::make("line"_str), json_usize(diagnostic.line));
    diagnostics.push(Json::Object(rstd::move(object)));
  }
  root.insert(String::make("diagnostics"_str),
              Json::Array(rstd::move(diagnostics)));

  auto coverage = JsonMap::make();
  coverage.insert(String::make("documented"_str),
                  json_usize(package.documented));
  coverage.insert(String::make("undocumented"_str),
                  json_usize(package.undocumented));
  coverage.insert(String::make("unsupported"_str),
                  json_usize(package.unsupported));
  root.insert(String::make("coverage"_str), Json::Object(rstd::move(coverage)));
  return Json::Object(rstd::move(root));
}

auto package_json(const Package &package) -> String {
  return json_text(encode_package(package));
}

auto required_member(const Json &value, ref<str> name, ref<str> context)
    -> Result<ref<Json>, String> {
  auto member = value.get(name);
  if (member.is_none())
    return Err(rstd::format("{} is missing '{}'", context, name));
  return Ok(*member);
}

auto required_string(const Json &value, ref<str> name, ref<str> context)
    -> Result<String, String> {
  auto member = required_member(value, name, context);
  if (member.is_err())
    return Err(rstd::move(member).unwrap_err());
  auto text = (**member).as_str();
  if (text.is_none())
    return Err(rstd::format("{}.{} must be a string", context, name));
  return Ok(String::make(*text));
}

auto optional_string(const Json &value, ref<str> name, ref<str> context)
    -> Result<Option<String>, String> {
  auto member = required_member(value, name, context);
  if (member.is_err())
    return Err(rstd::move(member).unwrap_err());
  if ((**member).is_null())
    return Ok(Option<String>{});
  auto text = (**member).as_str();
  if (text.is_none())
    return Err(rstd::format("{}.{} must be a string or null", context, name));
  return Ok(Some(String::make(*text)));
}

auto defaulted_string(const Json &value, ref<str> name, ref<str> context)
    -> Result<String, String> {
  auto member = value.get(name);
  if (member.is_none())
    return Ok(String::make());
  auto text = (**member).as_str();
  if (text.is_none())
    return Err(rstd::format("{}.{} must be a string", context, name));
  return Ok(String::make(*text));
}

auto required_usize(const Json &value, ref<str> name, ref<str> context)
    -> Result<usize, String> {
  auto member = required_member(value, name, context);
  if (member.is_err())
    return Err(rstd::move(member).unwrap_err());
  auto number = (**member).as_u64();
  if (number.is_none() || *number > u64(usize::MAX.to_primitive()))
    return Err(
        rstd::format("{}.{} must be an unsigned integer", context, name));
  return Ok(usize(static_cast<size_t>((*number).to_primitive())));
}

auto required_bool(const Json &value, ref<str> name, ref<str> context)
    -> Result<bool, String> {
  auto member = required_member(value, name, context);
  if (member.is_err())
    return Err(rstd::move(member).unwrap_err());
  auto boolean = (**member).as_bool();
  if (boolean.is_none())
    return Err(rstd::format("{}.{} must be a boolean", context, name));
  return Ok(*boolean);
}

auto required_array(const Json &value, ref<str> name, ref<str> context)
    -> Result<ref<JsonArray>, String> {
  auto member = required_member(value, name, context);
  if (member.is_err())
    return Err(rstd::move(member).unwrap_err());
  auto array = (**member).as_array();
  if (array.is_none())
    return Err(rstd::format("{}.{} must be an array", context, name));
  return Ok(*array);
}

auto expect_header(const Json &value, ref<str> format, usize version,
                   ref<str> context) -> Result<empty, String> {
  auto actual_format = required_string(value, "format"_str, context);
  auto actual_version = required_usize(value, "version"_str, context);
  if (actual_format.is_err())
    return Err(rstd::move(actual_format).unwrap_err());
  if (actual_version.is_err())
    return Err(rstd::move(actual_version).unwrap_err());
  if (actual_format->as_str() != format)
    return Err(rstd::format("{} has format '{}', expected '{}'", context,
                            actual_format->as_str(), format));
  if (*actual_version != version)
    return Err(rstd::format("{} has unsupported version {}", context,
                            *actual_version));
  return Ok(empty{});
}

auto parse_kind(ref<str> value) -> Option<DeclarationKind> {
  if (value == "module"_str)
    return Some(DeclarationKind::Module);
  if (value == "namespace"_str)
    return Some(DeclarationKind::Namespace);
  if (value == "record"_str)
    return Some(DeclarationKind::Record);
  if (value == "enum"_str)
    return Some(DeclarationKind::Enum);
  if (value == "concept"_str)
    return Some(DeclarationKind::Concept);
  if (value == "alias"_str)
    return Some(DeclarationKind::Alias);
  if (value == "function"_str)
    return Some(DeclarationKind::Function);
  if (value == "variable"_str)
    return Some(DeclarationKind::Variable);
  if (value == "field"_str)
    return Some(DeclarationKind::Field);
  return None();
}

auto parse_placement(ref<str> value) -> Option<SymbolPlacement> {
  if (value == "standalone"_str)
    return Some(SymbolPlacement::Standalone);
  if (value == "record-member"_str)
    return Some(SymbolPlacement::RecordMember);
  return None();
}

auto parse_json_text(ref<str> contents, ref<str> context)
    -> Result<Json, String> {
  auto parsed = rstd::json::from_str(contents);
  if (parsed.is_err())
    return Err(
        rstd::format("invalid {} JSON: {}", context, parsed.unwrap_err()));
  if (!parsed->is_object())
    return Err(rstd::format("{} must be a JSON object", context));
  return Ok(rstd::move(parsed).unwrap());
}

auto read_data_text(ref<rstd::path::Path> root, ref<str> relative)
    -> Result<String, String> {
  auto path = data_path(root, relative);
  if (path.is_err())
    return Err(rstd::move(path).unwrap_err());
  auto contents = rstd::fs::read_to_string(path->as_path());
  if (contents.is_err())
    return Err(rstd::format("cannot read doc data '{}': {}", path->as_path(),
                            rstd::move(contents).unwrap_err()));
  return Ok(rstd::move(contents).unwrap());
}

auto decode_source(ref<rstd::path::Path> root, ref<str> package_name,
                   const Json &descriptor, const Json *manifest_descriptor)
    -> Result<Source, String> {
  auto path =
      required_string(descriptor, "path"_str, "doc source descriptor"_str);
  auto page =
      required_string(descriptor, "page"_str, "doc source descriptor"_str);
  auto data =
      required_string(descriptor, "data"_str, "doc source descriptor"_str);
  auto digest =
      required_string(descriptor, "digest"_str, "doc source descriptor"_str);
  if (path.is_err())
    return Err(rstd::move(path).unwrap_err());
  if (page.is_err())
    return Err(rstd::move(page).unwrap_err());
  if (data.is_err())
    return Err(rstd::move(data).unwrap_err());
  if (digest.is_err())
    return Err(rstd::move(digest).unwrap_err());
  if (manifest_descriptor != nullptr) {
    auto manifest_data = required_string(*manifest_descriptor, "path"_str,
                                         "doc manifest source"_str);
    auto manifest_digest = required_string(*manifest_descriptor, "digest"_str,
                                           "doc manifest source"_str);
    if (manifest_data.is_err())
      return Err(rstd::move(manifest_data).unwrap_err());
    if (manifest_digest.is_err())
      return Err(rstd::move(manifest_digest).unwrap_err());
    if (manifest_data->as_str() != data->as_str() ||
        manifest_digest->as_str() != digest->as_str())
      return Err(rstd::format("doc source manifest mismatch for '{}'",
                              path->as_str()));
  }
  auto contents = read_data_text(root, data->as_str());
  if (contents.is_err())
    return Err(rstd::move(contents).unwrap_err());
  auto actual_digest = data_digest(contents->as_str());
  if (actual_digest.as_str() != digest->as_str())
    return Err(rstd::format("doc source '{}' digest mismatch", data->as_str()));
  auto parsed = parse_json_text(contents->as_str(), "doc source"_str);
  if (parsed.is_err())
    return Err(rstd::move(parsed).unwrap_err());
  auto header =
      expect_header(*parsed, "lito-doc-source"_str, usize(1), "doc source"_str);
  if (header.is_err())
    return Err(rstd::move(header).unwrap_err());
  auto fragment_package =
      required_string(*parsed, "package"_str, "doc source"_str);
  auto fragment_path = required_string(*parsed, "path"_str, "doc source"_str);
  auto fragment_page = required_string(*parsed, "page"_str, "doc source"_str);
  auto fragment_text =
      required_string(*parsed, "contents"_str, "doc source"_str);
  if (fragment_package.is_err())
    return Err(rstd::move(fragment_package).unwrap_err());
  if (fragment_path.is_err())
    return Err(rstd::move(fragment_path).unwrap_err());
  if (fragment_page.is_err())
    return Err(rstd::move(fragment_page).unwrap_err());
  if (fragment_text.is_err())
    return Err(rstd::move(fragment_text).unwrap_err());
  if (fragment_package->as_str() != package_name ||
      fragment_path->as_str() != path->as_str() ||
      fragment_page->as_str() != page->as_str())
    return Err(
        rstd::format("doc source '{}' identity mismatch", data->as_str()));
  return Ok(Source{
      .path = rstd::move(path).unwrap(),
      .page = rstd::move(page).unwrap(),
      .contents = rstd::move(fragment_text).unwrap(),
  });
}

auto decode_package(const Json &document, Option<ref<rstd::path::Path>> root,
                    Option<ref<JsonArray>> manifest_sources)
    -> Result<Package, String> {
  auto header =
      expect_header(document, "lito-doc"_str, usize(3), "doc package"_str);
  if (header.is_err())
    return Err(rstd::move(header).unwrap_err());
  auto generator =
      required_member(document, "generator"_str, "doc package"_str);
  auto metadata = required_member(document, "package"_str, "doc package"_str);
  if (generator.is_err())
    return Err(rstd::move(generator).unwrap_err());
  if (metadata.is_err())
    return Err(rstd::move(metadata).unwrap_err());
  auto package = Package{};
  auto name =
      required_string(**metadata, "name"_str, "doc package metadata"_str);
  auto version =
      required_string(**metadata, "version"_str, "doc package metadata"_str);
  auto root_module = required_string(**metadata, "root-module"_str,
                                     "doc package metadata"_str);
  auto profile =
      required_string(**metadata, "profile"_str, "doc package metadata"_str);
  auto toolchain_version = required_string(**generator, "toolchain-version"_str,
                                           "doc generator"_str);
  auto toolchain_target =
      required_string(**generator, "toolchain-target"_str, "doc generator"_str);
  auto language_standard = required_string(**generator, "language-standard"_str,
                                           "doc generator"_str);
  if (name.is_err())
    return Err(rstd::move(name).unwrap_err());
  if (version.is_err())
    return Err(rstd::move(version).unwrap_err());
  if (root_module.is_err())
    return Err(rstd::move(root_module).unwrap_err());
  if (profile.is_err())
    return Err(rstd::move(profile).unwrap_err());
  if (toolchain_version.is_err())
    return Err(rstd::move(toolchain_version).unwrap_err());
  if (toolchain_target.is_err())
    return Err(rstd::move(toolchain_target).unwrap_err());
  if (language_standard.is_err())
    return Err(rstd::move(language_standard).unwrap_err());
  package.name = rstd::move(name).unwrap();
  package.version = rstd::move(version).unwrap();
  package.root_module = rstd::move(root_module).unwrap();
  package.profile = rstd::move(profile).unwrap();
  package.toolchain_version = rstd::move(toolchain_version).unwrap();
  package.toolchain_target = rstd::move(toolchain_target).unwrap();
  package.language_standard = rstd::move(language_standard).unwrap();

  auto modules = required_array(document, "modules"_str, "doc package"_str);
  if (modules.is_err())
    return Err(rstd::move(modules).unwrap_err());
  for (const auto &value : **modules) {
    auto module_name = required_string(value, "name"_str, "doc module"_str);
    auto module_page = required_string(value, "page"_str, "doc module"_str);
    auto comment = optional_string(value, "comment"_str, "doc module"_str);
    auto reexports = required_array(value, "reexports"_str, "doc module"_str);
    if (module_name.is_err())
      return Err(rstd::move(module_name).unwrap_err());
    if (module_page.is_err())
      return Err(rstd::move(module_page).unwrap_err());
    if (comment.is_err())
      return Err(rstd::move(comment).unwrap_err());
    if (reexports.is_err())
      return Err(rstd::move(reexports).unwrap_err());
    auto module = Module{
        .name = rstd::move(module_name).unwrap(),
        .page = rstd::move(module_page).unwrap(),
        .comment = rstd::move(comment).unwrap(),
    };
    for (const auto &reexport : **reexports) {
      auto text = reexport.as_str();
      if (text.is_none())
        return Err(String::make("doc reexport must be a string"_str));
      module.reexports.push(String::make(*text));
    }
    package.modules.push(rstd::move(module));
  }

  auto symbols = required_array(document, "symbols"_str, "doc package"_str);
  if (symbols.is_err())
    return Err(rstd::move(symbols).unwrap_err());
  for (const auto &value : **symbols) {
    auto key = required_string(value, "key"_str, "doc symbol"_str);
    auto page = required_string(value, "page"_str, "doc symbol"_str);
    auto module = required_string(value, "module"_str, "doc symbol"_str);
    auto module_page =
        required_string(value, "module-page"_str, "doc symbol"_str);
    auto kind_text = required_string(value, "kind"_str, "doc symbol"_str);
    auto symbol_name = required_string(value, "name"_str, "doc symbol"_str);
    auto qualified_name =
        required_string(value, "qualified-name"_str, "doc symbol"_str);
    auto namespace_name =
        defaulted_string(value, "namespace"_str, "doc symbol"_str);
    auto signature = required_string(value, "signature"_str, "doc symbol"_str);
    auto scope_signature =
        required_string(value, "scope-signature"_str, "doc symbol"_str);
    auto record_keyword =
        optional_string(value, "record-keyword"_str, "doc symbol"_str);
    auto record_header =
        optional_string(value, "record-header"_str, "doc symbol"_str);
    auto is_definition =
        required_bool(value, "is-definition"_str, "doc symbol"_str);
    auto placement_text =
        required_string(value, "placement"_str, "doc symbol"_str);
    auto anchor = optional_string(value, "anchor"_str, "doc symbol"_str);
    auto declaration_order =
        required_usize(value, "declaration-order"_str, "doc symbol"_str);
    auto parent = optional_string(value, "parent"_str, "doc symbol"_str);
    auto group = optional_string(value, "group"_str, "doc symbol"_str);
    auto comment = optional_string(value, "comment"_str, "doc symbol"_str);
    auto source = required_member(value, "source"_str, "doc symbol"_str);
    if (key.is_err())
      return Err(rstd::move(key).unwrap_err());
    if (page.is_err())
      return Err(rstd::move(page).unwrap_err());
    if (module.is_err())
      return Err(rstd::move(module).unwrap_err());
    if (module_page.is_err())
      return Err(rstd::move(module_page).unwrap_err());
    if (kind_text.is_err())
      return Err(rstd::move(kind_text).unwrap_err());
    if (symbol_name.is_err())
      return Err(rstd::move(symbol_name).unwrap_err());
    if (qualified_name.is_err())
      return Err(rstd::move(qualified_name).unwrap_err());
    if (namespace_name.is_err())
      return Err(rstd::move(namespace_name).unwrap_err());
    if (signature.is_err())
      return Err(rstd::move(signature).unwrap_err());
    if (scope_signature.is_err())
      return Err(rstd::move(scope_signature).unwrap_err());
    if (record_keyword.is_err())
      return Err(rstd::move(record_keyword).unwrap_err());
    if (record_header.is_err())
      return Err(rstd::move(record_header).unwrap_err());
    if (is_definition.is_err())
      return Err(rstd::move(is_definition).unwrap_err());
    if (placement_text.is_err())
      return Err(rstd::move(placement_text).unwrap_err());
    if (anchor.is_err())
      return Err(rstd::move(anchor).unwrap_err());
    if (declaration_order.is_err())
      return Err(rstd::move(declaration_order).unwrap_err());
    if (parent.is_err())
      return Err(rstd::move(parent).unwrap_err());
    if (group.is_err())
      return Err(rstd::move(group).unwrap_err());
    if (comment.is_err())
      return Err(rstd::move(comment).unwrap_err());
    if (source.is_err())
      return Err(rstd::move(source).unwrap_err());
    auto kind = parse_kind(kind_text->as_str());
    if (kind.is_none())
      return Err(rstd::format("unknown doc declaration kind '{}'",
                              kind_text->as_str()));
    auto placement = parse_placement(placement_text->as_str());
    if (placement.is_none())
      return Err(rstd::format("unknown doc symbol placement '{}'",
                              placement_text->as_str()));
    if (*placement == SymbolPlacement::RecordMember && anchor->is_none())
      return Err(
          String::make("record member doc symbol must provide an anchor"_str));
    if (*placement == SymbolPlacement::Standalone && anchor->is_some())
      return Err(
          String::make("standalone doc symbol must not provide an anchor"_str));
    auto source_path =
        required_string(**source, "path"_str, "doc symbol source"_str);
    auto source_page =
        required_string(**source, "page"_str, "doc symbol source"_str);
    auto source_line =
        required_usize(**source, "line"_str, "doc symbol source"_str);
    auto source_column =
        required_usize(**source, "column"_str, "doc symbol source"_str);
    auto source_end_line =
        required_usize(**source, "end-line"_str, "doc symbol source"_str);
    auto source_end_column =
        required_usize(**source, "end-column"_str, "doc symbol source"_str);
    if (source_path.is_err())
      return Err(rstd::move(source_path).unwrap_err());
    if (source_page.is_err())
      return Err(rstd::move(source_page).unwrap_err());
    if (source_line.is_err())
      return Err(rstd::move(source_line).unwrap_err());
    if (source_column.is_err())
      return Err(rstd::move(source_column).unwrap_err());
    if (source_end_line.is_err())
      return Err(rstd::move(source_end_line).unwrap_err());
    if (source_end_column.is_err())
      return Err(rstd::move(source_end_column).unwrap_err());
    package.symbols.push(Symbol{
        .key = rstd::move(key).unwrap(),
        .page = rstd::move(page).unwrap(),
        .module = rstd::move(module).unwrap(),
        .module_page = rstd::move(module_page).unwrap(),
        .kind = *kind,
        .name = rstd::move(symbol_name).unwrap(),
        .qualified_name = rstd::move(qualified_name).unwrap(),
        .namespace_name = rstd::move(namespace_name).unwrap(),
        .signature = rstd::move(signature).unwrap(),
        .scope_signature = rstd::move(scope_signature).unwrap(),
        .record_keyword = rstd::move(record_keyword).unwrap(),
        .record_header = rstd::move(record_header).unwrap(),
        .is_definition = *is_definition,
        .placement = *placement,
        .anchor = rstd::move(anchor).unwrap(),
        .declaration_order = *declaration_order,
        .parent_key = rstd::move(parent).unwrap(),
        .group = rstd::move(group).unwrap(),
        .comment = rstd::move(comment).unwrap(),
        .source_page = rstd::move(source_page).unwrap(),
        .source_path = rstd::move(source_path).unwrap(),
        .source_line = *source_line,
        .source_column = *source_column,
        .source_end_line = *source_end_line,
        .source_end_column = *source_end_column,
    });
  }

  auto sources = required_array(document, "sources"_str, "doc package"_str);
  if (sources.is_err())
    return Err(rstd::move(sources).unwrap_err());
  if (manifest_sources.is_some() &&
      (**manifest_sources).len() != (**sources).len())
    return Err(rstd::format("doc package '{}' source manifest length mismatch",
                            package.name.as_str()));
  if (root.is_some()) {
    for (auto index = usize{}; index < (**sources).len(); ++index) {
      const auto *expected = manifest_sources.is_some()
                                 ? rstd::addressof((**manifest_sources)[index])
                                 : nullptr;
      auto source = decode_source(*root, package.name.as_str(),
                                  (**sources)[index], expected);
      if (source.is_err())
        return Err(rstd::move(source).unwrap_err());
      package.sources.push(rstd::move(source).unwrap());
    }
  } else {
    for (const auto &value : **sources) {
      auto path =
          required_string(value, "path"_str, "doc source descriptor"_str);
      auto page =
          required_string(value, "page"_str, "doc source descriptor"_str);
      auto data =
          required_string(value, "data"_str, "doc source descriptor"_str);
      auto digest =
          required_string(value, "digest"_str, "doc source descriptor"_str);
      if (path.is_err())
        return Err(rstd::move(path).unwrap_err());
      if (page.is_err())
        return Err(rstd::move(page).unwrap_err());
      if (data.is_err())
        return Err(rstd::move(data).unwrap_err());
      if (digest.is_err())
        return Err(rstd::move(digest).unwrap_err());
      if (!safe_data_path(data->as_str()))
        return Err(
            rstd::format("invalid doc source data path '{}'", data->as_str()));
      package.sources.push(Source{
          .path = rstd::move(path).unwrap(),
          .page = rstd::move(page).unwrap(),
      });
    }
  }

  auto diagnostics =
      required_array(document, "diagnostics"_str, "doc package"_str);
  if (diagnostics.is_err())
    return Err(rstd::move(diagnostics).unwrap_err());
  for (const auto &value : **diagnostics) {
    auto severity =
        required_string(value, "severity"_str, "doc diagnostic"_str);
    auto code = required_string(value, "code"_str, "doc diagnostic"_str);
    auto message = required_string(value, "message"_str, "doc diagnostic"_str);
    auto path = required_string(value, "path"_str, "doc diagnostic"_str);
    auto line = required_usize(value, "line"_str, "doc diagnostic"_str);
    if (severity.is_err())
      return Err(rstd::move(severity).unwrap_err());
    if (code.is_err())
      return Err(rstd::move(code).unwrap_err());
    if (message.is_err())
      return Err(rstd::move(message).unwrap_err());
    if (path.is_err())
      return Err(rstd::move(path).unwrap_err());
    if (line.is_err())
      return Err(rstd::move(line).unwrap_err());
    if (severity->as_str() != "warning"_str &&
        severity->as_str() != "error"_str)
      return Err(rstd::format("invalid doc diagnostic severity '{}'",
                              severity->as_str()));
    package.diagnostics.push(Diagnostic{
        .severity = severity->as_str() == "error"_str
                        ? DocumentationSeverity::Error
                        : DocumentationSeverity::Warning,
        .code = rstd::move(code).unwrap(),
        .message = rstd::move(message).unwrap(),
        .path = rstd::move(path).unwrap(),
        .line = *line,
    });
  }

  auto coverage = required_member(document, "coverage"_str, "doc package"_str);
  if (coverage.is_err())
    return Err(rstd::move(coverage).unwrap_err());
  auto documented =
      required_usize(**coverage, "documented"_str, "doc coverage"_str);
  auto undocumented =
      required_usize(**coverage, "undocumented"_str, "doc coverage"_str);
  auto unsupported =
      required_usize(**coverage, "unsupported"_str, "doc coverage"_str);
  if (documented.is_err())
    return Err(rstd::move(documented).unwrap_err());
  if (undocumented.is_err())
    return Err(rstd::move(undocumented).unwrap_err());
  if (unsupported.is_err())
    return Err(rstd::move(unsupported).unwrap_err());
  package.documented = *documented;
  package.undocumented = *undocumented;
  package.unsupported = *unsupported;
  if (package.documented + package.undocumented != package.symbols.len())
    return Err(rstd::format("doc package '{}' coverage does not match symbols",
                            package.name.as_str()));
  return Ok(rstd::move(package));
}

auto validate_package_json(ref<str> contents) -> Result<empty, String> {
  auto parsed = parse_json_text(contents, "lito doc package"_str);
  if (parsed.is_err())
    return Err(rstd::move(parsed).unwrap_err());
  auto package = decode_package(*parsed, None(), None());
  if (package.is_err())
    return Err(rstd::move(package).unwrap_err());
  return Ok(empty{});
}

auto make_dataset(String title, Database database) -> Dataset {
  return Dataset{
      .title = rstd::move(title),
      .packages = rstd::move(database.packages),
  };
}

auto dataset_manifest_json(const Dataset &dataset) -> String {
  auto manifest_packages = JsonArray::make();
  for (const auto &package : dataset.packages) {
    auto source_entries = JsonArray::make();
    for (const auto &source : package.sources) {
      auto text =
          json_text(encode_source_fragment(package.name.as_str(), source));
      auto entry = JsonMap::make();
      entry.insert(String::make("path"_str),
                   json_string(source_data_path(package.name.as_str(),
                                                source.path.as_str())
                                   .as_str()));
      entry.insert(String::make("digest"_str),
                   json_string(data_digest(text.as_str()).as_str()));
      source_entries.push(Json::Object(rstd::move(entry)));
    }
    auto text = package_json(package);
    auto entry = JsonMap::make();
    entry.insert(String::make("name"_str), json_string(package.name.as_str()));
    entry.insert(
        String::make("path"_str),
        json_string(package_data_path(package.name.as_str()).as_str()));
    entry.insert(String::make("digest"_str),
                 json_string(data_digest(text.as_str()).as_str()));
    entry.insert(String::make("sources"_str),
                 Json::Array(rstd::move(source_entries)));
    manifest_packages.push(Json::Object(rstd::move(entry)));
  }
  auto site = JsonMap::make();
  site.insert(String::make("title"_str), json_string(dataset.title.as_str()));
  site.insert(String::make("default-package"_str),
              dataset.packages.is_empty()
                  ? Json::Null()
                  : json_string(dataset.packages[usize{}].name.as_str()));
  auto manifest = JsonMap::make();
  manifest.insert(String::make("format"_str),
                  json_string("lito-doc-dataset"_str));
  manifest.insert(String::make("version"_str), json_usize(usize(1)));
  manifest.insert(String::make("data-api"_str), json_usize(usize(3)));
  manifest.insert(String::make("generator"_str),
                  json_string("lito-doc-data-v3"_str));
  manifest.insert(String::make("site"_str), Json::Object(rstd::move(site)));
  manifest.insert(String::make("packages"_str),
                  Json::Array(rstd::move(manifest_packages)));
  return json_text(Json::Object(rstd::move(manifest)));
}

auto summarize_dataset(ref<rstd::path::Path> root, const Dataset &dataset)
    -> DataSummary {
  auto manifest_text = dataset_manifest_json(dataset);
  auto summary = DataSummary{
      .root = rstd::path::PathBuf::from(root),
      .manifest = rstd::path::PathBuf::from(root).join(
          rstd::path::PathBuf::from("manifest.json"_str).as_path()),
      .digest = data_digest(manifest_text.as_str()),
  };
  for (const auto &package : dataset.packages) {
    auto relative = package_data_path(package.name.as_str());
    summary.packages.push(PackageDataSummary{
        .name = package.name.clone(),
        .json = rstd::path::PathBuf::from(root).join(
            rstd::path::PathBuf::from(relative.as_str()).as_path()),
    });
  }
  return summary;
}

auto load_dataset(ref<rstd::path::Path> root) -> Result<Dataset, String> {
  auto manifest_text = read_data_text(root, "manifest.json"_str);
  if (manifest_text.is_err())
    return Err(rstd::move(manifest_text).unwrap_err());
  auto manifest =
      parse_json_text(manifest_text->as_str(), "doc dataset manifest"_str);
  if (manifest.is_err())
    return Err(rstd::move(manifest).unwrap_err());
  auto header = expect_header(*manifest, "lito-doc-dataset"_str, usize(1),
                              "doc dataset manifest"_str);
  if (header.is_err())
    return Err(rstd::move(header).unwrap_err());
  auto data_api =
      required_usize(*manifest, "data-api"_str, "doc dataset manifest"_str);
  if (data_api.is_err())
    return Err(rstd::move(data_api).unwrap_err());
  if (*data_api != usize(3))
    return Err(rstd::format("unsupported doc data API {}", *data_api));
  auto generator =
      required_string(*manifest, "generator"_str, "doc dataset manifest"_str);
  auto site =
      required_member(*manifest, "site"_str, "doc dataset manifest"_str);
  if (generator.is_err())
    return Err(rstd::move(generator).unwrap_err());
  if (site.is_err())
    return Err(rstd::move(site).unwrap_err());
  if (generator->as_str() != "lito-doc-data-v3"_str)
    return Err(rstd::format("unsupported doc data generator '{}'",
                            generator->as_str()));
  auto title = required_string(**site, "title"_str, "doc dataset site"_str);
  auto default_package =
      optional_string(**site, "default-package"_str, "doc dataset site"_str);
  if (title.is_err())
    return Err(rstd::move(title).unwrap_err());
  if (default_package.is_err())
    return Err(rstd::move(default_package).unwrap_err());
  auto packages =
      required_array(*manifest, "packages"_str, "doc dataset manifest"_str);
  if (packages.is_err())
    return Err(rstd::move(packages).unwrap_err());
  auto names = rstd::collections::BTreeSet<String>::make();
  auto dataset = Dataset{.title = rstd::move(title).unwrap()};
  auto paths = rstd::collections::BTreeSet<String>::make();
  for (const auto &entry : **packages) {
    auto name = required_string(entry, "name"_str, "doc manifest package"_str);
    auto path = required_string(entry, "path"_str, "doc manifest package"_str);
    auto digest =
        required_string(entry, "digest"_str, "doc manifest package"_str);
    auto sources =
        required_array(entry, "sources"_str, "doc manifest package"_str);
    if (name.is_err())
      return Err(rstd::move(name).unwrap_err());
    if (path.is_err())
      return Err(rstd::move(path).unwrap_err());
    if (digest.is_err())
      return Err(rstd::move(digest).unwrap_err());
    if (sources.is_err())
      return Err(rstd::move(sources).unwrap_err());
    if (!names.insert(name->clone()))
      return Err(rstd::format("duplicate doc package '{}'", name->as_str()));
    if (!safe_data_path(path->as_str()))
      return Err(
          rstd::format("invalid doc package data path '{}'", path->as_str()));
    if (!paths.insert(path->clone()))
      return Err(rstd::format("duplicate doc data path '{}'", path->as_str()));
    for (const auto &source : **sources) {
      auto source_path =
          required_string(source, "path"_str, "doc manifest source"_str);
      if (source_path.is_err())
        return Err(rstd::move(source_path).unwrap_err());
      if (!safe_data_path(source_path->as_str()))
        return Err(rstd::format("invalid doc source data path '{}'",
                                source_path->as_str()));
      if (!paths.insert(source_path->clone()))
        return Err(rstd::format("duplicate doc data path '{}'",
                                source_path->as_str()));
    }
    auto package_text = read_data_text(root, path->as_str());
    if (package_text.is_err())
      return Err(rstd::move(package_text).unwrap_err());
    auto actual_digest = data_digest(package_text->as_str());
    if (actual_digest.as_str() != digest->as_str())
      return Err(
          rstd::format("doc package '{}' digest mismatch", name->as_str()));
    auto parsed = parse_json_text(package_text->as_str(), "doc package"_str);
    if (parsed.is_err())
      return Err(rstd::move(parsed).unwrap_err());
    auto package = decode_package(*parsed, Some(root), Some(*sources));
    if (package.is_err())
      return Err(rstd::move(package).unwrap_err());
    if (package->name.as_str() != name->as_str())
      return Err(
          rstd::format("doc package '{}' identity mismatch", name->as_str()));
    dataset.packages.push(rstd::move(package).unwrap());
  }
  if (default_package->is_some() &&
      !names.contains((**default_package).as_str()))
    return Err(rstd::format("doc dataset default package '{}' does not exist",
                            (**default_package).as_str()));
  return Ok(rstd::move(dataset));
}

auto publish_dataset(ref<rstd::path::Path> output, const Dataset &dataset)
    -> Result<DataSummary, String> {
  auto publication = begin_publication(output, "doc-data"_str);
  if (publication.is_err())
    return Err(rstd::move(publication).unwrap_err());
  for (const auto &package : dataset.packages) {
    for (const auto &source : package.sources) {
      auto relative =
          source_data_path(package.name.as_str(), source.path.as_str());
      auto text =
          json_text(encode_source_fragment(package.name.as_str(), source));
      auto written = write_data_file(publication->staging.as_path(),
                                     relative.as_str(), text.as_str());
      if (written.is_err()) {
        (void)abort_publication(*publication);
        return Err(rstd::move(written).unwrap_err());
      }
    }
    auto relative = package_data_path(package.name.as_str());
    auto text = package_json(package);
    auto written = write_data_file(publication->staging.as_path(),
                                   relative.as_str(), text.as_str());
    if (written.is_err()) {
      (void)abort_publication(*publication);
      return Err(rstd::move(written).unwrap_err());
    }
  }
  auto manifest_text = dataset_manifest_json(dataset);
  auto written = write_data_file(publication->staging.as_path(),
                                 "manifest.json"_str, manifest_text.as_str());
  if (written.is_err()) {
    (void)abort_publication(*publication);
    return Err(rstd::move(written).unwrap_err());
  }
  auto validated = load_dataset(publication->staging.as_path());
  if (validated.is_err()) {
    (void)abort_publication(*publication);
    return Err(rstd::format("generated doc dataset is invalid: {}",
                            rstd::move(validated).unwrap_err()));
  }
  auto committed = commit_publication(*publication);
  if (committed.is_err())
    return Err(rstd::move(committed).unwrap_err());
  return Ok(summarize_dataset(output, dataset));
}

auto append_search_entries(JsonArray &entries, const Package &package,
                           bool package_root) -> void {
  for (const auto &symbol : package.symbols) {
    auto href = symbol_href(symbol);
    auto object = JsonMap::make();
    object.insert(String::make("package"_str),
                  json_string(package.name.as_str()));
    object.insert(String::make("module"_str),
                  json_string(symbol.module.as_str()));
    object.insert(String::make("kind"_str),
                  json_string(declaration_kind_name(symbol.kind)));
    object.insert(String::make("name"_str), json_string(symbol.name.as_str()));
    object.insert(String::make("qualified-name"_str),
                  json_string(symbol.qualified_name.as_str()));
    object.insert(
        String::make("url"_str),
        json_string((package_root
                         ? href.clone()
                         : rstd::format("package/{}/{}", package.name.as_str(),
                                        href.as_str()))
                        .as_str()));
    entries.push(Json::Object(rstd::move(object)));
  }
}

auto search_json(const Dataset &dataset) -> String {
  auto entries = JsonArray::make();
  for (const auto &package : dataset.packages) {
    append_search_entries(entries, package, false);
  }
  return json_text(Json::Array(rstd::move(entries)));
}

auto search_script(const Dataset &dataset) -> String {
  auto result = String::make("window.__LITO_DOC_SEARCH__ = "_str);
  auto json = search_json(dataset);
  auto body = json.as_str().trim_ascii();
  result.push_str(body);
  result.push_str(";\n"_str);
  return result;
}

auto package_search_json(const Package &package) -> String {
  auto entries = JsonArray::make();
  append_search_entries(entries, package, true);
  return json_text(Json::Array(rstd::move(entries)));
}

auto package_search_script(const Package &package) -> String {
  auto result = String::make("window.__LITO_DOC_SEARCH__ = "_str);
  auto json = package_search_json(package);
  result.push_str(json.as_str().trim_ascii());
  result.push_str(";\n"_str);
  return result;
}

} // namespace lito::doc
