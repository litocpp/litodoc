module;
#include <rstd/macro.hpp>

export module lito.book:graph;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::book {

auto graph_error(const SourceSpan &span, ref<str> message) -> String {
  return rstd::format("{}:{}:{}: {}", span.path.as_path(), span.line,
                      span.column, message);
}

auto page_output(ref<str> source) -> String {
  if (source == "index.md"_str)
    return String::make("index.html"_str);
  auto stem = source.get(usize{}, source.len() - usize(3)).unwrap();
  return rstd::format("{}/index.html", stem);
}

} // namespace lito::book

export namespace lito::book {

auto build_book_graph(const BookProject &project, SummaryDocument summary)
    -> Result<BookGraph, String> {
  auto graph = BookGraph{};
  auto sources = rstd::collections::BTreeSet<String>::make();
  auto outputs = rstd::collections::BTreeSet<String>::make();
  auto parents = Vec<usize>::make();
  for (auto &entry : summary.entries) {
    if (sources.contains(entry.source.as_str()))
      return Err(graph_error(
          entry.span,
          rstd::format("summary repeats page '{}'", entry.source).as_str()));
    auto output = page_output(entry.source.as_str());
    if (outputs.contains(output.as_str()))
      return Err(graph_error(entry.span,
                             rstd::format("page '{}' conflicts at output '{}'",
                                          entry.source, output)
                                 .as_str()));
    auto requested = project.source_root.join(
        rstd::path::PathBuf::from(entry.source.as_str()).as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err())
      return Err(graph_error(entry.span,
                             rstd::format("cannot resolve page '{}': {}",
                                          requested.as_path(),
                                          rstd::move(canonical).unwrap_err())
                                 .as_str()));
    if (canonical->as_path()
            .strip_prefix(project.source_root.as_path())
            .is_none())
      return Err(
          graph_error(entry.span, "summary page escapes the source root"_str));
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err() || !metadata->is_file())
      return Err(
          graph_error(entry.span, "summary page must be a regular file"_str));
    while (parents.len() > entry.depth)
      parents.pop();
    if (entry.depth > parents.len())
      return Err(graph_error(
          entry.span, "summary page has no parent at the previous level"_str));
    auto parent = entry.depth == usize{}
                      ? Option<usize>{}
                      : Some(parents[entry.depth - usize(1)]);
    auto identity = rstd::crypto::sha256_hex(
        rstd::format("lito-book-page-v1\n{}\n{}", project.identity.as_str(),
                     entry.source.as_str())
            .as_str());
    auto index = graph.pages.len();
    graph.pages.push(BookPage{
        .identity = rstd::move(identity),
        .title = rstd::move(entry.title),
        .source = entry.source.clone(),
        .source_path = rstd::move(canonical).unwrap(),
        .output = output.clone(),
        .url = rstd::move(output),
        .parent = parent,
        .span =
            SourceSpan{
                .path = entry.span.path.clone(),
                .line = entry.span.line,
                .column = entry.span.column,
            },
    });
    if (parent.is_some())
      graph.pages[*parent].children.emplace_back(index);
    else
      graph.roots.emplace_back(index);
    if (parents.len() == entry.depth)
      parents.emplace_back(index);
    else
      parents[entry.depth] = index;
    sources.insert(entry.source.clone());
    outputs.insert(graph.pages[index].output.clone());
  }
  for (auto index = usize{}; index < graph.pages.len(); ++index) {
    if (index != usize{})
      graph.pages[index].previous = Some(index - usize(1));
    if (index + usize(1) < graph.pages.len())
      graph.pages[index].next = Some(index + usize(1));
    auto chain = Vec<usize>::make();
    auto cursor = Some(index);
    while (cursor.is_some()) {
      chain.emplace_back(*cursor);
      cursor = graph.pages[*cursor].parent;
    }
    while (!chain.is_empty())
      graph.pages[index].breadcrumb.push(chain.pop().unwrap());
  }
  return Ok(rstd::move(graph));
}

} // namespace lito::book
