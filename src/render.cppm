export module tenon.doc:render;

import rstd;
import rstd.json;
import :model;
import :data;
import :markdown;
import :templates;
import :frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using RenderJson    = rstd::json::Value;
using RenderJsonMap = rstd::json::Map;

namespace tenon::doc
{

auto doc_path(ref<rstd::path::Path> root, ref<str> relative) -> rstd::path::PathBuf {
    return rstd::path::PathBuf::from(root).join(rstd::path::PathBuf::from(relative).as_path());
}

auto write_doc_file(ref<rstd::path::Path> root, ref<str> relative, ref<str> contents)
    -> Result<empty, String> {
    if (! safe_frontend_path(relative))
        return Err(rstd::format("invalid doc output path '{}'", relative));
    auto path   = doc_path(root, relative);
    auto parent = path.as_path().parent();
    if (parent.is_none()) return Err(rstd::format("doc output '{}' has no parent", path.as_path()));
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err())
        return Err(rstd::format(
            "cannot create doc directory '{}': {}", *parent, rstd::move(created).unwrap_err()));
    auto written = rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
    if (written.is_err())
        return Err(rstd::format(
            "cannot write doc output '{}': {}", path.as_path(), rstd::move(written).unwrap_err()));
    return Ok(empty {});
}

auto template_text(ref<str> value) -> TemplateValue {
    return TemplateValue::text_value(value);
}

auto template_number(usize value) -> TemplateValue {
    return TemplateValue::text_value(rstd::format("{}", value));
}

auto append_outline(TemplateValue& page, ref<str> href, ref<str> label) -> void {
    auto outline = page.object.get_mut("outline"_str).unwrap();
    auto item    = TemplateValue::object_value();
    item.insert("href"_str, template_text(href));
    item.insert("label"_str, template_text(label));
    outline->array.push(rstd::move(item));
    page.object.insert(String::make("has_outline"_str), TemplateValue::boolean_value(true));
}

auto page_value(ref<str> title, ref<str> kind, ref<str> asset_prefix) -> TemplateValue {
    auto page = TemplateValue::object_value();
    page.insert("title"_str, template_text(title));
    page.insert("kind"_str, template_text(kind));
    page.insert("asset_prefix"_str, template_text(asset_prefix));
    page.insert("has_outline"_str, TemplateValue::boolean_value(false));
    page.insert("outline"_str, TemplateValue::array_value());
    return page;
}

auto site_value(const Dataset& dataset) -> TemplateValue {
    auto site = TemplateValue::object_value();
    site.insert("title"_str, template_text(dataset.title.as_str()));
    site.insert("package_count"_str, template_number(dataset.packages.len()));
    return site;
}

auto package_route(ref<str> package) -> String {
    return rstd::format("package/{}/index.html", package);
}

auto navigation_value(const Dataset& dataset, const Package* current, ref<str> asset_prefix)
    -> TemplateValue {
    auto navigation = TemplateValue::object_value();
    auto packages   = TemplateValue::array_value();
    for (const auto& package : dataset.packages) {
        auto item = TemplateValue::object_value();
        item.insert("name"_str, template_text(package.name.as_str()));
        item.insert("version"_str, template_text(package.version.as_str()));
        item.insert("href"_str,
                    TemplateValue::text_value(rstd::format(
                        "{}{}", asset_prefix, package_route(package.name.as_str()).as_str())));
        packages.array.push(rstd::move(item));
    }
    navigation.insert("packages"_str, rstd::move(packages));
    navigation.insert("has_modules"_str, TemplateValue::boolean_value(current != nullptr));
    auto modules = TemplateValue::array_value();
    if (current != nullptr) {
        for (const auto& module : current->modules) {
            auto item = TemplateValue::object_value();
            item.insert("name"_str, template_text(module.name.as_str()));
            item.insert("href"_str,
                        TemplateValue::text_value(rstd::format("{}package/{}/{}",
                                                               asset_prefix,
                                                               current->name.as_str(),
                                                               module.page.as_str())));
            modules.array.push(rstd::move(item));
        }
    }
    navigation.insert("modules"_str, rstd::move(modules));
    return navigation;
}

auto base_context(const Dataset& dataset,
                  const Package* current,
                  ref<str>       title,
                  ref<str>       kind,
                  ref<str>       asset_prefix) -> TemplateValue {
    auto context = TemplateValue::object_value();
    context.insert("site"_str, site_value(dataset));
    context.insert("page"_str, page_value(title, kind, asset_prefix));
    context.insert("navigation"_str, navigation_value(dataset, current, asset_prefix));
    return context;
}

