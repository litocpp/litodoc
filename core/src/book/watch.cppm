module;
#include <rstd/macro.hpp>

export module lito.book:watch;

import rstd;
import licrypto;
import lito.site;
import :model;
import :project;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::book
{

auto append_watch_root(Vec<rstd::path::PathBuf>& roots, rstd::path::PathBuf path) -> void {
    for (const auto& root : roots)
        if (path.as_path().strip_prefix(root.as_path()).is_some()) return;
    roots.push(rstd::move(path));
}

auto watch_path_excluded(const BookWatchLayout& layout, ref<rstd::path::Path> path) -> bool {
    for (const auto& excluded : layout.excluded)
        if (path.strip_prefix(excluded.as_path()).is_some()) return true;
    return false;
}

auto watch_digest(slice<u8> bytes) -> Result<String, String> {
    return licrypto::sha256_hex(bytes).map_err([](licrypto::Error error) {
        return rstd::format("cannot fingerprint Book input: {}", error);
    });
}

auto scan_watch_layout(const BookWatchLayout& layout) -> Result<BookWatchSnapshot, String> {
    auto                     entries = rstd::collections::BTreeMap<String, String>::make();
    Vec<rstd::path::PathBuf> pending;
    for (const auto& root : layout.roots) pending.push(root.clone());
    while (! pending.is_empty()) {
        auto path = pending.pop().unwrap();
        if (watch_path_excluded(layout, path.as_path())) continue;
        auto name = path.as_path().file_name();
        if (name.is_some() && name->as_encoded_bytes() == ".git"_bytes) continue;
        auto text = path.as_path().to_str();
        if (text.is_none()) return Err(String::make("Book watch input path is not UTF-8"_str));
        if (entries.contains_key(*text)) continue;
        auto metadata = rstd::fs::symlink_metadata(path.as_path());
        if (metadata.is_err()) {
            auto error = rstd::move(metadata).unwrap_err();
            if (error.kind() != rstd::io::ErrorKind { rstd::io::ErrorKind::NotFound })
                return Err(rstd::format(
                    "cannot inspect Book watch input '{}': {}", path.as_path(), error));
            entries.insert(String::make(*text), String::make("missing"_str));
            continue;
        }
        if (metadata->is_symlink()) {
            auto target = rstd::fs::read_link(path.as_path());
            if (target.is_err())
                return Err(rstd::format("cannot inspect Book watch symlink '{}': {}",
                                        path.as_path(),
                                        rstd::move(target).unwrap_err()));
            auto digest = rstd_try(watch_digest(target->as_path().as_os_str().as_encoded_bytes()));
            entries.insert(String::make(*text), rstd::format("symlink:{}", digest));
            continue;
        }
        if (metadata->is_dir()) {
            // Creating an output's previously absent parents is not a source change.
            auto excluded_parent = false;
            for (const auto& excluded : layout.excluded)
                if (excluded.as_path().strip_prefix(path.as_path()).is_some())
                    excluded_parent = true;
            if (! excluded_parent || path.as_path() == layout.root.as_path())
                entries.insert(String::make(*text), String::make("directory"_str));
            auto directory = rstd::fs::read_dir(path.as_path());
            if (directory.is_err())
                return Err(rstd::format("cannot scan Book watch directory '{}': {}",
                                        path.as_path(),
                                        rstd::move(directory).unwrap_err()));
            for (auto item = directory->next(); item.is_some(); item = directory->next()) {
                if (item->is_err())
                    return Err(rstd::format("cannot scan Book watch directory '{}': {}",
                                            path.as_path(),
                                            rstd::move(*item).unwrap_err()));
                auto entry = rstd::move(*item).unwrap();
                pending.push(entry.path());
            }
            continue;
        }
        if (metadata->is_file()) {
            auto bytes = rstd::fs::read(path.as_path());
            if (bytes.is_err()) {
                auto error = rstd::move(bytes).unwrap_err();
                if (error.kind() != rstd::io::ErrorKind { rstd::io::ErrorKind::NotFound })
                    return Err(rstd::format(
                        "cannot fingerprint Book watch input '{}': {}", path.as_path(), error));
                entries.insert(String::make(*text), String::make("missing"_str));
                continue;
            }
            auto digest = rstd_try(watch_digest(bytes->as_slice()));
            entries.insert(String::make(*text), rstd::format("file:{}", digest));
            continue;
        }
        entries.insert(String::make(*text), String::make("special"_str));
    }
    String description;
    auto   iterator = entries.iter();
    for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
        auto path  = (*(*item).template get<0>()).as_str();
        auto value = (*(*item).template get<1>()).as_str();
        description.push_str(
            rstd::format("{}:{}{}:{}", path.len(), path, value.len(), value).as_str());
    }
    return Ok(BookWatchSnapshot { rstd_try(watch_digest(description.as_str().as_bytes())) });
}

} // namespace lito::book

