export module lito.site:templates;

import rstd;
import :html;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site
{

enum class TemplateValueKind
{
    Null,
    Boolean,
    Text,
    TrustedHtml,
    Array,
    Object,
};

struct TemplateValue {
    TemplateValueKind                                  kind { TemplateValueKind::Null };
    bool                                               boolean {};
    String                                             text;
    Vec<TemplateValue>                                 array;
    rstd::collections::BTreeMap<String, TemplateValue> object;

    static auto null() -> TemplateValue { return {}; }

    static auto boolean_value(bool value) -> TemplateValue {
        return TemplateValue { .kind = TemplateValueKind::Boolean, .boolean = value };
    }

    static auto text_value(String value) -> TemplateValue {
        return TemplateValue { .kind = TemplateValueKind::Text, .text = rstd::move(value) };
    }

    static auto text_value(ref<str> value) -> TemplateValue {
        return text_value(String::make(value));
    }

    static auto trusted_html(String value) -> TemplateValue {
        return TemplateValue { .kind = TemplateValueKind::TrustedHtml, .text = rstd::move(value) };
    }

    static auto array_value() -> TemplateValue {
        return TemplateValue { .kind = TemplateValueKind::Array };
    }

    static auto object_value() -> TemplateValue {
        return TemplateValue {
            .kind   = TemplateValueKind::Object,
            .object = rstd::collections::BTreeMap<String, TemplateValue>::make(),
        };
    }

    auto insert(ref<str> name, TemplateValue value) -> void {
        object.insert(String::make(name), rstd::move(value));
    }

    auto clone() const -> TemplateValue {
        auto copy = TemplateValue { .kind = kind, .boolean = boolean, .text = text.clone() };
        for (const auto& item : array) copy.array.push(item.clone());
        auto entries = object.iter();
        for (auto item = entries.next(); item.is_some(); item = entries.next())
            copy.insert((*(*item).template get<0>()).as_str(),
                        (*(*item).template get<1>()).clone());
        return copy;
    }
};

enum class TemplateNodeKind
{
    Text,
    Escaped,
    Raw,
    If,
    Each,
    Partial,
};

enum class TemplateConditionKind
{
    Truthy,
    Equal,
    NotEqual,
};

struct TemplateOperand {
    bool   literal { false };
    String value;
};

struct TemplateCondition {
    TemplateConditionKind kind { TemplateConditionKind::Truthy };
    TemplateOperand       left;
    TemplateOperand       right;
};

struct TemplateNode {
    TemplateNodeKind  kind { TemplateNodeKind::Text };
    String            value;
    TemplateCondition condition;
    Vec<TemplateNode> children;
    Vec<TemplateNode> alternative;
    usize             line { usize(1) };
    usize             column { usize(1) };
};

struct TemplateDocument {
    String            path;
    Vec<TemplateNode> nodes;
};

struct TemplateSet {
    String                                                identity;
    rstd::collections::BTreeMap<String, TemplateDocument> documents;
};

auto template_location(ref<str> source, usize offset) -> rstd::tuple<usize, usize> {
    auto line   = usize(1);
    auto column = usize(1);
    for (auto index = usize {}; index < offset; ++index) {
        if (source[index] == u8('\n')) {
            ++line;
            column = usize(1);
        } else {
            ++column;
        }
    }
    return { line, column };
}

auto template_error(ref<str> path, ref<str> source, usize offset, ref<str> message) -> String {
    auto location = template_location(source, offset);
    return rstd::format(
        "{}:{}:{}: {}", path, location.template get<0>(), location.template get<1>(), message);
}

auto find_template_marker(ref<str> source, usize begin, ref<str> marker) -> Option<usize> {
    if (marker.is_empty() || source.len() < marker.len()) return None();
    for (auto index = begin; index + marker.len() <= source.len(); ++index) {
        auto matches = true;
        for (auto offset = usize {}; offset < marker.len(); ++offset) {
            if (source[index + offset] != marker[offset]) {
                matches = false;
                break;
            }
        }
        if (matches) return Some(index);
    }
    return None();
}

auto template_expression_space(u8 byte) -> bool {
    return byte == u8(' ') || byte == u8('\t') || byte == u8('\r') || byte == u8('\n');
}

auto template_value_path(ref<str> path) -> bool {
    if (path == "."_str) return true;
    auto segment = false;
    for (auto index = usize {}; index < path.len(); ++index) {
        auto byte = path[index];
        if (byte == u8('.')) {
            if (! segment) return false;
            segment = false;
        } else if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
                   (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') || byte == u8('-') ||
                   byte >= u8(0x80)) {
            segment = true;
        } else {
            return false;
        }
    }
    return segment;
}

auto parse_template_operand(ref<str> expression, usize& cursor) -> Result<TemplateOperand, String> {
    while (cursor < expression.len() && template_expression_space(expression[cursor])) ++cursor;
    if (cursor == expression.len()) return Err("template condition is missing an operand"_Str);
    if (expression[cursor] == u8('"')) {
        ++cursor;
        auto begin = cursor;
        auto text  = String::make();
        while (cursor < expression.len()) {
            if (expression[cursor] == u8('"')) {
                text.push_str(expression.get(begin, cursor).unwrap());
                ++cursor;
                return Ok(TemplateOperand { .literal = true, .value = rstd::move(text) });
            }
            if (expression[cursor] == u8('\\')) {
                text.push_str(expression.get(begin, cursor).unwrap());
                ++cursor;
                if (cursor == expression.len() ||
                    (expression[cursor] != u8('"') && expression[cursor] != u8('\\')))
                    return Err("unsupported escape in template text literal"_Str);
                text.push_str(expression.get(cursor, cursor + usize(1)).unwrap());
                ++cursor;
                begin = cursor;
            } else {
                ++cursor;
            }
        }
        return Err("unterminated template text literal"_Str);
    }
    auto begin = cursor;
    while (cursor < expression.len() && ! template_expression_space(expression[cursor]) &&
           expression[cursor] != u8('=') && expression[cursor] != u8('!'))
        ++cursor;
    auto path = expression.get(begin, cursor).unwrap();
    if (! template_value_path(path)) return Err("invalid template condition value path"_Str);
    return Ok(TemplateOperand { .value = String::make(path) });
}

auto parse_template_condition(ref<str> expression) -> Result<TemplateCondition, String> {
    auto cursor = usize {};
    auto left   = parse_template_operand(expression, cursor);
    if (left.is_err()) return Err(rstd::move(left).unwrap_err());
    while (cursor < expression.len() && template_expression_space(expression[cursor])) ++cursor;
    if (cursor == expression.len()) {
        if (left->literal) return Err("template truthiness condition requires a value path"_Str);
        return Ok(TemplateCondition { .left = rstd::move(left).unwrap() });
    }
    if (cursor + usize(1) >= expression.len() || expression[cursor + usize(1)] != u8('=') ||
        (expression[cursor] != u8('=') && expression[cursor] != u8('!')))
        return Err("template condition requires a single '==' or '!=' comparison"_Str);
    auto kind = expression[cursor] == u8('=') ? TemplateConditionKind::Equal
                                              : TemplateConditionKind::NotEqual;
    cursor += usize(2);
    auto right = parse_template_operand(expression, cursor);
    if (right.is_err()) return Err(rstd::move(right).unwrap_err());
    while (cursor < expression.len() && template_expression_space(expression[cursor])) ++cursor;
    if (cursor != expression.len()) return Err("unexpected text after template comparison"_Str);
    return Ok(TemplateCondition {
        .kind  = kind,
        .left  = rstd::move(left).unwrap(),
        .right = rstd::move(right).unwrap(),
    });
}

auto parse_template_nodes(ref<str>           path,
                          ref<str>           source,
                          usize&             cursor,
                          ref<str>           expected_close,
                          Vec<TemplateNode>* alternative = nullptr)
    -> Result<Vec<TemplateNode>, String> {
    auto nodes = Vec<TemplateNode>::make();
    while (cursor < source.len()) {
        auto marker = find_template_marker(source, cursor, "{{"_str);
        if (marker.is_none()) {
            if (! expected_close.is_empty())
                return Err(
                    template_error(path,
                                   source,
                                   cursor,
                                   rstd::format("missing '{{{{/{}}}}}'", expected_close).as_str()));
            nodes.push(TemplateNode {
                .kind  = TemplateNodeKind::Text,
                .value = String::make(source.get(cursor, source.len()).unwrap()),
            });
            cursor = source.len();
            break;
        }
        if (*marker > cursor) {
            nodes.push(TemplateNode {
                .kind  = TemplateNodeKind::Text,
                .value = String::make(source.get(cursor, *marker).unwrap()),
            });
        }
        auto location = template_location(source, *marker);
        auto triple   = *marker + usize(2) < source.len() && source[*marker + usize(2)] == u8('{');
        auto close_marker     = triple ? "}}}"_str : "}}"_str;
        auto expression_begin = *marker + (triple ? usize(3) : usize(2));
        auto close            = find_template_marker(source, expression_begin, close_marker);
        if (close.is_none())
            return Err(
                template_error(path, source, *marker, "unterminated template expression"_str));
        auto expression = source.get(expression_begin, *close).unwrap().trim_ascii();
        cursor          = *close + close_marker.len();
        if (expression.is_empty())
            return Err(template_error(path, source, *marker, "empty template expression"_str));
        if (triple) {
            nodes.push(TemplateNode {
                .kind   = TemplateNodeKind::Raw,
                .value  = String::make(expression),
                .line   = location.template get<0>(),
                .column = location.template get<1>(),
            });
            continue;
        }
        if (expression == "else"_str) {
            if (expected_close != "if"_str || alternative == nullptr)
                return Err(template_error(
                    path, source, *marker, "unexpected or repeated template else"_str));
            auto remaining = parse_template_nodes(path, source, cursor, expected_close);
            if (remaining.is_err()) return Err(rstd::move(remaining).unwrap_err());
            *alternative = rstd::move(remaining).unwrap();
            return Ok(rstd::move(nodes));
        }
        if (expression.starts_with("/"_str)) {
            auto close_name = expression.get(usize(1), expression.len()).unwrap().trim_ascii();
            if (expected_close.is_empty())
                return Err(template_error(path, source, *marker, "unexpected template close"_str));
            if (close_name != expected_close)
                return Err(template_error(
                    path,
                    source,
                    *marker,
                    rstd::format("template closes '{}', expected '{}'", close_name, expected_close)
                        .as_str()));
            return Ok(rstd::move(nodes));
        }
        if (expression.starts_with("#if "_str) || expression.starts_with("#each "_str)) {
            auto is_if  = expression.starts_with("#if "_str);
            auto offset = is_if ? usize(4) : usize(6);
            auto name   = expression.get(offset, expression.len()).unwrap().trim_ascii();
            if (name.is_empty())
                return Err(
                    template_error(path, source, *marker, "template block has no value"_str));
            auto condition = TemplateCondition {};
            if (is_if) {
                auto parsed = parse_template_condition(name);
                if (parsed.is_err())
                    return Err(template_error(path, source, *marker, parsed.unwrap_err().as_str()));
                condition = rstd::move(parsed).unwrap();
            } else if (! template_value_path(name)) {
                return Err(template_error(
                    path, source, *marker, "template each requires a value path"_str));
            }
            Vec<TemplateNode> otherwise;
            auto children = parse_template_nodes(path,
                                                 source,
                                                 cursor,
                                                 is_if ? "if"_str : "each"_str,
                                                 is_if ? rstd::addressof(otherwise) : nullptr);
            if (children.is_err()) return Err(rstd::move(children).unwrap_err());
            nodes.push(TemplateNode {
                .kind        = is_if ? TemplateNodeKind::If : TemplateNodeKind::Each,
                .value       = String::make(name),
                .condition   = rstd::move(condition),
                .children    = rstd::move(children).unwrap(),
                .alternative = rstd::move(otherwise),
                .line        = location.template get<0>(),
                .column      = location.template get<1>(),
            });
            continue;
        }
        if (expression.starts_with(">"_str)) {
            auto name = expression.get(usize(1), expression.len()).unwrap().trim_ascii();
            if (name.is_empty())
                return Err(
                    template_error(path, source, *marker, "template partial has no path"_str));
            nodes.push(TemplateNode {
                .kind   = TemplateNodeKind::Partial,
                .value  = String::make(name),
                .line   = location.template get<0>(),
                .column = location.template get<1>(),
            });
            continue;
        }
        if (expression.starts_with("#"_str))
            return Err(template_error(path, source, *marker, "unknown template block"_str));
        nodes.push(TemplateNode {
            .kind   = TemplateNodeKind::Escaped,
            .value  = String::make(expression),
            .line   = location.template get<0>(),
            .column = location.template get<1>(),
        });
    }
    if (! expected_close.is_empty())
        return Err(template_error(
            path, source, cursor, rstd::format("missing '{{{{/{}}}}}'", expected_close).as_str()));
    return Ok(rstd::move(nodes));
}

auto parse_template(ref<str> path, ref<str> source) -> Result<TemplateDocument, String> {
    auto cursor = usize {};
    auto nodes  = parse_template_nodes(path, source, cursor, ref<str> {});
    if (nodes.is_err()) return Err(rstd::move(nodes).unwrap_err());
    return Ok(TemplateDocument {
        .path  = String::make(path),
        .nodes = rstd::move(nodes).unwrap(),
    });
}

auto object_member(const TemplateValue& value, ref<str> name) -> Option<ref<TemplateValue>> {
    if (value.kind != TemplateValueKind::Object) return None();
    return value.object.get(name);
}

auto resolve_template_value(const TemplateValue& root, const TemplateValue& current, ref<str> path)
    -> Option<ref<TemplateValue>> {
    if (path == "this"_str || path == "."_str)
        return Some(ref<TemplateValue>::from_raw_parts(rstd::addressof(current)));
    auto begin = usize {};
    auto end   = usize {};
    while (end < path.len() && path[end] != u8('.')) ++end;
    auto first = path.get(begin, end).unwrap();
    auto value = object_member(current, first);
    if (value.is_none()) value = object_member(root, first);
    if (value.is_none()) return None();
    while (end < path.len()) {
        begin = end + usize(1);
        end   = begin;
        while (end < path.len() && path[end] != u8('.')) ++end;
        auto part = path.get(begin, end).unwrap();
        if (part.is_empty()) return None();
        value = object_member(**value, part);
        if (value.is_none()) return None();
    }
    return value;
}

auto template_truthy(const TemplateValue& value) -> bool {
    switch (value.kind) {
    case TemplateValueKind::Null: return false;
    case TemplateValueKind::Boolean: return value.boolean;
    case TemplateValueKind::Text:
    case TemplateValueKind::TrustedHtml: return ! value.text.is_empty();
    case TemplateValueKind::Array: return ! value.array.is_empty();
    case TemplateValueKind::Object: return ! value.object.is_empty();
    }
    __builtin_unreachable();
}

auto template_render_error(const TemplateDocument& document,
                           const TemplateNode&     node,
                           ref<str>                message) -> String {
    return rstd::format("{}:{}:{}: {}", document.path.as_str(), node.line, node.column, message);
}

auto template_operand_text(const TemplateOperand& operand,
                           const TemplateValue&   root,
                           const TemplateValue&   current) -> Result<ref<str>, String> {
    if (operand.literal) return Ok(operand.value.as_str());
    auto value = resolve_template_value(root, current, operand.value.as_str());
    if (value.is_none()) return Err(rstd::format("missing template value '{}'", operand.value));
    if ((**value).kind != TemplateValueKind::Text)
        return Err(rstd::format("template comparison value '{}' is not text", operand.value));
    return Ok((**value).text.as_str());
}

auto evaluate_template_condition(const TemplateNode&  node,
                                 const TemplateValue& root,
                                 const TemplateValue& current) -> Result<bool, String> {
    const auto& condition = node.condition;
    if (condition.kind == TemplateConditionKind::Truthy) {
        auto value = resolve_template_value(root, current, condition.left.value.as_str());
        if (value.is_none())
            return Err(rstd::format("missing template value '{}'", condition.left.value));
        return Ok(template_truthy(**value));
    }
    auto left = template_operand_text(condition.left, root, current);
    if (left.is_err()) return Err(rstd::move(left).unwrap_err());
    auto right = template_operand_text(condition.right, root, current);
    if (right.is_err()) return Err(rstd::move(right).unwrap_err());
    auto equal = *left == *right;
    return Ok(condition.kind == TemplateConditionKind::Equal ? equal : ! equal);
}

auto render_template_nodes(const TemplateSet&       templates,
                           const TemplateDocument&  document,
                           const Vec<TemplateNode>& nodes,
                           const TemplateValue&     root,
                           const TemplateValue&     current,
                           Vec<String>&             partial_stack,
                           String&                  output) -> Result<empty, String> {
    for (const auto& node : nodes) {
        if (node.kind == TemplateNodeKind::Text) {
            output.push_str(node.value.as_str());
            continue;
        }
        if (node.kind == TemplateNodeKind::Partial) {
            for (const auto& active : partial_stack) {
                if (active.as_str() == node.value.as_str())
                    return Err(template_render_error(
                        document,
                        node,
                        rstd::format("template partial cycle through '{}'", node.value.as_str())
                            .as_str()));
            }
            auto partial = templates.documents.get(node.value.as_str());
            if (partial.is_none())
                return Err(template_render_error(
                    document,
                    node,
                    rstd::format("missing template partial '{}'", node.value.as_str()).as_str()));
            partial_stack.push(node.value.clone());
            auto rendered = render_template_nodes(
                templates, **partial, (**partial).nodes, root, current, partial_stack, output);
            partial_stack.pop();
            if (rendered.is_err()) return rendered;
            continue;
        }
        if (node.kind == TemplateNodeKind::If) {
            auto condition = evaluate_template_condition(node, root, current);
            if (condition.is_err())
                return Err(template_render_error(document, node, condition.unwrap_err().as_str()));
            auto rendered = render_template_nodes(templates,
                                                  document,
                                                  *condition ? node.children : node.alternative,
                                                  root,
                                                  current,
                                                  partial_stack,
                                                  output);
            if (rendered.is_err()) return rendered;
            continue;
        }
        auto value = resolve_template_value(root, current, node.value.as_str());
        if (value.is_none())
            return Err(template_render_error(
                document,
                node,
                rstd::format("missing template value '{}'", node.value.as_str()).as_str()));
        if (node.kind == TemplateNodeKind::Escaped) {
            if ((**value).kind != TemplateValueKind::Text &&
                (**value).kind != TemplateValueKind::TrustedHtml)
                return Err(template_render_error(
                    document,
                    node,
                    rstd::format("template value '{}' is not text", node.value.as_str()).as_str()));
            output.push_str(escape_html((**value).text.as_str()).as_str());
            continue;
        }
        if (node.kind == TemplateNodeKind::Raw) {
            if ((**value).kind != TemplateValueKind::TrustedHtml)
                return Err(template_render_error(
                    document,
                    node,
                    rstd::format("template value '{}' is not trusted HTML", node.value.as_str())
                        .as_str()));
            output.push_str((**value).text.as_str());
            continue;
        }
        if ((**value).kind != TemplateValueKind::Array)
            return Err(template_render_error(
                document,
                node,
                rstd::format("template value '{}' is not an array", node.value.as_str()).as_str()));
        for (const auto& item : (**value).array) {
            auto rendered = render_template_nodes(
                templates, document, node.children, root, item, partial_stack, output);
            if (rendered.is_err()) return rendered;
        }
    }
    return Ok(empty {});
}

auto render_template(const TemplateSet& templates, ref<str> path, const TemplateValue& context)
    -> Result<String, String> {
    auto document = templates.documents.get(path);
    if (document.is_none())
        return Err(
            rstd::format("frontend '{}' has no template '{}'", templates.identity.as_str(), path));
    auto stack = Vec<String>::make();
    stack.push(String::make(path));
    auto output   = String::make();
    auto rendered = render_template_nodes(
        templates, **document, (**document).nodes, context, context, stack, output);
    if (rendered.is_err())
        return Err(rstd::format(
            "frontend '{}': {}", templates.identity.as_str(), rstd::move(rendered).unwrap_err()));
    return Ok(rstd::move(output));
}

} // namespace lito::site
