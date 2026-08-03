export module tenon.doc:json;

import rstd;
import rstd.json;
import tenon.frontend;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace tenon::doc
{

auto json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto json_usize(usize value) -> Json {
    return Json::Number(
        rstd::json::Number::from_u64(u64(static_cast<rstd::uint64_t>(value.to_primitive()))));
}

auto encode_package(const Package& package) -> Json {
    auto root = JsonMap::make();
    root.insert(String::make("format"_str), json_string("tenon-doc"_str));
    root.insert(String::make("version"_str), json_usize(usize(1)));
    auto generator = JsonMap::make();
    generator.insert(String::make("frontend"_str), json_string("tenon-native-frontend-v1"_str));
    generator.insert(String::make("parser"_str), json_string("tenon-doc-outline-v1"_str));
    generator.insert(String::make("renderer"_str), json_string("tenon-doc-html-v1"_str));
    generator.insert(String::make("toolchain-version"_str),
                     json_string(package.toolchain_version.as_str()));
    generator.insert(String::make("toolchain-target"_str),
                     json_string(package.toolchain_target.as_str()));
    generator.insert(String::make("language-standard"_str),
                     json_string(package.language_standard.as_str()));
    root.insert(String::make("generator"_str), Json::Object(rstd::move(generator)));

    auto metadata = JsonMap::make();
    metadata.insert(String::make("name"_str), json_string(package.name.as_str()));
    metadata.insert(String::make("version"_str), json_string(package.version.as_str()));
    metadata.insert(String::make("root-module"_str), json_string(package.root_module.as_str()));
    metadata.insert(String::make("profile"_str), json_string(package.profile.as_str()));
    root.insert(String::make("package"_str), Json::Object(rstd::move(metadata)));

    auto modules = JsonArray::make();
    for (const auto& module : package.modules) {
        auto object = JsonMap::make();
        object.insert(String::make("name"_str), json_string(module.name.as_str()));
        object.insert(String::make("page"_str), json_string(module.page.as_str()));
        object.insert(String::make("comment"_str),
                      module.comment.is_some() ? json_string(module.comment->as_str())
                                               : Json::Null());
        auto reexports = JsonArray::make();
        for (const auto& reexport : module.reexports)
            reexports.push(json_string(reexport.as_str()));
        object.insert(String::make("reexports"_str), Json::Array(rstd::move(reexports)));
        modules.push(Json::Object(rstd::move(object)));
    }
    root.insert(String::make("modules"_str), Json::Array(rstd::move(modules)));

    auto symbols = JsonArray::make();
    for (const auto& symbol : package.symbols) {
        auto object = JsonMap::make();
        object.insert(String::make("key"_str), json_string(symbol.key.as_str()));
        object.insert(String::make("page"_str), json_string(symbol.page.as_str()));
        object.insert(String::make("module"_str), json_string(symbol.module.as_str()));
        object.insert(String::make("kind"_str), json_string(declaration_kind_name(symbol.kind)));
        object.insert(String::make("name"_str), json_string(symbol.name.as_str()));
        object.insert(String::make("qualified-name"_str),
                      json_string(symbol.qualified_name.as_str()));
        object.insert(String::make("signature"_str), json_string(symbol.signature.as_str()));
        object.insert(String::make("parent"_str),
                      symbol.parent_key.is_some() ? json_string(symbol.parent_key->as_str())
                                                  : Json::Null());
        object.insert(String::make("group"_str),
                      symbol.group.is_some() ? json_string(symbol.group->as_str()) : Json::Null());
        object.insert(String::make("comment"_str),
                      symbol.comment.is_some() ? json_string(symbol.comment->as_str())
                                               : Json::Null());
        auto source = JsonMap::make();
        source.insert(String::make("path"_str), json_string(symbol.source_path.as_str()));
        source.insert(String::make("page"_str), json_string(symbol.source_page.as_str()));
        source.insert(String::make("line"_str), json_usize(symbol.source_line));
        source.insert(String::make("column"_str), json_usize(symbol.source_column));
        source.insert(String::make("end-line"_str), json_usize(symbol.source_end_line));
        source.insert(String::make("end-column"_str), json_usize(symbol.source_end_column));
        object.insert(String::make("source"_str), Json::Object(rstd::move(source)));
        symbols.push(Json::Object(rstd::move(object)));
    }
    root.insert(String::make("symbols"_str), Json::Array(rstd::move(symbols)));

    auto sources = JsonArray::make();
    for (const auto& source : package.sources) {
        auto object = JsonMap::make();
        object.insert(String::make("path"_str), json_string(source.path.as_str()));
        object.insert(String::make("page"_str), json_string(source.page.as_str()));
        sources.push(Json::Object(rstd::move(object)));
    }
    root.insert(String::make("sources"_str), Json::Array(rstd::move(sources)));

    auto diagnostics = JsonArray::make();
    for (const auto& diagnostic : package.diagnostics) {
        auto object = JsonMap::make();
        object.insert(String::make("severity"_str),
                      json_string(diagnostic.severity == frontend::DocumentationSeverity::Error
                                      ? "error"_str
                                      : "warning"_str));
        object.insert(String::make("code"_str), json_string(diagnostic.code.as_str()));
        object.insert(String::make("message"_str), json_string(diagnostic.message.as_str()));
        object.insert(String::make("path"_str), json_string(diagnostic.path.as_str()));
        object.insert(String::make("line"_str), json_usize(diagnostic.line));
        diagnostics.push(Json::Object(rstd::move(object)));
    }
    root.insert(String::make("diagnostics"_str), Json::Array(rstd::move(diagnostics)));

    auto coverage = JsonMap::make();
    coverage.insert(String::make("documented"_str), json_usize(package.documented));
    coverage.insert(String::make("undocumented"_str), json_usize(package.undocumented));
    coverage.insert(String::make("unsupported"_str), json_usize(package.unsupported));
    root.insert(String::make("coverage"_str), Json::Object(rstd::move(coverage)));
    return Json::Object(rstd::move(root));
}

auto package_json(const Package& package) -> String {
    auto result = rstd::json::to_string(
        encode_package(package), rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    result.push_ascii('\n');
    return result;
}

auto validate_package_json(ref<str> contents) -> Result<empty, String> {
    auto parsed = rstd::json::from_str(contents);
    if (parsed.is_err())
        return Err(rstd::format("invalid tenon doc JSON: {}", parsed.unwrap_err()));
    auto format  = parsed->get("format"_str);
    auto version = parsed->get("version"_str);
    if (format.is_none() || (**format).as_str().is_none() ||
        *(**format).as_str() != "tenon-doc"_str)
        return Err(String::make("doc JSON has an invalid format"_str));
    if (version.is_none() || (**version).as_u64().is_none() || *(**version).as_u64() != u64(1))
        return Err(String::make("unsupported tenon doc JSON version"_str));
    return Ok(empty {});
}

auto search_json(const Database& database) -> String {
    auto entries = JsonArray::make();
    for (const auto& package : database.packages) {
        for (const auto& symbol : package.symbols) {
            auto object = JsonMap::make();
            object.insert(String::make("package"_str), json_string(package.name.as_str()));
            object.insert(String::make("module"_str), json_string(symbol.module.as_str()));
            object.insert(String::make("kind"_str),
                          json_string(declaration_kind_name(symbol.kind)));
            object.insert(String::make("name"_str), json_string(symbol.name.as_str()));
            object.insert(String::make("qualified-name"_str),
                          json_string(symbol.qualified_name.as_str()));
            object.insert(
                String::make("url"_str),
                json_string(
                    rstd::format("package/{}/{}", package.name.as_str(), symbol.page.as_str())
                        .as_str()));
            entries.push(Json::Object(rstd::move(object)));
        }
    }
    auto result =
        rstd::json::to_string(Json::Array(rstd::move(entries)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    result.push_ascii('\n');
    return result;
}

} // namespace tenon::doc