export namespace lito::book
{

class BookWatchSession {
    BookBuildInput           input_;
    BookWatchLayout          layout_;
    Vec<rstd::path::PathBuf> extra_excluded_;

    BookWatchSession(BookBuildInput input, BookWatchLayout layout)
        : input_(rstd::move(input)), layout_(rstd::move(layout)) {}

    auto update_layout(const BookProject& project) -> Result<empty, String> {
        auto layout = BookWatchLayout { .root = layout_.root.clone() };
        append_watch_root(layout.roots, layout.root.clone());
        append_watch_root(
            layout.roots,
            rstd_try(normalize_book_path(layout.root.as_path(), project.manifest.as_path())));
        if (input_.frontend.is_some()) append_watch_root(layout.roots, input_.frontend->clone());
        if (project.frontend.is_some()) append_watch_root(layout.roots, project.frontend->clone());
        auto publication =
            rstd_try(lito::site::publication_layout(project.output.as_path(), "book-site"_str));
        layout.excluded.push(rstd::move(publication.output));
        layout.excluded.push(rstd::move(publication.staging));
        layout.excluded.push(rstd::move(publication.backup));
        for (const auto& excluded : extra_excluded_) layout.excluded.push(excluded.clone());
        layout_ = rstd::move(layout);
        return Ok(empty {});
    }

public:
    BookWatchSession(const BookWatchSession&)                        = delete;
    BookWatchSession(BookWatchSession&&) noexcept                    = default;
    auto operator=(BookWatchSession&&) noexcept -> BookWatchSession& = default;

    static auto open(BookBuildInput input) -> Result<BookWatchSession, String> {
        auto manifest = rstd_try(find_book_manifest(input.directory.as_path()));
        auto root =
            manifest.is_some()
                ? rstd::path::PathBuf::from(manifest->as_path().parent().unwrap())
                : rstd_try(rstd::fs::canonicalize(input.directory.as_path())
                               .map_err([](rstd::io::Error error) {
                                   return rstd::format("cannot resolve Book watch root: {}", error);
                               }));
        input.directory = root.clone();
        auto layout     = BookWatchLayout { .root = root.clone() };
        append_watch_root(layout.roots, root.clone());
        if (manifest.is_some())
            append_watch_root(layout.roots,
                              rstd_try(normalize_book_path(root.as_path(), manifest->as_path())));
        if (input.frontend.is_some()) {
            input.frontend =
                Some(rstd_try(normalize_book_path(root.as_path(), input.frontend->as_path())));
            append_watch_root(layout.roots, input.frontend->clone());
        }
        if (input.output.is_some()) {
            input.output =
                Some(rstd_try(normalize_book_path(root.as_path(), input.output->as_path())));
            auto publication =
                rstd_try(lito::site::publication_layout(input.output->as_path(), "book-site"_str));
            layout.excluded.push(rstd::move(publication.output));
            layout.excluded.push(rstd::move(publication.staging));
            layout.excluded.push(rstd::move(publication.backup));
        }
        return Ok(BookWatchSession(rstd::move(input), rstd::move(layout)));
    }

    auto layout() const -> const BookWatchLayout& { return layout_; }

    auto exclude(ref<rstd::path::Path> path) -> Result<empty, String> {
        auto normalized = rstd_try(normalize_book_path(layout_.root.as_path(), path));
        if (layout_.root.as_path().strip_prefix(normalized.as_path()).is_some())
            return Err(String::make("Book watch exclusion must not contain the Book root"_str));
        for (const auto& excluded : extra_excluded_)
            if (excluded.as_path() == normalized.as_path()) return Ok(empty {});
        layout_.excluded.push(normalized.clone());
        extra_excluded_.push(rstd::move(normalized));
        return Ok(empty {});
    }

    auto scan() const -> Result<BookWatchSnapshot, String> { return scan_watch_layout(layout_); }

    auto prepare(Option<rstd::path::PathBuf>        output_override  = {},
                 Option<lito::site::FrontendBundle> default_frontend = {})
        -> Result<PreparedBook, String>;
};

} // namespace lito::book
