module;
#include <rstd/macro.hpp>

export module lito.book;

export import :model;
export import :data;

import rstd;
import lito.site;
import :project;
import :summary;
import :graph;
import :markdown;
import :render;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book {

struct PreparedBook {
  BookProject project;
  BookGraph graph;
  BookContent content;
  BookDataset dataset;
};

auto prepare_book(ref<rstd::path::Path> directory,
                  const Option<rstd::path::PathBuf> &output,
                  const Option<rstd::path::PathBuf> &frontend)
    -> Result<PreparedBook, String> {
  auto project = rstd_try(load_book_project(directory, output, frontend));
  auto summary = rstd_try(parse_summary(project.summary.as_path()));
  auto graph = rstd_try(build_book_graph(project, rstd::move(summary)));
  auto documents = Vec<MarkdownDocument>::with_capacity(graph.pages.len());
  for (const auto &page : graph.pages) {
    auto source = rstd::fs::read_to_string(page.source_path.as_path());
    if (source.is_err())
      return Err(rstd::format("cannot read Book page '{}': {}",
                              page.source_path.as_path(),
                              rstd::move(source).unwrap_err()));
    documents.push(
        rstd_try(parse_markdown(page.source_path.as_path(), source->as_str())));
  }
  auto content =
      rstd_try(resolve_book_content(project, graph, rstd::move(documents)));
  auto dataset = rstd_try(make_book_dataset(project, graph, content));
  return Ok(PreparedBook{
      .project = rstd::move(project),
      .graph = rstd::move(graph),
      .content = rstd::move(content),
      .dataset = rstd::move(dataset),
  });
}

auto load_book_frontend(const BookProject &project,
                        ref<rstd::path::Path> default_frontend)
    -> Result<FrontendBundle, String> {
  auto selected = project.frontend.is_some() ? project.frontend->as_path()
                                             : default_frontend;
  if (selected.is_empty())
    return Err(String::make("default Book frontend path is required"_str));
  auto frontend = load_frontend_directory(selected);
  if (frontend.is_err())
    return Err(rstd::move(frontend).unwrap_err());
  if (!frontend->supports_book)
    return Err(rstd::format("frontend '{}' does not support Book documentation",
                            frontend->identity.as_str()));
  return frontend;
}

} // namespace lito::book

export namespace lito::book {

auto check(BookCheckInput input) -> Result<BookCheckSummary, String> {
  auto prepared = rstd_try(prepare_book(input.directory.as_path(), {}, {}));
  auto frontend =
      load_book_frontend(prepared.project, input.default_frontend.as_path());
  if (frontend.is_err())
    return Err(rstd::move(frontend).unwrap_err());
  auto headings = usize{};
  auto links = usize{};
  for (const auto &page : prepared.content.pages) {
    headings += page.document.headings.len();
    links += page.document.links.len();
  }
  return Ok(BookCheckSummary{
      .project = rstd::move(prepared.project),
      .pages = prepared.graph.pages.len(),
      .headings = headings,
      .links = links,
  });
}

auto build(BookBuildInput input) -> Result<BookSummary, String> {
  auto prepared = rstd_try(
      prepare_book(input.directory.as_path(), input.output, input.frontend));
  auto frontend = rstd_try(
      load_book_frontend(prepared.project, input.default_frontend.as_path()));
  return publish_book_site(prepared.project, prepared.graph, prepared.content,
                           prepared.dataset, frontend);
}

} // namespace lito::book
