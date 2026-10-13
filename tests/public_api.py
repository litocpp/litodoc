"""Verify extraction, publication and optional Lito cache compatibility."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

doc, compiler = map(str, sys.argv[1:3])

def run(arguments, **kwargs):
    result = subprocess.run(arguments, text=True, capture_output=True, **kwargs)
    assert result.returncode == 0, result.stderr + result.stdout
    return result

with tempfile.TemporaryDirectory(prefix="litodoc-public-api-") as directory:
    root = Path(directory)
    sources = {
        "api.cppm": '''export module visibility:api;
export namespace api {
template<class T> concept Enabled = true;
struct Public {
  int field;
  union { int anonymous_field; double another_field; };
  void method();
  template<class T> requires Enabled<T> void constrained();
  struct Nested { int value; };
private:
  struct Hidden { int leak; void leak_method(); };
protected:
  struct Protected { int leak; };
};
struct Forward;
int answer();
}
namespace api { int helper(); struct Internal { int state; }; }
export using Indirect = api::Internal;
export api::Internal indirect();
export struct Reopened;
struct Reopened { int public_after_forward; };
namespace left { export template<class T> struct Twin { int field; void read(); }; }
''',
        "peer.cppm": '''export module visibility:peer;
export import :api;
namespace right { export template<class T> struct Twin { int field; void read(); }; }
''',
        "detail.cppm": '''export module visibility:detail;
export struct DetailOnly { int state; };
export int internal_partition_function();
''',
        "root.cppm": '''export module visibility;
export import :peer;
import :detail;
export extern "C" int public_c_function();
export { struct BlockExport {}; }
''',
        "impl.cpp": '''module visibility;
/// Definition documentation.
int api::answer() { return 42; }
/// Member definition documentation.
void api::Public::method() {}
template<class T> void left::Twin<T>::read() {}
struct api::Forward { int implementation_state; };
struct Local { int state; };
static int helper() { return 1; }
''',
        "ordinary.cpp": '''struct Plain { int field; private: struct Hidden { int leak; }; };
int ordinary_function() { return 1; }
''',
        "private.cppm": '''export module private_visibility;
export struct Exposed {};
module :private;
struct PrivateFragmentOnly { int state; };
''',
    }
    for name, contents in sources.items():
        (root / name).write_text(contents)
    flags = []
    for name, module in [("api.cppm", "visibility:api"), ("peer.cppm", "visibility:peer"), ("detail.cppm", "visibility:detail"),
                         ("root.cppm", "visibility")]:
        pcm = root / (name + ".pcm")
        run([compiler, "-std=c++20", "--precompile", str(root / name), *flags, "-o", str(pcm)])
        flags.append(f"-fmodule-file={module}={pcm}")

    def extract(name, module, interface):
        request = {
            "format": "litodoc-extract", "version": 2, "request_id": name,
            "package": {"name": "sample", "identity": "path+sample"}, "package_root": str(root),
            "target": {"name": "sample", "kind": "lib"},
            "unit": {"identity": name, "kind": "source", "is_interface": interface,
                     "source": str(root / name), "module": module},
            "invocation": {"cwd": str(root), "arguments": [compiler, "-std=c++20", *flags, str(root / name)]},
            "compiler": {"identity": "clang", "target": "x86_64-unknown-linux-gnu"},
            "imported_artifacts": [],
        }
        request_path, response_path = root / (name + ".request.json"), root / (name + ".response.json")
        request_path.write_text(json.dumps(request))
        run([doc, "extract", "--request", str(request_path), "--response", str(response_path)])
        response = json.loads(response_path.read_text())
        assert response["version"] == 2
        request["version"] = 1
        request_path.write_text(json.dumps(request))
        rejected = subprocess.run([doc, "extract", "--request", str(request_path),
                                   "--response", str(root / "old-response.json")], capture_output=True)
        assert rejected.returncode != 0 and not (root / "old-response.json").exists()
        return response_path, response

    responses = {}
    for name, module, interface in [("api.cppm", "visibility:api", True),
                                    ("peer.cppm", "visibility:peer", True),
                                    ("detail.cppm", "visibility:detail", True),
                                    ("root.cppm", "visibility", True), ("impl.cpp", "visibility", False),
                                    ("ordinary.cpp", "ordinary", False),
                                    ("private.cppm", "private_visibility", True)]:
        responses[name] = extract(name, module, interface)
    assert {x["module"] for x in responses["root.cppm"][1]["reexports"]} == {"visibility:peer"}, responses["root.cppm"][1]["reexports"]
    assert {x["module"] for x in responses["peer.cppm"][1]["reexports"]} == {"visibility:api"}
    hidden = next(x for x in responses["api.cppm"][1]["declarations"] if x["qualified_name"] == "api::Public::Hidden::leak")
    assert hidden["access"] == "public" and any(x["access"] == "private" for x in hidden["record_ancestors"])
    definition = next(x for x in responses["impl.cpp"][1]["declarations"] if x["qualified_name"] == "api::Public::method")
    assert definition["origin"] == "implementation" and definition["exported_redeclaration"]
    assert definition["owner"]["semantic_identity"]

    def package(name, module, names):
        return {"name": name, "version": "1.0.0", "source_identity": "path+" + name,
                "root_module": module, "profile": "debug", "root": str(root),
                "toolchain_version": "clang 22", "toolchain_target": "x86_64-unknown-linux-gnu",
                "language_standard": "c++20", "responses": [
                    {"path": str(responses[n][0]), "digest": hashlib.sha256(responses[n][0].read_bytes()).hexdigest()}
                    for n in names]}

    def generate(label, names, publication="site"):
        manifest = {"format": "litodoc-site", "version": 1, "data_api": 4, "template_api": 1,
                    "title": "Visibility", "output": str(root / label), "data_output": str(root / (label + "-data")),
                    "data_only": False, "publication": publication, "packages": [
                        package("sample", "visibility", names),
                        package("ordinary", "ordinary", ["ordinary.cpp"]),
                        package("private", "private_visibility", ["private.cppm"])]}
        path = root / (label + ".json")
        path.write_text(json.dumps(manifest))
        audit_path = root / (label + ".audit.json")
        run([doc, "generate", "--manifest", str(path), "--audit", str(audit_path)])
        audit = json.loads(audit_path.read_text())
        for info in audit["packages"]:
            assert info["published_symbols"] == info["declaration_counts"].get("public", 0)
        sample = next(p for p in audit["packages"] if p["name"] == "sample")
        assert sample["declaration_counts"]["non-public-access"] > 0
        assert sample["declaration_counts"]["internal-module"] > 0
        return {p: json.loads((root / (label + "-data") / "packages" / p / "doc.json").read_text())
                for p in ("sample", "ordinary", "private")}

    names = ["root.cppm", "api.cppm", "peer.cppm", "detail.cppm", "impl.cpp"]
    first = generate("site", names)
    reversed_site = generate("reversed", list(reversed(names)))
    assert first == reversed_site, "input order changed the public dataset"
    publications = generate("packages", names, "package-set")
    assert first == publications, "publication layout changed the public dataset"
    symbols = first["sample"]["symbols"]
    by_name = {s["qualified-name"]: s for s in symbols}
    assert "api::Public" in by_name and "api::Public::Nested" in by_name
    assert by_name["Reopened::public_after_forward"]["placement"] == "record-member"
    assert by_name["api::Public::field"]["placement"] == "record-member"
    anonymous = next((s for s in symbols if s["name"] == "anonymous_field"), None)
    assert anonymous is not None, list(by_name)
    assert anonymous["placement"] == "record-member" and anonymous["parent"] == by_name["api::Public"]["key"]
    assert by_name["api::answer"]["comment"] == "Definition documentation."
    assert by_name["api::Public::method"]["comment"] == "Member definition documentation."
    assert "public_c_function" in by_name
    assert "BlockExport" in by_name and "Indirect" in by_name and "indirect" in by_name
    assert by_name["left::Twin::field"]["parent"] == by_name["left::Twin"]["key"]
    assert by_name["right::Twin::field"]["parent"] == by_name["right::Twin"]["key"]
    assert by_name["left::Twin"]["semantic-identity"] != by_name["right::Twin"]["semantic-identity"]
    indirect_html = (root / "site" / "package/sample" / by_name["Indirect"]["page"]).read_text()
    assert "Internal" in indirect_html and ">Internal</a>" not in indirect_html
    assert "api::Forward" in by_name and "implementation_state" not in by_name["api::Forward"]["declaration"]["text"]
    assert not by_name["api::Forward"]["is-definition"]
    assert all(not any(x in name for x in ("Hidden", "Protected", "helper", "Local", "DetailOnly", "Internal", "implementation_state"))
               for name in by_name)
    assert all(s["placement"] == "record-member" for s in symbols if s["kind"] == "field")
    assert {m["name"] for m in first["sample"]["modules"]} == {"visibility", "visibility:api", "visibility:peer"}
    assert {s["qualified-name"] for s in first["private"]["symbols"]} == {"Exposed"}
    assert {s["qualified-name"] for s in first["ordinary"]["symbols"]} == {"Plain", "Plain::field", "ordinary_function"}
    public = by_name["api::Public"]
    html = (root / "site" / "package/sample" / public["page"]).read_text()
    assert ">Enabled</a>" in html and "implementation_state" not in html

    def reject_generate(label, response_names, error):
        manifest = {"format": "litodoc-site", "version": 1, "data_api": 4, "template_api": 1,
                    "title": "Invalid visibility", "output": str(root / label),
                    "data_output": str(root / (label + "-data")), "data_only": True,
                    "packages": [package("sample", "visibility", response_names)]}
        path = root / (label + ".json")
        path.write_text(json.dumps(manifest))
        rejected = subprocess.run([doc, "generate", "--manifest", str(path)], text=True, capture_output=True)
        assert rejected.returncode != 0 and error in rejected.stderr, rejected.stderr
        assert not (root / (label + "-data") / "manifest.json").exists()

    reject_generate("missing-root", ["api.cppm", "impl.cpp"], "has no interface unit")
    reject_generate("missing-partition", ["root.cppm", "detail.cppm"], "missing from extraction input")
    reject_generate("duplicate-interface", names + ["api.cppm"], "duplicate interface unit")
    saved = responses["api.cppm"][0].read_text()
    damaged = json.loads(saved)
    del damaged["declarations"][0]["origin"]
    responses["api.cppm"][0].write_text(json.dumps(damaged))
    reject_generate("missing-facts", names, "origin must be")
    damaged = json.loads(saved)
    damaged["declarations"][0]["parent"] = "invalid"
    responses["api.cppm"][0].write_text(json.dumps(damaged))
    reject_generate("invalid-parent", names, "parent is invalid")
    damaged = json.loads(saved)
    damaged["declarations"][0]["origin"] = "implementation"
    responses["api.cppm"][0].write_text(json.dumps(damaged))
    reject_generate("invalid-origin", names, "inconsistent module origin")
    responses["api.cppm"][0].write_text(saved)

    if len(sys.argv) > 3:
        lito = str(Path(sys.argv[3]).resolve())
        project = root / "cli"
        project.mkdir()
        (project / "lito.toml").write_text('''[package]
name = "doc-sample"
version = "0.1.0"
[lib]
name = "doc-sample"
module = "cli_visibility"
archive = "cli_visibility"
sources = ["interface.cppm", "implementation.cpp"]
''')
        (project / "interface.cppm").write_text("export module cli_visibility; export int answer();\n")
        (project / "implementation.cpp").write_text("module cli_visibility; int answer() { return 42; } static int hidden() { return 0; }\n")
        command = [lito, "doc", "--litodoc-executable", doc, "--output", str(project / "site"),
                   "--data-output", str(project / "data")]
        for expected in [(2, 0), (0, 2)]:
            completed = run(command, cwd=project)
            assert re.search(rf"{expected[0]} extracted, {expected[1]} reused", completed.stdout), completed.stdout
        cached = next((project / "build/debug/lito-doc-cache").glob("*/response.json"))
        original = json.loads(cached.read_text())
        original["version"] = 1
        cached.write_text(json.dumps(original))
        repaired = run(command, cwd=project)
        assert "1 extracted, 1 reused" in repaired.stdout, repaired.stdout
        assert json.loads(cached.read_text())["version"] == 2
        if len(sys.argv) > 4:
            old = subprocess.run([lito, "doc", "--litodoc-executable", str(Path(sys.argv[4]).resolve())], cwd=project,
                                 text=True, capture_output=True)
            assert old.returncode != 0 and "unsupported litodoc capabilities" in old.stderr, old.stderr + old.stdout
    print("PASS: public visibility, module closure, owner chains, definitions, order, layouts and protocol/cache compatibility")
