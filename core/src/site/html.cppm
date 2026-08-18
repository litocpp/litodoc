export module lito.site:html;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site {

auto escape_html(ref<str> value) -> String {
  auto result = String::make();
  auto begin = usize{};
  for (auto index = usize{}; index < value.len(); ++index) {
    auto replacement = ref<str>{};
    switch (value[index].to_primitive()) {
    case '&':
      replacement = "&amp;"_str;
      break;
    case '<':
      replacement = "&lt;"_str;
      break;
    case '>':
      replacement = "&gt;"_str;
      break;
    case '"':
      replacement = "&quot;"_str;
      break;
    case '\'':
      replacement = "&#39;"_str;
      break;
    default:
      continue;
    }
    result.push_str(value.get(begin, index).unwrap());
    result.push_str(replacement);
    begin = index + usize(1);
  }
  result.push_str(value.get(begin, value.len()).unwrap());
  return result;
}

auto safe_link(ref<str> value) -> bool {
  return value.starts_with("https://"_str) ||
         value.starts_with("http://"_str) || value.starts_with("#"_str);
}

} // namespace lito::site
