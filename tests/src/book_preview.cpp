#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#include <fcntl.h>
#endif

import lito.book.preview;

using namespace rstd::prelude;
using namespace rstd::literals;
using lito::book::preview::PreviewState;
using lito::book::preview::PreviewReclaimer;
using rstd::bytes::Bytes;
using rstd::bytes::BytesMut;
using rstd::sync::Arc;
using rstd::task::Context;
using rstd::task::Poll;

namespace
{
struct PreviewWire {
    Bytes    input;
    BytesMut output;
};
struct PreviewIO {
    Arc<PreviewWire> wire;
    auto             poll_read(mut_ref<PreviewIO> self, Context&, BytesMut& output)
        -> Poll<rstd::io::Result<usize>> {
        auto input = rstd::exchange(self->wire->input, Bytes {});
        output.put_slice(input.as_slice());
        return Poll<rstd::io::Result<usize>>::Ready(Ok(input.len()));
    }
    auto poll_write(mut_ref<PreviewIO> self, Context&, const Bytes& input)
        -> Poll<rstd::io::Result<usize>> {
        self->wire->output.put_slice(input.as_slice());
        return Poll<rstd::io::Result<usize>>::Ready(Ok(input.len()));
    }
    auto poll_flush(mut_ref<PreviewIO>, Context&) -> Poll<rstd::io::Result<empty>> {
        return Poll<rstd::io::Result<empty>>::Ready(Ok(empty {}));
    }
    auto poll_shutdown(mut_ref<PreviewIO>, Context&) -> Poll<rstd::io::Result<empty>> {
        return Poll<rstd::io::Result<empty>>::Ready(Ok(empty {}));
    }
};

auto preview_exchange(rstd::async::Runtime&      runtime,
                      const liserveto::Endpoint& endpoint,
                      ref<str>                   path,
                      ref<str>                   method  = "GET"_str,
                      ref<str>                   headers = ""_str) -> String {
    auto wire    = Arc<PreviewWire>::make();
    auto request = rstd::format(
        "{} {} HTTP/1.1\r\nHost: preview\r\nConnection: close\r\n{}\r\n", method, path, headers);
    wire->input = Bytes::copy_from_slice(request.as_str().as_bytes());
    EXPECT_TRUE(runtime.block_on(
        liserveto::serve_connection(PreviewIO { wire.clone() },
                                    Arc<liserveto::Endpoint>::make(endpoint.clone()),
                                    liserveto::Shutdown::make().unwrap(),
                                    {})));
    return String::make(rstd::str_::from_utf8(wire->output.as_slice()).unwrap());
}

auto preview_request(ref<str> path, const liserveto::Shutdown& shutdown) -> liserveto::Request {
    lihttpto::RequestHeadDecoder decoder;
    auto text = rstd::format("GET {} HTTP/1.1\r\nHost: preview\r\n\r\n", path);
    decoder.feed(text.as_str().as_bytes()).unwrap();
    return liserveto::Request(decoder.take().unwrap(),
                              liserveto::IncomingBody::from_body(liserveto::Body {}),
                              shutdown.clone());
}

auto next_event(liserveto::Body& body) -> rstd::async::coro<String> {
    auto item = co_await rstd::stream::next(body);
    if (item.is_none() || item->is_err() || ! (**item).is_Data()) co_return String {};
    co_return String::make(rstd::str_::from_utf8((**item).as_Data().bytes.as_slice()).unwrap());
}

#if RSTD_OS_LINUX || (RSTD_OS_MACOS && defined(O_RESOLVE_BENEATH) && defined(O_NOFOLLOW_ANY))
auto preview_directory(ref<str> html) -> rstd::fs::TempDir {
    auto parent = rstd::env::temp_dir();
    auto temporary =
        rstd::fs::TempDir::make_in(parent.as_path(), "litobook-preview-test"_str).unwrap();
    auto site = rstd::path::PathBuf::from(temporary.path()).join("site"_str);
    rstd::fs::create_dir_all(site.join("nested"_str).as_path()).unwrap();
    rstd::fs::write(site.join("index.html"_str).as_path(), html.as_bytes()).unwrap();
    rstd::fs::write(site.join("nested/index.html"_str).as_path(), html.as_bytes()).unwrap();
    rstd::fs::write(site.join("style.css"_str).as_path(), "body{color:red}"_bytes).unwrap();
    rstd::fs::write(site.join(".env"_str).as_path(), "secret"_bytes).unwrap();
    rstd::fs::write(rstd::path::PathBuf::from(temporary.path()).join("outside.txt"_str).as_path(),
                    "outside"_bytes)
        .unwrap();
    rstd::fs::soft_link("../outside.txt"_str, site.join("link"_str).as_path()).unwrap();
    return temporary;
}
#endif
} // namespace

