module;
#include <rstd/macro.hpp>
export module lito.book:markdown;

import rstd;
import licrypto;
import limarkto;
import lito.site;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;
using limarkto::NodeId;
using limarkto::NodeKind;

namespace lito::book
{
auto markdown_error(const SourceSpan& span, ref<str> message) -> String {
    return rstd::format("{}:{}:{}: {}", span.path.as_path(), span.line, span.column, message);
}
auto syntax_node(const MarkdownDocument& document, NodeId id) -> const limarkto::Node& {
    return document.syntax.node(document.syntax.handle(id)).unwrap().get();
}
auto source_span(const MarkdownDocument& document, usize offset) -> SourceSpan {
    const auto position = document.syntax.position(offset).unwrap();
    return { .path = document.path.clone(), .line = position.line, .column = position.column };
}
auto append_plain_text(const MarkdownDocument& document, NodeId id, String& text) -> void {
    const auto& node = syntax_node(document, id);
    if (node.kind == NodeKind::Text || node.kind == NodeKind::Code ||
        node.kind == NodeKind::HtmlInline)
        text.push_str(node.literal.as_str());
    else if (node.kind == NodeKind::SoftBreak || node.kind == NodeKind::HardBreak)
        text.push_ascii(' ');
    else
        for (auto child : node.children) append_plain_text(document, child, text);
}

auto heading_slug(ref<str> text) -> Result<String, String> {
    auto result    = String::make();
    auto separator = false;
    for (auto character : text) {
        const auto value = character.to_primitive();
        if (value >= 'A' && value <= 'Z') {
            if (separator && ! result.is_empty()) result.push_ascii('-');
            separator = false;
            result.push_ascii(static_cast<char>(value - 'A' + 'a'));
        } else if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')) {
            if (separator && ! result.is_empty()) result.push_ascii('-');
            separator = false;
            result.push_ascii(static_cast<char>(value));
        } else if (value == '-' || value == '_' || value == ' ' || value == '\t') {
            separator = ! result.is_empty();
        }
    }
    if (result.is_empty()) {
        auto digest = rstd_try(licrypto::sha256_hex(text).map_err([](licrypto::Error error) {
            return rstd::format("SHA-256 failed: {}", error);
        }));
        result      = rstd::format("section-{}", digest.as_str().get(usize {}, usize(12)).unwrap());
    }
    return Ok(rstd::move(result));
}

auto path_segments(ref<str> path, bool directory) -> Vec<String> {
    auto result = Vec<String>::make();
    auto begin  = usize {};
    while (begin < path.len()) {
        auto end = begin;
        while (end < path.len() && path[end] != u8('/')) ++end;
        if (end > begin) result.push(String::make(path.get(begin, end).unwrap()));
        begin = end + usize(1);
    }
    if (directory && ! result.is_empty()) result.pop();
    return result;
}

auto relative_url(ref<str> from_output, ref<str> to_output) -> String {
    auto from   = path_segments(from_output, true);
    auto to     = path_segments(to_output, false);
    auto common = usize {};
    while (common < from.len() && common < to.len() && from[common] == to[common].as_str())
        ++common;
    auto result = String::make();
    for (auto index = common; index < from.len(); ++index) result.push_str("../"_str);
    for (auto index = common; index < to.len(); ++index) {
        if (index != common) result.push_ascii('/');
        result.push_str(to[index].as_str());
    }
    return result.is_empty() ? String::make("index.html"_str) : rstd::move(result);
}

auto split_destination(ref<str> destination) -> rstd::tuple<ref<str>, ref<str>> {
    for (auto index = usize {}; index < destination.len(); ++index) {
        if (destination[index] == u8('#')) {
            return { destination.get(usize {}, index).unwrap(),
                     destination.get(index + usize(1), destination.len()).unwrap() };
        }
    }
    return { destination, ref<str> {} };
}

auto heading_exists(const MarkdownDocument& document, ref<str> anchor) -> bool {
    for (const auto& heading : document.headings) {
        if (heading.anchor == anchor) return true;
    }
    return false;
}

auto page_for_path(const BookGraph& graph, ref<rstd::path::Path> path) -> Option<usize> {
    for (auto index = usize {}; index < graph.pages.len(); ++index) {
        if (graph.pages[index].source_path.as_path() == path) return Some(index);
    }
    return None();
}

