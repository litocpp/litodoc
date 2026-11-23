module;
#include <rstd/macro.hpp>

export module lito.book:render;

import rstd;
import lito.site;
import :model;
import :data;
import :markdown;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::book
{

auto book_template_text(ref<str> value) -> TemplateValue {
    return TemplateValue::text_value(value);
}

auto book_template_number(usize value) -> TemplateValue {
    return TemplateValue::text_value(rstd::format("{}", value));
}

auto book_asset_prefix(ref<str> output) -> String {
    auto result = String::make();
    for (auto character : output) {
        if (character == u8('/')) result.push_str("../"_str);
    }
    return result;
}

auto book_write_text(ref<rstd::path::Path> root, ref<str> relative, ref<str> contents)
    -> Result<empty, String> {
    if (! safe_frontend_path(relative))
        return Err(rstd::format("invalid Book output path '{}'", relative));
    auto path = rstd::path::PathBuf::from(root).join(rstd::path::PathBuf::from(relative).as_path());
    auto parent = path.as_path().parent();
    if (parent.is_none())
        return Err(rstd::format("Book output '{}' has no parent", path.as_path()));
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err())
        return Err(rstd::format("cannot create Book output directory '{}': {}",
                                *parent,
                                rstd::move(created).unwrap_err()));
    auto written = rstd::fs::write_atomic(path.as_path(), contents.as_bytes());
    if (written.is_err())
        return Err(rstd::format(
            "cannot write Book output '{}': {}", path.as_path(), rstd::move(written).unwrap_err()));
    return Ok(empty {});
}

auto book_copy_file(ref<rstd::path::Path> root, const BookAsset& asset) -> Result<empty, String> {
    if (! safe_frontend_path(asset.output.as_str()))
        return Err(rstd::format("invalid Book asset path '{}'", asset.output));
    auto contents = rstd::fs::read(asset.source.as_path());
    if (contents.is_err())
        return Err(rstd::format("cannot read Book asset '{}': {}",
                                asset.source.as_path(),
                                rstd::move(contents).unwrap_err()));
    auto output = rstd::path::PathBuf::from(root).join(
        rstd::path::PathBuf::from(asset.output.as_str()).as_path());
    auto parent  = output.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err())
        return Err(rstd::format("cannot create Book asset directory '{}': {}",
                                parent,
                                rstd::move(created).unwrap_err()));
    auto written = rstd::fs::write_atomic(output.as_path(), contents->as_slice());
    if (written.is_err())
        return Err(rstd::format("cannot write Book asset '{}': {}",
                                output.as_path(),
                                rstd::move(written).unwrap_err()));
    return Ok(empty {});
}

auto book_page_link(const BookGraph& graph, usize target, ref<str> current_output, ref<str> label)
    -> TemplateValue {
    auto value = TemplateValue::object_value();
    value.insert("label"_str, book_template_text(label));
    value.insert("title"_str, book_template_text(graph.pages[target].title.as_str()));
    value.insert("href"_str,
                 TemplateValue::text_value(
                     relative_url(current_output, graph.pages[target].output.as_str())));
    return value;
}

auto book_configured_link(const BookSiteLink& link, ref<str> output) -> TemplateValue {
    auto value = TemplateValue::object_value();
    value.insert("label"_str, book_template_text(link.label.as_str()));
    value.insert("href"_str, TemplateValue::text_value(relative_site_link(output, link.target)));
    return value;
}