auto package_link_value(const Package& package, ref<str> href) -> TemplateValue {
    auto value = TemplateValue::object_value();
    value.insert("name"_str, template_text(package.name.as_str()));
    value.insert("version"_str, template_text(package.version.as_str()));
    value.insert("root_module"_str, template_text(package.root_module.as_str()));
    value.insert("href"_str, template_text(href));
    return value;
}

auto split_module(ref<str> name) -> rstd::tuple<String, String> {
    for (auto index = usize {}; index < name.len(); ++index) {
        if (name[index] == u8(':')) {
            return {
                String::make(name.get(usize {}, index).unwrap()),
                String::make(name.get(index + usize(1), name.len()).unwrap()),
            };
        }
    }
    return { String::make(name), String::make() };
}

auto root_context(const Dataset& dataset) -> TemplateValue {
    auto context =
        base_context(dataset, nullptr, "Workspace documentation"_str, "root"_str, ""_str);
    auto packages = TemplateValue::array_value();
    for (const auto& package : dataset.packages) {
        packages.array.push(
            package_link_value(package, package_route(package.name.as_str()).as_str()));
    }
    context.insert("packages"_str, rstd::move(packages));
    auto page = context.object.get_mut("page"_str).unwrap();
    append_outline(*page, "#packages"_str, "Packages"_str);
    return context;
}

auto package_context(const Dataset& dataset, const Package& package) -> TemplateValue {
    auto context = base_context(
        dataset, rstd::addressof(package), package.name.as_str(), "package"_str, "../../"_str);
    auto package_value = package_link_value(package, "index.html"_str);
    package_value.insert("documented"_str, template_number(package.documented));
    package_value.insert("undocumented"_str, template_number(package.undocumented));
    package_value.insert("unsupported"_str, template_number(package.unsupported));
    package_value.insert("module_count"_str, template_number(package.modules.len()));
    package_value.insert("symbol_count"_str, template_number(package.symbols.len()));
    context.insert("package"_str, rstd::move(package_value));
    auto modules = TemplateValue::array_value();
    for (const auto& module : package.modules) {
        auto item = TemplateValue::object_value();
        item.insert("name"_str, template_text(module.name.as_str()));
        item.insert("href"_str, template_text(module.page.as_str()));
        item.insert("reexport_count"_str, template_number(module.reexports.len()));
        modules.array.push(rstd::move(item));
    }
    context.insert("modules"_str, rstd::move(modules));
    auto symbols = TemplateValue::array_value();
    for (const auto& symbol : package.symbols) {
        auto item = TemplateValue::object_value();
        item.insert("kind"_str, template_text(declaration_kind_name(symbol.kind)));
        item.insert("qualified_name"_str, template_text(symbol.qualified_name.as_str()));
        item.insert("href"_str, template_text(symbol.page.as_str()));
        symbols.array.push(rstd::move(item));
    }
    context.insert("symbols"_str, rstd::move(symbols));
    auto page = context.object.get_mut("page"_str).unwrap();
    append_outline(*page, "#modules"_str, "Modules"_str);
    append_outline(*page, "#symbols"_str, "Public API"_str);
    return context;
}

