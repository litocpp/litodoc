export module litobook.executable:runner;

import rstd;
import lito.book;
import lito.book.preview;
import lito.doc.web;
import liserveto;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::path::PathBuf;
using rstd::sync::Arc;
using StopFlag = rstd::sync::atomic::Atomic<bool>;

export namespace lito::book::tool
{
struct RunnerInput {
    BookBuildInput input;
    bool           serve {};
    u16            port { 3000 };
    u64            interval_ms { 250 };
};
} // namespace lito::book::tool

namespace lito::book::tool
{
struct BuildFailure {
    String message;
    bool   fatal {};
};

auto rebuild(BookWatchSession&                    session,
             const Option<preview::PreviewState>& preview,
             const Option<PathBuf>&               temporary_root) -> Result<empty, BuildFailure> {
    Option<rstd::fs::TempDir> generation;
    Option<PathBuf>           output;
    if (preview.is_some()) {
        auto created = rstd::fs::TempDir::make_in(temporary_root->as_path(), "generation"_str);
        if (created.is_err())
            return Err(BuildFailure {
                rstd::format("cannot create preview generation: {}", created.unwrap_err()), true });
        generation.insert(rstd::move(created).unwrap());
        output.insert(PathBuf::from(generation->path()).join(PathBuf::from("site"_str).as_path()));
    }
    auto frontend = lito::doc::web::load_default_frontend();
    if (frontend.is_err()) return Err(BuildFailure { rstd::move(frontend).unwrap_err() });
    auto prepared = session.prepare(rstd::move(output), Some(rstd::move(frontend).unwrap()));
    if (prepared.is_err()) return Err(BuildFailure { rstd::move(prepared).unwrap_err() });
    if (preview.is_some()) prepared->html.not_found_base = "/"_Str;
    auto built = lito::book::publish(*prepared);
    if (built.is_err()) return Err(BuildFailure { rstd::move(built).unwrap_err() });
    if (preview.is_some()) {
        auto published = preview->publish(generation.take().unwrap());
        if (published.is_err())
            return Err(BuildFailure { rstd::move(published).unwrap_err(), true });
        rstd::io::eprintln("generated {} pages, preview generation {}", built->pages, *published);
    } else {
        rstd::io::eprintln("generated {} pages at {}", built->pages, built->output.as_path());
    }
    return Ok(empty {});
}

void report_build_error(Option<String>& previous, String error) {
    if (previous.is_none() || previous->as_str() != error.as_str())
        rstd::io::eprintln("litobook: {}", error);
    previous.insert(rstd::move(error));
}

auto build_loop(BookBuildInput                input,
                Option<preview::PreviewState> preview,
                Option<PathBuf>               temporary_root,
                u64                           interval_ms,
                const Arc<StopFlag>&          stop) -> Result<empty, String> {
    auto opened = BookWatchSession::open(rstd::move(input));
    if (opened.is_err()) return Err(rstd::move(opened).unwrap_err());
    auto session = rstd::move(opened).unwrap();
    if (temporary_root.is_some()) {
        auto excluded = session.exclude(temporary_root->as_path());
        if (excluded.is_err()) return Err(rstd::move(excluded).unwrap_err());
    }
    Option<String> baseline;
    Option<String> pending;
    Option<String> previous_error;
    while (! stop->load()) {
        auto scanned = session.scan();
        if (scanned.is_err()) {
            pending = None();
            report_build_error(previous_error, rstd::move(scanned).unwrap_err());
        } else if (baseline.is_none() || baseline->as_str() != scanned->digest.as_str()) {
            if (baseline.is_some() &&
                (pending.is_none() || pending->as_str() != scanned->digest.as_str())) {
                pending.insert(rstd::move(scanned->digest));
                if (! stop->load())
                    rstd::thread::park_timeout(rstd::time::Duration::from_millis(interval_ms));
                continue;
            }
            pending = None();
            baseline.insert(rstd::move(scanned->digest));
            auto built = rebuild(session, preview, temporary_root);
            if (built.is_err()) {
                auto failure = rstd::move(built).unwrap_err();
                if (failure.fatal) return Err(rstd::move(failure.message));
                report_build_error(previous_error, rstd::move(failure.message));
            } else {
                previous_error = None();
            }
            // Compare again with the inputs captured before the build, not its final state.
            continue;
        } else {
            pending = None();
        }
        if (! stop->load())
            rstd::thread::park_timeout(rstd::time::Duration::from_millis(interval_ms));
    }
    return Ok(empty {});
}

using WorkerResult = Result<empty, String>;
using Completion   = rstd::async::oneshot::Receiver<WorkerResult>;

auto wait_for_stop(rstd::async::signal::Recv interrupt, Completion completion)
    -> rstd::async::coro<WorkerResult> {
    auto selected = co_await rstd::async::select(rstd::move(interrupt), rstd::move(completion));
    if (selected.is_left()) {
        auto signal = rstd::move(selected).unwrap_left();
        if (signal.is_err())
            co_return Err(rstd::format("cannot receive Ctrl-C: {}", signal.unwrap_err()));
        if (signal->is_none()) co_return Err("Ctrl-C subscription closed"_Str);
        co_return Ok(empty {});
    }
    auto completed = rstd::move(selected).unwrap_right();
    if (completed.is_err()) co_return Err("Book build worker terminated unexpectedly"_Str);
    auto result = rstd::move(completed).unwrap();
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    co_return Err("Book build worker stopped unexpectedly"_Str);
}

auto run_server(rstd::net::TcpListener    listener,
                preview::PreviewState     preview,
                rstd::async::signal::Recv interrupt,
                Completion                completion,
                const Arc<StopFlag>&      stop,
                rstd::thread::Thread      worker) -> rstd::async::coro<WorkerResult> {
    auto shutdown = liserveto::Shutdown::make();
    if (shutdown.is_err())
        co_return Err(rstd::format("cannot create preview shutdown: {}", shutdown.unwrap_err()));
    auto stopping = rstd::move(shutdown).unwrap();
    auto serving  = rstd::async::into_future(
        liserveto::serve(rstd::move(listener),
                         Arc<liserveto::Endpoint>::make(preview.endpoint()),
                         stopping.clone()));
    auto selected = co_await rstd::async::select(
        rstd::async::into_future(wait_for_stop(rstd::move(interrupt), rstd::move(completion))),
        rstd::future::by_ref(serving));
    stop->store(true);
    worker.unpark();
    if (selected.is_right()) {
        auto served = rstd::move(selected).unwrap_right();
        if (served.is_err())
            co_return Err(rstd::format("preview server failed: {}", served.unwrap_err()));
        co_return Err("preview server stopped unexpectedly"_Str);
    }
    auto result    = rstd::move(selected).unwrap_left();
    auto requested = stopping.request_stop();
    auto served    = co_await rstd::future::by_ref(serving);
    if (served.is_err())
        co_return Err(rstd::format("preview server failed: {}", served.unwrap_err()));
    if (requested.is_err())
        co_return Err(rstd::format("cannot stop preview server: {}", requested.unwrap_err()));
    co_return rstd::move(result);
}

auto run_loop(RunnerInput input, rstd::async::signal::Recv interrupt)
    -> rstd::async::coro<WorkerResult> {
    Option<preview::PreviewReclaimer> reclaimer;
    Option<preview::PreviewState>     preview;
    Option<rstd::fs::TempDir>         temporary;
    Option<PathBuf>                   temporary_root;
    Option<rstd::net::TcpListener>    listener;
    if (input.serve) {
        auto reclaiming = preview::PreviewReclaimer::create();
        if (reclaiming.is_err()) co_return Err(rstd::move(reclaiming).unwrap_err());
        reclaimer.insert(rstd::move(reclaiming).unwrap());
        auto client = lito::doc::web::load_preview_client();
        if (client.is_err()) co_return Err(rstd::move(client).unwrap_err());
        auto created = preview::PreviewState::create(rstd::move(client).unwrap(), *reclaimer);
        if (created.is_err()) co_return Err(rstd::move(created).unwrap_err());
        preview.insert(rstd::move(created).unwrap());
        auto directory = rstd::fs::TempDir::make("litobook-preview"_str);
        if (directory.is_err())
            co_return Err(
                rstd::format("cannot create preview directory: {}", directory.unwrap_err()));
        temporary.insert(rstd::move(directory).unwrap());
        temporary_root.insert(PathBuf::from(temporary->path()));
        auto bound = rstd::net::TcpListener::bind(rstd::net::SocketAddr::ipv4_loopback(input.port));
        if (bound.is_err())
            co_return Err(rstd::format("cannot bind preview server: {}", bound.unwrap_err()));
        listener.insert(rstd::move(bound).unwrap());
        auto address = listener->local_addr();
        if (address.is_err())
            co_return Err(rstd::format("cannot read preview address: {}", address.unwrap_err()));
        rstd::io::eprintln("litobook preview: http://127.0.0.1:{}/", address->port());
    }
    auto stop                 = Arc<StopFlag>::make(false);
    auto [sender, completion] = rstd::async::oneshot::channel<WorkerResult>();
    auto spawned = rstd::thread::builder::Builder::make()
                       .name("litobook-build"_Str)
                       .spawn([input          = rstd::move(input.input),
                               preview        = preview.is_some() ? Some(preview->clone())
                                                                  : None<preview::PreviewState>(),
                               temporary_root = rstd::move(temporary_root),
                               interval       = input.interval_ms,
                               stop           = stop.clone(),
                               sender         = rstd::move(sender)]() mutable {
                           auto result = build_loop(rstd::move(input),
                                                    rstd::move(preview),
                                                    rstd::move(temporary_root),
                                                    interval,
                                                    stop);
                           (void)sender.send(rstd::move(result));
                       });
    if (spawned.is_err())
        co_return Err(rstd::format("cannot start Book build worker: {}", spawned.unwrap_err()));
    auto         worker = rstd::move(spawned).unwrap();
    WorkerResult result =
        input.serve ? co_await run_server(listener.take().unwrap(),
                                          preview->clone(),
                                          rstd::move(interrupt),
                                          rstd::move(completion),
                                          stop,
                                          worker.thread())
                    : co_await wait_for_stop(rstd::move(interrupt), rstd::move(completion));
    stop->store(true);
    worker.thread().unpark();
    auto joined = rstd::move(worker).join();
    preview     = None();
    if (reclaimer.is_some()) {
        auto reclaimed = rstd::move(reclaimer.take().unwrap()).finish();
        if (reclaimed.is_err()) co_return Err(rstd::move(reclaimed).unwrap_err());
    }
    if (joined.is_err()) co_return Err("cannot join Book build worker"_Str);
    co_return rstd::move(result);
}
} // namespace lito::book::tool

export namespace lito::book::tool
{
auto run_watch(RunnerInput input) -> Result<empty, String> {
    auto interrupt = rstd::async::signal::ctrl_c();
    if (interrupt.is_err())
        return Err(rstd::format("cannot subscribe to Ctrl-C: {}", interrupt.unwrap_err()));
    auto runtime = rstd::async::RuntimeBuilder::current_thread().enable_all().build();
    if (runtime.is_err())
        return Err(rstd::format("cannot start preview runtime: {}", runtime.unwrap_err()));
    return runtime->block_on(run_loop(rstd::move(input), rstd::move(interrupt).unwrap()));
}
} // namespace lito::book::tool