TEST(BookPreview, InitialPageAndClientUseNoStoreWithoutPublishing) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    EXPECT_TRUE(PreviewState::create(String {}, reclaimer).is_err());
    auto state    = PreviewState::create("console.log('preview')"_Str, reclaimer).unwrap();
    auto endpoint = state.endpoint();
    auto runtime  = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto page     = preview_exchange(runtime, endpoint, "/chapter/"_str);
    EXPECT_EQ(state.generation(), u64());
    EXPECT_TRUE(page.as_str().contains("Waiting for the first successful build."_str));
    EXPECT_TRUE(page.as_str().contains("cache-control: no-store\r\n"_str));
    EXPECT_TRUE(page.as_str().contains(
        "<script defer src=\"/__litobook/client.js\" data-generation=\"0\"></script>"_str));
    auto client = preview_exchange(runtime, endpoint, "/__litobook/client.js?v=1"_str);
    EXPECT_TRUE(client.as_str().contains("content-type: text/javascript; charset=utf-8\r\n"_str));
    EXPECT_TRUE(client.as_str().ends_with("\r\n\r\nconsole.log('preview')"_str));
    EXPECT_TRUE(preview_exchange(runtime, endpoint, "/__litobook/client.js"_str, "HEAD"_str)
                    .as_str()
                    .ends_with("\r\n\r\n"_str));
    EXPECT_TRUE(preview_exchange(runtime, endpoint, "/__litobook/events"_str, "POST"_str)
                    .as_str()
                    .contains("HTTP/1.1 405 "_str));
}

