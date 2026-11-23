export module lito.book.preview;
export import rstd;
export import liserveto;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::Arc;
using rstd::task::Context;
using rstd::task::Poll;
using rstd::tuple;

namespace lito::book::preview
{
struct ReclaimJob {
    Option<rstd::fs::TempDir>                               directory;
    Option<rstd::sync::mpmc::Sender<Result<empty, String>>> completed;
};

auto reclaim_directories(rstd::sync::mpmc::Receiver<ReclaimJob> receiver) -> Option<String> {
    Option<String> error;
    for (;;) {
        auto received = receiver.recv();
        if (received.is_err()) return error;
        auto job = rstd::move(received).unwrap();
        if (job.directory.is_some()) {
            auto closed = job.directory->close();
            if (closed.is_err() && error.is_none())
                error.insert(
                    rstd::format("cannot remove preview generation: {}", closed.unwrap_err()));
            job.directory = None();
        }
        if (job.completed.is_some())
            (void)job.completed->send(error.is_some() ? Result<empty, String>(Err(error->clone()))
                                                      : Result<empty, String>(Ok(empty {})));
    }
}

struct Generation {
    rstd::fs::TempDir                    directory;
    liserveto::Endpoint                  files;
    rstd::sync::mpmc::Sender<ReclaimJob> reclaims;
    u64                                  number {};

    Generation(rstd::fs::TempDir                    root,
               liserveto::Endpoint                  endpoint,
               rstd::sync::mpmc::Sender<ReclaimJob> sender)
        : directory(rstd::move(root)), files(rstd::move(endpoint)), reclaims(rstd::move(sender)) {}
    ~Generation() { (void)reclaims.send(ReclaimJob { Some(rstd::move(directory)), {} }); }
};

struct PreviewData {
    struct Current {
        Option<Arc<Generation>> site;
        u64                     number {};
    };
    String                               client;
    rstd::sync::mpmc::Sender<ReclaimJob> reclaims;
    rstd::sync::Mutex<Current>           current { Current {} };
    rstd::async::watch::Sender<u64>      changes { u64() };

    PreviewData(String script, rstd::sync::mpmc::Sender<ReclaimJob> sender)
        : client(rstd::move(script)), reclaims(rstd::move(sender)) {}
};

struct GenerationBody {
    using Item = liserveto::BodyItem;
    Option<Arc<Generation>>      generation;
    liserveto::Body              body;
    Option<liserveto::BodyClose> closing;

