export module lito.doc:render;

import rstd;
import rstd.json;
import licrypto;
import :model;
import :data;
import :markdown;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using RenderJson = rstd::json::Value;
using RenderJsonMap = rstd::json::Map;
using namespace lito::site;

namespace lito::doc {

auto doc_path(ref<rstd::path::Path> root, ref<str> relative)
    -> rstd::path::PathBuf {
  return rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path());
}

auto publication_media_type(ref<str> path) -> ref<str> {
  if (path.ends_with(".html"_str))
    return "text/html; charset=utf-8"_str;
  if (path.ends_with(".json"_str))
    return "application/json; charset=utf-8"_str;
  if (path.ends_with(".js"_str))
    return "text/javascript; charset=utf-8"_str;
  if (path.ends_with(".css"_str))
    return "text/css; charset=utf-8"_str;
  if (path.ends_with(".svg"_str))
    return "image/svg+xml"_str;
  return "application/octet-stream"_str;
}

auto write_doc_file(ref<rstd::path::Path> root, ref<str> relative,
                    ref<str> contents,
                    Vec<PublicationFile> *publication = nullptr)
    -> Result<empty, String> {
  if (!safe_frontend_path(relative))
    return Err(rstd::format("invalid doc output path '{}'", relative));
  auto path = doc_path(root, relative);
  auto parent = path.as_path().parent();
  if (parent.is_none())
    return Err(rstd::format("doc output '{}' has no parent", path.as_path()));
  auto created = rstd::fs::create_dir_all(*parent);
  if (created.is_err())
    return Err(rstd::format("cannot create doc directory '{}': {}", *parent,
                            rstd::move(created).unwrap_err()));
  auto written = rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
  if (written.is_err())
    return Err(rstd::format("cannot write doc output '{}': {}", path.as_path(),
                            rstd::move(written).unwrap_err()));
  if (publication != nullptr) {
    publication->push(PublicationFile{
        .path = String::make(relative),
        .size = contents.len(),
        .sha256 = licrypto::sha256_hex(contents),
        .media_type = String::make(publication_media_type(relative)),
        .cache = String::make("immutable"_str),
    });
  }
  return Ok(empty{});
}

auto template_text(ref<str> value) -> TemplateValue {
  return TemplateValue::text_value(value);
}

auto template_number(usize value) -> TemplateValue {
  return TemplateValue::text_value(rstd::format("{}", value));
}

auto append_outline(TemplateValue &page, ref<str> href, ref<str> label)
    -> void {
  auto outline = page.object.get_mut("outline"_str).unwrap();
  auto item = TemplateValue::object_value();
  item.insert("href"_str, template_text(href));
  item.insert("label"_str, template_text(label));
  item.insert("level"_str, template_number(usize(2)));
  outline->array.push(rstd::move(item));
  page.object.insert(String::make("has_outline"_str),
                     TemplateValue::boolean_value(true));
}

auto page_value(ref<str> title, ref<str> kind, ref<str> asset_prefix)
    -> TemplateValue {
  auto page = TemplateValue::object_value();
  page.insert("title"_str, template_text(title));
  page.insert("kind"_str, template_text(kind));
  page.insert("asset_prefix"_str, template_text(asset_prefix));
  page.insert("search_package"_str, TemplateValue::text_value(String::make()));
  page.insert("search_module"_str, TemplateValue::text_value(String::make()));
  page.insert("is_api"_str, TemplateValue::boolean_value(true));
  page.insert("is_book"_str, TemplateValue::boolean_value(false));
  page.insert("has_outline"_str, TemplateValue::boolean_value(false));
  page.insert("outline"_str, TemplateValue::array_value());
  return page;
}

auto site_value(const Dataset &dataset) -> TemplateValue {
  auto site = TemplateValue::object_value();
  site.insert("title"_str, template_text(dataset.title.as_str()));
  site.insert("package_count"_str, template_number(dataset.packages.len()));
  return site;
}

auto package_site_value(const Package &package) -> TemplateValue {
  auto site = TemplateValue::object_value();
  site.insert("title"_str, template_text(package.name.as_str()));
  site.insert("package_count"_str, template_number(usize(1)));
  return site;
}

auto package_route(ref<str> package) -> String {
  return rstd::format("package/{}/index.html", package);
}

auto direct_module_child(ref<str> parent, ref<str> candidate) -> bool {
  if (candidate == parent || !candidate.starts_with(parent) ||
      candidate.len() <= parent.len())
    return false;
  const auto boundary = candidate[parent.len()];
  if (boundary != u8('.') && boundary != u8(':'))
    return false;
  if (parent.contains(":"_str) && boundary != u8('.'))
    return false;
  for (auto index = parent.len() + usize(1); index < candidate.len(); ++index) {
    if (candidate[index] == u8('.') || candidate[index] == u8(':'))
      return false;
  }
  return true;
}

struct ModuleNavigationEntry {
  String name;
  String label;
  String path;
};

struct ModuleLinks {
  TemplateValue items;
  usize count{};
};

enum class RenderLayout {
  Workspace,
  Package,
};

struct PackageRenderIndex {
  rstd::collections::BTreeMap<String, usize> symbols;
  rstd::collections::BTreeMap<String, rstd::collections::BTreeMap<usize, usize>>
      children;
};

struct DeclarationTarget {
  usize package{};
  usize symbol{};
};

struct DeclarationTargetIndex {
  rstd::collections::BTreeMap<String, Vec<DeclarationTarget>> targets;
};