auto book_site_value(const BookProject& project, usize page_count, ref<str> output)
    -> TemplateValue {
    auto site = site_shell_context();
    site.insert("title"_str, book_template_text(project.title.as_str()));
    site.insert("page_count"_str, book_template_number(page_count));
    site.insert("has_version"_str, TemplateValue::boolean_value(project.version.is_some()));
    site.insert(
        "version"_str,
        book_template_text(project.version.is_some() ? project.version->as_str() : ref<str> {}));
    site.insert("has_language"_str, TemplateValue::boolean_value(project.language.is_some()));
    site.insert(
        "language"_str,
        book_template_text(project.language.is_some() ? project.language->as_str() : "en"_str));
    const auto& html = project.html;
    site.insert("has_custom_brand"_str, TemplateValue::boolean_value(html.brand.is_some()));
    site.insert("has_default_brand"_str, TemplateValue::boolean_value(html.brand.is_none()));
    if (html.brand.is_some()) site.insert("brand"_str, book_configured_link(*html.brand, output));
    site.insert("has_repository"_str, TemplateValue::boolean_value(html.repository.is_some()));
    site.insert(
        "has_site_links"_str,
        TemplateValue::boolean_value(html.repository.is_some() || ! html.navigation.is_empty()));
    if (html.repository.is_some())
        site.insert("repository_url"_str, book_template_text(html.repository->href.as_str()));
    auto navigation = TemplateValue::array_value();
    for (const auto& link : html.navigation)
        navigation.array.push(book_configured_link(link, output));
    site.insert("has_navigation"_str, TemplateValue::boolean_value(! html.navigation.is_empty()));
    site.insert("navigation"_str, rstd::move(navigation));
    site.insert(
        "has_footer"_str,
        TemplateValue::boolean_value(html.footer_text.is_some() || ! html.footer_links.is_empty()));
    site.insert("has_footer_text"_str, TemplateValue::boolean_value(html.footer_text.is_some()));
    if (html.footer_text.is_some())
        site.insert("footer_text"_str, book_template_text(html.footer_text->as_str()));
    site.insert("has_footer_links"_str,
                TemplateValue::boolean_value(! html.footer_links.is_empty()));
    auto footer_links = TemplateValue::array_value();
    for (const auto& link : html.footer_links)
        footer_links.array.push(book_configured_link(link, output));
    site.insert("footer_links"_str, rstd::move(footer_links));
    return site;
}

auto book_navigation(const BookGraph& graph, ref<str> current_output) -> TemplateValue {
    auto navigation = TemplateValue::object_value();
    navigation.insert("has_packages"_str, TemplateValue::boolean_value(false));
    navigation.insert("packages"_str, TemplateValue::array_value());
    navigation.insert("show_modules"_str, TemplateValue::boolean_value(false));
    navigation.insert("modules"_str, TemplateValue::array_value());
    navigation.insert("has_scroll_state"_str, TemplateValue::boolean_value(true));
    navigation.insert(
        "scroll_state_url"_str,
        TemplateValue::text_value(relative_url(current_output, "data/book.json"_str)));
    navigation.insert("has_pages"_str, TemplateValue::boolean_value(! graph.pages.is_empty()));
    auto pages = TemplateValue::array_value();
    for (const auto& page : graph.pages) {
        auto item = TemplateValue::object_value();
        item.insert("title"_str, book_template_text(page.title.as_str()));
        item.insert("href"_str,
                    TemplateValue::text_value(relative_url(current_output, page.output.as_str())));
        item.insert("depth"_str, book_template_number(page.breadcrumb.len() - usize(1)));
        item.insert("is_root"_str, TemplateValue::boolean_value(page.parent.is_none()));
        item.insert("has_children"_str, TemplateValue::boolean_value(! page.children.is_empty()));
        pages.array.push(rstd::move(item));
    }
    navigation.insert("pages"_str, rstd::move(pages));
    return navigation;
}