auto resolve_local_path(const BookProject& project,
                        const BookPage&    page,
                        ref<str>           value,
                        const SourceSpan&  span) -> Result<rstd::path::PathBuf, String> {
    auto relative = rstd::path::PathBuf::from(value);
    if (relative.is_empty() || relative.as_path().is_absolute() || relative.as_path().has_root())
        return Err(markdown_error(span, "local link must use a relative path"_str));
    auto parent    = page.source_path.as_path().parent().unwrap();
    auto canonical = rstd::fs::canonicalize(
        rstd::path::PathBuf::from(parent).join(relative.as_path()).as_path());
    if (canonical.is_err())
        return Err(markdown_error(span,
                                  rstd::format("cannot resolve local link '{}': {}",
                                               value,
                                               rstd::move(canonical).unwrap_err())
                                      .as_str()));
    if (canonical->as_path().strip_prefix(project.source_root.as_path()).is_none())
        return Err(markdown_error(span, "local link escapes the Book source root"_str));
    return Ok(rstd::move(canonical).unwrap());
}

auto dangerous_scheme(ref<str> value) -> bool {
    for (auto index = usize {}; index < value.len(); ++index) {
        if (value[index] == u8('/') || value[index] == u8('#')) return false;
        if (value[index] == u8(':')) return true;
    }
    return false;
}