auto declaration_target_index(const Dataset &dataset)
    -> DeclarationTargetIndex {
  auto result = DeclarationTargetIndex{
      .targets =
          rstd::collections::BTreeMap<String, Vec<DeclarationTarget>>::make(),
  };
  for (auto package = usize{}; package < dataset.packages.len(); ++package) {
    for (auto symbol = usize{};
         symbol < dataset.packages[package].symbols.len(); ++symbol) {
      const auto &candidate = dataset.packages[package].symbols[symbol];
      if (!is_published_symbol_kind(candidate.kind) ||
          candidate.placement != SymbolPlacement::Standalone ||
          candidate.semantic_identity.is_empty())
        continue;
      auto targets =
          result.targets.get_mut(candidate.semantic_identity.as_str());
      if (targets.is_none()) {
        auto values = Vec<DeclarationTarget>::make();
        values.push(DeclarationTarget{.package = package, .symbol = symbol});
        result.targets.insert(candidate.semantic_identity.clone(),
                              rstd::move(values));
      } else {
        (**targets).push(
            DeclarationTarget{.package = package, .symbol = symbol});
      }
    }
  }
  return result;
}

auto resolve_declaration_target(const Dataset &dataset,
                                const DeclarationTargetIndex &index,
                                const Package &package, ref<str> identity,
                                RenderLayout layout)
    -> Option<DeclarationTarget> {
  auto targets = index.targets.get(identity);
  if (targets.is_none())
    return None();
  auto has_local = false;
  auto local = DeclarationTarget{};
  for (const auto &target : **targets) {
    if (dataset.packages[target.package].name.as_str() != package.name.as_str())
      continue;
    if (has_local)
      return None();
    local = DeclarationTarget{
        .package = target.package,
        .symbol = target.symbol,
    };
    has_local = true;
  }
  if (has_local)
    return Some(rstd::move(local));
  if (layout == RenderLayout::Workspace && (**targets).len() == usize(1))
    return Some(DeclarationTarget{
        .package = (**targets)[usize{}].package,
        .symbol = (**targets)[usize{}].symbol,
    });
  return None();
}

auto declaration_target_href(const Dataset &dataset, const Package &package,
                             DeclarationTarget target, RenderLayout layout)
    -> Option<String> {
  const auto &target_package = dataset.packages[target.package];
  const auto &target_symbol = target_package.symbols[target.symbol];
  if (target_package.name.as_str() == package.name.as_str())
    return Some(rstd::format("../{}", symbol_href(target_symbol)));
  if (layout != RenderLayout::Workspace)
    return None();
  return Some(rstd::format("../../{}/{}", target_package.name.as_str(),
                           symbol_href(target_symbol)));
}

auto declaration_fragments(const Dataset &dataset,
                           const DeclarationTargetIndex &index,
                           const Package &package,
                           const DeclarationText &declaration,
                           RenderLayout layout) -> TemplateValue {
  auto result = TemplateValue::array_value();
  auto append = [&result](ref<str> text, Option<String> href) {
    if (text.is_empty())
      return;
    auto item = TemplateValue::object_value();
    item.insert("text"_str, template_text(text));
    item.insert("has_href"_str, TemplateValue::boolean_value(href.is_some()));
    item.insert("plain"_str, TemplateValue::boolean_value(href.is_none()));
    item.insert("href"_str,
                template_text(href.is_some() ? href->as_str() : ""_str));
    result.array.push(rstd::move(item));
  };
  auto position = usize{};
  for (const auto &reference : declaration.references) {
    append(declaration.text.as_str().get(position, reference.begin).unwrap(),
           None());
    auto target = resolve_declaration_target(
        dataset, index, package, reference.semantic_identity.as_str(), layout);
    auto href = target.is_some()
                    ? declaration_target_href(dataset, package, *target, layout)
                    : Option<String>{};
    append(
        declaration.text.as_str().get(reference.begin, reference.end).unwrap(),
        rstd::move(href));
    position = reference.end;
  }
  append(
      declaration.text.as_str().get(position, declaration.text.len()).unwrap(),
      None());
  return result;
}

auto package_render_index(const Package &package) -> PackageRenderIndex {
  auto index = PackageRenderIndex{
      .symbols = rstd::collections::BTreeMap<String, usize>::make(),
      .children = rstd::collections::BTreeMap<
          String, rstd::collections::BTreeMap<usize, usize>>::make(),
  };
  for (auto position = usize{}; position < package.symbols.len(); ++position) {
    const auto &symbol = package.symbols[position];
    if (!is_published_symbol_kind(symbol.kind))
      continue;
    index.symbols.insert(symbol.key.clone(), position);
  }
  for (auto position = usize{}; position < package.symbols.len(); ++position) {
    const auto &symbol = package.symbols[position];
    if (!is_published_symbol_kind(symbol.kind))
      continue;
    if (symbol.parent_key.is_none())
      continue;
    if (!index.symbols.contains_key(symbol.parent_key->as_str()))
      continue;
    auto children = index.children.get_mut(symbol.parent_key->as_str());
    if (children.is_none()) {
      auto positions = rstd::collections::BTreeMap<usize, usize>::make();
      positions.insert(symbol.declaration_order,
                       usize(position.to_primitive()));
      index.children.insert(symbol.parent_key->clone(), rstd::move(positions));
    } else {
      (**children)
          .insert(symbol.declaration_order, usize(position.to_primitive()));
    }
  }
  return index;
}

auto is_record_member(const Symbol &symbol) -> bool {
  return symbol.placement == SymbolPlacement::RecordMember;
}

