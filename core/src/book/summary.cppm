module;
#include <rstd/macro.hpp>

export module lito.book:summary;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::book {

auto summary_error(ref<rstd::path::Path> path, usize line, usize column,
                   ref<str> message) -> String {
  return rstd::format("{}:{}:{}: {}", path, line, column, message);
}

auto summary_link(ref<str> body, ref<rstd::path::Path> path, usize line,
                  usize column) -> Result<rstd::tuple<String, String>, String> {
  if (!body.starts_with("["_str))
    return Err(summary_error(path, line, column,
                             "summary item must be a Markdown link"_str));
  auto label_end = Option<usize>{};
  for (auto index = usize(1); index < body.len(); ++index) {
    if (body[index] == u8(']')) {
      label_end = Some(index);
      break;
    }
  }
  if (label_end.is_none() || *label_end + usize(2) >= body.len() ||
      body[*label_end + usize(1)] != u8('(') ||
      body[body.len() - usize(1)] != u8(')')) {
    return Err(summary_error(path, line, column,
                             "summary item has an invalid link"_str));
  }
  auto title = body.get(usize(1), *label_end).unwrap().trim_ascii();
  auto target = body.get(*label_end + usize(2), body.len() - usize(1))
                    .unwrap()
                    .trim_ascii();
  if (title.is_empty() || target.is_empty())
    return Err(summary_error(path, line, column,
                             "summary link title and target are required"_str));
  if (target.contains("#"_str) || target.contains("?"_str) ||
      target.contains("\\"_str))
    return Err(
        summary_error(path, line, column,
                      "summary target must be a local Markdown path"_str));
  auto relative = rstd::path::PathBuf::from(target);
  if (!relative.as_path().is_safe_relative() || !target.ends_with(".md"_str))
    return Err(
        summary_error(path, line, column,
                      "summary target must be a safe relative .md path"_str));
  auto text = relative.as_path().to_str();
  if (text.is_none())
    return Err(summary_error(path, line, column,
                             "summary target is not valid UTF-8"_str));
  return Ok(
      rstd::tuple<String, String>{String::make(title), String::make(*text)});
}

auto parse_summary_item(ref<str> line, ref<rstd::path::Path> path,
                        usize line_number)
    -> Result<Option<SummaryEntry>, String> {
  if (line.trim_ascii().is_empty() || line.trim_ascii().starts_with("#"_str))
    return Ok(Option<SummaryEntry>{});
  auto spaces = usize{};
  while (spaces < line.len() && line[spaces] == u8(' '))
    ++spaces;
  if (spaces < line.len() && line[spaces] == u8('\t'))
    return Err(summary_error(path, line_number, spaces + usize(1),
                             "summary indentation must use spaces"_str));
  if (spaces % usize(2) != usize{})
    return Err(
        summary_error(path, line_number, usize(1),
                      "summary indentation must use two spaces per level"_str));
  auto marker_end = spaces;
  if (marker_end < line.len() &&
      (line[marker_end] == u8('-') || line[marker_end] == u8('*') ||
       line[marker_end] == u8('+'))) {
    ++marker_end;
  } else {
    while (marker_end < line.len() && line[marker_end] >= u8('0') &&
           line[marker_end] <= u8('9'))
      ++marker_end;
    if (marker_end == spaces || marker_end >= line.len() ||
        line[marker_end] != u8('.'))
      return Err(summary_error(
          path, line_number, spaces + usize(1),
          "summary content must be a list of Markdown links"_str));
    ++marker_end;
  }
  if (marker_end >= line.len() || line[marker_end] != u8(' '))
    return Err(
        summary_error(path, line_number, marker_end + usize(1),
                      "summary list marker must be followed by a space"_str));
  auto body = line.get(marker_end + usize(1), line.len()).unwrap().trim_ascii();
  auto parsed =
      rstd_try(summary_link(body, path, line_number, marker_end + usize(2)));
  return Ok(Some(SummaryEntry{
      .title = rstd::move(parsed.template get<0>()),
      .source = rstd::move(parsed.template get<1>()),
      .depth = spaces / usize(2),
      .span =
          SourceSpan{
              .path = rstd::path::PathBuf::from(path),
              .line = line_number,
              .column = spaces + usize(1),
          },
  }));
}

} // namespace lito::book

export namespace lito::book {

auto parse_summary(ref<rstd::path::Path> path)
    -> Result<SummaryDocument, String> {
  auto contents = rstd::fs::read_to_string(path);
  if (contents.is_err())
    return Err(rstd::format("cannot read Book summary '{}': {}", path,
                            rstd::move(contents).unwrap_err()));
  auto document = SummaryDocument{.path = rstd::path::PathBuf::from(path)};
  auto source = contents->as_str();
  auto begin = usize{};
  auto line = usize(1);
  while (begin <= contents->len()) {
    auto end = begin;
    while (end < source.len() && source[end] != u8('\n') &&
           source[end] != u8('\r'))
      ++end;
    auto entry =
        parse_summary_item(source.get(begin, end).unwrap(), path, line);
    if (entry.is_err())
      return Err(rstd::move(entry).unwrap_err());
    if (entry->is_some()) {
      if (document.entries.is_empty() && (**entry).depth != usize{})
        return Err(summary_error(path, (**entry).span.line,
                                 (**entry).span.column,
                                 "first summary item must be at the root"_str));
      if (!document.entries.is_empty() &&
          (**entry).depth >
              document.entries[document.entries.len() - usize(1)].depth +
                  usize(1))
        return Err(summary_error(path, (**entry).span.line,
                                 (**entry).span.column,
                                 "summary nesting skips a level"_str));
      document.entries.push(rstd::move(entry).unwrap().unwrap());
    }
    if (end == source.len())
      break;
    if (source[end] == u8('\r') && end + usize(1) < source.len() &&
        source[end + usize(1)] == u8('\n'))
      ++end;
    begin = end + usize(1);
    ++line;
  }
  if (document.entries.is_empty())
    return Err(
        summary_error(path, usize(1), usize(1), "summary has no pages"_str));
  return Ok(rstd::move(document));
}

} // namespace lito::book