class BookMarkdownRenderer {
    const MarkdownDocument&                    document_;
    rstd::collections::BTreeMap<usize, String> destinations_;
    String                                     output_;
    auto literal(ref<str> text) -> void { output_.push_str(escape_html(text).as_str()); }
    auto children(const limarkto::Node& node, bool tight = false) -> Result<empty, String> {
        for (auto child : node.children) rstd_try(append(child, tight));
        return Ok(empty {});
    }
    auto wrap(const limarkto::Node& node, ref<str> tag) -> Result<empty, String> {
        output_.push_str(rstd::format("<{}>", tag).as_str());
        rstd_try(children(node));
        output_.push_str(rstd::format("</{}>", tag).as_str());
        return Ok(empty {});
    }
    auto append(NodeId id, bool tight = false) -> Result<empty, String> {
        const auto& node = syntax_node(document_, id);
        switch (node.kind) {
        case NodeKind::Document: return children(node);
        case NodeKind::Text:
        case NodeKind::HtmlInline:
        case NodeKind::HtmlBlock:
        case NodeKind::InlineMath:
        case NodeKind::Math: literal(node.literal.as_str()); break;
        case NodeKind::Paragraph:
            if (tight) return children(node);
            return wrap(node, "p"_str);
        case NodeKind::Heading: {
            const auto anchor = document_.heading_anchors.get(id.index);
            if (anchor.is_none()) return Err(String::make("Markdown heading has no anchor"_str));
            output_.push_str(
                rstd::format("<h{} id=\"{}\">", node.heading_level, **anchor).as_str());
            rstd_try(children(node));
            output_.push_str(rstd::format("</h{}>", node.heading_level).as_str());
            break;
        }
        case NodeKind::ThematicBreak: output_.push_str("<hr>"_str); break;
        case NodeKind::Code:
            output_.push_str("<code>"_str);
            literal(node.literal.as_str());
            output_.push_str("</code>"_str);
            break;
        case NodeKind::CodeBlock: {
            output_.push_str("<pre><code"_str);
            auto info = node.info.as_str();
            auto end  = usize {};
            while (end < info.len() && info[end] != u8(' ') && info[end] != u8('\t')) ++end;
            if (end != usize {}) {
                output_.push_str(" class=\"language-"_str);
                literal(info.get(usize {}, end).unwrap());
                output_.push_ascii('"');
            }
            output_.push_ascii('>');
            literal(node.literal.as_str());
            output_.push_str("</code></pre>"_str);
            break;
        }
        case NodeKind::BlockQuote: return wrap(node, "blockquote"_str);
        case NodeKind::List:
            if (node.ordered && node.list_start != 1)
                output_.push_str(rstd::format("<ol start=\"{}\">", node.list_start).as_str());
            else
                output_.push_str(node.ordered ? "<ol>"_str : "<ul>"_str);
            rstd_try(children(node, node.tight));
            output_.push_str(node.ordered ? "</ol>"_str : "</ul>"_str);
            break;
        case NodeKind::Item:
            output_.push_str("<li>"_str);
            if (node.checked.is_some())
                output_.push_str(*node.checked ? "<input type=\"checkbox\" disabled checked> "_str
                                               : "<input type=\"checkbox\" disabled> "_str);
            rstd_try(children(node, tight));
            output_.push_str("</li>"_str);
            break;
        case NodeKind::SoftBreak: output_.push_ascii(' '); break;
        case NodeKind::HardBreak: output_.push_str("<br>"_str); break;
        case NodeKind::Emphasis: return wrap(node, "em"_str);
        case NodeKind::Strong: return wrap(node, "strong"_str);
        case NodeKind::Strikethrough: return wrap(node, "del"_str);
        case NodeKind::Link:
        case NodeKind::Image: {
            const auto destination = destinations_.get(id.index);
            if (destination.is_none())
                return Err(markdown_error(source_span(document_, node.span.begin),
                                          "Markdown link has no resolved destination"_str));
            auto image = node.kind == NodeKind::Image;
            output_.push_str(image ? "<img src=\""_str : "<a href=\""_str);
            literal((**destination).as_str());
            output_.push_ascii('"');
            if (image) {
                String alt;
                append_plain_text(document_, id, alt);
                output_.push_str(" alt=\""_str);
                literal(alt.as_str());
                output_.push_ascii('"');
            }
            if (! node.title.is_empty()) {
                output_.push_str(" title=\""_str);
                literal(node.title.as_str());
                output_.push_ascii('"');
            }
            output_.push_ascii('>');
            if (! image) {
                rstd_try(children(node));
                output_.push_str("</a>"_str);
            }
            break;
        }
        case NodeKind::Table: {
            output_.push_str("<table>"_str);
            auto group = Option<bool> {};
            for (auto child : node.children) {
                auto header = syntax_node(document_, child).table_header;
                if (group.is_none() || *group != header) {
                    if (group.is_some()) output_.push_str(*group ? "</thead>"_str : "</tbody>"_str);
                    output_.push_str(header ? "<thead>"_str : "<tbody>"_str);
                    group = Some(header);
                }
                rstd_try(append(child));
            }
            if (group.is_some()) output_.push_str(*group ? "</thead>"_str : "</tbody>"_str);
            output_.push_str("</table>"_str);
            break;
        }
        case NodeKind::TableRow: return wrap(node, "tr"_str);
        case NodeKind::TableCell: {
            auto tag = node.table_header ? "th"_str : "td"_str;
            output_.push_str(rstd::format("<{}", tag).as_str());
            if (node.alignment != limarkto::TableAlignment::None) {
                auto alignment = node.alignment == limarkto::TableAlignment::Left    ? "left"_str
                                 : node.alignment == limarkto::TableAlignment::Right ? "right"_str
                                                                                     : "center"_str;
                output_.push_str(rstd::format(" style=\"text-align: {}\"", alignment).as_str());
            }
            output_.push_ascii('>');
            rstd_try(children(node));
            output_.push_str(rstd::format("</{}>", tag).as_str());
            break;
        }
        }
        return Ok(empty {});
    }

public:
    explicit BookMarkdownRenderer(const MarkdownDocument& document)
        : document_(document), destinations_(rstd::collections::BTreeMap<usize, String>::make()) {
        for (const auto& link : document.links)
            if (! link.resolved_destination.is_empty())
                destinations_.insert(usize(link.node.index.to_primitive()),
                                     link.resolved_destination.clone());
    }
    auto render() -> Result<String, String> {
        rstd_try(append(document_.syntax.root().id()));
        return Ok(rstd::move(output_));
    }
};

} // namespace lito::book
export namespace lito::book
{

auto parse_markdown(ref<rstd::path::Path> path, String source) -> Result<MarkdownDocument, String> {
    auto options       = limarkto::ParseOptions {};
    options.extensions = limarkto::Extensions::gfm();
    auto parsed        = limarkto::parse(rstd::move(source), options);
    if (parsed.is_err()) return Err(rstd::format("{}: {}", path, rstd::move(parsed).unwrap_err()));
    auto document = MarkdownDocument {
        .path            = rstd::path::PathBuf::from(path),
        .syntax          = rstd::move(parsed).unwrap(),
        .heading_anchors = rstd::collections::BTreeMap<usize, String>::make(),
    };
    auto        slugs  = rstd::collections::BTreeSet<String>::make();
    auto        counts = rstd::collections::BTreeMap<String, usize>::make();
    Vec<NodeId> pending;
    pending.push(document.syntax.root().id());
    while (! pending.is_empty()) {
        auto        id   = pending.pop().unwrap();
        const auto& node = syntax_node(document, id);
        if (node.kind == NodeKind::Heading) {
            String text;
            append_plain_text(document, id, text);
            auto base = rstd_try(heading_slug(text.as_str()));
            if (! counts.contains_key(base.as_str())) counts.insert(base.clone(), usize(1));
            auto& suffix = **counts.get_mut(base.as_str());
            auto  anchor = suffix == usize(1) ? base.clone() : rstd::format("{}-{}", base, suffix);
            while (slugs.contains(anchor.as_str())) anchor = rstd::format("{}-{}", base, ++suffix);
            ++suffix;
            slugs.insert(anchor.clone());
            document.heading_anchors.insert(usize(id.index.to_primitive()), anchor.clone());
            document.headings.push({
                .level  = usize(node.heading_level),
                .text   = rstd::move(text),
                .anchor = rstd::move(anchor),
                .span   = source_span(document, node.span.begin),
            });
        }
        if (node.kind == NodeKind::Link || node.kind == NodeKind::Image)
            document.links.push({
                .node        = id,
                .destination = node.destination.clone(),
                .image       = node.kind == NodeKind::Image,
                .span        = source_span(document, node.span.begin),
            });
        // Image alt content is rendered as text, not independent links or resources.
        if (node.kind == NodeKind::Image) continue;
        for (auto i = node.children.len(); i > usize {}; --i)
            pending.push(NodeId { node.children[i - usize(1)] });
    }
    return Ok(rstd::move(document));
}
auto render_markdown_document(const MarkdownDocument& document) -> Result<String, String> {
    return BookMarkdownRenderer(document).render();
}
auto resolve_book_content(const BookProject&    project,
                          const BookGraph&      graph,
                          Vec<MarkdownDocument> documents) -> Result<BookContent, String> {
    if (documents.len() != graph.pages.len())
        return Err(String::make("Book page documents do not match the Book graph"_str));
    auto content       = BookContent {};
    auto asset_outputs = rstd::collections::BTreeMap<String, String>::make();
    if (project.icon.is_some()) {
        const auto& icon = project.icon->asset;
        asset_outputs.insert(icon.source.as_path().to_string_lossy(), icon.output.clone());
        content.assets.push(icon.clone());
    }
    for (auto page_index = usize {}; page_index < documents.len(); ++page_index) {
        auto&       document = documents[page_index];
        const auto& page     = graph.pages[page_index];
        for (auto& item : document.links) {
            auto destination = item.destination.as_str();
            if (destination.starts_with("https://"_str) || destination.starts_with("http://"_str) ||
                (! item.image && destination.starts_with("mailto:"_str))) {
                item.resolved_destination = item.destination.clone();
                continue;
            }
            if (dangerous_scheme(destination))
                return Err(markdown_error(item.span, "unsafe link scheme"_str));
            auto split    = split_destination(destination);
            auto path     = split.template get<0>();
            auto fragment = split.template get<1>();
            if (path.is_empty()) {
                if (item.image)
                    return Err(markdown_error(item.span, "local image must have a file path"_str));
                if (! fragment.is_empty() && ! heading_exists(document, fragment))
                    return Err(markdown_error(
                        item.span, rstd::format("page has no heading '#{}'", fragment).as_str()));
                item.resolved_destination =
                    fragment.is_empty() ? String::make("#"_str) : rstd::format("#{}", fragment);
                continue;
            }
            auto local = rstd_try(resolve_local_path(project, page, path, item.span));
            if (item.image) {
                if (! fragment.is_empty())
                    return Err(
                        markdown_error(item.span, "local image must not use a fragment"_str));
                auto existing = asset_outputs.get(local.as_path().to_string_lossy().as_str());
                auto output   = String::make();
                if (existing.is_some()) {
                    output = (**existing).clone();
                } else {
                    auto name = local.as_path().file_name();
                    if (name.is_none() || name->to_str().is_none())
                        return Err(
                            markdown_error(item.span, "local image has no UTF-8 file name"_str));
                    auto digest =
                        rstd_try(licrypto::sha256_hex(local.as_path().to_string_lossy().as_str())
                                     .map_err([](licrypto::Error error) {
                                         return rstd::format("SHA-256 failed: {}", error);
                                     }));
                    output = rstd::format("assets/{}-{}",
                                          digest.as_str().get(usize {}, usize(16)).unwrap(),
                                          *name->to_str());
                    asset_outputs.insert(local.as_path().to_string_lossy(), output.clone());
                    content.assets.push(BookAsset {
                        .source = local.clone(),
                        .output = output.clone(),
                    });
                }
                item.resolved_destination = relative_url(page.output.as_str(), output.as_str());
                continue;
            }
            if (! path.ends_with(".md"_str))
                return Err(markdown_error(
                    item.span, "local links must target a .md page listed in SUMMARY.md"_str));
            auto target = page_for_path(graph, local.as_path());
            if (target.is_none())
                return Err(markdown_error(
                    item.span, "local Markdown link targets a page not listed in SUMMARY.md"_str));
            if (! fragment.is_empty() && ! heading_exists(documents[*target], fragment))
                return Err(markdown_error(
                    item.span,
                    rstd::format("target page has no heading '#{}'", fragment).as_str()));
            auto resolved =
                *target == page_index
                    ? String::make("#"_str)
                    : relative_url(page.output.as_str(), graph.pages[*target].output.as_str());
            if (! fragment.is_empty()) {
                if (*target != page_index) resolved.push_ascii('#');
                resolved.push_str(fragment);
            }
            item.resolved_destination = rstd::move(resolved);
        }
    }
    for (auto page_index = usize {}; page_index < documents.len(); ++page_index) {
        auto html = rstd_try(render_markdown_document(documents[page_index]));
        content.pages.push(BookPageDocument {
            .page     = page_index,
            .document = rstd::move(documents[page_index]),
            .html     = rstd::move(html),
        });
    }
    return Ok(rstd::move(content));
}

} // namespace lito::book
