#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.book;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto child(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto write_text(ref<rstd::path::Path> root, ref<str> relative, ref<str> text)
    -> rstd::io::Result<empty> {
    auto path    = child(root, relative);
    auto created = rstd::fs::create_dir_all(path.as_path().parent().unwrap());
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    return rstd::fs::write_atomic(path.as_path(), text.as_bytes());
}

auto make_frontend(ref<rstd::path::Path> root) -> rstd::io::Result<empty> {
    auto written = write_text(root,
                              "frontend.json"_str,
                              "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,"
                              "\"book-data-api\":1,\"capabilities\":[\"book\"],\"templates\":{"
                              "\"book-root\":\"root.html\",\"book-page\":\"page.html\"},"
                              "\"partials\":[],\"assets\":[]}"_str);
    if (written.is_err()) return written;
    written = write_text(root, "root.html"_str, "<!doctype html>{{{book.content}}}"_str);
    if (written.is_err()) return written;
    return write_text(root, "page.html"_str, "<!doctype html>{{{book.content}}}"_str);
}

auto make_book(ref<rstd::path::Path> root) -> rstd::io::Result<empty> {
    auto written = write_text(root,
                              "book.toml"_str,
                              "[book]\nname='watch-book'\ntitle='Watch Book'\n"
                              "[output.html]\nfrontend='frontend'\n"_str);
    if (written.is_err()) return written;
    written = write_text(root, "src/SUMMARY.md"_str, "# Summary\n\n- [Home](index.md)\n"_str);
    if (written.is_err()) return written;
    written = write_text(root, "src/index.md"_str, "# First\n"_str);
    if (written.is_err()) return written;
    return make_frontend(child(root, "frontend"_str).as_path());
}

} // namespace

TEST(BookWatch, FingerprintsContentsAndDirectoryMembership) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(make_book(root).is_ok());
    auto opened = lito::book::BookWatchSession::open({ .directory = PathBuf::from(root) });
    ASSERT_TRUE(opened.is_ok());
    auto session = rstd::move(opened).unwrap();
    ASSERT_TRUE(session.prepare().is_ok());
    auto first = session.scan();
    ASSERT_TRUE(first.is_ok());
    auto path     = child(root, "src/index.md"_str);
    auto metadata = rstd::fs::metadata(path.as_path());
    ASSERT_TRUE(metadata.is_ok());
    auto modified = metadata->modified();
    ASSERT_TRUE(modified.is_ok());
    ASSERT_TRUE(write_text(root, "src/index.md"_str, "# Other\n"_str).is_ok());
    auto file = rstd::fs::File::options().write(true).open(path.as_path());
    ASSERT_TRUE(file.is_ok());
    ASSERT_TRUE(file->set_modified(rstd::move(modified).unwrap()).is_ok());
    auto changed = session.scan();
    ASSERT_TRUE(changed.is_ok());
    EXPECT_NE(changed->digest.as_str(), first->digest.as_str());
    ASSERT_TRUE(write_text(root, "src/new/image.svg"_str, "<svg/>"_str).is_ok());
    auto added = session.scan();
    ASSERT_TRUE(added.is_ok());
    EXPECT_NE(added->digest.as_str(), changed->digest.as_str());
    ASSERT_TRUE(rstd::fs::remove_file(child(root, "src/new/image.svg"_str).as_path()).is_ok());
    auto removed = session.scan();
    ASSERT_TRUE(removed.is_ok());
    EXPECT_NE(removed->digest.as_str(), added->digest.as_str());
}

TEST(BookWatch, ExcludesPublicationAndStablePreviewRoot) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(make_book(root).is_ok());
    auto opened = lito::book::BookWatchSession::open({ .directory = PathBuf::from(root) });
    ASSERT_TRUE(opened.is_ok());
    auto session = rstd::move(opened).unwrap();
    ASSERT_TRUE(session.exclude(child(root, "preview"_str).as_path()).is_ok());
    auto prepared = session.prepare();
    ASSERT_TRUE(prepared.is_ok());
    auto before = session.scan();
    ASSERT_TRUE(before.is_ok());
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    ASSERT_TRUE(
        write_text(root, "build/.book.lito-book-site-staging/noise"_str, "staging"_str).is_ok());
    ASSERT_TRUE(
        write_text(root, "build/.book.lito-book-site-backup/noise"_str, "backup"_str).is_ok());
    ASSERT_TRUE(write_text(root, "preview/generation-a/index.html"_str, "preview"_str).is_ok());
    ASSERT_TRUE(write_text(root, ".git/index"_str, "git"_str).is_ok());
    auto after = session.scan();
    ASSERT_TRUE(after.is_ok());
    EXPECT_EQ(after->digest.as_str(), before->digest.as_str());
    auto count      = session.layout().excluded.len();
    auto generation = session.prepare(Some(child(root, "preview/generation-b"_str)));
    ASSERT_TRUE(generation.is_ok());
    EXPECT_EQ(session.layout().excluded.len(), count);
    EXPECT_EQ(generation->project.output.as_path(),
              child(root, "preview/generation-b"_str).as_path());
    EXPECT_EQ(prepared->project.output.as_path(), child(root, "build/book"_str).as_path());
    ASSERT_TRUE(lito::book::publish(*generation).is_ok());
    auto published = session.scan();
    ASSERT_TRUE(published.is_ok());
    EXPECT_EQ(published->digest.as_str(), before->digest.as_str());
}