TEST(BookPreview, InitialEventAndShutdownClosePendingSubscription) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    auto state     = PreviewState::create("preview"_Str, reclaimer).unwrap();
    auto endpoint  = state.endpoint();
    auto shutdown  = liserveto::Shutdown::make().unwrap();
    auto runtime   = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto response =
        runtime.block_on(endpoint.call(preview_request("/__litobook/events"_str, shutdown)))
            .unwrap();
    EXPECT_TRUE(response.long_lived);
    EXPECT_EQ(*response.head.headers.get_unique_text("cache-control"_str).unwrap(), "no-store"_str);
    EXPECT_EQ(runtime.block_on(next_event(response.body)), "event: generation\ndata: 0\n\n"_str);
    auto closing = [&]() -> rstd::async::coro<bool> {
        auto pending = rstd::async::spawn(rstd::stream::next(response.body));
        co_await rstd::async::yield_now();
        shutdown.request_stop().unwrap();
        auto ended = co_await rstd::move(pending);
        co_return ended.is_ok() && ended->is_none() && (co_await response.close()).is_ok();
    };
    auto closed = runtime.block_on(rstd::async::timeout(rstd::async::into_future(closing()),
                                                        rstd::time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(closed.is_ok());
    EXPECT_TRUE(*closed);
}

#if RSTD_OS_LINUX || (RSTD_OS_MACOS && defined(O_RESOLVE_BENEATH) && defined(O_NOFOLLOW_ANY))
TEST(BookPreview, ServesImmutableHtmlAndStaticPolicyFromCurrentGeneration) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    auto state     = PreviewState::create("preview"_Str, reclaimer).unwrap();
    auto temporary = preview_directory("<html><p>custom frontend</p></html>"_str);
    auto root      = rstd::path::PathBuf::from(temporary.path());
    ASSERT_TRUE(state.publish(rstd::move(temporary)).is_ok());
    EXPECT_EQ(state.generation(), u64(1));
    auto endpoint = state.endpoint();
    auto runtime  = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    for (auto path : array<ref<str>, 2> { "/"_str, "/nested/index.html"_str }) {
        auto page = preview_exchange(runtime, endpoint, path);
        EXPECT_TRUE(page.as_str().contains("HTTP/1.1 200 "_str));
        EXPECT_TRUE(
            page.as_str().contains("<html><p>custom frontend</p></html>\n<script defer"_str));
        EXPECT_TRUE(page.as_str().contains("data-generation=\"1\""_str));
        EXPECT_TRUE(page.as_str().contains("cache-control: no-store\r\n"_str));
        EXPECT_FALSE(page.as_str().contains("etag:"_str));
    }
    EXPECT_EQ(rstd::fs::read_to_string(root.join("site/index.html"_str).as_path()).unwrap(),
              "<html><p>custom frontend</p></html>"_str);
    auto css = preview_exchange(runtime, endpoint, "/style.css"_str);
    EXPECT_TRUE(css.as_str().ends_with("\r\n\r\nbody{color:red}"_str));
    EXPECT_TRUE(css.as_str().contains("cache-control: no-store\r\n"_str));
    EXPECT_TRUE(preview_exchange(runtime, endpoint, "/"_str, "HEAD"_str)
                    .as_str()
                    .ends_with("\r\n\r\n"_str));
    for (auto headers : array<ref<str>, 6> {
             "Range: bytes=0-3\r\n"_str,
             "If-None-Match: *\r\n"_str,
             "If-Match: \"not-current\"\r\n"_str,
             "If-Modified-Since: Wed, 21 Oct 2099 07:28:00 GMT\r\n"_str,
             "If-Unmodified-Since: Wed, 21 Oct 2015 07:28:00 GMT\r\n"_str,
             "Range: bytes=0-3\r\nIf-Range: Wed, 21 Oct 2099 07:28:00 GMT\r\n"_str }) {
        auto full = preview_exchange(runtime, endpoint, "/"_str, "GET"_str, headers);
        EXPECT_TRUE(full.as_str().contains("HTTP/1.1 200 "_str));
        EXPECT_TRUE(full.as_str().contains("data-generation=\"1\""_str));
        auto head = preview_exchange(runtime, endpoint, "/"_str, "HEAD"_str, headers);
        EXPECT_TRUE(head.as_str().contains("HTTP/1.1 200 "_str));
        auto full_length = full.as_str()
                               .split_once("Content-Length: "_str)
                               .unwrap()
                               .get<1>()
                               .split_once("\r\n"_str)
                               .unwrap()
                               .get<0>();
        auto head_length = head.as_str()
                               .split_once("Content-Length: "_str)
                               .unwrap()
                               .get<1>()
                               .split_once("\r\n"_str)
                               .unwrap()
                               .get<0>();
        EXPECT_EQ(full_length, head_length);
        EXPECT_TRUE(head.as_str().ends_with("\r\n\r\n"_str));
    }
    for (auto path : array<ref<str>, 3> { "/.env"_str, "/outside.txt"_str, "/missing"_str })
        EXPECT_TRUE(
            preview_exchange(runtime, endpoint, path).as_str().contains("HTTP/1.1 404 "_str));
    EXPECT_TRUE(
        preview_exchange(runtime, endpoint, "/link"_str).as_str().contains("HTTP/1.1 403 "_str));
    EXPECT_TRUE(preview_exchange(runtime, endpoint, "/%2e%2e/outside.txt"_str)
                    .as_str()
                    .contains("HTTP/1.1 400 "_str));
}

TEST(BookPreview, OldGenerationLivesUntilStreamingBodyCloses) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    auto state     = PreviewState::create("preview"_Str, reclaimer).unwrap();
    auto temporary = preview_directory("old"_str);
    auto old       = rstd::path::PathBuf::from(temporary.path());
    ASSERT_TRUE(state.publish(rstd::move(temporary)).is_ok());
    auto held    = Arc<rstd::sync::Mutex<Option<liserveto::Body>>>::make();
    auto capture = liserveto::Endpoint::make(
        [endpoint = state.endpoint(), held = held.clone()](
            liserveto::Request request) -> rstd::async::coro<liserveto::ResponseResult> {
            auto response = co_await endpoint.call(rstd::move(request));
            if (response.is_err()) co_return Err(rstd::move(response).unwrap_err());
            held->lock().unwrap()->insert(rstd::move(response->body));
            co_return Ok(liserveto::Response {});
        });
    auto runtime = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    preview_exchange(runtime, capture, "/style.css"_str);
    ASSERT_TRUE(state.publish(preview_directory("new"_str)).is_ok());
    EXPECT_TRUE(rstd::fs::exists(old.as_path()).unwrap());
    auto body  = held->lock().unwrap()->take().unwrap();
    auto bytes = runtime.block_on(body.collect(u64(1024))).unwrap();
    EXPECT_EQ(bytes.data.as_slice(), "body{color:red}"_bytes);
    EXPECT_TRUE(runtime.block_on(body.close()).is_ok());
    EXPECT_TRUE(reclaimer.drain().is_ok());
    EXPECT_FALSE(rstd::fs::exists(old.as_path()).unwrap());
    EXPECT_TRUE(
        preview_exchange(runtime, state.endpoint(), "/"_str).as_str().contains("new\n<script"_str));
}