auto book_page_context(const BookProject&     project,
                       const BookGraph&       graph,
                       const BookDatasetPage* page,
                       usize                  page_index,
                       ref<str>               output,
                       ref<str>               digest) -> TemplateValue {
    auto context = TemplateValue::object_value();
    context.insert("site"_str, book_site_value(project, graph.pages.len(), output));
    auto page_value = page_shell_context();
    auto title      = page == nullptr ? project.title.as_str() : page->title.as_str();
    page_value.insert("title"_str, book_template_text(title));
    page_value.insert("kind"_str, book_template_text("book"_str));
    page_value.insert("asset_prefix"_str, TemplateValue::text_value(book_asset_prefix(output)));
    page_value.insert("icon_url"_str,
                      TemplateValue::text_value(
                          relative_url(output,
                                       project.icon.is_some() ? project.icon->asset.output.as_str()
                                                              : "static/favicon.svg"_str)));
    page_value.insert("icon_media_type"_str,
                      book_template_text(project.icon.is_some() ? project.icon->media_type.as_str()
                                                                : "image/svg+xml"_str));
    page_value.insert("search_package"_str, book_template_text(ref<str> {}));
    page_value.insert("search_module"_str, book_template_text(ref<str> {}));
    page_value.insert("is_api"_str, TemplateValue::boolean_value(false));
    page_value.insert("is_book"_str, TemplateValue::boolean_value(true));
    auto outline = TemplateValue::array_value();
    if (page != nullptr) {
        for (const auto& heading : page->headings) {
            if (heading.level == usize(1)) continue;
            auto item = TemplateValue::object_value();
            item.insert("href"_str,
                        TemplateValue::text_value(rstd::format("#{}", heading.anchor.as_str())));
            item.insert("label"_str, book_template_text(heading.text.as_str()));
            item.insert("level"_str, book_template_number(heading.level));
            outline.array.push(rstd::move(item));
        }
    }
    page_value.insert("has_outline"_str, TemplateValue::boolean_value(! outline.array.is_empty()));
    page_value.insert("outline"_str, rstd::move(outline));
    context.insert("page"_str, rstd::move(page_value));
    context.insert("navigation"_str, book_navigation(graph, output));
    auto book = TemplateValue::object_value();
    book.insert("data_digest"_str, book_template_text(digest));
    book.insert("has_page"_str, TemplateValue::boolean_value(page != nullptr));
    book.insert("title"_str, book_template_text(title));
    book.insert("content"_str,
                TemplateValue::trusted_html(page == nullptr ? String::make() : page->html.clone()));
    auto breadcrumbs = TemplateValue::array_value();
    if (page != nullptr) {
        for (auto item : graph.pages[page_index].breadcrumb) {
            breadcrumbs.array.push(
                book_page_link(graph, item, output, graph.pages[item].title.as_str()));
        }
    }
    book.insert("has_breadcrumbs"_str,
                TemplateValue::boolean_value(breadcrumbs.array.len() > usize(1)));
    book.insert("breadcrumbs"_str, rstd::move(breadcrumbs));
    auto roots = TemplateValue::array_value();
    for (auto root : graph.roots)
        roots.array.push(book_page_link(graph, root, output, graph.pages[root].title.as_str()));
    book.insert("roots"_str, rstd::move(roots));
    auto has_previous = page != nullptr && graph.pages[page_index].previous.is_some();
    auto has_next     = page != nullptr && graph.pages[page_index].next.is_some();
    book.insert("has_previous"_str, TemplateValue::boolean_value(has_previous));
    book.insert("has_next"_str, TemplateValue::boolean_value(has_next));
    book.insert("previous"_str,
                has_previous ? book_page_link(
                                   graph, *graph.pages[page_index].previous, output, "Previous"_str)
                             : TemplateValue::object_value());
    book.insert("next"_str,
                has_next ? book_page_link(graph, *graph.pages[page_index].next, output, "Next"_str)
                         : TemplateValue::object_value());
    context.insert("book"_str, rstd::move(book));
    return context;
}

auto final_book_data_summary(ref<rstd::path::Path> output,
                             const BookDataset&    dataset,
                             ref<str>              digest) -> BookDataSummary {
    auto data = BookDataSummary {
        .root     = rstd::path::PathBuf::from(output),
        .manifest = rstd::path::PathBuf::from(output).join(
            rstd::path::PathBuf::from("data/book.json"_str).as_path()),
        .digest = String::make(digest),
    };
    for (const auto& page : dataset.pages) {
        data.pages.push(BookDataPageSummary {
            .identity = page.identity.clone(),
            .json     = rstd::path::PathBuf::from(output).join(
                rstd::path::PathBuf::from(
                    rstd::format("data/pages/{}.json", page.identity.as_str()))
                    .as_path()),
        });
    }
    return data;
}

} // namespace lito::book