TEST(BookWatch, RecoversMissingManifestAndMissingChapter) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root   = temporary->path();
    auto opened = lito::book::BookWatchSession::open({ .directory = PathBuf::from(root) });
    ASSERT_TRUE(opened.is_ok());
    auto session = rstd::move(opened).unwrap();
    EXPECT_EQ(session.layout().root.as_path(), root);
    EXPECT_TRUE(session.prepare().is_err());
    auto empty = session.scan();
    ASSERT_TRUE(empty.is_ok());
    ASSERT_TRUE(make_book(root).is_ok());
    auto created = session.scan();
    ASSERT_TRUE(created.is_ok());
    EXPECT_NE(created->digest.as_str(), empty->digest.as_str());
    auto prepared = session.prepare();
    ASSERT_TRUE(prepared.is_ok());
    ASSERT_TRUE(lito::book::publish(*prepared).is_ok());
    auto index = rstd::fs::read_to_string(child(root, "build/book/index.html"_str).as_path());
    ASSERT_TRUE(index.is_ok());
    ASSERT_TRUE(write_text(root,
                           "src/SUMMARY.md"_str,
                           "# Summary\n\n- [Home](index.md)\n- [Next](next.md)\n"_str)
                    .is_ok());
    EXPECT_TRUE(session.prepare().is_err());
    auto preserved = rstd::fs::read_to_string(child(root, "build/book/index.html"_str).as_path());
    ASSERT_TRUE(preserved.is_ok());
    EXPECT_EQ(preserved->as_str(), index->as_str());
    auto failed = session.scan();
    ASSERT_TRUE(failed.is_ok());
    ASSERT_TRUE(write_text(root, "src/next.md"_str, "# Next\n"_str).is_ok());
    auto repaired = session.scan();
    ASSERT_TRUE(repaired.is_ok());
    EXPECT_NE(repaired->digest.as_str(), failed->digest.as_str());
    EXPECT_TRUE(session.prepare().is_ok());
}

TEST(BookWatch, TracksMissingExternalFrontendAndNewFiles) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(make_book(root).is_ok());
    auto outside = rstd::test::TempDir::make();
    ASSERT_TRUE(outside.is_ok());
    auto frontend = child(outside->path(), "frontend"_str);
    auto opened   = lito::book::BookWatchSession::open({
        .directory = PathBuf::from(root),
        .frontend  = Some(frontend.clone()),
    });
    ASSERT_TRUE(opened.is_ok());
    auto session = rstd::move(opened).unwrap();
    EXPECT_TRUE(session.prepare().is_err());
    auto missing = session.scan();
    ASSERT_TRUE(missing.is_ok());
    ASSERT_TRUE(make_frontend(frontend.as_path()).is_ok());
    auto created = session.scan();
    ASSERT_TRUE(created.is_ok());
    EXPECT_NE(created->digest.as_str(), missing->digest.as_str());
    EXPECT_TRUE(session.prepare().is_ok());
    ASSERT_TRUE(write_text(frontend.as_path(), "unexpected.css"_str, "body{}"_str).is_ok());
    auto added = session.scan();
    ASSERT_TRUE(added.is_ok());
    EXPECT_NE(added->digest.as_str(), created->digest.as_str());
    EXPECT_TRUE(session.prepare().is_err());
    ASSERT_TRUE(
        rstd::fs::remove_file(child(frontend.as_path(), "unexpected.css"_str).as_path()).is_ok());
    EXPECT_TRUE(session.prepare().is_ok());
}

TEST(BookWatch, PublicationLayoutDoesNotMutateAndProtectsReservedPaths) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(write_text(root, ".out.lito-book-site-staging/keep"_str, "keep"_str).is_ok());
    auto layout = lito::site::publication_layout(child(root, "out"_str).as_path(), "book-site"_str);
    ASSERT_TRUE(layout.is_ok());
    EXPECT_TRUE(
        rstd::fs::exists(child(root, ".out.lito-book-site-staging/keep"_str).as_path()).unwrap());
    ASSERT_TRUE(make_book(root).is_ok());
    ASSERT_TRUE(
        write_text(
            root,
            "book.toml"_str,
            "[book]\nname='watch-book'\ntitle='Watch Book'\nsrc='.out.lito-book-site-staging'\n"
            "[build]\noutput='out'\n[output.html]\nfrontend='frontend'\n"_str)
            .is_ok());
    ASSERT_TRUE(write_text(root,
                           ".out.lito-book-site-staging/SUMMARY.md"_str,
                           "# Summary\n\n- [Home](index.md)\n"_str)
                    .is_ok());
    ASSERT_TRUE(
        write_text(root, ".out.lito-book-site-staging/index.md"_str, "# Keep\n"_str).is_ok());
    EXPECT_TRUE(lito::book::prepare({ .directory = PathBuf::from(root) }).is_err());
    EXPECT_TRUE(
        rstd::fs::exists(child(root, ".out.lito-book-site-staging/keep"_str).as_path()).unwrap());
}

TEST(BookWatch, PublishRevalidatesMutatedPreparedOutput) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();
    ASSERT_TRUE(make_book(root).is_ok());
    auto prepared = lito::book::prepare({ .directory = PathBuf::from(root) });
    ASSERT_TRUE(prepared.is_ok());
    prepared->project.output = PathBuf::from(root);
    EXPECT_TRUE(lito::book::publish(*prepared).is_err());
    EXPECT_TRUE(rstd::fs::exists(child(root, "book.toml"_str).as_path()).unwrap());
    EXPECT_TRUE(rstd::fs::exists(child(root, "src/index.md"_str).as_path()).unwrap());
}
