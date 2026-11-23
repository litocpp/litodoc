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
    auto site = site_data_context();
    site.insert("title"_str, book_template_text(project.title.as_str()));
    site.insert("page_count"_str, book_template_number(page_count));
    if (project.version.is_some())
        site.insert("version"_str, book_template_text(project.version->as_str()));
    if (project.description.is_some())
        site.insert("description"_str, book_template_text(project.description->as_str()));
    site.insert(
        "language"_str,
        book_template_text(project.language.is_some() ? project.language->as_str() : "en"_str));
    const auto& html = project.html;
    if (html.brand.is_some()) site.insert("brand"_str, book_configured_link(*html.brand, output));
    if (html.repository.is_some()) {
        auto repository = TemplateValue::object_value();
        repository.insert("href"_str, book_template_text(html.repository->href.as_str()));
        site.insert("repository"_str, rstd::move(repository));
    }
    auto navigation = TemplateValue::array_value();
    for (const auto& link : html.navigation)
        navigation.array.push(book_configured_link(link, output));
    site.insert("navigation"_str, rstd::move(navigation));
    if (html.footer_text.is_some() || ! html.footer_links.is_empty()) {
        auto footer = TemplateValue::object_value();
        footer.insert("text"_str,
                      html.footer_text.is_some() ? book_template_text(html.footer_text->as_str())
                                                 : TemplateValue::null());
        auto links = TemplateValue::array_value();
        for (const auto& link : html.footer_links)
            links.array.push(book_configured_link(link, output));
        footer.insert("links"_str, rstd::move(links));
        site.insert("footer"_str, rstd::move(footer));
    }
    return site;
}

auto book_navigation(const BookGraph& graph, ref<str> current_output) -> TemplateValue {
    auto navigation = TemplateValue::object_value();
    navigation.insert(
        "data_url"_str,
        TemplateValue::text_value(relative_url(current_output, "data/book.json"_str)));
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
    auto page_value = page_data_context();
    auto title      = page == nullptr ? project.title.as_str() : page->title.as_str();
    page_value.insert("title"_str, book_template_text(title));
    page_value.insert(
        "kind"_str,
        book_template_text(output == "index.html"_str ? "book-root"_str : "book-page"_str));
    page_value.insert("output"_str, book_template_text(output));
    page_value.insert("url"_str, TemplateValue::text_value(encode_site_path(output)));
    page_value.insert("root_url"_str,
                      TemplateValue::text_value(relative_url(output, "index.html"_str)));
    page_value.insert(
        "content"_str,
        TemplateValue::trusted_html(page == nullptr ? String {} : page->html.clone()));
    page_value.insert("asset_prefix"_str, TemplateValue::text_value(book_asset_prefix(output)));
    if (project.icon.is_some()) {
        page_value.insert(
            "icon_url"_str,
            TemplateValue::text_value(relative_url(output, project.icon->asset.output.as_str())));
        page_value.insert("icon_media_type"_str,
                          book_template_text(project.icon->media_type.as_str()));
    }
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
    page_value.insert("outline"_str, rstd::move(outline));
    context.insert("page"_str, rstd::move(page_value));
    context.insert("navigation"_str, book_navigation(graph, output));
    auto book = TemplateValue::object_value();
    book.insert("data_digest"_str, book_template_text(digest));
    book.insert("title"_str, book_template_text(project.title.as_str()));
    auto breadcrumbs = TemplateValue::array_value();
    auto ancestors   = TemplateValue::array_value();
    if (page != nullptr) {
        for (auto item : graph.pages[page_index].breadcrumb) {
            if (item != page_index)
                ancestors.array.push(
                    book_page_link(graph, item, output, graph.pages[item].title.as_str()));
            breadcrumbs.array.push(
                book_page_link(graph, item, output, graph.pages[item].title.as_str()));
        }
    }
    book.insert("ancestors"_str, rstd::move(ancestors));
    book.insert("breadcrumbs"_str, rstd::move(breadcrumbs));
    auto roots = TemplateValue::array_value();
    for (auto root : graph.roots)
        roots.array.push(book_page_link(graph, root, output, graph.pages[root].title.as_str()));
    book.insert("roots"_str, rstd::move(roots));
    auto has_previous = page != nullptr && graph.pages[page_index].previous.is_some();
    auto has_next     = page != nullptr && graph.pages[page_index].next.is_some();
    book.insert("previous"_str,
                has_previous ? book_page_link(
                                   graph, *graph.pages[page_index].previous, output, "Previous"_str)
                             : TemplateValue::null());
    book.insert("next"_str,
                has_next ? book_page_link(graph, *graph.pages[page_index].next, output, "Next"_str)
                         : TemplateValue::null());
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
    auto valid = validate_book_theme(frontend);
    if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
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
        if (entry.source.is_some())
            values->insert("source"_str, book_template_text(entry.source->as_str()));
        if (entry.description.is_some())
            values->insert("description"_str, book_template_text(entry.description->as_str()));
        if (entry.canonical.is_some())
            values->insert("canonical_url"_str, book_template_text(entry.canonical->as_str()));
        if (entry.edit_url.is_some())
            values->insert("edit_url"_str, book_template_text(entry.edit_url->as_str()));
        auto template_path = entry.output.as_str() == "index.html"_str
                                 ? frontend.book_root_template.as_str()
                                 : frontend.book_page_template.as_str();
        if (entry.redirect.is_some()) {
            auto redirect = TemplateValue::object_value();
            redirect.insert("href"_str,
                            TemplateValue::text_value(
                                relative_site_link(entry.output.as_str(), *entry.redirect)));
            values->insert("redirect"_str, rstd::move(redirect));
            values->insert("kind"_str, book_template_text("redirect"_str));
            values->insert("noindex"_str, TemplateValue::boolean_value(true));
            template_path = frontend.book_redirect_template.as_str();
        } else if (entry.not_found) {
            values->insert("kind"_str, book_template_text("not-found"_str));
            values->insert("not_found"_str, TemplateValue::boolean_value(true));
            values->insert("noindex"_str, TemplateValue::boolean_value(true));
            values->insert("base_href"_str, book_template_text(html.not_found_base.as_str()));
            template_path = frontend.book_not_found_template.as_str();
        }
        auto rendered = render_template(frontend.templates, template_path, context);
        if (rendered.is_err()) return fail(rstd::move(rendered).unwrap_err());
        auto written = book_write_text(
            publication->staging.as_path(), entry.output.as_str(), rendered->as_str());
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
