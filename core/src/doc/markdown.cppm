export module lito.doc:markdown;

import rstd;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::doc {

auto find_byte(ref<str> value, usize begin, u8 needle) -> Option<usize> {
  for (auto index = begin; index < value.len(); ++index) {
    if (value[index] == needle)
      return Some(index);
  }
  return None();
}

auto inline_markdown(ref<str> value) -> String {
  auto result = String::make();
  auto begin = usize{};
  for (auto index = usize{}; index < value.len();) {
    auto marker = value[index];
    if (marker != u8('`') && marker != u8('*') && marker != u8('[')) {
      ++index;
      continue;
    }
    result.push_str(escape_html(value.get(begin, index).unwrap()).as_str());
    if (marker == u8('`') || marker == u8('*')) {
      auto close = find_byte(value, index + usize(1), marker);
      if (close.is_none()) {
        result.push_str(
            escape_html(value.get(index, index + usize(1)).unwrap()).as_str());
        begin = index + usize(1);
        index = begin;
        continue;
      }
      result.push_str(marker == u8('`') ? "<code>"_str : "<em>"_str);
      result.push_str(
          escape_html(value.get(index + usize(1), *close).unwrap()).as_str());
      result.push_str(marker == u8('`') ? "</code>"_str : "</em>"_str);
      begin = *close + usize(1);
      index = begin;
      continue;
    }
    auto label_end = find_byte(value, index + usize(1), u8(']'));
    if (label_end.is_none() || *label_end + usize(1) >= value.len() ||
        value[*label_end + usize(1)] != u8('(')) {
      result.push_str("["_str);
      begin = index + usize(1);
      index = begin;
      continue;
    }
    auto link_end = find_byte(value, *label_end + usize(2), u8(')'));
    if (link_end.is_none()) {
      result.push_str("["_str);
      begin = index + usize(1);
      index = begin;
      continue;
    }
    auto label = value.get(index + usize(1), *label_end).unwrap();
    auto link = value.get(*label_end + usize(2), *link_end).unwrap();
    if (safe_link(link)) {
      result.push_str("<a href=\""_str);
      result.push_str(escape_html(link).as_str());
      result.push_str("\">"_str);
      result.push_str(escape_html(label).as_str());
      result.push_str("</a>"_str);
    } else {
      result.push_str(escape_html(label).as_str());
    }
    begin = *link_end + usize(1);
    index = begin;
  }
  result.push_str(escape_html(value.get(begin, value.len()).unwrap()).as_str());
  return result;
}

auto render_markdown(ref<str> value) -> String {
  auto output = String::make();
  auto begin = usize{};
  auto code = false;
  auto list = false;
  auto bytes = value.as_bytes();
  while (begin <= bytes.len()) {
    auto end = begin;
    while (end < bytes.len() && bytes[end] != u8('\n') &&
           bytes[end] != u8('\r'))
      ++end;
    auto line = value.get(begin, end).unwrap();
    auto trimmed = line.trim_ascii();
    if (line.starts_with("```"_str)) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      output.push_str(code ? "</code></pre>"_str : "<pre><code>"_str);
      code = !code;
    } else if (code) {
      output.push_str(escape_html(line).as_str());
      output.push_ascii('\n');
    } else if (trimmed.starts_with("@brief "_str) ||
               trimmed.starts_with("\\brief "_str)) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      output.push_str("<p>"_str);
      output.push_str(
          inline_markdown(trimmed.get(usize(7), trimmed.len()).unwrap())
              .as_str());
      output.push_str("</p>"_str);
    } else if (trimmed.starts_with("@return "_str) ||
               trimmed.starts_with("@returns "_str) ||
               trimmed.starts_with("\\return "_str) ||
               trimmed.starts_with("\\returns "_str)) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      auto offset = trimmed.starts_with("@returns "_str) ||
                            trimmed.starts_with("\\returns "_str)
                        ? usize(9)
                        : usize(8);
      output.push_str("<h3>Returns</h3><p>"_str);
      output.push_str(
          inline_markdown(trimmed.get(offset, trimmed.len()).unwrap())
              .as_str());
      output.push_str("</p>"_str);
    } else if (trimmed.starts_with("@param "_str) ||
               trimmed.starts_with("@tparam "_str) ||
               trimmed.starts_with("\\param "_str) ||
               trimmed.starts_with("\\tparam "_str)) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      auto offset = trimmed.starts_with("@param "_str) ||
                            trimmed.starts_with("\\param "_str)
                        ? usize(7)
                        : usize(8);
      auto template_parameter = offset == usize(8);
      auto body = trimmed.get(offset, trimmed.len()).unwrap();
      auto split = find_byte(body, usize{}, u8(' '));
      auto name = split.is_some() ? body.get(usize{}, *split).unwrap() : body;
      auto text = split.is_some()
                      ? body.get(*split + usize(1), body.len()).unwrap()
                      : ref<str>{};
      output.push_str(template_parameter
                          ? "<p><strong>Template parameter <code>"_str
                          : "<p><strong>Parameter <code>"_str);
      output.push_str(escape_html(name).as_str());
      output.push_str("</code></strong> "_str);
      output.push_str(inline_markdown(text).as_str());
      output.push_str("</p>"_str);
    } else if (line.starts_with("# "_str)) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      output.push_str("<h2>"_str);
      output.push_str(
          inline_markdown(line.get(usize(2), line.len()).unwrap()).as_str());
      output.push_str("</h2>"_str);
    } else if (line.starts_with("- "_str)) {
      if (!list) {
        output.push_str("<ul>"_str);
        list = true;
      }
      output.push_str("<li>"_str);
      output.push_str(
          inline_markdown(line.get(usize(2), line.len()).unwrap()).as_str());
      output.push_str("</li>"_str);
    } else if (!line.trim_ascii().is_empty()) {
      if (list) {
        output.push_str("</ul>"_str);
        list = false;
      }
      output.push_str("<p>"_str);
      output.push_str(inline_markdown(line).as_str());
      output.push_str("</p>"_str);
    }
    if (end == bytes.len())
      break;
    if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
        bytes[end + usize(1)] == u8('\n'))
      ++end;
    begin = end + usize(1);
  }
  if (list)
    output.push_str("</ul>"_str);
  if (code)
    output.push_str("</code></pre>"_str);
  return output;
}

} // namespace lito::doc