    auto requires_input() const -> bool { return body.requires_input(); }
    auto poll_next(mut_ref<GenerationBody> self, Context& cx) -> Poll<Option<Item>> {
        return rstd::future::poll_next(self->body, cx);
    }
    auto poll_close(mut_ref<GenerationBody> self, Context& cx)
        -> Poll<Result<empty, liserveto::BodyError>> {
        if (self->closing.is_none()) self->closing.insert(self->body.close());
        auto result = rstd::future::poll(*self->closing, cx);
        if (result.is_ready()) {
            self->closing    = None();
            self->generation = None();
        }
        return result;
    }
};

struct EventState {
    rstd::async::watch::Receiver<u64> receiver;
    bool                              first { true };
};

auto next_generation(EventState state)
    -> rstd::async::coro<Option<tuple<liserveto::EventItem, EventState>>> {
    if (! state.first) {
        auto changed = co_await state.receiver.changed();
        if (changed.is_err()) co_return None();
    }
    state.first = false;
    u64 number;
    {
        auto value = state.receiver.borrow_and_update();
        number     = *value;
    }
    lihttpto::ServerEvent event;
    event.type = "generation"_Str;
    event.data = rstd::format("{}", number);
    co_return Some(tuple<liserveto::EventItem, EventState> {
        liserveto::EventItem(Ok(rstd::move(event))), rstd::move(state) });
}

void no_store(liserveto::Response& response) {
    response.head.headers.set("cache-control"_str, "no-store"_str).unwrap();
}

auto text_response(ref<str> text, ref<str> type, u16 status = u16(200)) -> liserveto::Response {
    auto response =
        liserveto::Response::bytes(rstd::bytes::Bytes::copy_from_slice(text.as_bytes()));
    response.head.status = lihttpto::StatusCode::make(status).unwrap();
    response.head.headers.set("content-type"_str, type).unwrap();
    response.head.headers.set("x-content-type-options"_str, "nosniff"_str).unwrap();
    no_store(response);
    return response;
}

auto script_tag(u64 number) -> String {
    return rstd::format(
        "\n<script defer src=\"/__litobook/client.js\" data-generation=\"{}\"></script>\n", number);
}

auto is_html(const liserveto::Response& response) -> bool {
    auto header = response.head.headers.get("content-type"_str);
    if (header.is_none()) return false;
    auto text = (*header)->to_str();
    return text.is_ok() && text->starts_with("text/html"_str);
}

auto serve_preview(Arc<PreviewData> state, liserveto::Request request)
    -> rstd::async::coro<liserveto::ResponseResult> {
    auto method = request.head().line.method.as_str();
    auto get    = method == "GET"_str;
    auto head   = method == "HEAD"_str;
    if (request.path_size() == usize(2) && request.path_segment(usize()) == "__litobook"_bytes) {
        auto events = request.path_segment(usize(1)) == "events"_bytes;
        auto client = request.path_segment(usize(1)) == "client.js"_bytes;
        if (events || client) {
            if (! get && ! (client && head)) {
                auto response = text_response(
                    "Method not allowed\n"_str, "text/plain; charset=utf-8"_str, u16(405));
                response.head.headers.set("allow"_str, client ? "GET, HEAD"_str : "GET"_str)
                    .unwrap();
                co_return Ok(rstd::move(response));
            }
            if (events) {
                auto source = rstd::stream::unfold(
                    EventState { state->changes.subscribe() }, [](EventState event) {
                        return rstd::async::into_future(next_generation(rstd::move(event)));
                    });
                auto response = liserveto::sse_response(rstd::move(source), request.shutdown);
                no_store(response);
                co_return Ok(rstd::move(response));
            }
            auto response =
                text_response(state->client.as_str(), "text/javascript; charset=utf-8"_str);
            if (head) response.body = liserveto::Body {};
            co_return Ok(rstd::move(response));
        }
    }

    Option<Arc<Generation>> selected;
    {
        auto current = state->current.lock().unwrap();
        if (current->site.is_some()) selected.insert(current->site->clone());
    }
    if (selected.is_none()) {
        if (! get && ! head) {
            auto response = text_response(
                "Method not allowed\n"_str, "text/plain; charset=utf-8"_str, u16(405));
            response.head.headers.set("allow"_str, "GET, HEAD"_str).unwrap();
            co_return Ok(rstd::move(response));
        }
        auto page = String::make(
            "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
            "<title>Litobook preview</title><p>Waiting for the first successful build.</p>"_str);
        page.push_str(script_tag(u64()).as_str());
        auto response = text_response(page.as_str(), "text/html; charset=utf-8"_str);
        if (head) response.body = liserveto::Body {};
        co_return Ok(rstd::move(response));
    }

    for (auto name : array<ref<str>, 6> { "range"_str,
                                          "if-range"_str,
                                          "if-match"_str,
                                          "if-none-match"_str,
                                          "if-modified-since"_str,
                                          "if-unmodified-since"_str })
        request.headers().remove(name);
    auto generation = selected.take().unwrap();
    auto cleanup    = request.cleanup();
    if (cleanup.is_err()) co_return Err(liserveto::Error { liserveto::ErrorKind::Internal });
    cleanup->retain(generation.clone());
    auto served = co_await generation->files.call(rstd::move(request));
    if (served.is_err()) co_return Err(rstd::move(served).unwrap_err());
    auto response = rstd::move(served).unwrap();
    no_store(response);
    if ((get || head) &&
        (response.head.status.value() == u16(200) || response.head.status.value() == u16(404)) &&
        is_html(response)) {
        auto tag = script_tag(generation->number);
        response.head.headers.remove("etag"_str);
        response.head.headers.remove("last-modified"_str);
        response.head.headers.remove("accept-ranges"_str);
        if (get) {
            auto collected = co_await response.body.collect(u64(64 * 1024 * 1024));
            if (collected.is_err())
                co_return Err(
                    liserveto::Error { liserveto::ErrorKind::Body, Some(collected.unwrap_err()) });
            rstd::bytes::BytesMut bytes;
            bytes.put_slice(collected->data.as_slice());
            bytes.put_slice(tag.as_str().as_bytes());
            auto body               = bytes.freeze();
            response.content_length = Some(u64(body.len().to_primitive()));
            response.body           = liserveto::Body::from_bytes(rstd::move(body));
        } else if (response.content_length.is_some()) {
            *response.content_length += u64(tag.len().to_primitive());
        }
    }
    response.body = liserveto::Body::from_stream(
        GenerationBody { Some(rstd::move(generation)), rstd::move(response.body), {} });
    co_return Ok(rstd::move(response));
}
} // namespace lito::book::preview

