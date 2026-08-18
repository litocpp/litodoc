module;
#include <rstd/macro.hpp>

export module lito.book:render;

import rstd;
import lito.site;
import :model;
import :data;
import :markdown;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book {

auto book_template_text(ref<str> value) -> TemplateValue {
  return TemplateValue::text_value(value);
}

auto book_template_number(usize value) -> TemplateValue {
  return TemplateValue::text_value(rstd::format("{}", value));
}

auto book_asset_prefix(ref<str> output) -> String {
  auto result = String::make();
  for (auto character : output) {
    if (character == u8('/'))
      result.push_str("../"_str);
  }
  return result;
}

auto book_write_text(ref<rstd::path::Path> root, ref<str> relative,
                     ref<str> contents) -> Result<empty, String> {
  if (!safe_frontend_path(relative))
    return Err(rstd::format("invalid Book output path '{}'", relative));
  auto path = rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path());
  auto parent = path.as_path().parent();
  if (parent.is_none())
    return Err(rstd::format("Book output '{}' has no parent", path.as_path()));
  auto created = rstd::fs::create_dir_all(*parent);
  if (created.is_err())
    return Err(rstd::format("cannot create Book output directory '{}': {}",
                            *parent, rstd::move(created).unwrap_err()));
  auto written = rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
  if (written.is_err())
    return Err(rstd::format("cannot write Book output '{}': {}", path.as_path(),
                            rstd::move(written).unwrap_err()));
  return Ok(empty{});
}

auto book_copy_file(ref<rstd::path::Path> root, const BookAsset &asset)
    -> Result<empty, String> {
  if (!safe_frontend_path(asset.output.as_str()))
    return Err(rstd::format("invalid Book asset path '{}'", asset.output));
  auto contents = rstd::fs::read(asset.source.as_path());
  if (contents.is_err())
    return Err(rstd::format("cannot read Book asset '{}': {}",
                            asset.source.as_path(),
                            rstd::move(contents).unwrap_err()));
  auto output = rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(asset.output.as_str()).as_path());
  auto parent = output.as_path().parent().unwrap();
  auto created = rstd::fs::create_dir_all(parent);
  if (created.is_err())
    return Err(rstd::format("cannot create Book asset directory '{}': {}",
                            parent, rstd::move(created).unwrap_err()));
  auto written = rstd::fs::write_atomic(output.as_path(), contents->as_slice());
  if (written.is_err())
    return Err(rstd::format("cannot write Book asset '{}': {}",
                            output.as_path(),
                            rstd::move(written).unwrap_err()));
  return Ok(empty{});
}

auto book_page_link(const BookGraph &graph, usize target,
                    ref<str> current_output, ref<str> label) -> TemplateValue {
  auto value = TemplateValue::object_value();
  value.insert("label"_str, book_template_text(label));
  value.insert("title"_str,
               book_template_text(graph.pages[target].title.as_str()));
  value.insert("href"_str,
               TemplateValue::text_value(relative_url(
                   current_output, graph.pages[target].output.as_str())));
  return value;
}

auto book_site_value(const BookProject &project, usize page_count)
    -> TemplateValue {
  auto site = TemplateValue::object_value();
  site.insert("title"_str, book_template_text(project.title.as_str()));
  site.insert("page_count"_str, book_template_number(page_count));
  site.insert("has_version"_str,
              TemplateValue::boolean_value(project.version.is_some()));
  site.insert("version"_str, book_template_text(project.version.is_some()
                                                    ? project.version->as_str()
                                                    : ref<str>{}));
  site.insert("has_language"_str,
              TemplateValue::boolean_value(project.language.is_some()));
  site.insert("language"_str,
              book_template_text(project.language.is_some()
                                     ? project.language->as_str()
                                     : "en"_str));
  return site;
}