auto declaration_link_value(const Symbol &symbol, ref<str> href_prefix)
    -> TemplateValue {
  auto item = TemplateValue::object_value();
  item.insert("name"_str, template_text(symbol.name.as_str()));
  item.insert("qualified_name"_str,
              template_text(symbol.qualified_name.as_str()));
  item.insert("has_namespace"_str,
              TemplateValue::boolean_value(!symbol.namespace_name.is_empty()));
  item.insert("namespace"_str, template_text(symbol.namespace_name.as_str()));
  auto href = symbol_href(symbol);
  item.insert("href"_str, TemplateValue::text_value(
                              rstd::format("{}{}", href_prefix, href)));
  return item;
}

auto record_member_value(const Dataset &dataset,
                         const DeclarationTargetIndex &targets,
                         const Package &package, const Symbol &symbol,
                         RenderLayout layout) -> TemplateValue {
  auto item = TemplateValue::object_value();
  item.insert("id"_str, template_text(symbol.anchor->as_str()));
  item.insert("name"_str, template_text(symbol.name.as_str()));
  item.insert("signature"_str, template_text(symbol.scope_signature.as_str()));
  item.insert("signature_fragments"_str,
              declaration_fragments(dataset, targets, package,
                                    symbol.scope_signature, layout));
  item.insert("has_documentation"_str,
              TemplateValue::boolean_value(symbol.comment.is_some()));
  item.insert("documentation"_str,
              TemplateValue::trusted_html(
                  symbol.comment.is_some()
                      ? render_markdown(symbol.comment->as_str())
                      : String::make()));
  item.insert("source_path"_str, template_text(symbol.source_path.as_str()));
  item.insert("source_line"_str, template_number(symbol.source_line));
  item.insert("source_href"_str,
              TemplateValue::text_value(rstd::format(
                  "../{}#L{}", symbol.source_page, symbol.source_line)));
  return item;
}

auto direct_module_navigation(const Package &package, ref<str> parent)
    -> Vec<ModuleNavigationEntry> {
  auto result = Vec<ModuleNavigationEntry>::make();
  for (const auto &module : package.modules) {
    if (!direct_module_child(parent, module.name.as_str()))
      continue;
    result.push(ModuleNavigationEntry{
        .name = module.name.clone(),
        .label =
            String::make(module.name.as_str()
                             .get(parent.len() + usize(1), module.name.len())
                             .unwrap()),
        .path = module.page.clone(),
    });
  }
  return result;
}

auto module_links(const Vec<ModuleNavigationEntry> &entries,
                  ref<str> href_prefix) -> ModuleLinks {
  auto result = ModuleLinks{.items = TemplateValue::array_value()};
  for (const auto &entry : entries) {
    auto item = TemplateValue::object_value();
    item.insert("name"_str, template_text(entry.name.as_str()));
    item.insert("label"_str, template_text(entry.label.as_str()));
    item.insert("href"_str, TemplateValue::text_value(rstd::format(
                                "{}{}", href_prefix, entry.path.as_str())));
    result.items.array.push(rstd::move(item));
    ++result.count;
  }
  return result;
}

