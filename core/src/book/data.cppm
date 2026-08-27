module;
#include <rstd/macro.hpp>

export module lito.book:data;

import rstd;
import rstd.json;
import licrypto;
import lito.site;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;
using BookJson = rstd::json::Value;
using BookJsonMap = rstd::json::Map;
using BookJsonArray = rstd::json::Array;

namespace lito::book {

auto book_json_string(ref<str> value) -> BookJson {
  return BookJson::String(String::make(value));
}

auto book_json_usize(usize value) -> BookJson {
  return BookJson::Number(rstd::json::Number::from_u64(
      u64(static_cast<uint64_t>(value.to_primitive()))));
}

auto book_json_text(BookJson value) -> String {
  auto result = rstd::json::to_string(
      value, rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  result.push_ascii('\n');
  return result;
}

auto optional_json(const Option<String> &value) -> BookJson {
  return value.is_some() ? book_json_string(value->as_str()) : BookJson::Null();
}

auto page_json(const BookDatasetPage &page) -> BookJson {
  auto root = BookJsonMap::make();
  root.insert(String::make("format"_str),
              book_json_string("lito-book-page"_str));
  root.insert(String::make("version"_str), book_json_usize(usize(1)));
  root.insert(String::make("identity"_str),
              book_json_string(page.identity.as_str()));
  root.insert(String::make("title"_str), book_json_string(page.title.as_str()));
  root.insert(String::make("source"_str),
              book_json_string(page.source.as_str()));
  root.insert(String::make("output"_str),
              book_json_string(page.output.as_str()));
  root.insert(String::make("url"_str), book_json_string(page.url.as_str()));
  root.insert(String::make("parent"_str), optional_json(page.parent));
  root.insert(String::make("previous"_str), optional_json(page.previous));
  root.insert(String::make("next"_str), optional_json(page.next));
  auto children = BookJsonArray::make();
  for (const auto &child : page.children)
    children.push(book_json_string(child.as_str()));
  root.insert(String::make("children"_str),
              BookJson::Array(rstd::move(children)));
  auto breadcrumb = BookJsonArray::make();
  for (const auto &item : page.breadcrumb)
    breadcrumb.push(book_json_string(item.as_str()));
  root.insert(String::make("breadcrumb"_str),
              BookJson::Array(rstd::move(breadcrumb)));
  auto headings = BookJsonArray::make();
  for (const auto &heading : page.headings) {
    auto item = BookJsonMap::make();
    item.insert(String::make("level"_str), book_json_usize(heading.level));
    item.insert(String::make("text"_str),
                book_json_string(heading.text.as_str()));
    item.insert(String::make("anchor"_str),
                book_json_string(heading.anchor.as_str()));
    headings.push(BookJson::Object(rstd::move(item)));
  }
  root.insert(String::make("headings"_str),
              BookJson::Array(rstd::move(headings)));
  root.insert(String::make("html"_str), book_json_string(page.html.as_str()));
  return BookJson::Object(rstd::move(root));
}

auto write_book_data_file(ref<rstd::path::Path> root, ref<str> relative,
                          ref<str> contents)
    -> Result<rstd::path::PathBuf, String> {
  if (!safe_frontend_path(relative))
    return Err(rstd::format("invalid Book data path '{}'", relative));
  auto path = rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path());
  auto parent = path.as_path().parent();
  if (parent.is_none())
    return Err(rstd::format("Book data '{}' has no parent", path.as_path()));
  auto created = rstd::fs::create_dir_all(*parent);
  if (created.is_err())
    return Err(rstd::format("cannot create Book data directory '{}': {}",
                            *parent, rstd::move(created).unwrap_err()));
  auto written = rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
  if (written.is_err())
    return Err(rstd::format("cannot write Book data '{}': {}", path.as_path(),
                            rstd::move(written).unwrap_err()));
  return Ok(rstd::move(path));
}

auto identity_index(const BookDataset &dataset)
    -> rstd::collections::BTreeSet<String> {
  auto result = rstd::collections::BTreeSet<String>::make();
  for (const auto &page : dataset.pages)
    result.insert(page.identity.clone());
  return result;
}

auto validate_reference(const rstd::collections::BTreeSet<String> &identities,
                        ref<str> value, ref<str> context)
    -> Result<empty, String> {
  if (!identities.contains(value))
    return Err(rstd::format("Book dataset {} references unknown page '{}'",
                            context, value));
  return Ok(empty{});
}

auto book_data_member(const BookJson &value, ref<str> name, ref<str> context)
    -> Result<ref<BookJson>, String> {
  auto member = value.get(name);
  if (member.is_none())
    return Err(rstd::format("{} is missing '{}'", context, name));
  return Ok(*member);
}

auto book_data_string(const BookJson &value, ref<str> name, ref<str> context)
    -> Result<String, String> {
  auto member = rstd_try(book_data_member(value, name, context));
  auto text = member->as_str();
  if (text.is_none())
    return Err(rstd::format("{}.{} must be a string", context, name));
  return Ok(String::make(*text));
}

auto book_data_optional_string(const BookJson &value, ref<str> name,
                               ref<str> context)
    -> Result<Option<String>, String> {
  auto member = rstd_try(book_data_member(value, name, context));
  if (member->is_null())
    return Ok(Option<String>{});
  auto text = member->as_str();
  if (text.is_none())
    return Err(rstd::format("{}.{} must be a string or null", context, name));
  return Ok(Some(String::make(*text)));
}

auto book_data_usize(const BookJson &value, ref<str> name, ref<str> context)
    -> Result<usize, String> {
  auto member = rstd_try(book_data_member(value, name, context));
  auto number = member->as_u64();
  if (number.is_none() || *number > u64(usize::MAX.to_primitive()))
    return Err(
        rstd::format("{}.{} must be an unsigned integer", context, name));
  return Ok(usize(static_cast<size_t>(number->to_primitive())));
}

auto book_data_array(const BookJson &value, ref<str> name, ref<str> context)
    -> Result<ref<BookJsonArray>, String> {
  auto member = rstd_try(book_data_member(value, name, context));
  auto array = member->as_array();
  if (array.is_none())
    return Err(rstd::format("{}.{} must be an array", context, name));
  return Ok(*array);
}

auto book_data_strings(const BookJson &value, ref<str> name, ref<str> context)
    -> Result<Vec<String>, String> {
  auto values = rstd_try(book_data_array(value, name, context));
  auto result = Vec<String>::with_capacity(values->len());
  for (const auto &value : *values) {
    auto text = value.as_str();
    if (text.is_none())
      return Err(rstd::format("{}.{} must contain strings", context, name));
    result.push(String::make(*text));
  }
  return Ok(rstd::move(result));
}

auto expect_book_data_header(const BookJson &value, ref<str> format,
                             ref<str> context) -> Result<empty, String> {
  auto actual_format = rstd_try(book_data_string(value, "format"_str, context));
  auto actual_version =
      rstd_try(book_data_usize(value, "version"_str, context));
  if (actual_format.as_str() != format)
    return Err(rstd::format("{} has format '{}', expected '{}'", context,
                            actual_format.as_str(), format));
  if (actual_version != usize(1))
    return Err(
        rstd::format("{} has unsupported version {}", context, actual_version));
  return Ok(empty{});
}

auto parse_book_data(ref<str> contents, ref<str> context)
    -> Result<BookJson, String> {
  auto parsed = rstd::json::from_str(contents);
  if (parsed.is_err())
    return Err(
        rstd::format("invalid {} JSON: {}", context, parsed.unwrap_err()));
  if (!parsed->is_object())
    return Err(rstd::format("{} must be a JSON object", context));
  return Ok(rstd::move(parsed).unwrap());
}

auto read_book_data(ref<rstd::path::Path> root, ref<str> relative)
    -> Result<String, String> {
  if (!safe_frontend_path(relative))
    return Err(rstd::format("invalid Book data path '{}'", relative));
  auto path = rstd::path::PathBuf::from(root).join(
      rstd::path::PathBuf::from(relative).as_path());
  auto contents = rstd::fs::read_to_string(path.as_path());
  if (contents.is_err())
    return Err(rstd::format("cannot read Book data '{}': {}", path.as_path(),
                            rstd::move(contents).unwrap_err()));
  return Ok(rstd::move(contents).unwrap());
}

auto decode_book_page(const BookJson &value)
    -> Result<BookDatasetPage, String> {
  rstd_try(expect_book_data_header(value, "lito-book-page"_str,
                                   "Book page data"_str));
  auto page = BookDatasetPage{
      .identity = rstd_try(
          book_data_string(value, "identity"_str, "Book page data"_str)),
      .title =
          rstd_try(book_data_string(value, "title"_str, "Book page data"_str)),
      .source =
          rstd_try(book_data_string(value, "source"_str, "Book page data"_str)),
      .output =
          rstd_try(book_data_string(value, "output"_str, "Book page data"_str)),
      .url = rstd_try(book_data_string(value, "url"_str, "Book page data"_str)),
      .parent = rstd_try(
          book_data_optional_string(value, "parent"_str, "Book page data"_str)),
      .children = rstd_try(
          book_data_strings(value, "children"_str, "Book page data"_str)),
      .previous = rstd_try(book_data_optional_string(value, "previous"_str,
                                                     "Book page data"_str)),
      .next = rstd_try(
          book_data_optional_string(value, "next"_str, "Book page data"_str)),
      .breadcrumb = rstd_try(
          book_data_strings(value, "breadcrumb"_str, "Book page data"_str)),
      .html =
          rstd_try(book_data_string(value, "html"_str, "Book page data"_str)),
  };
  auto headings =
      rstd_try(book_data_array(value, "headings"_str, "Book page data"_str));
  for (const auto &heading : *headings) {
    page.headings.push(MarkdownHeading{
        .level =
            rstd_try(book_data_usize(heading, "level"_str, "Book heading"_str)),
        .text =
            rstd_try(book_data_string(heading, "text"_str, "Book heading"_str)),
        .anchor = rstd_try(
            book_data_string(heading, "anchor"_str, "Book heading"_str)),
    });
  }
  return Ok(rstd::move(page));
}

} // namespace lito::book