auto module_context(const Dataset& dataset, const Package& package, const Module& module)
    -> TemplateValue {
    auto context = base_context(
        dataset, rstd::addressof(package), module.name.as_str(), "module"_str, "../../../"_str);
    context.insert("package"_str, package_link_value(package, "../index.html"_str));
    auto parts        = split_module(module.name.as_str());
    auto module_value = TemplateValue::object_value();
    module_value.insert("name"_str, template_text(module.name.as_str()));
    module_value.insert("prefix"_str, template_text(parts.template get<0>().as_str()));
    module_value.insert("partition"_str, template_text(parts.template get<1>().as_str()));
    module_value.insert("has_documentation"_str,
                        TemplateValue::boolean_value(module.comment.is_some()));
    module_value.insert("documentation"_str,
                        TemplateValue::trusted_html(module.comment.is_some()
                                                        ? render_markdown(module.comment->as_str())
                                                        : String::make()));
    module_value.insert("has_reexports"_str,
                        TemplateValue::boolean_value(! module.reexports.is_empty()));
    module_value.insert("reexport_count"_str, template_number(module.reexports.len()));
    auto reexports = TemplateValue::array_value();
    for (const auto& reexport : module.reexports) {
        auto item = TemplateValue::object_value();
        item.insert("name"_str, template_text(reexport.as_str()));
        reexports.array.push(rstd::move(item));
    }
    context.insert("reexports"_str, rstd::move(reexports));
    auto symbols      = TemplateValue::array_value();
    auto symbol_count = usize {};
    for (const auto& symbol : package.symbols) {
        if (symbol.module.as_str() != module.name.as_str()) continue;
        auto item = TemplateValue::object_value();
        item.insert("kind"_str, template_text(declaration_kind_name(symbol.kind)));
        item.insert("qualified_name"_str, template_text(symbol.qualified_name.as_str()));
        item.insert("href"_str, TemplateValue::text_value(rstd::format("../{}", symbol.page)));
        symbols.array.push(rstd::move(item));
        ++symbol_count;
    }
    module_value.insert("symbol_count"_str, template_number(symbol_count));
    context.insert("module"_str, rstd::move(module_value));
    context.insert("symbols"_str, rstd::move(symbols));
    auto page = context.object.get_mut("page"_str).unwrap();
    if (module.comment.is_some()) append_outline(*page, "#documentation"_str, "Documentation"_str);
    if (! module.reexports.is_empty()) append_outline(*page, "#reexports"_str, "Reexports"_str);
    append_outline(*page, "#symbols"_str, "Symbols"_str);
    return context;
}

auto symbol_context(const Dataset& dataset, const Package& package, const Symbol& symbol)
    -> TemplateValue {
    auto context = base_context(dataset,
                                rstd::addressof(package),
                                symbol.qualified_name.as_str(),
                                "symbol"_str,
                                "../../../"_str);
    context.insert("package"_str, package_link_value(package, "../index.html"_str));
    auto parts        = split_module(symbol.module.as_str());
    auto module_value = TemplateValue::object_value();
    module_value.insert("prefix"_str, template_text(parts.template get<0>().as_str()));
    module_value.insert("partition"_str, template_text(parts.template get<1>().as_str()));
    module_value.insert("href"_str,
                        TemplateValue::text_value(rstd::format("../{}", symbol.module_page)));
    context.insert("module"_str, rstd::move(module_value));
    auto symbol_value = TemplateValue::object_value();
    symbol_value.insert("name"_str, template_text(symbol.name.as_str()));
    symbol_value.insert("qualified_name"_str, template_text(symbol.qualified_name.as_str()));
    symbol_value.insert("kind"_str, template_text(declaration_kind_name(symbol.kind)));
    symbol_value.insert("signature"_str, template_text(symbol.signature.as_str()));
    symbol_value.insert("has_documentation"_str,
                        TemplateValue::boolean_value(symbol.comment.is_some()));
    symbol_value.insert("documentation"_str,
                        TemplateValue::trusted_html(symbol.comment.is_some()
                                                        ? render_markdown(symbol.comment->as_str())
                                                        : String::make()));
    symbol_value.insert("source_path"_str, template_text(symbol.source_path.as_str()));
    symbol_value.insert("source_line"_str, template_number(symbol.source_line));
    symbol_value.insert("source_href"_str,
                        TemplateValue::text_value(
                            rstd::format("../{}#L{}", symbol.source_page, symbol.source_line)));
    context.insert("symbol"_str, rstd::move(symbol_value));
    if (symbol.comment.is_some()) {
        auto page = context.object.get_mut("page"_str).unwrap();
        append_outline(*page, "#documentation"_str, "Documentation"_str);
    }
    return context;
}

auto source_lines(ref<str> contents) -> TemplateValue {
    auto result = TemplateValue::array_value();
    auto begin  = usize {};
    auto line   = usize(1);
    auto bytes  = contents.as_bytes();
    while (begin <= bytes.len()) {
        auto end = begin;
        while (end < bytes.len() && bytes[end] != u8('\n') && bytes[end] != u8('\r')) ++end;
        auto item = TemplateValue::object_value();
        item.insert("id"_str, TemplateValue::text_value(rstd::format("L{}", line)));
        item.insert("number"_str, template_number(line));
        item.insert("text"_str, template_text(contents.get(begin, end).unwrap()));
        result.array.push(rstd::move(item));
        ++line;
        if (end == bytes.len()) break;
        if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
            bytes[end + usize(1)] == u8('\n'))
            ++end;
        begin = end + usize(1);
    }
    return result;
}

