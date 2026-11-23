"""Run the focused loopback preview checks with a built litobook executable."""

import argparse
import http.client
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time


def wait_for(check, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = check()
        except FileNotFoundError:
            result = None
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("timed out waiting for preview state")


def write(root, name, content):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def request(port, path="/", method="GET", headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        return response.status, {name.lower(): value for name, value in response.getheaders()}, response.read()
    finally:
        connection.close()


def generation(body):
    value = re.search(rb'data-generation="(\d+)"', body)
    return int(value[1]) if value else 0


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
    try:
        assert process.wait(timeout=8) == 0
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise AssertionError("preview did not drain after SIGINT")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    options = parser.parse_args()
    executable = options.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="litobook-loopback-") as temporary:
        logs = Path(temporary)
        root = logs / "book"
        manifest = ('[book]\nname="preview"\ntitle="Preview"\nlanguage="zh-CN"\n'
                    'description="Default description"\n'
                    '[output.html]\nsite-url="https://example.org/manual/"\n'
                    'repository-url="https://forge.example.org/project"\n'
                    'edit-url="https://forge.example.org/edit/main/{path}"\n'
                    '[output.html.brand]\nlabel="Project"\nhref="https://example.org/"\n'
                    '[[output.html.navigation]]\nlabel="Home"\nhref="index.html"\n'
                    '[output.html.footer]\ntext="Preview footer"\n'
                    '[output.html.pages."index.md"]\ndescription="Home description"\n'
                    '[output.html.redirects]\n"old.html"="index.md"\n')
        write(root, "book.toml", manifest)
        write(root, "src/SUMMARY.md", "# Summary\n\n- [Home](index.md)\n- [Guide](guide/start.md)\n")
        write(root, "src/index.md", "# Hello\n\nVersion alpha.\n")
        write(root, "src/guide/start.md", "# Guide\n\nNested page.\n")
        write(root, ".secret", "not public")
        for arguments in (["serve", "--interval-ms", "49"], ["serve", "--port", "65536"],
                          ["watch", "--interval-ms", "60001"], ["serve", "--out", "output"]):
            invalid = subprocess.run([str(executable), "-C", str(root), *arguments],
                                     capture_output=True, timeout=5)
            assert invalid.returncode != 0, arguments

        log_path = logs / "serve.log"
        write(root, "book.toml", "invalid [")
        preview_directories = set(Path(tempfile.gettempdir()).glob("litobook-preview-*"))
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(executable), "-C", str(root), "serve", "--port", "0",
                                        "--interval-ms", "50"], stdout=log, stderr=log)
            try:
                def address():
                    assert process.poll() is None, log_path.read_text()
                    match = re.search(r"http://127\.0\.0\.1:(\d+)/", log_path.read_text())
                    return int(match[1]) if match else None

                port = wait_for(address)

                def page_when(predicate):
                    page = request(port)[2]
                    return page if predicate(page) else None

                waiting = request(port)[2]
                assert b"Waiting for the first successful build" in waiting
                assert generation(waiting) == 0
                write(root, "book.toml", manifest)
                body = wait_for(lambda: page_when(generation))
                assert b'lang="zh-CN"' in body
                assert b'content="Home description"' in body
                assert b'href="https://example.org/manual/index.html"' in body
                assert b'https://forge.example.org/edit/main/src/index.md' in body
                assert b'Preview footer' in body
                current = generation(body)
                time.sleep(0.35)
                assert generation(request(port)[2]) == current, "preview must not rebuild its own output"
                status, headers, nested = request(port, "/guide/start/")
                assert status == 200 and b"Nested page" in nested
                assert headers["cache-control"] == "no-store"
                status, head, data = request(port, method="HEAD")
                assert status == 200 and not data and int(head["content-length"]) == len(body)
                assert request(port, "/__litobook/client.js")[0] == 200
                status, error_headers, missing = request(port, "/deep/missing/page")
                assert status == 404 and b"Page not found" in missing
                assert b'<base href="/">' in missing and generation(missing) == current
                assert b'Preview footer' in missing and b'rel="canonical"' not in missing
                status, error_head, data = request(port, "/deep/missing/page", method="HEAD")
                assert status == 404 and not data and int(error_head["content-length"]) == len(missing)
                status, _, alias = request(port, "/old.html")
                assert status == 200 and b'http-equiv="refresh"' in alias
                assert not (root / "build/book").exists(), "serve must not replace build output"

                events = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
                events.request("GET", "/__litobook/events")
                stream = events.getresponse()
                assert stream.status == 200

                def next_event():
                    data = None
                    while True:
                        line = stream.readline().decode().strip()
                        if line.startswith("data:"):
                            data = int(line.removeprefix("data:").strip())
                        if not line and data is not None:
                            return data

                assert next_event() == current
                page = root / "src/index.md"
                old_time = page.stat().st_mtime_ns
                page.write_text("# Hello\n\nVersion bravo.\n")
                os.utime(page, ns=(old_time, old_time))
                body = wait_for(lambda: page_when(lambda value: b"Version bravo" in value))
                assert generation(body) > current and next_event() == generation(body)
                current = generation(body)
                for invalid in ("invalid [", manifest + '\n[unknown]\nbad=true\n'):
                    write(root, "book.toml", invalid)
                    time.sleep(0.35)
                    assert request(port)[2] == body, "failed builds must preserve the last site"
                write(root, "book.toml", manifest)
                body = wait_for(lambda: page_when(lambda value: generation(value) > current))
                current = generation(body)
                write(root, "src/new.md", "# Added\n\nNew chapter.\n")
                write(root, "src/SUMMARY.md", "# Summary\n\n- [Home](index.md)\n- [New](new.md)\n")
                wait_for(lambda: request(port, "/new/")[0] == 200)
                write(root, "src/index.md", "# Broken\n\n[Missing](absent.md)\n")
                time.sleep(0.35)
                preserved = request(port)[2]
                assert b"Broken" not in preserved
                write(root, "src/absent.md", "# Restored\n")
                write(root, "src/SUMMARY.md", "# Summary\n\n- [Home](index.md)\n- [Restored](absent.md)\n")
                wait_for(lambda: b"Broken" in request(port)[2])
                for path in ("/.secret", "/%2e%2e/book.toml", "/src/index.md"):
                    assert request(port, path)[0] != 200, path
                events.close()
            except BaseException:
                print(log_path.read_text())
                raise
            finally:
                stop(process)
        assert set(Path(tempfile.gettempdir()).glob("litobook-preview-*")) == preview_directories
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", port))

        write(root, "src/index.md", "# Watch\n\nFirst version.\n")
        with (logs / "watch.log").open("wb") as log:
            process = subprocess.Popen([str(executable), "-C", str(root), "watch", "--interval-ms", "50"],
                                       stdout=log, stderr=log)
            output = root / "build/book/index.html"
            try:
                wait_for(output.exists)
                initial = output.read_bytes()
                assert b"/__litobook/" not in initial
                assert b'<base href="/manual/">' in (root / "build/book/404.html").read_bytes()
                time.sleep(0.35)
                assert output.read_bytes() == initial
                write(root, "src/index.md", "# Watch\n\nOther version.\n")
                wait_for(lambda: b"Other version" in output.read_bytes())
            except BaseException:
                print((logs / "watch.log").read_text())
                raise
            finally:
                stop(process)
    print("litobook CLI, loopback HTTP/SSE, recovery, watch and shutdown checks passed")


if __name__ == "__main__":
    main()
