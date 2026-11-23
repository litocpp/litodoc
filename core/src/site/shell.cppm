export module lito.site:shell;
import rstd;
import :templates;
using namespace rstd::prelude;
using namespace rstd::literals;
export namespace lito::site
{
auto site_shell_context() -> TemplateValue {
    auto value = TemplateValue::object_value();
    value.insert("has_custom_brand"_str, TemplateValue::boolean_value(false));
    value.insert("has_default_brand"_str, TemplateValue::boolean_value(true));
    value.insert("has_repository"_str, TemplateValue::boolean_value(false));
    value.insert("has_navigation"_str, TemplateValue::boolean_value(false));
    value.insert("has_site_links"_str, TemplateValue::boolean_value(false));
    value.insert("has_footer"_str, TemplateValue::boolean_value(false));
    return value;
}
auto page_shell_context() -> TemplateValue {
    auto value = TemplateValue::object_value();
    for (auto key : array<ref<str>, 5> { "has_description"_str,
                                         "has_canonical"_str,
                                         "has_edit_url"_str,
                                         "is_not_found"_str,
                                         "noindex"_str })
        value.insert(key, TemplateValue::boolean_value(false));
    return value;
}
} // namespace lito::site
