#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace
{
auto render_condition(ref<str> source, const TemplateValue& context) -> Result<String, String> {
    auto parsed = parse_template("condition.html"_str, source);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    TemplateSet templates { .identity = "conditions"_Str };
    templates.documents.insert("condition.html"_Str, rstd::move(parsed).unwrap());
    return render_template(templates, "condition.html"_str, context);
}
} // namespace

TEST(TemplateConditions, ComparesTextPathsAndQuotedLiterals) {
    auto context = TemplateValue::object_value();
    auto symbol  = TemplateValue::object_value();
    symbol.insert("kind"_str, TemplateValue::text_value("record"_str));
    context.insert("symbol"_str, rstd::move(symbol));
    context.insert("target"_str, TemplateValue::text_value("record"_str));
    auto rendered = render_condition("{{#if symbol.kind == \"record\"}}a{{else}}wrong{{/if}}"
                                     "{{#if symbol.kind != \"function\"}}b{{/if}}"
                                     "{{#if symbol.kind == target}}c{{/if}}"
                                     "{{#if \"record\" == symbol.kind}}d{{/if}}"
                                     "{{#if \"record\" != \"function\"}}e{{/if}}"
                                     "{{#if symbol.kind!=target}}wrong{{else}}f{{/if}}"_str,
                                     context);
    ASSERT_TRUE(rendered.is_ok());
    EXPECT_EQ(rendered->as_str(), "abcdef"_str);
}

TEST(TemplateConditions, PreservesUnicodeQuotedEscapesAndLiteralOperators) {
    auto context = TemplateValue::object_value();
    context.insert("unicode"_str, TemplateValue::text_value("中文类型"_str));
    context.insert("quoted"_str, TemplateValue::text_value("a\"b\\c"_str));
    context.insert("operators"_str, TemplateValue::text_value("x == y != z"_str));
    context.insert("empty"_str, TemplateValue::text_value(""_str));
    auto rendered = render_condition("{{#if unicode == \"中文类型\"}}中{{/if}}"
                                     "{{#if quoted == \"a\\\"b\\\\c\"}}文{{/if}}"
                                     "{{#if operators == \"x == y != z\"}}字{{/if}}"
                                     "{{#if empty == \"\"}}符{{/if}}"_str,
                                     context);
    ASSERT_TRUE(rendered.is_ok());
    EXPECT_EQ(rendered->as_str(), "中文字符"_str);
}

TEST(TemplateConditions, NestedElseEachUsesCurrentAndRootFields) {
    auto context = TemplateValue::object_value();
    context.insert("enabled"_str, TemplateValue::boolean_value(true));
    context.insert("target"_str, TemplateValue::text_value("record"_str));
    auto items = TemplateValue::array_value();
    for (auto kind : array<ref<str>, 2> { "record"_str, "function"_str }) {
        auto item = TemplateValue::object_value();
        item.insert("kind"_str, TemplateValue::text_value(kind));
        items.array.push(rstd::move(item));
    }
    context.insert("items"_str, rstd::move(items));
    auto rendered =
        render_condition("{{#if enabled}}{{#each items}}{{#if kind == target}}{{kind}}"
                         "{{else}}{{#if kind != \"variable\"}}other{{else}}variable{{/if}}{{/if}};"
                         "{{/each}}{{else}}off{{/if}}"_str,
                         context);
    ASSERT_TRUE(rendered.is_ok());
    EXPECT_EQ(rendered->as_str(), "record;other;"_str);
}

TEST(TemplateConditions, InvalidExpressionsFailAtParseWithLocation) {
    for (auto expression :
         array<ref<str>, 15> { "{{#if kind = \"record\"}}yes{{/if}}"_str,
                               "{{#if kind === \"record\"}}yes{{/if}}"_str,
                               "{{#if kind == \"record\" == kind}}yes{{/if}}"_str,
                               "{{#if kind != \"record\" != kind}}yes{{/if}}"_str,
                               "{{#if kind ==}}yes{{/if}}"_str,
                               "{{#if == kind}}yes{{/if}}"_str,
                               "{{#if kind == \"record}}yes{{/if}}"_str,
                               "{{#if kind == \"re\\q\"}}yes{{/if}}"_str,
                               "{{#if kind == \"record\"tail}}yes{{/if}}"_str,
                               "{{#if kind..value == \"record\"}}yes{{/if}}"_str,
                               "{{#if kind.value.}}yes{{/if}}"_str,
                               "{{#if \"record\"}}yes{{/if}}"_str,
                               "{{#if kind > other}}yes{{/if}}"_str,
                               "{{#if kind && other}}yes{{/if}}"_str,
                               "{{#each items == \"record\"}}yes{{/each}}"_str }) {
        auto source = rstd::format("line\n  {}", expression);
        auto parsed = parse_template("invalid.html"_str, source.as_str());
        ASSERT_TRUE(parsed.is_err());
        EXPECT_TRUE(parsed.unwrap_err().as_str().contains("invalid.html:2:3:"_str));
    }
}

TEST(TemplateConditions, MissingValuesAndNonTextComparisonsFailAtRender) {
    auto context = TemplateValue::object_value();
    context.insert("kind"_str, TemplateValue::text_value("record"_str));
    context.insert("nothing"_str, TemplateValue::null());
    context.insert("enabled"_str, TemplateValue::boolean_value(true));
    context.insert("items"_str, TemplateValue::array_value());
    context.insert("object"_str, TemplateValue::object_value());
    context.insert("html"_str, TemplateValue::trusted_html("record"_Str));
    for (auto operand : array<ref<str>, 6> {
             "missing"_str, "nothing"_str, "enabled"_str, "items"_str, "object"_str, "html"_str }) {
        auto left = String::make("line\n  {{#if "_str);
        left.push_str(operand);
        left.push_str(" == kind}}yes{{/if}}"_str);
        auto right = String::make("line\n  {{#if kind != "_str);
        right.push_str(operand);
        right.push_str("}}yes{{/if}}"_str);
        for (const auto& expression : array<String, 2> { rstd::move(left), rstd::move(right) }) {
            auto rendered = render_condition(expression.as_str(), context);
            ASSERT_TRUE(rendered.is_err());
            EXPECT_TRUE(rendered.unwrap_err().as_str().contains("condition.html:2:3:"_str));
        }
    }
    auto guarded = render_condition(
        "{{#if nothing}}{{#if nothing == kind}}wrong{{/if}}{{else}}absent{{/if}}"_str, context);
    ASSERT_TRUE(guarded.is_ok());
    EXPECT_EQ(guarded->as_str(), "absent"_str);
}

TEST(TemplateConditions, ComparisonsDoNotChangeEscapingOrRawRestrictions) {
    auto context = TemplateValue::object_value();
    context.insert("kind"_str, TemplateValue::text_value("<record>"_str));
    context.insert("content"_str, TemplateValue::trusted_html("<b>record</b>"_Str));
    auto rendered =
        render_condition("{{#if kind == \"<record>\"}}{{kind}}{{{content}}}{{/if}}"_str, context);
    ASSERT_TRUE(rendered.is_ok());
    EXPECT_EQ(rendered->as_str(), "&lt;record&gt;<b>record</b>"_str);
    EXPECT_TRUE(
        render_condition("{{#if kind == \"<record>\"}}{{{kind}}}{{/if}}"_str, context).is_err());
    EXPECT_TRUE(render_condition("{{kind == \"<record>\"}}"_str, context).is_err());
}