TEST(BookPreview, FailedPublishKeepsSiteAndDoesNotNotifySubscribers) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    auto state     = PreviewState::create("preview"_Str, reclaimer).unwrap();
    auto endpoint  = state.endpoint();
    auto shutdown  = liserveto::Shutdown::make().unwrap();
    auto runtime   = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto response =
        runtime.block_on(endpoint.call(preview_request("/__litobook/events"_str, shutdown)))
            .unwrap();
    EXPECT_EQ(runtime.block_on(next_event(response.body)), "event: generation\ndata: 0\n\n"_str);
    ASSERT_TRUE(state.publish(preview_directory("good"_str)).is_ok());
    EXPECT_EQ(runtime.block_on(next_event(response.body)), "event: generation\ndata: 1\n\n"_str);
    auto parent = rstd::env::temp_dir();
    auto invalid =
        rstd::fs::TempDir::make_in(parent.as_path(), "litobook-preview-invalid"_str).unwrap();
    auto invalid_path = rstd::path::PathBuf::from(invalid.path());
    EXPECT_TRUE(state.publish(rstd::move(invalid)).is_err());
    EXPECT_FALSE(rstd::fs::exists(invalid_path.as_path()).unwrap());
    EXPECT_EQ(state.generation(), u64(1));
    auto unchanged =
        runtime.block_on(rstd::async::timeout(rstd::async::into_future(next_event(response.body)),
                                              rstd::time::Duration::from_millis(u64(5))));
    EXPECT_TRUE(unchanged.is_err());
    EXPECT_TRUE(
        preview_exchange(runtime, endpoint, "/"_str).as_str().contains("good\n<script"_str));
    ASSERT_TRUE(state.publish(preview_directory("fixed"_str)).is_ok());
    EXPECT_EQ(runtime.block_on(next_event(response.body)), "event: generation\ndata: 2\n\n"_str);
    auto late = runtime.block_on(endpoint.call(preview_request("/__litobook/events"_str, shutdown)))
                    .unwrap();
    EXPECT_EQ(runtime.block_on(next_event(late.body)), "event: generation\ndata: 2\n\n"_str);
    EXPECT_TRUE(runtime.block_on(response.close()).is_ok());
    EXPECT_TRUE(runtime.block_on(late.close()).is_ok());
}

TEST(BookPreview, BrandedNotFoundKeepsStatusAndInjectsReloadForGetAndHead) {
    auto reclaimer = PreviewReclaimer::create().unwrap();
    auto state     = PreviewState::create("preview"_Str, reclaimer).unwrap();
    auto directory = preview_directory("home"_str);
    auto error     = rstd::path::PathBuf::from(directory.path()).join("site/404.html"_str);
    rstd::fs::write(error.as_path(),
                    "<html><head><base href=\"/\"></head><body>Branded missing</body></html>"_bytes)
        .unwrap();
    ASSERT_TRUE(state.publish(rstd::move(directory)).is_ok());
    auto endpoint = state.endpoint();
    auto runtime  = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto page     = preview_exchange(runtime,
                                     endpoint,
                                     "/deep/unknown/page"_str,
                                     "GET"_str,
                                     "If-None-Match: *\r\nRange: bytes=0-3\r\n"_str);
    EXPECT_TRUE(page.as_str().contains("HTTP/1.1 404 "_str));
    EXPECT_TRUE(page.as_str().contains("Branded missing"_str));
    EXPECT_TRUE(page.as_str().contains("data-generation=\"1\""_str));
    auto head = preview_exchange(runtime, endpoint, "/deep/unknown/page"_str, "HEAD"_str);
    EXPECT_TRUE(head.as_str().contains("HTTP/1.1 404 "_str));
    EXPECT_TRUE(head.as_str().ends_with("\r\n\r\n"_str));
    auto get_length  = page.as_str()
                           .split_once("Content-Length: "_str)
                           .unwrap()
                           .get<1>()
                           .split_once("\r\n"_str)
                           .unwrap()
                           .get<0>();
    auto head_length = head.as_str()
                           .split_once("Content-Length: "_str)
                           .unwrap()
                           .get<1>()
                           .split_once("\r\n"_str)
                           .unwrap()
                           .get<0>();
    EXPECT_EQ(get_length, head_length);
    EXPECT_FALSE(
        preview_exchange(runtime, endpoint, "/link"_str).as_str().contains("Branded missing"_str));
}
#endif