auto book_navigation(const BookGraph &graph, ref<str> current_output)
    -> TemplateValue {
  auto navigation = TemplateValue::object_value();
  navigation.insert("show_modules"_str, TemplateValue::boolean_value(false));
  navigation.insert("modules"_str, TemplateValue::array_value());
  navigation.insert("has_pages"_str,
                    TemplateValue::boolean_value(!graph.pages.is_empty()));
  auto pages = TemplateValue::array_value();
  for (const auto &page : graph.pages) {
    auto item = TemplateValue::object_value();
    item.insert("title"_str, book_template_text(page.title.as_str()));
    item.insert("href"_str, TemplateValue::text_value(relative_url(
                                current_output, page.output.as_str())));
    item.insert("depth"_str,
                book_template_number(page.breadcrumb.len() - usize(1)));
    item.insert("is_root"_str,
                TemplateValue::boolean_value(page.parent.is_none()));
    pages.array.push(rstd::move(item));
  }
  navigation.insert("pages"_str, rstd::move(pages));
  return navigation;
}

auto book_page_context(const BookProject &project, const BookGraph &graph,
                       const BookDatasetPage *page, usize page_index,
                       ref<str> output, ref<str> digest) -> TemplateValue {
  auto context = TemplateValue::object_value();
  context.insert("site"_str, book_site_value(project, graph.pages.len()));
  auto page_value = TemplateValue::object_value();
  auto title = page == nullptr ? project.title.as_str() : page->title.as_str();
  page_value.insert("title"_str, book_template_text(title));
  page_value.insert("kind"_str, book_template_text("book"_str));
  page_value.insert("asset_prefix"_str,
                    TemplateValue::text_value(book_asset_prefix(output)));
  page_value.insert("search_package"_str, book_template_text(ref<str>{}));
  page_value.insert("search_module"_str, book_template_text(ref<str>{}));
  page_value.insert("is_api"_str, TemplateValue::boolean_value(false));
  page_value.insert("is_book"_str, TemplateValue::boolean_value(true));
  auto outline = TemplateValue::array_value();
  if (page != nullptr) {
    for (const auto &heading : page->headings) {
      auto item = TemplateValue::object_value();
      item.insert("href"_str, TemplateValue::text_value(rstd::format(
                                  "#{}", heading.anchor.as_str())));
      item.insert("label"_str, book_template_text(heading.text.as_str()));
      outline.array.push(rstd::move(item));
    }
  }
  page_value.insert("has_outline"_str,
                    TemplateValue::boolean_value(!outline.array.is_empty()));
  page_value.insert("outline"_str, rstd::move(outline));
  context.insert("page"_str, rstd::move(page_value));
  context.insert("navigation"_str, book_navigation(graph, output));
  auto book = TemplateValue::object_value();
  book.insert("data_digest"_str, book_template_text(digest));
  book.insert("has_page"_str, TemplateValue::boolean_value(page != nullptr));
  book.insert("title"_str, book_template_text(title));
  book.insert("content"_str,
              TemplateValue::trusted_html(
                  page == nullptr ? String::make() : page->html.clone()));
  auto breadcrumbs = TemplateValue::array_value();
  if (page != nullptr) {
    for (auto item : graph.pages[page_index].breadcrumb) {
      breadcrumbs.array.push(book_page_link(graph, item, output,
                                            graph.pages[item].title.as_str()));
    }
  }
  book.insert("has_breadcrumbs"_str,
              TemplateValue::boolean_value(breadcrumbs.array.len() > usize(1)));
  book.insert("breadcrumbs"_str, rstd::move(breadcrumbs));
  auto roots = TemplateValue::array_value();
  for (auto root : graph.roots)
    roots.array.push(
        book_page_link(graph, root, output, graph.pages[root].title.as_str()));
  book.insert("roots"_str, rstd::move(roots));
  auto has_previous =
      page != nullptr && graph.pages[page_index].previous.is_some();
  auto has_next = page != nullptr && graph.pages[page_index].next.is_some();
  book.insert("has_previous"_str, TemplateValue::boolean_value(has_previous));
  book.insert("has_next"_str, TemplateValue::boolean_value(has_next));
  book.insert("previous"_str,
              has_previous
                  ? book_page_link(graph, *graph.pages[page_index].previous,
                                   output, "Previous"_str)
                  : TemplateValue::object_value());
  book.insert("next"_str,
              has_next ? book_page_link(graph, *graph.pages[page_index].next,
                                        output, "Next"_str)
                       : TemplateValue::object_value());
  context.insert("book"_str, rstd::move(book));
  return context;
}

