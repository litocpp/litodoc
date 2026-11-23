module;
#include <rstd/macro.hpp>

export module lito.book;

export import :model;
export import :data;
export import :watch;

import rstd;
import lito.site;
import :project;
import :summary;
import :graph;
import :markdown;
import :render;
import :publication;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book
{

auto validate_prepared_output(const PreparedBook& prepared) -> Result<empty, String> {
    auto output =
        rstd_try(validate_book_output(prepared.project, prepared.project.output.as_path()));
    if (output.as_path() != prepared.project.output.as_path())
        return Err(String::make("prepared Book output path must be normalized"_str));
    auto       publication = rstd_try(publication_layout(output.as_path(), "book-site"_str));
    const auto targets     = array<ref<rstd::path::Path>, 3> { publication.output.as_path(),
                                                               publication.staging.as_path(),
                                                               publication.backup.as_path() };
    for (auto target : targets) {
        for (const auto& page : prepared.graph.pages)
            if (page.source_path.as_path().strip_prefix(target).is_some())
                return Err(rstd::format("Book publication path '{}' overlaps page input '{}'",
                                        target,
                                        page.source_path.as_path()));
        for (const auto& asset : prepared.content.assets)
            if (asset.source.as_path().strip_prefix(target).is_some())
                return Err(rstd::format("Book publication path '{}' overlaps asset input '{}'",
                                        target,
                                        asset.source.as_path()));
    }
    return Ok(empty {});
}

auto prepare_book_content(BookProject project) -> Result<PreparedBook, String> {
    auto summary   = rstd_try(parse_summary(project.summary.as_path()));
    auto graph     = rstd_try(build_book_graph(project, rstd::move(summary)));
    auto documents = Vec<MarkdownDocument>::with_capacity(graph.pages.len());
    for (const auto& page : graph.pages) {
        auto source = rstd::fs::read_to_string(page.source_path.as_path());
        if (source.is_err())
            return Err(rstd::format("cannot read Book page '{}': {}",
                                    page.source_path.as_path(),
                                    rstd::move(source).unwrap_err()));
        documents.push(
            rstd_try(parse_markdown(page.source_path.as_path(), rstd::move(source).unwrap())));
    }
    auto content  = rstd_try(resolve_book_content(project, graph, rstd::move(documents)));
    auto dataset  = rstd_try(make_book_dataset(project, graph, content));
    auto html     = rstd_try(prepare_html_publication(project, graph, content));
    auto prepared = PreparedBook {
        .project = rstd::move(project),
        .graph   = rstd::move(graph),
        .content = rstd::move(content),
        .dataset = rstd::move(dataset),
        .html    = rstd::move(html),
    };
    rstd_try(validate_prepared_output(prepared));
    return Ok(rstd::move(prepared));
}

auto load_book_frontend(const BookProject& project, Option<FrontendBundle> default_frontend)
    -> Result<FrontendBundle, String> {
    auto frontend = Option<FrontendBundle> {};
    if (project.frontend.is_some()) {
        frontend = Some(rstd_try(load_frontend_directory(project.frontend->as_path())));
    } else if (default_frontend.is_some()) {
        frontend = rstd::move(default_frontend);
    } else {
        return Err(String::make("default Book frontend is required"_str));
    }
    if (! frontend->supports_book)
        return Err(rstd::format("frontend '{}' does not support Book documentation",
                                frontend->identity.as_str()));
    return Ok(rstd::move(frontend).unwrap());
}

auto prepare_book_project(BookProject project, Option<FrontendBundle> default_frontend)
    -> Result<PreparedBook, String> {
    auto prepared = rstd_try(prepare_book_content(rstd::move(project)));
    prepared.frontend =
        rstd_try(load_book_frontend(prepared.project, rstd::move(default_frontend)));
    rstd_try(validate_html_publication(prepared));
    return Ok(rstd::move(prepared));
}

} // namespace lito::book

export namespace lito::book
{

auto prepare(BookBuildInput input, Option<FrontendBundle> default_frontend = {})
    -> Result<PreparedBook, String> {
    auto project =
        rstd_try(load_book_project(input.directory.as_path(), input.output, input.frontend));
    return prepare_book_project(rstd::move(project), rstd::move(default_frontend));
}

auto publish(const PreparedBook& prepared) -> Result<BookSummary, String> {
    rstd_try(validate_prepared_output(prepared));
    rstd_try(validate_html_publication(prepared));
    return publish_book_site(prepared.project,
                             prepared.graph,
                             prepared.content,
                             prepared.dataset,
                             prepared.frontend,
                             prepared.html);
}

auto BookWatchSession::prepare(Option<rstd::path::PathBuf> output_override,
                               Option<FrontendBundle>      default_frontend)
    -> Result<PreparedBook, String> {
    auto project =
        rstd_try(load_book_project(input_.directory.as_path(), input_.output, input_.frontend));
    rstd_try(update_layout(project));
    auto prepared =
        rstd_try(prepare_book_project(rstd::move(project), rstd::move(default_frontend)));
    if (output_override.is_some()) {
        prepared.project.output =
            rstd_try(validate_book_output(prepared.project, output_override->as_path()));
        rstd_try(validate_prepared_output(prepared));
    }
    return Ok(rstd::move(prepared));
}

auto check(BookCheckInput input, Option<FrontendBundle> default_frontend = {})
    -> Result<BookCheckSummary, String> {
    auto project  = rstd_try(load_book_project(input.directory.as_path()));
    auto prepared = rstd_try(prepare_book_content(rstd::move(project)));
    if (prepared.project.frontend.is_some() || default_frontend.is_some())
        prepared.frontend =
            rstd_try(load_book_frontend(prepared.project, rstd::move(default_frontend)));
    rstd_try(validate_html_publication(prepared));
    auto headings = usize {};
    auto links    = usize {};
    for (const auto& page : prepared.content.pages) {
        headings += page.document.headings.len();
        links += page.document.links.len();
    }
    return Ok(BookCheckSummary {
        .project  = rstd::move(prepared.project),
        .pages    = prepared.graph.pages.len(),
        .headings = headings,
        .links    = links,
    });
}

auto build(BookBuildInput input, Option<FrontendBundle> default_frontend = {})
    -> Result<BookSummary, String> {
    auto prepared = rstd_try(prepare(rstd::move(input), rstd::move(default_frontend)));
    return publish(prepared);
}

} // namespace lito::book
