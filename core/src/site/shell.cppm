export module lito.site:shell;
import rstd;
import :templates;
using namespace rstd::prelude;
using namespace rstd::literals;
export namespace lito::site
{
auto site_data_context() -> TemplateValue {
    auto value = TemplateValue::object_value();
    for (auto key : array<ref<str>, 5> {
             "brand"_str, "repository"_str, "footer"_str, "description"_str, "version"_str })
        value.insert(key, TemplateValue::null());
    value.insert("navigation"_str, TemplateValue::array_value());
    return value;
}
auto page_data_context() -> TemplateValue {
    auto value = TemplateValue::object_value();
    for (auto key : array<ref<str>, 8> { "description"_str,
                                         "canonical_url"_str,
                                         "edit_url"_str,
                                         "source"_str,
                                         "redirect"_str,
                                         "base_href"_str,
                                         "icon_url"_str,
                                         "icon_media_type"_str })
        value.insert(key, TemplateValue::null());
    value.insert("noindex"_str, TemplateValue::boolean_value(false));
    value.insert("not_found"_str, TemplateValue::boolean_value(false));
    return value;
}
} // namespace lito::site