auto final_book_data_summary(ref<rstd::path::Path> output,
                             const BookDataset &dataset, ref<str> digest)
    -> BookDataSummary {
  auto data = BookDataSummary{
      .root = rstd::path::PathBuf::from(output),
      .manifest = rstd::path::PathBuf::from(output).join(
          rstd::path::PathBuf::from("data/book.json"_str).as_path()),
      .digest = String::make(digest),
  };
  for (const auto &page : dataset.pages) {
    data.pages.push(BookDataPageSummary{
        .identity = page.identity.clone(),
        .json = rstd::path::PathBuf::from(output).join(
            rstd::path::PathBuf::from(
                rstd::format("data/pages/{}.json", page.identity.as_str()))
                .as_path()),
    });
  }
  return data;
}

} // namespace lito::book

export namespace lito::book {

auto publish_book_site(const BookProject &project, const BookGraph &graph,
                       const BookContent &content, const BookDataset &dataset,
                       const FrontendBundle &frontend)
    -> Result<BookSummary, String> {
  if (!frontend.supports_book)
    return Err(rstd::format("frontend '{}' does not support Book documentation",
                            frontend.identity.as_str()));
  auto publication =
      begin_publication(project.output.as_path(), "book-site"_str);
  if (publication.is_err())
    return Err(rstd::move(publication).unwrap_err());
  auto fail = [&publication](String error) -> Result<BookSummary, String> {
    (void)abort_publication(*publication);
    return Err(rstd::move(error));
  };
  auto data = publish_book_dataset(publication->staging.as_path(), dataset);
  if (data.is_err())
    return fail(rstd::move(data).unwrap_err());
  for (const auto &asset : frontend.assets) {
    auto written =
        book_write_text(publication->staging.as_path(), asset.path.as_str(),
                        asset.contents.as_str());
    if (written.is_err())
      return fail(rstd::move(written).unwrap_err());
  }
  for (const auto &asset : content.assets) {
    auto copied = book_copy_file(publication->staging.as_path(), asset);
    if (copied.is_err())
      return fail(rstd::move(copied).unwrap_err());
  }
  auto has_index = false;
  for (auto index = usize{}; index < dataset.pages.len(); ++index) {
    const auto &page = dataset.pages[index];
    auto root_page = page.output.as_str() == "index.html"_str;
    if (root_page)
      has_index = true;
    auto context =
        book_page_context(project, graph, rstd::addressof(page), index,
                          page.output.as_str(), data->digest.as_str());
    auto rendered =
        render_template(frontend.templates,
                        root_page ? frontend.book_root_template.as_str()
                                  : frontend.book_page_template.as_str(),
                        context);
    if (rendered.is_err())
      return fail(rstd::move(rendered).unwrap_err());
    auto written = book_write_text(publication->staging.as_path(),
                                   page.output.as_str(), rendered->as_str());
    if (written.is_err())
      return fail(rstd::move(written).unwrap_err());
  }
  if (!has_index) {
    auto context = book_page_context(project, graph, nullptr, usize{},
                                     "index.html"_str, data->digest.as_str());
    auto rendered = render_template(
        frontend.templates, frontend.book_root_template.as_str(), context);
    if (rendered.is_err())
      return fail(rstd::move(rendered).unwrap_err());
    auto written = book_write_text(publication->staging.as_path(),
                                   "index.html"_str, rendered->as_str());
    if (written.is_err())
      return fail(rstd::move(written).unwrap_err());
  }
  auto committed = commit_publication(*publication);
  if (committed.is_err())
    return Err(rstd::move(committed).unwrap_err());
  return Ok(BookSummary{
      .output = project.output.clone(),
      .index = project.output.join(
          rstd::path::PathBuf::from("index.html"_str).as_path()),
      .data = final_book_data_summary(project.output.as_path(), dataset,
                                      data->digest.as_str()),
      .pages = dataset.pages.len(),
      .site_generated = true,
  });
}

} // namespace lito::book