auto module_navigation_json(const Package &package) -> String {
  auto root = RenderJsonMap::make();
  root.insert(String::make("format"_str),
              RenderJson::String(String::make("lito-doc-navigation"_str)));
  root.insert(String::make("version"_str),
              RenderJson::Number(rstd::json::Number::from_u64(u64(1))));
  root.insert(String::make("package"_str),
              RenderJson::String(package.name.clone()));
  auto modules = rstd::json::Array::make();
  auto entries =
      direct_module_navigation(package, package.root_module.as_str());
  for (const auto &entry : entries) {
    auto module = RenderJsonMap::make();
    module.insert(String::make("label"_str),
                  RenderJson::String(entry.label.clone()));
    module.insert(String::make("url"_str),
                  RenderJson::String(entry.path.clone()));
    modules.push(RenderJson::Object(rstd::move(module)));
  }
  root.insert(String::make("modules"_str),
              RenderJson::Array(rstd::move(modules)));
  auto result = rstd::json::to_string(
      RenderJson::Object(rstd::move(root)),
      rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  result.push_ascii('\n');
  return result;
}

auto navigation_value(const Dataset &dataset, const Package *current,
                      ref<str> asset_prefix, bool show_module_supplement,
                      RenderLayout layout) -> TemplateValue {
  auto navigation = TemplateValue::object_value();
  auto packages = TemplateValue::array_value();
  if (current == nullptr) {
    for (const auto &package : dataset.packages) {
      auto item = TemplateValue::object_value();
      item.insert("name"_str, template_text(package.name.as_str()));
      item.insert("version"_str, template_text(package.version.as_str()));
      item.insert("href"_str,
                  TemplateValue::text_value(rstd::format(
                      "{}{}", asset_prefix,
                      package_route(package.name.as_str()).as_str())));
      packages.array.push(rstd::move(item));
    }
  }
  navigation.insert("has_packages"_str,
                    TemplateValue::boolean_value(current == nullptr));
  navigation.insert("packages"_str, rstd::move(packages));
  auto modules = ModuleLinks{.items = TemplateValue::array_value()};
  auto module_navigation_url = String::make();
  auto module_fallback_url = String::make();
  if (current != nullptr) {
    auto href_prefix = layout == RenderLayout::Package
                           ? String::make(asset_prefix)
                           : rstd::format("{}package/{}/", asset_prefix,
                                          current->name.as_str());
    auto entries =
        direct_module_navigation(*current, current->root_module.as_str());
    modules = module_links(entries, href_prefix.as_str());
    module_navigation_url = rstd::format("{}navigation.json", href_prefix);
    module_fallback_url = rstd::format("{}index.html", href_prefix);
  }
  auto show_modules = show_module_supplement && modules.count != usize{};
  navigation.insert("has_modules"_str,
                    TemplateValue::boolean_value(modules.count != usize{}));
  navigation.insert("show_modules"_str,
                    TemplateValue::boolean_value(show_modules));
  navigation.insert("has_scroll_state"_str,
                    TemplateValue::boolean_value(show_modules));
  navigation.insert(
      "scroll_state_url"_str,
      TemplateValue::text_value(show_modules ? module_navigation_url.clone()
                                             : String::make()));
  navigation.insert(
      "module_navigation_url"_str,
      TemplateValue::text_value(rstd::move(module_navigation_url)));
  navigation.insert("module_fallback_url"_str,
                    TemplateValue::text_value(rstd::move(module_fallback_url)));
  navigation.insert("modules"_str, rstd::move(modules.items));
  return navigation;
}

auto base_context(const Dataset &dataset, const Package *current,
                  ref<str> title, ref<str> kind, ref<str> asset_prefix,
                  bool show_module_supplement,
                  RenderLayout layout = RenderLayout::Workspace)
    -> TemplateValue {
  auto context = TemplateValue::object_value();
  context.insert("site"_str,
                 layout == RenderLayout::Package && current != nullptr
                     ? package_site_value(*current)
                     : site_value(dataset));
  auto page = page_value(title, kind, asset_prefix);
  if (current != nullptr)
    page.insert("search_package"_str, template_text(current->name.as_str()));
  context.insert("page"_str, rstd::move(page));
  context.insert("navigation"_str,
                 navigation_value(dataset, current, asset_prefix,
                                  show_module_supplement, layout));
  return context;
}

auto package_link_value(const Package &package, ref<str> href)
    -> TemplateValue {
  auto value = TemplateValue::object_value();
  value.insert("name"_str, template_text(package.name.as_str()));
  value.insert("version"_str, template_text(package.version.as_str()));
  value.insert("root_module"_str, template_text(package.root_module.as_str()));
  value.insert("href"_str, template_text(href));
  return value;
}

auto split_module(ref<str> name) -> rstd::tuple<String, String> {
  for (auto index = usize{}; index < name.len(); ++index) {
    if (name[index] == u8(':')) {
      return {
          String::make(name.get(usize{}, index).unwrap()),
          String::make(name.get(index + usize(1), name.len()).unwrap()),
      };
    }
  }
  return {String::make(name), String::make()};
}

auto root_context(const Dataset &dataset) -> TemplateValue {
  auto context =
      base_context(dataset, nullptr, "Overview"_str, "root"_str, ""_str, false);
  auto packages = TemplateValue::array_value();
  for (const auto &package : dataset.packages) {
    packages.array.push(package_link_value(
        package, package_route(package.name.as_str()).as_str()));
  }
  context.insert("packages"_str, rstd::move(packages));
  auto page = context.object.get_mut("page"_str).unwrap();
  append_outline(*page, "#packages"_str, "Packages"_str);
  return context;
}

auto package_context(const Dataset &dataset, const Package &package,
                     RenderLayout layout = RenderLayout::Workspace)
    -> TemplateValue {
  auto asset_prefix = layout == RenderLayout::Package ? ""_str : "../../"_str;
  auto context =
      base_context(dataset, rstd::addressof(package), package.name.as_str(),
                   "package"_str, asset_prefix, false, layout);
  auto package_value = package_link_value(package, "index.html"_str);
  auto statistics = published_symbol_statistics(package);
  package_value.insert("documented"_str,
                       template_number(statistics.documented));
  package_value.insert("undocumented"_str,
                       template_number(statistics.undocumented));
  package_value.insert("unsupported"_str, template_number(package.unsupported));
  package_value.insert("symbol_count"_str, template_number(statistics.total));
  const Module *root_module = nullptr;
  for (const auto &module : package.modules) {
    if (module.name.as_str() == package.root_module.as_str()) {
      root_module = rstd::addressof(module);
      break;
    }
  }
  const auto has_documentation =
      root_module != nullptr && root_module->comment.is_some();
  package_value.insert("has_documentation"_str,
                       TemplateValue::boolean_value(has_documentation));
  package_value.insert("documentation"_str,
                       TemplateValue::trusted_html(
                           has_documentation
                               ? render_markdown(root_module->comment->as_str())
                               : String::make()));
  auto module_navigation =
      direct_module_navigation(package, package.root_module.as_str());
  auto modules = module_links(module_navigation, ""_str);
  package_value.insert("module_count"_str, template_number(modules.count));
  context.insert("package"_str, rstd::move(package_value));
  context.insert("modules"_str, rstd::move(modules.items));
  auto symbols = TemplateValue::array_value();
  for (const auto &symbol : package.symbols) {
    if (!is_published_symbol_kind(symbol.kind))
      continue;
    auto item = TemplateValue::object_value();
    item.insert("kind"_str, template_text(declaration_kind_name(symbol.kind)));
    item.insert("qualified_name"_str,
                template_text(symbol.qualified_name.as_str()));
    auto href = symbol_href(symbol);
    item.insert("href"_str, template_text(href.as_str()));
    symbols.array.push(rstd::move(item));
  }
  context.insert("symbols"_str, rstd::move(symbols));
  auto page = context.object.get_mut("page"_str).unwrap();
  if (has_documentation)
    append_outline(*page, "#documentation"_str, "Documentation"_str);
  append_outline(*page, "#modules"_str, "Modules"_str);
  return context;
}

auto module_context(const Dataset &dataset, const Package &package,
                    const Module &module,
                    RenderLayout layout = RenderLayout::Workspace)
    -> TemplateValue {
  auto asset_prefix =
      layout == RenderLayout::Package ? "../"_str : "../../../"_str;
  auto context = base_context(
      dataset, rstd::addressof(package), module.name.as_str(), "module"_str,
      asset_prefix, module.name.as_str() != package.root_module.as_str(),
      layout);
  context.insert("package"_str,
                 package_link_value(package, "../index.html"_str));
  auto parts = split_module(module.name.as_str());
  auto module_value = TemplateValue::object_value();
  module_value.insert("name"_str, template_text(module.name.as_str()));
  module_value.insert("prefix"_str,
                      template_text(parts.template get<0>().as_str()));
  module_value.insert("partition"_str,
                      template_text(parts.template get<1>().as_str()));
  module_value.insert("has_documentation"_str,
                      TemplateValue::boolean_value(module.comment.is_some()));
  module_value.insert("documentation"_str,
                      TemplateValue::trusted_html(
                          module.comment.is_some()
                              ? render_markdown(module.comment->as_str())
                              : String::make()));
  module_value.insert("has_reexports"_str, TemplateValue::boolean_value(
                                               !module.reexports.is_empty()));
  module_value.insert("reexport_count"_str,
                      template_number(module.reexports.len()));
  auto reexports = TemplateValue::array_value();
  for (const auto &reexport : module.reexports) {
    auto item = TemplateValue::object_value();
    item.insert("name"_str, template_text(reexport.as_str()));
    reexports.array.push(rstd::move(item));
  }
  context.insert("reexports"_str, rstd::move(reexports));
  auto module_navigation =
      direct_module_navigation(package, module.name.as_str());
  auto modules = module_links(module_navigation, "../"_str);
  module_value.insert("has_modules"_str,
                      TemplateValue::boolean_value(modules.count != usize{}));
  module_value.insert("module_count"_str, template_number(modules.count));
  context.insert("modules"_str, rstd::move(modules.items));
  auto page = context.object.get_mut("page"_str).unwrap();
  page->insert("search_module"_str, template_text(module.name.as_str()));
  auto symbols = TemplateValue::array_value();
  auto namespaces = TemplateValue::array_value();
  auto structs = TemplateValue::array_value();
  auto enums = TemplateValue::array_value();
  auto concepts = TemplateValue::array_value();
  auto aliases = TemplateValue::array_value();
  auto functions = TemplateValue::array_value();
  auto variables = TemplateValue::array_value();
  auto symbol_count = usize{};
  auto struct_count = usize{};
  auto enum_count = usize{};
  auto concept_count = usize{};
  auto alias_count = usize{};
  auto function_count = usize{};
  auto variable_count = usize{};
  for (const auto &symbol : package.symbols) {
    if (!is_published_symbol_kind(symbol.kind) ||
        symbol.module.as_str() != module.name.as_str())
      continue;
    auto item = TemplateValue::object_value();
    item.insert("kind"_str, template_text(declaration_kind_name(symbol.kind)));
    item.insert("qualified_name"_str,
                template_text(symbol.qualified_name.as_str()));
    auto href = symbol_href(symbol);
    item.insert("href"_str,
                TemplateValue::text_value(rstd::format("../{}", href)));
    symbols.array.push(rstd::move(item));
    ++symbol_count;
    if (is_record_member(symbol))
      continue;
    switch (symbol.kind) {
    case DeclarationKind::Namespace:
      break;
    case DeclarationKind::Record:
      structs.array.push(declaration_link_value(symbol, "../"_str));
      ++struct_count;
      break;
    case DeclarationKind::Enum:
      enums.array.push(declaration_link_value(symbol, "../"_str));
      ++enum_count;
      break;
    case DeclarationKind::Concept:
      concepts.array.push(declaration_link_value(symbol, "../"_str));
      ++concept_count;
      break;
    case DeclarationKind::Alias:
      aliases.array.push(declaration_link_value(symbol, "../"_str));
      ++alias_count;
      break;
    case DeclarationKind::Function:
      functions.array.push(declaration_link_value(symbol, "../"_str));
      ++function_count;
      break;
    case DeclarationKind::Variable:
      variables.array.push(declaration_link_value(symbol, "../"_str));
      ++variable_count;
      break;
    default:
      break;
    }
  }
  module_value.insert("symbol_count"_str, template_number(symbol_count));
  module_value.insert("has_namespaces"_str,
                      TemplateValue::boolean_value(false));
  module_value.insert("namespace_count"_str, template_number(usize{}));
  module_value.insert("has_structs"_str,
                      TemplateValue::boolean_value(struct_count != usize{}));
  module_value.insert("struct_count"_str, template_number(struct_count));
  module_value.insert("has_enums"_str,
                      TemplateValue::boolean_value(enum_count != usize{}));
  module_value.insert("enum_count"_str, template_number(enum_count));
  module_value.insert("has_concepts"_str,
                      TemplateValue::boolean_value(concept_count != usize{}));
  module_value.insert("concept_count"_str, template_number(concept_count));
  module_value.insert("has_aliases"_str,
                      TemplateValue::boolean_value(alias_count != usize{}));
  module_value.insert("alias_count"_str, template_number(alias_count));
  module_value.insert("has_functions"_str,
                      TemplateValue::boolean_value(function_count != usize{}));
  module_value.insert("function_count"_str, template_number(function_count));
  module_value.insert("has_variables"_str,
                      TemplateValue::boolean_value(variable_count != usize{}));
  module_value.insert("variable_count"_str, template_number(variable_count));
  context.insert("module"_str, rstd::move(module_value));
  context.insert("symbols"_str, rstd::move(symbols));
  context.insert("namespaces"_str, rstd::move(namespaces));
  context.insert("structs"_str, rstd::move(structs));
  context.insert("enums"_str, rstd::move(enums));
  context.insert("concepts"_str, rstd::move(concepts));
  context.insert("aliases"_str, rstd::move(aliases));
  context.insert("functions"_str, rstd::move(functions));
  context.insert("variables"_str, rstd::move(variables));
  page = context.object.get_mut("page"_str).unwrap();
  if (module.comment.is_some())
    append_outline(*page, "#documentation"_str, "Documentation"_str);
  if (modules.count != usize{})
    append_outline(*page, "#modules"_str, "Modules"_str);
  if (struct_count != usize{})
    append_outline(*page, "#structs"_str, "Structs"_str);
  if (enum_count != usize{})
    append_outline(*page, "#enums"_str, "Enums"_str);
  if (concept_count != usize{})
    append_outline(*page, "#concepts"_str, "Concepts"_str);
  if (alias_count != usize{})
    append_outline(*page, "#aliases"_str, "Aliases"_str);
  if (function_count != usize{})
    append_outline(*page, "#functions"_str, "Functions"_str);
  if (variable_count != usize{})
    append_outline(*page, "#variables"_str, "Variables"_str);
  return context;
}

auto symbol_context(const Dataset &dataset, const Package &package,
                    const PackageRenderIndex &index,
                    const DeclarationTargetIndex &targets, const Symbol &symbol,
                    RenderLayout layout = RenderLayout::Workspace)
    -> TemplateValue {
  auto asset_prefix =
      layout == RenderLayout::Package ? "../"_str : "../../../"_str;
  auto context = base_context(dataset, rstd::addressof(package),
                              symbol.qualified_name.as_str(), "symbol"_str,
                              asset_prefix, true, layout);
  context.insert("package"_str,
                 package_link_value(package, "../index.html"_str));
  auto parts = split_module(symbol.module.as_str());
  auto module_value = TemplateValue::object_value();
  module_value.insert("prefix"_str,
                      template_text(parts.template get<0>().as_str()));
  module_value.insert("partition"_str,
                      template_text(parts.template get<1>().as_str()));
  module_value.insert("href"_str, TemplateValue::text_value(rstd::format(
                                      "../{}", symbol.module_page)));
  context.insert("module"_str, rstd::move(module_value));
  auto page = context.object.get_mut("page"_str).unwrap();
  page->insert("search_module"_str, template_text(symbol.module.as_str()));
  auto symbol_value = TemplateValue::object_value();
  symbol_value.insert("name"_str, template_text(symbol.name.as_str()));
  symbol_value.insert("qualified_name"_str,
                      template_text(symbol.qualified_name.as_str()));
  symbol_value.insert("kind"_str,
                      template_text(declaration_kind_name(symbol.kind)));
  auto has_namespace = !symbol.namespace_name.is_empty() &&
                       symbol.placement == SymbolPlacement::Standalone &&
                       (symbol.kind == DeclarationKind::Record ||
                        symbol.kind == DeclarationKind::Function);
  symbol_value.insert("has_namespace"_str,
                      TemplateValue::boolean_value(has_namespace));
  symbol_value.insert("namespace"_str,
                      template_text(symbol.namespace_name.as_str()));
  symbol_value.insert("signature"_str,
                      template_text(symbol.signature.as_str()));
  symbol_value.insert("has_documentation"_str,
                      TemplateValue::boolean_value(symbol.comment.is_some()));
  symbol_value.insert("documentation"_str,
                      TemplateValue::trusted_html(
                          symbol.comment.is_some()
                              ? render_markdown(symbol.comment->as_str())
                              : String::make()));
  symbol_value.insert("source_path"_str,
                      template_text(symbol.source_path.as_str()));
  symbol_value.insert("source_line"_str, template_number(symbol.source_line));
  symbol_value.insert("source_href"_str, TemplateValue::text_value(rstd::format(
                                             "../{}#L{}", symbol.source_page,
                                             symbol.source_line)));
  const Symbol *parent = nullptr;
  if (symbol.parent_key.is_some()) {
    auto position = index.symbols.get(symbol.parent_key->as_str());
    if (position.is_some())
      parent = rstd::addressof(package.symbols[**position]);
  }
  symbol_value.insert("has_parent"_str,
                      TemplateValue::boolean_value(parent != nullptr));
  symbol_value.insert(
      "parent_name"_str,
      template_text(parent != nullptr ? parent->name.as_str() : ""_str));
  symbol_value.insert("parent_href"_str,
                      TemplateValue::text_value(
                          parent != nullptr
                              ? rstd::format("../{}", symbol_href(*parent))
                              : String::make()));

  auto methods = TemplateValue::array_value();
  auto member_types = TemplateValue::array_value();
  auto fields = TemplateValue::array_value();
  auto member_values = TemplateValue::array_value();
  auto method_count = usize{};
  auto member_type_count = usize{};
  auto field_count = usize{};
  auto member_value_count = usize{};
  auto field_declarations = String::make();
  if (symbol.kind == DeclarationKind::Record) {
    auto children = index.children.get(symbol.key.as_str());
    if (children.is_some()) {
      auto positions = (**children).values();
      for (auto position = positions.next(); position.is_some();
           position = positions.next()) {
        const auto &candidate = package.symbols[**position];
        switch (candidate.kind) {
        case DeclarationKind::Function:
          methods.array.push(record_member_value(dataset, targets, package,
                                                 candidate, layout));
          ++method_count;
          break;
        case DeclarationKind::Record:
        case DeclarationKind::Enum:
        case DeclarationKind::Alias:
        case DeclarationKind::Concept:
          member_types.array.push(declaration_link_value(candidate, "../"_str));
          ++member_type_count;
          break;
        case DeclarationKind::Field:
          fields.array.push(record_member_value(dataset, targets, package,
                                                candidate, layout));
          field_declarations.push_str(symbol.record_keyword.is_some() &&
                                              symbol.record_keyword->as_str() ==
                                                  "class"_str
                                          ? "    "_str
                                          : "  "_str);
          field_declarations.push_str(candidate.scope_signature.as_str());
          field_declarations.push_ascii('\n');
          ++field_count;
          break;
        case DeclarationKind::Variable:
          member_values.array.push(
              declaration_link_value(candidate, "../"_str));
          ++member_value_count;
          break;
        default:
          break;
        }
      }
    }
  }
  symbol_value.insert("has_methods"_str,
                      TemplateValue::boolean_value(method_count != usize{}));
  symbol_value.insert("method_count"_str, template_number(method_count));
  symbol_value.insert(
      "has_member_types"_str,
      TemplateValue::boolean_value(member_type_count != usize{}));
  symbol_value.insert("member_type_count"_str,
                      template_number(member_type_count));
  symbol_value.insert("has_fields"_str,
                      TemplateValue::boolean_value(field_count != usize{}));
  symbol_value.insert("field_count"_str, template_number(field_count));
  auto synopsis = String::make();
  if (field_count != usize{} && symbol.record_header.is_some()) {
    synopsis.push_str(symbol.record_header->as_str());
    synopsis.push_str(" {\n"_str);
    if (symbol.record_keyword.is_some() &&
        symbol.record_keyword->as_str() == "class"_str)
      synopsis.push_str("public:\n"_str);
    synopsis.push_str(field_declarations.as_str());
    synopsis.push_str("};"_str);
  }
  symbol_value.insert("field_synopsis"_str, template_text(synopsis.as_str()));
  symbol_value.insert(
      "has_member_values"_str,
      TemplateValue::boolean_value(member_value_count != usize{}));
  symbol_value.insert("member_value_count"_str,
                      template_number(member_value_count));
  context.insert("symbol"_str, rstd::move(symbol_value));
  context.insert("methods"_str, rstd::move(methods));
  context.insert("member_types"_str, rstd::move(member_types));
  context.insert("fields"_str, rstd::move(fields));
  context.insert("member_values"_str, rstd::move(member_values));
  if (symbol.comment.is_some()) {
    page = context.object.get_mut("page"_str).unwrap();
    append_outline(*page, "#documentation"_str, "Documentation"_str);
  }
  page = context.object.get_mut("page"_str).unwrap();
  if (member_type_count != usize{})
    append_outline(*page, "#member-types"_str, "Member Types"_str);
  if (field_count != usize{})
    append_outline(*page, "#fields"_str, "Fields"_str);
  if (method_count != usize{})
    append_outline(*page, "#methods"_str, "Methods"_str);
  if (member_value_count != usize{})
    append_outline(*page, "#member-values"_str, "Member Values"_str);
  return context;
}

auto source_lines(ref<str> contents) -> TemplateValue {
  auto result = TemplateValue::array_value();
  auto begin = usize{};
  auto line = usize(1);
  auto bytes = contents.as_bytes();
  while (begin <= bytes.len()) {
    auto end = begin;
    while (end < bytes.len() && bytes[end] != u8('\n') &&
           bytes[end] != u8('\r'))
      ++end;
    auto item = TemplateValue::object_value();
    item.insert("id"_str, TemplateValue::text_value(rstd::format("L{}", line)));
    item.insert("number"_str, template_number(line));
    item.insert("text"_str, template_text(contents.get(begin, end).unwrap()));
    result.array.push(rstd::move(item));
    ++line;
    if (end == bytes.len())
      break;
    if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
        bytes[end + usize(1)] == u8('\n'))
      ++end;
    begin = end + usize(1);
  }
  return result;
}