export namespace lito::book {

auto validate_book_dataset(const BookDataset &dataset) -> Result<empty, String>;

auto make_book_dataset(const BookProject &project, const BookGraph &graph,
                       const BookContent &content)
    -> Result<BookDataset, String> {
  if (graph.pages.len() != content.pages.len())
    return Err(String::make("Book content does not match the Book graph"_str));
  auto dataset = BookDataset{
      .identity = project.identity.clone(),
      .title = project.title.clone(),
      .version = project.version.is_some() ? Some(project.version->clone())
                                           : Option<String>{},
      .language = project.language.is_some() ? Some(project.language->clone())
                                             : Option<String>{},
  };
  for (auto root : graph.roots)
    dataset.roots.push(graph.pages[root].identity.clone());
  for (auto index = usize{}; index < graph.pages.len(); ++index) {
    const auto &page = graph.pages[index];
    const auto &rendered = content.pages[index];
    if (rendered.page != index)
      return Err(String::make(
          "Book content page order does not match the Book graph"_str));
    auto value = BookDatasetPage{
        .identity = page.identity.clone(),
        .title = page.title.clone(),
        .source = page.source.clone(),
        .output = page.output.clone(),
        .url = page.url.clone(),
        .parent = page.parent.is_some()
                      ? Some(graph.pages[*page.parent].identity.clone())
                      : Option<String>{},
        .previous = page.previous.is_some()
                        ? Some(graph.pages[*page.previous].identity.clone())
                        : Option<String>{},
        .next = page.next.is_some()
                    ? Some(graph.pages[*page.next].identity.clone())
                    : Option<String>{},
        .html = rendered.html.clone(),
    };
    for (auto child : page.children)
      value.children.push(graph.pages[child].identity.clone());
    for (auto item : page.breadcrumb)
      value.breadcrumb.push(graph.pages[item].identity.clone());
    for (const auto &heading : rendered.document.headings) {
      value.headings.push(MarkdownHeading{
          .level = heading.level,
          .text = heading.text.clone(),
          .anchor = heading.anchor.clone(),
          .span =
              SourceSpan{
                  .path = heading.span.path.clone(),
                  .line = heading.span.line,
                  .column = heading.span.column,
              },
      });
    }
    dataset.pages.push(rstd::move(value));
  }
  rstd_try(validate_book_dataset(dataset));
  return Ok(rstd::move(dataset));
}

auto validate_book_dataset(const BookDataset &dataset)
    -> Result<empty, String> {
  if (dataset.identity.is_empty() || dataset.title.is_empty() ||
      dataset.pages.is_empty())
    return Err(String::make(
        "Book dataset identity, title, and pages are required"_str));
  auto identities = identity_index(dataset);
  if (identities.len() != dataset.pages.len())
    return Err(String::make("Book dataset repeats a page identity"_str));
  auto outputs = rstd::collections::BTreeSet<String>::make();
  for (const auto &root : dataset.roots)
    rstd_try(validate_reference(identities, root.as_str(), "root"_str));
  for (const auto &page : dataset.pages) {
    if (page.title.is_empty() || page.source.is_empty() ||
        page.output.is_empty() || page.url.is_empty())
      return Err(
          String::make("Book dataset page fields must not be empty"_str));
    if (!safe_frontend_path(page.output.as_str()))
      return Err(
          rstd::format("invalid Book page output '{}'", page.output.as_str()));
    if (!outputs.insert(page.output.clone()))
      return Err(rstd::format("duplicate Book page output '{}'",
                              page.output.as_str()));
    if (page.parent.is_some())
      rstd_try(
          validate_reference(identities, page.parent->as_str(), "parent"_str));
    if (page.previous.is_some())
      rstd_try(validate_reference(identities, page.previous->as_str(),
                                  "previous"_str));
    if (page.next.is_some())
      rstd_try(validate_reference(identities, page.next->as_str(), "next"_str));
    for (const auto &child : page.children)
      rstd_try(validate_reference(identities, child.as_str(), "child"_str));
    for (const auto &item : page.breadcrumb)
      rstd_try(validate_reference(identities, item.as_str(), "breadcrumb"_str));
  }
  return Ok(empty{});
}

auto load_book_dataset(ref<rstd::path::Path> root)
    -> Result<BookDataset, String> {
  auto manifest_text = rstd_try(read_book_data(root, "data/book.json"_str));
  auto manifest =
      rstd_try(parse_book_data(manifest_text.as_str(), "Book manifest"_str));
  rstd_try(expect_book_data_header(manifest, "lito-book-data"_str,
                                   "Book manifest"_str));
  auto book =
      rstd_try(book_data_member(manifest, "book"_str, "Book manifest"_str));
  auto dataset = BookDataset{
      .identity = rstd_try(
          book_data_string(*book, "identity"_str, "Book manifest book"_str)),
      .title = rstd_try(
          book_data_string(*book, "title"_str, "Book manifest book"_str)),
      .version = rstd_try(book_data_optional_string(*book, "version"_str,
                                                    "Book manifest book"_str)),
      .language = rstd_try(book_data_optional_string(*book, "language"_str,
                                                     "Book manifest book"_str)),
      .roots = rstd_try(
          book_data_strings(*book, "roots"_str, "Book manifest book"_str)),
  };
  auto descriptors =
      rstd_try(book_data_array(manifest, "pages"_str, "Book manifest"_str));
  auto data_paths = rstd::collections::BTreeSet<String>::make();
  for (const auto &descriptor : *descriptors) {
    auto identity = rstd_try(book_data_string(descriptor, "identity"_str,
                                              "Book page descriptor"_str));
    auto title = rstd_try(
        book_data_string(descriptor, "title"_str, "Book page descriptor"_str));
    auto url = rstd_try(
        book_data_string(descriptor, "url"_str, "Book page descriptor"_str));
    auto data = rstd_try(
        book_data_string(descriptor, "data"_str, "Book page descriptor"_str));
    auto digest = rstd_try(
        book_data_string(descriptor, "digest"_str, "Book page descriptor"_str));
    if (!safe_frontend_path(data.as_str()))
      return Err(
          rstd::format("invalid Book page data path '{}'", data.as_str()));
    if (!data_paths.insert(data.clone()))
      return Err(
          rstd::format("duplicate Book page data path '{}'", data.as_str()));
    auto page_text = rstd_try(read_book_data(root, data.as_str()));
    if (licrypto::sha256_hex(page_text.as_str()).as_str() != digest.as_str())
      return Err(
          rstd::format("Book page '{}' digest mismatch", identity.as_str()));
    auto document =
        rstd_try(parse_book_data(page_text.as_str(), "Book page data"_str));
    auto page = rstd_try(decode_book_page(document));
    if (page.identity.as_str() != identity.as_str() ||
        page.title.as_str() != title.as_str() ||
        page.url.as_str() != url.as_str())
      return Err(rstd::format("Book page '{}' descriptor mismatch",
                              identity.as_str()));
    dataset.pages.push(rstd::move(page));
  }
  rstd_try(validate_book_dataset(dataset));
  return Ok(rstd::move(dataset));
}

auto publish_book_dataset(ref<rstd::path::Path> root,
                          const BookDataset &dataset)
    -> Result<BookDataSummary, String> {
  rstd_try(validate_book_dataset(dataset));
  auto summary = BookDataSummary{
      .root = rstd::path::PathBuf::from(root),
  };
  auto pages = BookJsonArray::make();
  for (const auto &page : dataset.pages) {
    auto text = book_json_text(page_json(page));
    auto relative = rstd::format("data/pages/{}.json", page.identity.as_str());
    auto path =
        rstd_try(write_book_data_file(root, relative.as_str(), text.as_str()));
    auto entry = BookJsonMap::make();
    entry.insert(String::make("identity"_str),
                 book_json_string(page.identity.as_str()));
    entry.insert(String::make("title"_str),
                 book_json_string(page.title.as_str()));
    entry.insert(String::make("url"_str), book_json_string(page.url.as_str()));
    entry.insert(String::make("data"_str), book_json_string(relative.as_str()));
    entry.insert(
        String::make("digest"_str),
        book_json_string(licrypto::sha256_hex(text.as_str()).as_str()));
    pages.push(BookJson::Object(rstd::move(entry)));
    summary.pages.push(BookDataPageSummary{
        .identity = page.identity.clone(),
        .json = rstd::move(path),
    });
  }
  auto manifest = BookJsonMap::make();
  manifest.insert(String::make("format"_str),
                  book_json_string("lito-book-data"_str));
  manifest.insert(String::make("version"_str), book_json_usize(usize(1)));
  auto book = BookJsonMap::make();
  book.insert(String::make("identity"_str),
              book_json_string(dataset.identity.as_str()));
  book.insert(String::make("title"_str),
              book_json_string(dataset.title.as_str()));
  book.insert(String::make("version"_str), optional_json(dataset.version));
  book.insert(String::make("language"_str), optional_json(dataset.language));
  auto roots = BookJsonArray::make();
  for (const auto &root_page : dataset.roots)
    roots.push(book_json_string(root_page.as_str()));
  book.insert(String::make("roots"_str), BookJson::Array(rstd::move(roots)));
  manifest.insert(String::make("book"_str), BookJson::Object(rstd::move(book)));
  manifest.insert(String::make("pages"_str),
                  BookJson::Array(rstd::move(pages)));
  auto text = book_json_text(BookJson::Object(rstd::move(manifest)));
  summary.digest = licrypto::sha256_hex(text.as_str());
  summary.manifest =
      rstd_try(write_book_data_file(root, "data/book.json"_str, text.as_str()));
  auto validated = rstd_try(load_book_dataset(root));
  if (validated.identity.as_str() != dataset.identity.as_str() ||
      validated.pages.len() != dataset.pages.len())
    return Err(String::make("published Book dataset does not round-trip"_str));
  return Ok(rstd::move(summary));
}

} // namespace lito::book