export namespace lito::book::preview
{
class PreviewState;
class PreviewReclaimer {
    friend class PreviewState;
    Option<rstd::sync::mpmc::Sender<ReclaimJob>>     sender_;
    Option<rstd::thread::JoinHandle<Option<String>>> worker_;

    PreviewReclaimer(rstd::sync::mpmc::Sender<ReclaimJob>     sender,
                     rstd::thread::JoinHandle<Option<String>> worker)
        : sender_(Some(rstd::move(sender))), worker_(Some(rstd::move(worker))) {}

public:
    PreviewReclaimer(const PreviewReclaimer&) = delete;
    PreviewReclaimer(PreviewReclaimer&& other) noexcept
        : sender_(other.sender_.take()), worker_(other.worker_.take()) {}
    auto operator=(PreviewReclaimer&&) -> PreviewReclaimer& = delete;
    ~PreviewReclaimer() { (void)rstd::move(*this).finish(); }

    static auto create() -> Result<PreviewReclaimer, String> {
        auto channel = rstd::sync::mpmc::channel<ReclaimJob>();
        auto worker  = rstd::thread::spawn([receiver = rstd::move(channel.get<1>())]() mutable {
            return reclaim_directories(rstd::move(receiver));
        });
        if (worker.is_err())
            return Err(rstd::format("cannot start preview reclaimer: {}", worker.unwrap_err()));
        return Ok(PreviewReclaimer(rstd::move(channel.get<0>()), rstd::move(worker).unwrap()));
    }
    auto drain() const -> Result<empty, String> {
        if (sender_.is_none()) return Err("preview reclaimer is closed"_Str);
        auto completed = rstd::sync::mpmc::channel<Result<empty, String>>();
        auto sent      = sender_->send(ReclaimJob { {}, Some(rstd::move(completed.get<0>())) });
        if (sent.is_err()) return Err("preview reclaimer disconnected"_Str);
        auto result = completed.get<1>().recv();
        if (result.is_err()) return Err("preview reclaimer disconnected"_Str);
        return rstd::move(result).unwrap();
    }
    // Blocking: stop HTTP/build work and drop all states, endpoints and responses before joining.
    auto finish() && -> Result<empty, String> {
        sender_ = None();
        if (worker_.is_none()) return Ok(empty {});
        auto joined = rstd::move(worker_.take().unwrap()).join();
        if (joined.is_err()) return Err("preview reclaimer failed"_Str);
        auto error = rstd::move(joined).unwrap();
        if (error.is_some()) return Err(rstd::move(error).unwrap());
        return Ok(empty {});
    }
};

class PreviewState {
    Arc<PreviewData> state_;
    explicit PreviewState(Arc<PreviewData> state): state_(rstd::move(state)) {}

public:
    static auto create(String client, const PreviewReclaimer& reclaimer)
        -> Result<PreviewState, String> {
        if (client.is_empty()) return Err("preview client is empty"_Str);
        if (reclaimer.sender_.is_none()) return Err("preview reclaimer is closed"_Str);
        return Ok(PreviewState(Arc<PreviewData>::make(rstd::move(client), *reclaimer.sender_)));
    }
    auto clone() const -> PreviewState { return PreviewState(state_.clone()); }
    auto endpoint() const -> liserveto::Endpoint {
        return liserveto::Endpoint::make([state = state_.clone()](liserveto::Request request) {
            return serve_preview(state.clone(), rstd::move(request));
        });
    }
    auto generation() const -> u64 { return state_->current.lock().unwrap()->number; }
    auto publish(rstd::fs::TempDir directory) const -> Result<u64, String> {
        auto path = rstd::path::PathBuf::from(directory.path()).join("site"_str);
        auto root = liserveto::ConfinedDir::open(path.as_path());
        if (root.is_err())
            return Err(rstd::format("cannot open preview site: {}", root.unwrap_err()));
        liserveto::StaticFilesOptions options;
        options.not_found.insert("404.html"_Str);
        auto files = liserveto::StaticFiles::make(rstd::move(root).unwrap(), rstd::move(options));
        if (files.is_err())
            return Err(rstd::format("cannot prepare preview site: {}", files.unwrap_err()));
        auto next =
            Arc<Generation>::make(rstd::move(directory), files->endpoint(), state_->reclaims);
        Option<Arc<Generation>> previous;
        u64                     number;
        {
            auto current = state_->current.lock().unwrap();
            if (current->number == u64::MAX) return Err("preview generation exhausted"_Str);
            number       = current->number + u64(1);
            next->number = number;
            previous     = current->site.take();
            current->site.insert(rstd::move(next));
            current->number = number;
            state_->changes.send_replace(number);
        }
        return Ok(number);
    }
};
} // namespace lito::book::preview
