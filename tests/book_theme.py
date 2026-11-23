"""Verify a complete independent theme through build, check and live preview."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time

from book_preview import generation, request, stop, wait_for, write


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    executable = parser.parse_args().executable.resolve()
    with tempfile.TemporaryDirectory(prefix="litobook-independent-") as temporary:
        logs = Path(temporary)
        root = logs / "book"
        write(root, "book.toml", '[book]\nname="custom"\ntitle="My Book"\n'
              '[output.html]\nfrontend="theme"\nsite-url="https://example.org/manual/"\n'
              'repository-url="https://forge.example.org/project"\n'
              '[[output.html.navigation]]\nlabel="Home"\nhref="index.html"\n'
              '[output.html.redirects]\n"old.html"="chapter.md"\n')
        write(root, "src/SUMMARY.md", "- [Home](index.md)\n- [Chapter](chapter.md)\n")
        write(root, "src/index.md", "# Home\n\nIndependent content.\n")
        write(root, "src/chapter.md", "# Chapter\n\nMore content.\n")
        manifest = {
            "format": "lito-doc-frontend", "version": 2, "template-api": 1,
            "book-data-api": 1, "book-template-api": 2, "capabilities": ["book"],
            "templates": {"book-root": "root.html", "book-page": "page.html",
                          "book-not-found": "missing.html", "book-redirect": "moved.html"},
            "partials": ["footer.html"],
            "assets": [{"path": "skin.css", "media-type": "text/css"}],
        }
        write(root, "theme/frontend.json", json.dumps(manifest))
        body = ('<!doctype html><html><head><link rel="stylesheet" '
                'href="{{page.asset_prefix}}skin.css"></head><body><h1>{{page.title}}</h1>'
                '<main>{{{page.content}}}</main>{{> footer.html}}</body></html>')
        write(root, "theme/root.html", body)
        write(root, "theme/page.html", body)
        footer = ('<footer>Independent footer {{#each site.navigation}}'
                  '<a href="{{href}}">{{label}}</a>{{/each}}'
                  '{{#if site.repository}}<a href="{{site.repository.href}}">My source</a>{{/if}}</footer>')
        write(root, "theme/footer.html", footer)
        missing = ('<!doctype html><html><head><base href="{{page.base_href}}">'
                   '<link rel="stylesheet" href="skin.css"></head>'
                   '<body>Independent missing {{> footer.html}}</body></html>')
        write(root, "theme/missing.html", missing)
        write(root, "theme/moved.html", '<!doctype html><meta http-equiv="refresh" '
              'content="0;url={{page.redirect.href}}"><p>Independent redirect</p>')
        write(root, "theme/skin.css", "body { color: navy; }")
        for command in ("check", "build"):
            checked = subprocess.run([str(executable), "-C", str(root), command],
                                     capture_output=True, timeout=10)
            assert checked.returncode == 0, checked.stderr.decode()
        production = root / "build/book"
        assert not (production / "static").exists()
        assert b"/__litobook/" not in (production / "index.html").read_bytes()
        assert b'<base href="/manual/">' in (production / "404.html").read_bytes()
        original = (production / "index.html").read_bytes()
        rejected = []
        for version in (None, 1, 99):
            old = dict(manifest)
            if version is None:
                del old["book-template-api"]
            else:
                old["book-template-api"] = version
            rejected.append(old)
        rejected.append(dict(manifest, version=1))
        for entry in ("book-not-found", "book-redirect"):
            incomplete = dict(manifest)
            incomplete["templates"] = dict(manifest["templates"])
            del incomplete["templates"][entry]
            rejected.append(incomplete)
        for incompatible in rejected:
            write(root, "theme/frontend.json", json.dumps(incompatible))
            for command in ("check", "build"):
                checked = subprocess.run([str(executable), "-C", str(root), command],
                                         capture_output=True, timeout=10)
                assert checked.returncode != 0, incompatible
            assert (production / "index.html").read_bytes() == original
        write(root, "theme/frontend.json", json.dumps(manifest))
        log_path = logs / "serve.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(executable), "-C", str(root), "serve", "--port", "0",
                                        "--interval-ms", "50"], stdout=log, stderr=log)
            try:
                def address():
                    assert process.poll() is None, log_path.read_text()
                    match = re.search(r"http://127\.0\.0\.1:(\d+)/", log_path.read_text())
                    return int(match[1]) if match else None

                port = wait_for(address)
                wait_for(lambda: generation(request(port)[2]) > 0)
                page = request(port)[2]
                assert b"Independent footer" in page and b"lito-doc-shell" not in page
                assert b"static/app.js" not in page
                assert request(port, "/skin.css")[0] == 200
                assert request(port, "/chapter/")[0] == 200
                status, _, error = request(port, "/unknown/deep/route")
                assert status == 404 and b"Independent missing" in error
                assert b'<base href="/">' in error and generation(error) == generation(page)
                assert b"Page not found" not in error
                status, headers, data = request(port, "/unknown/deep/route", method="HEAD")
                assert status == 404 and not data and int(headers["content-length"]) == len(error)
                assert b"Independent redirect" in request(port, "/old.html")[2]
                write(root, "theme/missing.html", missing.replace("Independent missing", "Updated missing"))
                wait_for(lambda: b"Updated missing" in request(port, "/unknown/deep/route")[2])
                before = request(port)[2]
                write(root, "theme/root.html", "{{#if broken}}")
                time.sleep(.4)
                assert request(port)[2] == before
                write(root, "theme/root.html", body.replace("<main>", "<main>Restored "))
                wait_for(lambda: b"Restored" in request(port)[2])
                assert b"Restored" not in (production / "index.html").read_bytes()
            except BaseException:
                print(log_path.read_text())
                raise
            finally:
                stop(process)
    print("independent theme build/check, old-contract rejection, assets, preview 404/redirect, reload and recovery passed")


if __name__ == "__main__":
    main()