export namespace lito::book
{

auto publish_book_site(const BookProject&                   project,
                       const BookGraph&                     graph,
                       const BookContent&                   content,
                       const BookDataset&                   dataset,
                       const FrontendBundle&                frontend,
                       const PreparedBook::HtmlPublication& html) -> Result<BookSummary, String> {
    if (! frontend.supports_book)
        return Err(rstd::format("frontend '{}' does not support Book documentation",
                                frontend.identity.as_str()));
    auto publication = begin_publication(project.output.as_path(), "book-site"_str);
    if (publication.is_err()) return Err(rstd::move(publication).unwrap_err());
    auto fail = [&publication](String error) -> Result<BookSummary, String> {
        (void)abort_publication(*publication);
        return Err(rstd::move(error));
    };
    auto data = publish_book_dataset(publication->staging.as_path(), dataset);
    if (data.is_err()) return fail(rstd::move(data).unwrap_err());
    for (const auto& asset : frontend.assets) {
        auto written = book_write_text(
            publication->staging.as_path(), asset.path.as_str(), asset.contents.as_str());
        if (written.is_err()) return fail(rstd::move(written).unwrap_err());
    }
    for (const auto& asset : content.assets) {
        auto copied = book_copy_file(publication->staging.as_path(), asset);
        if (copied.is_err()) return fail(rstd::move(copied).unwrap_err());
    }
    for (const auto& entry : html.pages) {
        const auto* page =
            entry.chapter.is_some() ? rstd::addressof(dataset.pages[*entry.chapter]) : nullptr;
        auto index   = entry.chapter.is_some() ? *entry.chapter : usize {};
        auto context = book_page_context(
            project, graph, page, index, entry.output.as_str(), data->digest.as_str());
        auto values = context.object.get_mut("page"_str).unwrap();
        values->insert("has_description"_str,
                       TemplateValue::boolean_value(entry.description.is_some()));
        if (entry.description.is_some())
            values->insert("description"_str, book_template_text(entry.description->as_str()));
        values->insert("has_canonical"_str,
                       TemplateValue::boolean_value(entry.canonical.is_some()));
        if (entry.canonical.is_some())
            values->insert("canonical_url"_str, book_template_text(entry.canonical->as_str()));
        values->insert("has_edit_url"_str, TemplateValue::boolean_value(entry.edit_url.is_some()));
        if (entry.edit_url.is_some())
            values->insert("edit_url"_str, book_template_text(entry.edit_url->as_str()));
        auto   template_path = entry.output.as_str() == "index.html"_str
                                   ? frontend.book_root_template.as_str()
                                   : frontend.book_page_template.as_str();
        String rendered;
        if (entry.redirect.is_some()) {
            auto target =
                escape_html(relative_site_link(entry.output.as_str(), *entry.redirect).as_str());
            rendered = rstd::format(
                "<!doctype html><html lang=\"{}\"><head><meta charset=\"utf-8\"><meta "
                "name=\"robots\" content=\"noindex\"><title>Page moved</title><meta "
                "http-equiv=\"refresh\" content=\"0;url={}\">",
                escape_html(project.language.is_some() ? project.language->as_str() : "en"_str),
                target);
            if (entry.canonical.is_some())
                rendered.push_str(rstd::format("<link rel=\"canonical\" href=\"{}\">",
                                               escape_html(entry.canonical->as_str()))
                                      .as_str());
            rendered.push_str(
                rstd::format("</head><body><p>This page has moved. <a href=\"{}\">Continue to the "
                             "current page</a>.</p></body></html>\n",
                             target)
                    .as_str());
        } else {
            if (entry.not_found) {
                values->insert("title"_str, book_template_text("Page not found"_str));
                values->insert("is_not_found"_str, TemplateValue::boolean_value(true));
                values->insert("noindex"_str, TemplateValue::boolean_value(true));
                values->insert("base_href"_str, book_template_text(html.not_found_base.as_str()));
                template_path = frontend.book_not_found_template.as_str();
            }
            if (entry.not_found && template_path.is_empty()) {
                rendered = rstd::format(
                    "<!doctype html><html lang=\"{}\"><head><meta charset=\"utf-8\"><base "
                    "href=\"{}\"><meta name=\"robots\" content=\"noindex\"><title>Page not "
                    "found</title></head><body><h1>Page not found</h1><p><a "
                    "href=\"index.html\">Return to {}</a></p></body></html>\n",
                    escape_html(project.language.is_some() ? project.language->as_str() : "en"_str),
                    escape_html(html.not_found_base.as_str()),
                    escape_html(project.title.as_str()));
            } else {
                auto result = render_template(frontend.templates, template_path, context);
                if (result.is_err()) return fail(rstd::move(result).unwrap_err());
                rendered = rstd::move(result).unwrap();
            }
        }
        auto written = book_write_text(
            publication->staging.as_path(), entry.output.as_str(), rendered.as_str());
        if (written.is_err()) return fail(rstd::move(written).unwrap_err());
    }
    auto committed = commit_publication(*publication);
    if (committed.is_err()) return Err(rstd::move(committed).unwrap_err());
    return Ok(BookSummary {
        .output = project.output.clone(),
        .index  = project.output.join(rstd::path::PathBuf::from("index.html"_str).as_path()),
        .data   = final_book_data_summary(project.output.as_path(), dataset, data->digest.as_str()),
        .pages  = dataset.pages.len(),
        .site_generated = true,
    });
}

} // namespace lito::book
