module;
#include <rstd/macro.hpp>

export module lito.book:markdown;

import rstd;
import licrypto;
import lito.site;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book {

auto markdown_span(ref<rstd::path::Path> path, usize line, usize column)
    -> SourceSpan {
  return SourceSpan{
      .path = rstd::path::PathBuf::from(path),
      .line = line,
      .column = column,
  };
}

auto markdown_error(const SourceSpan &span, ref<str> message) -> String {
  return rstd::format("{}:{}:{}: {}", span.path.as_path(), span.line,
                      span.column, message);
}

auto find_marker(ref<str> text, usize begin, ref<str> marker) -> Option<usize> {
  if (marker.is_empty() || marker.len() > text.len())
    return None();
  for (auto index = begin; index + marker.len() <= text.len(); ++index) {
    auto matches = true;
    for (auto offset = usize{}; offset < marker.len(); ++offset) {
      if (text[index + offset] != marker[offset]) {
        matches = false;
        break;
      }
    }
    if (matches)
      return Some(index);
  }
  return None();
}

auto push_inline_text(Vec<MarkdownInline> &output, ref<str> text,
                      ref<rstd::path::Path> path, usize line, usize column)
    -> void {
  if (text.is_empty())
    return;
  output.push(MarkdownInline{
      .kind = MarkdownInlineKind::Text,
      .text = String::make(text),
      .span = markdown_span(path, line, column),
  });
}

auto parse_inlines(ref<str> text, ref<rstd::path::Path> path, usize line,
                   usize column, Vec<MarkdownLink> &links)
    -> Result<Vec<MarkdownInline>, String> {
  auto output = Vec<MarkdownInline>::make();
  auto cursor = usize{};
  auto plain = usize{};
  while (cursor < text.len()) {
    auto marker = text[cursor];
    auto image = marker == u8('!') && cursor + usize(1) < text.len() &&
                 text[cursor + usize(1)] == u8('[');
    auto link = marker == u8('[');
    auto code = marker == u8('`');
    auto strong = marker == u8('*') && cursor + usize(1) < text.len() &&
                  text[cursor + usize(1)] == u8('*');
    auto emphasis = marker == u8('*') && !strong;
    if (!image && !link && !code && !strong && !emphasis) {
      ++cursor;
      continue;
    }
    push_inline_text(output, text.get(plain, cursor).unwrap(), path, line,
                     column + plain);
    if (code || strong || emphasis) {
      auto token = code ? "`"_str : (strong ? "**"_str : "*"_str);
      auto close = find_marker(text, cursor + token.len(), token);
      if (close.is_none()) {
        push_inline_text(output,
                         text.get(cursor, cursor + token.len()).unwrap(), path,
                         line, column + cursor);
        cursor += token.len();
        plain = cursor;
        continue;
      }
      output.push(MarkdownInline{
          .kind = code ? MarkdownInlineKind::Code
                       : (strong ? MarkdownInlineKind::Strong
                                 : MarkdownInlineKind::Emphasis),
          .text = String::make(text.get(cursor + token.len(), *close).unwrap()),
          .span = markdown_span(path, line, column + cursor),
      });
      cursor = *close + token.len();
      plain = cursor;
      continue;
    }
    auto label_begin = cursor + (image ? usize(2) : usize(1));
    auto label_end = find_marker(text, label_begin, "]("_str);
    if (label_end.is_none()) {
      push_inline_text(output, text.get(cursor, cursor + usize(1)).unwrap(),
                       path, line, column + cursor);
      ++cursor;
      plain = cursor;
      continue;
    }
    auto destination_end = find_marker(text, *label_end + usize(2), ")"_str);
    if (destination_end.is_none()) {
      push_inline_text(output, text.get(cursor, cursor + usize(1)).unwrap(),
                       path, line, column + cursor);
      ++cursor;
      plain = cursor;
      continue;
    }
    auto label = text.get(label_begin, *label_end).unwrap();
    auto destination =
        text.get(*label_end + usize(2), *destination_end).unwrap().trim_ascii();
    if (label.is_empty() || destination.is_empty())
      return Err(markdown_error(
          markdown_span(path, line, column + cursor),
          "Markdown link label and destination are required"_str));
    auto span = markdown_span(path, line, column + cursor);
    output.push(MarkdownInline{
        .kind = image ? MarkdownInlineKind::Image : MarkdownInlineKind::Link,
        .text = String::make(label),
        .destination = String::make(destination),
        .span =
            SourceSpan{
                .path = span.path.clone(),
                .line = span.line,
                .column = span.column,
            },
    });
    links.push(MarkdownLink{
        .destination = String::make(destination),
        .image = image,
        .span = rstd::move(span),
    });
    cursor = *destination_end + usize(1);
    plain = cursor;
  }
  push_inline_text(output, text.get(plain, text.len()).unwrap(), path, line,
                   column + plain);
  return Ok(rstd::move(output));
}

auto inline_plain_text(const Vec<MarkdownInline> &inlines) -> String {
  auto text = String::make();
  for (const auto &item : inlines)
    text.push_str(item.text.as_str());
  return text;
}

auto heading_slug(ref<str> text) -> String {
  auto result = String::make();
  auto separator = false;
  for (auto character : text) {
    const auto value = character.to_primitive();
    if (value >= 'A' && value <= 'Z') {
      if (separator && !result.is_empty())
        result.push_ascii('-');
      separator = false;
      result.push_ascii(static_cast<char>(value - 'A' + 'a'));
    } else if ((value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9')) {
      if (separator && !result.is_empty())
        result.push_ascii('-');
      separator = false;
      result.push_ascii(static_cast<char>(value));
    } else if (value == '-' || value == '_' || value == ' ' || value == '\t') {
      separator = !result.is_empty();
    }
  }
  if (result.is_empty()) {
    auto digest = licrypto::sha256_hex(text);
    result = rstd::format("section-{}",
                          digest.as_str().get(usize{}, usize(12)).unwrap());
  }
  return result;
}

auto list_item(ref<str> line) -> Option<rstd::tuple<usize, bool, usize>> {
  auto spaces = usize{};
  while (spaces < line.len() && line[spaces] == u8(' '))
    ++spaces;
  if (spaces % usize(2) != usize{})
    return None();
  auto marker = spaces;
  auto ordered = false;
  if (marker < line.len() &&
      (line[marker] == u8('-') || line[marker] == u8('*') ||
       line[marker] == u8('+'))) {
    ++marker;
  } else {
    ordered = true;
    while (marker < line.len() && line[marker] >= u8('0') &&
           line[marker] <= u8('9'))
      ++marker;
    if (marker == spaces || marker >= line.len() || line[marker] != u8('.'))
      return None();
    ++marker;
  }
  if (marker >= line.len() || line[marker] != u8(' '))
    return None();
  return Some(rstd::tuple<usize, bool, usize>{spaces / usize(2), ordered,
                                              marker + usize(1)});
}

auto list_continuation(ref<str> line, usize depth) -> Option<usize> {
  auto spaces = usize{};
  while (spaces < line.len() && line[spaces] == u8(' '))
    ++spaces;
  if (spaces < (depth + usize(1)) * usize(2) || spaces == line.len())
    return None();
  return Some(spaces);
}

auto heading_level(ref<str> line) -> usize {
  auto level = usize{};
  while (level < line.len() && level < usize(6) && line[level] == u8('#'))
    ++level;
  if (level == usize{} || level >= line.len() || line[level] != u8(' '))
    return usize{};
  return level;
}

auto starts_block(ref<str> line) -> bool {
  auto trimmed = line.trim_ascii();
  return trimmed.is_empty() || line.starts_with("```"_str) ||
         heading_level(line) != usize{} || list_item(line).is_some() ||
         line.starts_with("> "_str);
}

auto append_inline_html(String &output, const MarkdownInline &item) -> void {
  auto escaped = escape_html(item.text.as_str());
  switch (item.kind) {
  case MarkdownInlineKind::Text:
    output.push_str(escaped.as_str());
    break;
  case MarkdownInlineKind::Emphasis:
    output.push_str("<em>"_str);
    output.push_str(escaped.as_str());
    output.push_str("</em>"_str);
    break;
  case MarkdownInlineKind::Strong:
    output.push_str("<strong>"_str);
    output.push_str(escaped.as_str());
    output.push_str("</strong>"_str);
    break;
  case MarkdownInlineKind::Code:
    output.push_str("<code>"_str);
    output.push_str(escaped.as_str());
    output.push_str("</code>"_str);
    break;
  case MarkdownInlineKind::Link:
    output.push_str("<a href=\""_str);
    output.push_str(escape_html(item.resolved_destination.as_str()).as_str());
    output.push_str("\">"_str);
    output.push_str(escaped.as_str());
    output.push_str("</a>"_str);
    break;
  case MarkdownInlineKind::Image:
    output.push_str("<img src=\""_str);
    output.push_str(escape_html(item.resolved_destination.as_str()).as_str());
    output.push_str("\" alt=\""_str);
    output.push_str(escaped.as_str());
    output.push_str("\">"_str);
    break;
  }
}

auto append_inlines_html(String &output, const Vec<MarkdownInline> &inlines)
    -> void {
  for (const auto &item : inlines)
    append_inline_html(output, item);
}

auto close_lists(String &output, Vec<bool> &lists, bool &item_open,
                 usize keep = usize{}) -> void {
  while (lists.len() > keep) {
    if (item_open) {
      output.push_str("</li>"_str);
      item_open = false;
    }
    output.push_str(lists.pop().unwrap() ? "</ol>"_str : "</ul>"_str);
    if (!lists.is_empty()) {
      output.push_str("</li>"_str);
    }
  }
}

auto path_segments(ref<str> path, bool directory) -> Vec<String> {
  auto result = Vec<String>::make();
  auto begin = usize{};
  while (begin < path.len()) {
    auto end = begin;
    while (end < path.len() && path[end] != u8('/'))
      ++end;
    if (end > begin)
      result.push(String::make(path.get(begin, end).unwrap()));
    begin = end + usize(1);
  }
  if (directory && !result.is_empty())
    result.pop();
  return result;
}

auto relative_url(ref<str> from_output, ref<str> to_output) -> String {
  auto from = path_segments(from_output, true);
  auto to = path_segments(to_output, false);
  auto common = usize{};
  while (common < from.len() && common < to.len() &&
         from[common] == to[common].as_str())
    ++common;
  auto result = String::make();
  for (auto index = common; index < from.len(); ++index)
    result.push_str("../"_str);
  for (auto index = common; index < to.len(); ++index) {
    if (index != common)
      result.push_ascii('/');
    result.push_str(to[index].as_str());
  }
  return result.is_empty() ? String::make("index.html"_str)
                           : rstd::move(result);
}

auto split_destination(ref<str> destination)
    -> rstd::tuple<ref<str>, ref<str>> {
  for (auto index = usize{}; index < destination.len(); ++index) {
    if (destination[index] == u8('#')) {
      return {destination.get(usize{}, index).unwrap(),
              destination.get(index + usize(1), destination.len()).unwrap()};
    }
  }
  return {destination, ref<str>{}};
}

auto heading_exists(const MarkdownDocument &document, ref<str> anchor) -> bool {
  for (const auto &heading : document.headings) {
    if (heading.anchor == anchor)
      return true;
  }
  return false;
}

auto page_for_path(const BookGraph &graph, ref<rstd::path::Path> path)
    -> Option<usize> {
  for (auto index = usize{}; index < graph.pages.len(); ++index) {
    if (graph.pages[index].source_path.as_path() == path)
      return Some(index);
  }
  return None();
}

auto resolve_local_path(const BookProject &project, const BookPage &page,
                        ref<str> value, const SourceSpan &span)
    -> Result<rstd::path::PathBuf, String> {
  auto relative = rstd::path::PathBuf::from(value);
  if (relative.is_empty() || relative.as_path().is_absolute() ||
      relative.as_path().has_root())
    return Err(markdown_error(span, "local link must use a relative path"_str));
  auto parent = page.source_path.as_path().parent().unwrap();
  auto canonical = rstd::fs::canonicalize(
      rstd::path::PathBuf::from(parent).join(relative.as_path()).as_path());
  if (canonical.is_err())
    return Err(markdown_error(
        span, rstd::format("cannot resolve local link '{}': {}", value,
                           rstd::move(canonical).unwrap_err())
                  .as_str()));
  if (canonical->as_path()
          .strip_prefix(project.source_root.as_path())
          .is_none())
    return Err(
        markdown_error(span, "local link escapes the Book source root"_str));
  return Ok(rstd::move(canonical).unwrap());
}

auto dangerous_scheme(ref<str> value) -> bool {
  for (auto index = usize{}; index < value.len(); ++index) {
    if (value[index] == u8('/') || value[index] == u8('#'))
      return false;
    if (value[index] == u8(':'))
      return true;
  }
  return false;
}

} // namespace lito::book

export namespace lito::book {

auto parse_markdown(ref<rstd::path::Path> path, ref<str> source)
    -> Result<MarkdownDocument, String> {
  auto document = MarkdownDocument{.path = rstd::path::PathBuf::from(path)};
  auto slugs = rstd::collections::BTreeMap<String, usize>::make();
  auto begin = usize{};
  auto line = usize(1);
  auto code = Option<MarkdownBlock>{};
  auto paragraph = Option<MarkdownBlock>{};
  auto list_continuation_allowed = false;
  auto flush_paragraph = [&document, &paragraph]() {
    if (paragraph.is_some())
      document.blocks.push(paragraph.take().unwrap());
  };
  while (begin <= source.len()) {
    auto end = begin;
    while (end < source.len() && source[end] != u8('\n') &&
           source[end] != u8('\r'))
      ++end;
    auto current = source.get(begin, end).unwrap();
    if (code.is_some()) {
      if (current.starts_with("```"_str)) {
        document.blocks.push(code.take().unwrap());
        list_continuation_allowed = false;
      } else {
        if (!code->literal.is_empty())
          code->literal.push_ascii('\n');
        code->literal.push_str(current);
      }
    } else if (current.starts_with("```"_str)) {
      flush_paragraph();
      list_continuation_allowed = false;
      code = Some(MarkdownBlock{
          .kind = MarkdownBlockKind::Code,
          .language = String::make(
              current.get(usize(3), current.len()).unwrap().trim_ascii()),
          .span = markdown_span(path, line, usize(1)),
      });
    } else {
      auto heading = heading_level(current);
      auto list = list_item(current);
      auto continuation = Option<usize>{};
      if (list_continuation_allowed && !document.blocks.is_empty() &&
          document.blocks[document.blocks.len() - usize(1)].kind ==
              MarkdownBlockKind::ListItem) {
        continuation = list_continuation(
            current, document.blocks[document.blocks.len() - usize(1)].depth);
      }
      if (current.trim_ascii().is_empty()) {
        flush_paragraph();
        list_continuation_allowed = false;
      } else if (heading != usize{}) {
        flush_paragraph();
        list_continuation_allowed = false;
        auto text = current.get(heading + usize(1), current.len())
                        .unwrap()
                        .trim_ascii();
        auto inlines = rstd_try(parse_inlines(
            text, path, line, heading + usize(2), document.links));
        auto plain = inline_plain_text(inlines);
        auto slug = heading_slug(plain.as_str());
        auto count = slugs.get_mut(slug.as_str());
        if (count.is_some()) {
          ++(**count);
          slug = rstd::format("{}-{}", slug.as_str(), **count);
        } else {
          slugs.insert(slug.clone(), usize(1));
        }
        auto span = markdown_span(path, line, usize(1));
        document.headings.push(MarkdownHeading{
            .level = heading,
            .text = rstd::move(plain),
            .anchor = slug.clone(),
            .span =
                SourceSpan{
                    .path = span.path.clone(),
                    .line = span.line,
                    .column = span.column,
                },
        });
        document.blocks.push(MarkdownBlock{
            .kind = MarkdownBlockKind::Heading,
            .level = heading,
            .anchor = rstd::move(slug),
            .inlines = rstd::move(inlines),
            .span = rstd::move(span),
        });
      } else if (list.is_some()) {
        flush_paragraph();
        auto offset = list->template get<2>();
        auto inlines = rstd_try(
            parse_inlines(current.get(offset, current.len()).unwrap(), path,
                          line, offset + usize(1), document.links));
        document.blocks.push(MarkdownBlock{
            .kind = MarkdownBlockKind::ListItem,
            .depth = list->template get<0>(),
            .ordered = list->template get<1>(),
            .inlines = rstd::move(inlines),
            .span = markdown_span(path, line, usize(1)),
        });
        list_continuation_allowed = true;
      } else if (continuation.is_some()) {
        auto &previous = document.blocks[document.blocks.len() - usize(1)];
        auto text = current.get(*continuation, current.len()).unwrap();
        auto inlines = rstd_try(parse_inlines(
            text, path, line, *continuation + usize(1), document.links));
        push_inline_text(previous.inlines, " "_str, path, line,
                         *continuation + usize(1));
        for (auto &item : inlines)
          previous.inlines.push(rstd::move(item));
      } else if (current.starts_with("> "_str)) {
        flush_paragraph();
        list_continuation_allowed = false;
        auto inlines = rstd_try(
            parse_inlines(current.get(usize(2), current.len()).unwrap(), path,
                          line, usize(3), document.links));
        document.blocks.push(MarkdownBlock{
            .kind = MarkdownBlockKind::Blockquote,
            .inlines = rstd::move(inlines),
            .span = markdown_span(path, line, usize(1)),
        });
      } else {
        list_continuation_allowed = false;
        auto inlines = rstd_try(parse_inlines(current.trim_ascii(), path, line,
                                              usize(1), document.links));
        if (paragraph.is_none()) {
          paragraph = Some(MarkdownBlock{
              .kind = MarkdownBlockKind::Paragraph,
              .inlines = rstd::move(inlines),
              .span = markdown_span(path, line, usize(1)),
          });
        } else {
          push_inline_text(paragraph->inlines, " "_str, path, line, usize(1));
          for (auto &item : inlines)
            paragraph->inlines.push(rstd::move(item));
        }
      }
    }
    if (end == source.len())
      break;
    if (source[end] == u8('\r') && end + usize(1) < source.len() &&
        source[end + usize(1)] == u8('\n'))
      ++end;
    begin = end + usize(1);
    ++line;
  }
  flush_paragraph();
  if (code.is_some())
    return Err(
        markdown_error(code->span, "unterminated fenced code block"_str));
  return Ok(rstd::move(document));
}

auto render_markdown_document(const MarkdownDocument &document) -> String {
  auto output = String::make();
  auto lists = Vec<bool>::make();
  auto item_open = false;
  for (const auto &block : document.blocks) {
    if (block.kind != MarkdownBlockKind::ListItem)
      close_lists(output, lists, item_open);
    if (block.kind == MarkdownBlockKind::Heading) {
      output.push_str(
          rstd::format("<h{} id=\"{}\">", block.level, block.anchor).as_str());
      append_inlines_html(output, block.inlines);
      output.push_str(rstd::format("</h{}>", block.level).as_str());
    } else if (block.kind == MarkdownBlockKind::Paragraph) {
      output.push_str("<p>"_str);
      append_inlines_html(output, block.inlines);
      output.push_str("</p>"_str);
    } else if (block.kind == MarkdownBlockKind::Code) {
      output.push_str("<pre><code"_str);
      if (!block.language.is_empty()) {
        output.push_str(" class=\"language-"_str);
        output.push_str(escape_html(block.language.as_str()).as_str());
        output.push_ascii('"');
      }
      output.push_ascii('>');
      output.push_str(escape_html(block.literal.as_str()).as_str());
      output.push_str("</code></pre>"_str);
    } else if (block.kind == MarkdownBlockKind::Blockquote) {
      output.push_str("<blockquote><p>"_str);
      append_inlines_html(output, block.inlines);
      output.push_str("</p></blockquote>"_str);
    } else {
      auto desired = block.depth + usize(1);
      if (lists.len() > desired)
        close_lists(output, lists, item_open, desired);
      if (lists.len() == desired &&
          lists[desired - usize(1)] != block.ordered) {
        close_lists(output, lists, item_open, desired - usize(1));
      }
      if (lists.len() == desired && item_open) {
        output.push_str("</li>"_str);
        item_open = false;
      }
      while (lists.len() < desired) {
        output.push_str(block.ordered ? "<ol>"_str : "<ul>"_str);
        lists.emplace_back(block.ordered);
      }
      output.push_str("<li>"_str);
      append_inlines_html(output, block.inlines);
      item_open = true;
    }
  }
  close_lists(output, lists, item_open);
  return output;
}

auto resolve_book_content(const BookProject &project, const BookGraph &graph,
                          Vec<MarkdownDocument> documents)
    -> Result<BookContent, String> {
  if (documents.len() != graph.pages.len())
    return Err(
        String::make("Book page documents do not match the Book graph"_str));
  auto content = BookContent{};
  auto asset_outputs = rstd::collections::BTreeMap<String, String>::make();
  for (auto page_index = usize{}; page_index < documents.len(); ++page_index) {
    auto &document = documents[page_index];
    const auto &page = graph.pages[page_index];
    for (auto &block : document.blocks) {
      for (auto &item : block.inlines) {
        if (item.kind != MarkdownInlineKind::Link &&
            item.kind != MarkdownInlineKind::Image)
          continue;
        auto destination = item.destination.as_str();
        if (destination.starts_with("https://"_str) ||
            destination.starts_with("http://"_str)) {
          item.resolved_destination = item.destination.clone();
          continue;
        }
        if (dangerous_scheme(destination))
          return Err(markdown_error(item.span, "unsafe link scheme"_str));
        auto split = split_destination(destination);
        auto path = split.template get<0>();
        auto fragment = split.template get<1>();
        if (path.is_empty()) {
          if (fragment.is_empty() || !heading_exists(document, fragment))
            return Err(markdown_error(
                item.span,
                rstd::format("page has no heading '#{}'", fragment).as_str()));
          item.resolved_destination = rstd::format("#{}", fragment);
          continue;
        }
        auto local =
            rstd_try(resolve_local_path(project, page, path, item.span));
        if (item.kind == MarkdownInlineKind::Image) {
          if (!fragment.is_empty())
            return Err(markdown_error(
                item.span, "local image must not use a fragment"_str));
          auto existing =
              asset_outputs.get(local.as_path().to_string_lossy().as_str());
          auto output = String::make();
          if (existing.is_some()) {
            output = (**existing).clone();
          } else {
            auto name = local.as_path().file_name();
            if (name.is_none() || name->to_str().is_none())
              return Err(markdown_error(
                  item.span, "local image has no UTF-8 file name"_str));
            auto digest = licrypto::sha256_hex(
                local.as_path().to_string_lossy().as_str());
            output =
                rstd::format("assets/{}-{}",
                             digest.as_str().get(usize{}, usize(16)).unwrap(),
                             *name->to_str());
            asset_outputs.insert(local.as_path().to_string_lossy(),
                                 output.clone());
            content.assets.push(BookAsset{
                .source = local.clone(),
                .output = output.clone(),
            });
          }
          item.resolved_destination =
              relative_url(page.output.as_str(), output.as_str());
          continue;
        }
        if (!path.ends_with(".md"_str))
          return Err(markdown_error(
              item.span, "local links must target a catalogued .md page"_str));
        auto target = page_for_path(graph, local.as_path());
        if (target.is_none())
          return Err(markdown_error(
              item.span,
              "local Markdown link targets a page not listed in SUMMARY.md"_str));
        if (!fragment.is_empty() &&
            !heading_exists(documents[*target], fragment))
          return Err(markdown_error(
              item.span,
              rstd::format("target page has no heading '#{}'", fragment)
                  .as_str()));
        auto resolved =
            *target == page_index
                ? String::make(fragment.is_empty() ? "#"_str : "#"_str)
                : relative_url(page.output.as_str(),
                               graph.pages[*target].output.as_str());
        if (*target == page_index && fragment.is_empty())
          resolved = String::make("#"_str);
        if (!fragment.is_empty()) {
          if (*target != page_index)
            resolved.push_ascii('#');
          resolved.push_str(fragment);
        }
        item.resolved_destination = rstd::move(resolved);
      }
    }
  }
  for (auto page_index = usize{}; page_index < documents.len(); ++page_index) {
    auto html = render_markdown_document(documents[page_index]);
    content.pages.push(BookPageDocument{
        .page = page_index,
        .document = rstd::move(documents[page_index]),
        .html = rstd::move(html),
    });
  }
  return Ok(rstd::move(content));
}

} // namespace lito::book
