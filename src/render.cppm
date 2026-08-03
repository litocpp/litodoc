export module tenon.doc:render;

import rstd;
import :model;
import :json;
import :html;
import :markdown;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::doc
{

auto doc_path(ref<rstd::path::Path> root, ref<str> relative) -> rstd::path::PathBuf {
    return rstd::path::PathBuf::from(root).join(rstd::path::PathBuf::from(relative).as_path());
}

auto write_doc_file(ref<rstd::path::Path> root, ref<str> relative, ref<str> contents)
    -> Result<empty, String> {
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

auto page_begin(ref<str> title, ref<str> asset_prefix) -> String {
    return rstd::format(
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>{}</title><link rel=\"stylesheet\" href=\"{}static/style.css\"></head>"
        "<body><main>",
        escape_html(title).as_str(),
        asset_prefix);
}

auto page_end() -> ref<str> {
    return "</main></body></html>"_str;
}

auto render_root(const Database& database) -> String {
    auto output = page_begin("Tenon documentation"_str, ""_str);
    output.push_str("<h1>Tenon documentation</h1><ul class=\"cards\">"_str);
    for (const auto& package : database.packages) {
        output.push_str(rstd::format("<li><a href=\"package/{}/index.html\">{}</a> "
                                     "<span>{}</span></li>",
                                     package.name.as_str(),
                                     escape_html(package.name.as_str()).as_str(),
                                     escape_html(package.version.as_str()).as_str())
                            .as_str());
    }
    output.push_str("</ul>"_str);
    output.push_str(page_end());
    return output;
}

auto render_package(const Package& package) -> String {
    auto output = page_begin(package.name.as_str(), "../../"_str);
    output.push_str(rstd::format("<nav><a href=\"../../index.html\">Packages</a></nav>"
                                 "<h1>{}</h1><p class=\"meta\">version {}</p>",
                                 escape_html(package.name.as_str()).as_str(),
                                 escape_html(package.version.as_str()).as_str())
                        .as_str());
    output.push_str("<h2>Modules</h2><ul>"_str);
    for (const auto& module : package.modules) {
        output.push_str(rstd::format("<li><a href=\"{}\">{}</a></li>",
                                     module.page.as_str(),
                                     escape_html(module.name.as_str()).as_str())
                            .as_str());
    }
    output.push_str("</ul><h2>Public API</h2><ul>"_str);
    for (const auto& symbol : package.symbols) {
        output.push_str(rstd::format("<li><span class=\"kind\">{}</span> "
                                     "<a href=\"{}\">{}</a></li>",
                                     declaration_kind_name(symbol.kind),
                                     symbol.page.as_str(),
                                     escape_html(symbol.qualified_name.as_str()).as_str())
                            .as_str());
    }
    output.push_str(rstd::format("</ul><p class=\"coverage\">{} documented, {} undocumented, "
                                 "{} unsupported</p>",
                                 package.documented,
                                 package.undocumented,
                                 package.unsupported)
                        .as_str());
    output.push_str(page_end());
    return output;
}

auto render_module(const Package& package, const Module& module) -> String {
    auto output = page_begin(module.name.as_str(), "../../../"_str);
    output.push_str(rstd::format("<nav><a href=\"../index.html\">{}</a></nav><h1>module {}</h1>",
                                 escape_html(package.name.as_str()).as_str(),
                                 escape_html(module.name.as_str()).as_str())
                        .as_str());
    if (module.comment.is_some())
        output.push_str(render_markdown(module.comment->as_str()).as_str());
    if (! module.reexports.is_empty()) {
        output.push_str("<h2>Reexports</h2><ul>"_str);
        for (const auto& reexport : module.reexports)
            output.push_str(
                rstd::format("<li><code>{}</code></li>", escape_html(reexport.as_str()).as_str())
                    .as_str());
        output.push_str("</ul>"_str);
    }
    output.push_str("<h2>Symbols</h2><ul>"_str);
    for (const auto& symbol : package.symbols) {
        if (symbol.module.as_str() != module.name.as_str()) continue;
        output.push_str(rstd::format("<li><a href=\"../{}\">{}</a></li>",
                                     symbol.page.as_str(),
                                     escape_html(symbol.qualified_name.as_str()).as_str())
                            .as_str());
    }
    output.push_str("</ul>"_str);
    output.push_str(page_end());
    return output;
}

auto render_symbol(const Package& package, const Symbol& symbol) -> String {
    auto output = page_begin(symbol.qualified_name.as_str(), "../../../"_str);
    output.push_str(rstd::format("<nav><a href=\"../index.html\">{}</a> / "
                                 "<a href=\"../{}\">{}</a></nav>",
                                 escape_html(package.name.as_str()).as_str(),
                                 symbol.module_page.as_str(),
                                 escape_html(symbol.module.as_str()).as_str())
                        .as_str());
    output.push_str(rstd::format("<p class=\"kind\">{}</p><h1>{}</h1><pre><code>{}</code></pre>",
                                 declaration_kind_name(symbol.kind),
                                 escape_html(symbol.qualified_name.as_str()).as_str(),
                                 escape_html(symbol.signature.as_str()).as_str())
                        .as_str());
    if (symbol.comment.is_some())
        output.push_str(render_markdown(symbol.comment->as_str()).as_str());
    output.push_str(rstd::format("<p><a href=\"../{}#L{}\">source</a></p>",
                                 symbol.source_page.as_str(),
                                 symbol.source_line)
                        .as_str());
    output.push_str(page_end());
    return output;
}

auto render_source(const Package& package, const Source& source) -> String {
    auto output = page_begin(source.path.as_str(), "../../../"_str);
    output.push_str(rstd::format("<nav><a href=\"../index.html\">{}</a></nav><h1>{}</h1><pre>",
                                 escape_html(package.name.as_str()).as_str(),
                                 escape_html(source.path.as_str()).as_str())
                        .as_str());
    auto begin = usize {};
    auto line  = usize(1);
    auto bytes = source.contents.as_str().as_bytes();
    while (begin <= bytes.len()) {
        auto end = begin;
        while (end < bytes.len() && bytes[end] != u8('\n') && bytes[end] != u8('\r')) ++end;
        output.push_str(
            rstd::format("<span id=\"L{}\" class=\"line\"><a href=\"#L{}\">{}</a> {}</span>\n",
                         line,
                         line,
                         line,
                         escape_html(source.contents.as_str().get(begin, end).unwrap()).as_str())
                .as_str());
        ++line;
        if (end == bytes.len()) break;
        if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
            bytes[end + usize(1)] == u8('\n'))
            ++end;
        begin = end + usize(1);
    }
    output.push_str("</pre>"_str);
    output.push_str(page_end());
    return output;
}

auto render_database(ref<rstd::path::Path> root, const Database& database)
    -> Result<empty, String> {
    auto written = write_doc_file(root, "index.html"_str, render_root(database).as_str());
    if (written.is_err()) return written;
    written = write_doc_file(root, "search-index.json"_str, search_json(database).as_str());
    if (written.is_err()) return written;
    constexpr auto style =
        ":root{color-scheme:light dark;font:16px/1.55 system-ui,sans-serif}body{margin:0}"
        "main{max-width:72rem;margin:auto;padding:2rem}a{color:#4f8cff}.meta,.kind{opacity:.72}"
        "pre{overflow:auto;padding:1rem;background:#161b22;border-radius:.5rem}.line{display:block}"
        ".line>a{display:inline-block;width:3rem;text-align:right;margin-right:1rem;opacity:.55}"
        ".cards{display:grid;gap:.75rem;list-style:none;padding:0}.cards li{padding:1rem;border:1px solid #7775;border-radius:.5rem}"_str;
    written = write_doc_file(root, "static/style.css"_str, style);
    if (written.is_err()) return written;
    for (const auto& package : database.packages) {
        auto prefix = rstd::format("package/{}/", package.name.as_str());
        written     = write_doc_file(root,
                                     rstd::format("{}index.html", prefix.as_str()).as_str(),
                                     render_package(package).as_str());
        if (written.is_err()) return written;
        written = write_doc_file(root,
                                 rstd::format("{}doc.json", prefix.as_str()).as_str(),
                                 package_json(package).as_str());
        if (written.is_err()) return written;
        for (const auto& module : package.modules) {
            written =
                write_doc_file(root,
                               rstd::format("{}{}", prefix.as_str(), module.page.as_str()).as_str(),
                               render_module(package, module).as_str());
            if (written.is_err()) return written;
        }
        for (const auto& symbol : package.symbols) {
            written =
                write_doc_file(root,
                               rstd::format("{}{}", prefix.as_str(), symbol.page.as_str()).as_str(),
                               render_symbol(package, symbol).as_str());
            if (written.is_err()) return written;
        }
        for (const auto& source : package.sources) {
            written =
                write_doc_file(root,
                               rstd::format("{}{}", prefix.as_str(), source.page.as_str()).as_str(),
                               render_source(package, source).as_str());
            if (written.is_err()) return written;
        }
    }
    return Ok(empty {});
}

} // namespace tenon::doc