auto source_context(const Dataset &dataset, const Package &package,
                    const Source &source,
                    RenderLayout layout = RenderLayout::Workspace)
    -> TemplateValue {
  auto asset_prefix =
      layout == RenderLayout::Package ? "../"_str : "../../../"_str;
  auto context =
      base_context(dataset, rstd::addressof(package), source.path.as_str(),
                   "source"_str, asset_prefix, true, layout);
  context.insert("package"_str,
                 package_link_value(package, "../index.html"_str));
  auto source_value = TemplateValue::object_value();
  source_value.insert("path"_str, template_text(source.path.as_str()));
  source_value.insert("lines"_str, source_lines(source.contents.as_str()));
  context.insert("source"_str, rstd::move(source_value));
  return context;
}

auto render_page(ref<rstd::path::Path> root, ref<str> relative,
                 const FrontendBundle &frontend, ref<str> template_path,
                 const TemplateValue &context,
                 Vec<PublicationFile> *publication = nullptr)
    -> Result<empty, String> {
  auto rendered = render_template(frontend.templates, template_path, context);
  if (rendered.is_err())
    return Err(rstd::move(rendered).unwrap_err());
  return write_doc_file(root, relative, rendered->as_str(), publication);
}

auto site_manifest_json(const FrontendBundle &frontend, ref<str> data_digest)
    -> String {
  auto object = RenderJsonMap::make();
  object.insert(String::make("format"_str),
                RenderJson::String(String::make("lito-doc-site"_str)));
  object.insert(String::make("version"_str),
                RenderJson::Number(rstd::json::Number::from_u64(u64(1))));
  object.insert(String::make("data-api"_str),
                RenderJson::Number(rstd::json::Number::from_u64(u64(4))));
  object.insert(String::make("frontend"_str),
                RenderJson::String(frontend.identity.clone()));
  object.insert(String::make("frontend-digest"_str),
                RenderJson::String(frontend.digest.clone()));
  object.insert(String::make("data-digest"_str),
                RenderJson::String(String::make(data_digest)));
  auto manifest = rstd::json::to_string(
      RenderJson::Object(rstd::move(object)),
      rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  manifest.push_ascii('\n');
  return manifest;
}

auto render_package_site(ref<rstd::path::Path> root, const Dataset &dataset,
                         const Package &package, const FrontendBundle &frontend,
                         ref<str> data_digest, Vec<PublicationFile> &files)
    -> Result<empty, String> {
  for (const auto &asset : frontend.assets) {
    auto written = write_doc_file(root, asset.path.as_str(),
                                  asset.contents.as_str(), &files);
    if (written.is_err())
      return written;
  }
  auto written =
      write_doc_file(root, "static/search-index.js"_str,
                     package_search_script(package).as_str(), &files);
  if (written.is_err())
    return written;
  written = write_doc_file(root, "search-index.json"_str,
                           package_search_json(package).as_str(), &files);
  if (written.is_err())
    return written;
  written = write_doc_file(root, "navigation.json"_str,
                           module_navigation_json(package).as_str(), &files);
  if (written.is_err())
    return written;
  auto render_index = package_render_index(package);
  written = render_page(
      root, "index.html"_str, frontend, frontend.package_template.as_str(),
      package_context(dataset, package, RenderLayout::Package), &files);
  if (written.is_err())
    return written;
  written = write_doc_file(root, "doc.json"_str, package_json(package).as_str(),
                           &files);
  if (written.is_err())
    return written;
  auto target_index = declaration_target_index(dataset);
  for (const auto &module : package.modules) {
    auto context =
        module_context(dataset, package, module, RenderLayout::Package);
    written = render_page(root, module.page.as_str(), frontend,
                          frontend.module_template.as_str(), context, &files);
    if (written.is_err())
      return written;
  }
  for (const auto &symbol : package.symbols) {
    if (!is_published_symbol_kind(symbol.kind) ||
        symbol.placement != SymbolPlacement::Standalone)
      continue;
    auto context = symbol_context(dataset, package, render_index, target_index,
                                  symbol, RenderLayout::Package);
    written = render_page(root, symbol.page.as_str(), frontend,
                          frontend.symbol_template.as_str(), context, &files);
    if (written.is_err())
      return written;
  }
  for (const auto &source : package.sources) {
    auto context =
        source_context(dataset, package, source, RenderLayout::Package);
    written = render_page(root, source.page.as_str(), frontend,
                          frontend.source_template.as_str(), context, &files);
    if (written.is_err())
      return written;
  }
  auto manifest = site_manifest_json(frontend, data_digest);
  return write_doc_file(root, "site-manifest.json"_str, manifest.as_str(),
                        &files);
}

auto render_site(ref<rstd::path::Path> root, const Dataset &dataset,
                 const FrontendBundle &frontend, ref<str> data_digest)
    -> Result<empty, String> {
  for (const auto &asset : frontend.assets) {
    auto written =
        write_doc_file(root, asset.path.as_str(), asset.contents.as_str());
    if (written.is_err())
      return written;
  }
  auto written = write_doc_file(root, "static/search-index.js"_str,
                                search_script(dataset).as_str());
  if (written.is_err())
    return written;
  written = write_doc_file(root, "search-index.json"_str,
                           search_json(dataset).as_str());
  if (written.is_err())
    return written;
  auto root_value = root_context(dataset);
  written = render_page(root, "index.html"_str, frontend,
                        frontend.root_template.as_str(), root_value);
  if (written.is_err())
    return written;
  auto target_index = declaration_target_index(dataset);
  for (const auto &package : dataset.packages) {
    auto render_index = package_render_index(package);
    auto prefix = rstd::format("package/{}/", package.name.as_str());
    written = write_doc_file(
        root, rstd::format("{}navigation.json", prefix.as_str()).as_str(),
        module_navigation_json(package).as_str());
    if (written.is_err())
      return written;
    auto package_value = package_context(dataset, package);
    auto package_relative = rstd::format("{}index.html", prefix.as_str());
    written = render_page(root, package_relative.as_str(), frontend,
                          frontend.package_template.as_str(), package_value);
    if (written.is_err())
      return written;
    written = write_doc_file(
        root, rstd::format("{}doc.json", prefix.as_str()).as_str(),
        package_json(package).as_str());
    if (written.is_err())
      return written;
    for (const auto &module : package.modules) {
      auto context = module_context(dataset, package, module);
      written = render_page(
          root,
          rstd::format("{}{}", prefix.as_str(), module.page.as_str()).as_str(),
          frontend, frontend.module_template.as_str(), context);
      if (written.is_err())
        return written;
    }
    for (const auto &symbol : package.symbols) {
      if (!is_published_symbol_kind(symbol.kind) ||
          symbol.placement != SymbolPlacement::Standalone)
        continue;
      auto context =
          symbol_context(dataset, package, render_index, target_index, symbol);
      written = render_page(
          root,
          rstd::format("{}{}", prefix.as_str(), symbol.page.as_str()).as_str(),
          frontend, frontend.symbol_template.as_str(), context);
      if (written.is_err())
        return written;
    }
    for (const auto &source : package.sources) {
      auto context = source_context(dataset, package, source);
      written = render_page(
          root,
          rstd::format("{}{}", prefix.as_str(), source.page.as_str()).as_str(),
          frontend, frontend.source_template.as_str(), context);
      if (written.is_err())
        return written;
    }
  }
  auto manifest = site_manifest_json(frontend, data_digest);
  return write_doc_file(root, "site-manifest.json"_str, manifest.as_str());
}

} // namespace lito::doc
