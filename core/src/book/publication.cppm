module;
#include <rstd/macro.hpp>
export module lito.book:publication;
import rstd;
import lito.site;
import :model;
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book
{
auto add_html_output(Vec<String>& outputs, ref<str> path) -> Result<empty, String> {
    if (! safe_frontend_path(path))
        return Err(rstd::format("invalid Book publication path '{}'", path));
    for (const auto& existing : outputs) {
        if (path == existing.as_str() || path.starts_with(rstd::format("{}/", existing).as_str()) ||
            existing.as_str().starts_with(rstd::format("{}/", path).as_str()))
            return Err(
                rstd::format("Book publication output '{}' conflicts with '{}'", path, existing));
    }
    outputs.push(String::make(path));
    return Ok(empty {});
}

auto chapter_target(const BookGraph& graph, ref<str> source) -> Result<SiteLink, String> {
    auto split = source.split_once("#"_str);
    auto name  = split.is_some() ? split->template get<0>() : source;
    for (const auto& page : graph.pages) {
        if (page.source.as_str() != name) continue;
        auto target = encode_site_path(page.output.as_str());
        if (split.is_some()) {
            target.push_ascii('#');
            target.push_str(split->template get<1>());
        }
        return site_link(target.as_str());
    }
    return Err(rstd::format("redirect references unknown chapter '{}'", source));
}

auto validate_html_link(const SiteLink&     link,
                        const PreparedBook& prepared,
                        const Vec<String>&  outputs) -> Result<empty, String> {
    if (link.output.is_none()) return Ok(empty {});
    bool found = false;
    for (const auto& output : outputs) found = found || output.as_str() == link.output->as_str();
    if (! found) return Err(rstd::format("site link references missing output '{}'", *link.output));
    if (link.fragment.is_none() || link.fragment->is_empty()) return Ok(empty {});
    for (auto index = usize {}; index < prepared.graph.pages.len(); ++index) {
        if (prepared.graph.pages[index].output.as_str() != link.output->as_str()) continue;
        for (const auto& heading : prepared.content.pages[index].document.headings)
            if (heading.anchor.as_str() == link.fragment->as_str()) return Ok(empty {});
    }
    return Err(rstd::format("site link references missing fragment '{}'", *link.fragment));
}
} // namespace lito::book

export namespace lito::book
{
auto prepare_html_publication(const BookProject& project,
                              const BookGraph&   graph,
                              const BookContent& content)
    -> Result<PreparedBook::HtmlPublication, String> {
    PreparedBook::HtmlPublication html;
    html.not_found_base =
        project.html.site_url.is_some() ? String::make(project.html.site_url->url.path()) : "/"_Str;
    auto has_index = false;
    for (const auto& metadata : project.html.pages) {
        bool found = false;
        for (const auto& page : graph.pages)
            found = found || page.source.as_str() == metadata.source.as_str();
        if (! found)
            return Err(
                rstd::format("page metadata references unknown chapter '{}'", metadata.source));
    }
    for (auto index = usize {}; index < graph.pages.len(); ++index) {
        const auto&            chapter = graph.pages[index];
        PreparedBook::HtmlPage page { .output = chapter.output.clone(), .chapter = Some(index) };
        has_index = has_index || page.output.as_str() == "index.html"_str;
        if (project.description.is_some()) page.description.insert(project.description->clone());
        for (const auto& metadata : project.html.pages)
            if (metadata.source.as_str() == chapter.source.as_str())
                page.description.insert(metadata.description.clone());
        if (project.html.site_url.is_some())
            page.canonical.insert(
                rstd_try(canonical_site_url(*project.html.site_url, page.output.as_str())));
        if (project.html.edit.is_some()) {
            auto source =
                (project.source_prefix.is_empty() || project.source_prefix.as_str() == "."_str)
                    ? chapter.source.clone()
                    : rstd::format("{}/{}", project.source_prefix, chapter.source);
            auto target = rstd::format("{}{}{}",
                                       project.html.edit->prefix,
                                       encode_site_path(source.as_str()),
                                       project.html.edit->suffix);
            auto url    = rstd_try(absolute_site_url(target.as_str()));
            page.edit_url.insert(String::make(url.as_ref()));
        }
        html.pages.push(rstd::move(page));
    }
    if (! has_index) {
        PreparedBook::HtmlPage page { .output = "index.html"_Str };
        if (project.description.is_some()) page.description.insert(project.description->clone());
        if (project.html.site_url.is_some())
            page.canonical.insert(
                rstd_try(canonical_site_url(*project.html.site_url, page.output.as_str())));
        html.pages.push(rstd::move(page));
    }
    html.pages.push(PreparedBook::HtmlPage { .output = "404.html"_Str, .not_found = true });
    for (const auto& redirect : project.html.redirects) {
        auto old = rstd_try(site_link(redirect.from.as_str()));
        if (old.output.is_none() || old.fragment.is_some() || old.href.as_str().contains("?"_str) ||
            (! redirect.from.as_str().ends_with(".html"_str) &&
             ! redirect.from.as_str().ends_with("/"_str)))
            return Err(rstd::format("redirect source '{}' must name an HTML file or directory URL",
                                    redirect.from));
        auto output = old.output.take().unwrap();
        if (output.as_str() == "index.html"_str || output.as_str() == "404.html"_str ||
            output.as_str().starts_with("data/"_str) || output.as_str().starts_with("static/"_str))
            return Err(rstd::format("redirect source '{}' is reserved", redirect.from));
        auto                   target = rstd_try(chapter_target(graph, redirect.to.as_str()));
        PreparedBook::HtmlPage page { .output   = rstd::move(output),
                                      .redirect = Some(rstd::move(target)) };
        if (project.html.site_url.is_some())
            page.canonical.insert(rstd_try(
                canonical_site_url(*project.html.site_url, page.redirect->output->as_str())));
        html.pages.push(rstd::move(page));
    }
    for (const auto& page : html.pages)
        rstd_try(add_html_output(html.outputs, page.output.as_str()));
    rstd_try(add_html_output(html.outputs, "data/book.json"_str));
    for (const auto& page : graph.pages)
        rstd_try(add_html_output(html.outputs,
                                 rstd::format("data/pages/{}.json", page.identity).as_str()));
    for (const auto& asset : content.assets)
        rstd_try(add_html_output(html.outputs, asset.output.as_str()));
    return Ok(rstd::move(html));
}

auto validate_html_publication(const PreparedBook& prepared) -> Result<empty, String> {
    Vec<String> outputs;
    for (const auto& output : prepared.html.outputs)
        rstd_try(add_html_output(outputs, output.as_str()));
    for (const auto& asset : prepared.frontend.assets) {
        auto added = add_html_output(outputs, asset.path.as_str());
        if (added.is_err())
            return Err(rstd::format(
                "{}; conflicts with a frontend asset '{}'", added.unwrap_err(), asset.path));
    }
    const auto& config = prepared.project.html;
    if (config.brand.is_some())
        rstd_try(validate_html_link(config.brand->target, prepared, outputs));
    for (const auto& link : config.navigation)
        rstd_try(validate_html_link(link.target, prepared, outputs));
    for (const auto& link : config.footer_links)
        rstd_try(validate_html_link(link.target, prepared, outputs));
    for (const auto& page : prepared.html.pages)
        if (page.redirect.is_some())
            rstd_try(validate_html_link(*page.redirect, prepared, outputs));
    return Ok(empty {});
}
} // namespace lito::book