auto source_context(const Dataset& dataset, const Package& package, const Source& source)
    -> TemplateValue {
    auto context = base_context(
        dataset, rstd::addressof(package), source.path.as_str(), "source"_str, "../../../"_str);
    context.insert("package"_str, package_link_value(package, "../index.html"_str));
    auto source_value = TemplateValue::object_value();
    source_value.insert("path"_str, template_text(source.path.as_str()));
    source_value.insert("lines"_str, source_lines(source.contents.as_str()));
    context.insert("source"_str, rstd::move(source_value));
    return context;
}

auto render_page(ref<rstd::path::Path> root,
                 ref<str>              relative,
                 const FrontendBundle& frontend,
                 ref<str>              template_path,
                 const TemplateValue&  context) -> Result<empty, String> {
    auto rendered = render_template(frontend.templates, template_path, context);
    if (rendered.is_err()) return Err(rstd::move(rendered).unwrap_err());
    return write_doc_file(root, relative, rendered->as_str());
}

auto render_site(ref<rstd::path::Path> root,
                 const Dataset&        dataset,
                 const FrontendBundle& frontend,
                 ref<str>              data_digest) -> Result<empty, String> {
    for (const auto& asset : frontend.assets) {
        auto written = write_doc_file(root, asset.path.as_str(), asset.contents.as_str());
        if (written.is_err()) return written;
    }
    auto written =
        write_doc_file(root, "static/search-index.js"_str, search_script(dataset).as_str());
    if (written.is_err()) return written;
    written = write_doc_file(root, "search-index.json"_str, search_json(dataset).as_str());
    if (written.is_err()) return written;
    auto root_value = root_context(dataset);
    written =
        render_page(root, "index.html"_str, frontend, frontend.root_template.as_str(), root_value);
    if (written.is_err()) return written;
    for (const auto& package : dataset.packages) {
        auto prefix           = rstd::format("package/{}/", package.name.as_str());
        auto package_value    = package_context(dataset, package);
        auto package_relative = rstd::format("{}index.html", prefix.as_str());
        written               = render_page(root,
                                            package_relative.as_str(),
                                            frontend,
                                            frontend.package_template.as_str(),
                                            package_value);
        if (written.is_err()) return written;
        written = write_doc_file(root,
                                 rstd::format("{}doc.json", prefix.as_str()).as_str(),
                                 package_json(package).as_str());
        if (written.is_err()) return written;
        for (const auto& module : package.modules) {
            auto context = module_context(dataset, package, module);
            written =
                render_page(root,
                            rstd::format("{}{}", prefix.as_str(), module.page.as_str()).as_str(),
                            frontend,
                            frontend.module_template.as_str(),
                            context);
            if (written.is_err()) return written;
        }
        for (const auto& symbol : package.symbols) {
            auto context = symbol_context(dataset, package, symbol);
            written =
                render_page(root,
                            rstd::format("{}{}", prefix.as_str(), symbol.page.as_str()).as_str(),
                            frontend,
                            frontend.symbol_template.as_str(),
                            context);
            if (written.is_err()) return written;
        }
        for (const auto& source : package.sources) {
            auto context = source_context(dataset, package, source);
            written =
                render_page(root,
                            rstd::format("{}{}", prefix.as_str(), source.page.as_str()).as_str(),
                            frontend,
                            frontend.source_template.as_str(),
                            context);
            if (written.is_err()) return written;
        }
    }
    auto site_manifest_object = RenderJsonMap::make();
    site_manifest_object.insert(String::make("format"_str),
                                RenderJson::String(String::make("tenon-doc-site"_str)));
    site_manifest_object.insert(String::make("version"_str),
                                RenderJson::Number(rstd::json::Number::from_u64(u64(1))));
    site_manifest_object.insert(String::make("data-api"_str),
                                RenderJson::Number(rstd::json::Number::from_u64(u64(1))));
    site_manifest_object.insert(String::make("frontend"_str),
                                RenderJson::String(frontend.identity.clone()));
    site_manifest_object.insert(String::make("frontend-digest"_str),
                                RenderJson::String(frontend.digest.clone()));
    site_manifest_object.insert(String::make("data-digest"_str),
                                RenderJson::String(String::make(data_digest)));
    auto site_manifest =
        rstd::json::to_string(RenderJson::Object(rstd::move(site_manifest_object)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    site_manifest.push_ascii('\n');
    return write_doc_file(root, "site-manifest.json"_str, site_manifest.as_str());
}

} // namespace tenon::doc
