"""Exercise concept references through the public extraction protocol."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="litodoc-concepts-") as directory:
    root = Path(directory)
    source = root / "concepts.cpp"
    source.write_text('''
namespace first { template<class T, class... Traits> concept Impled = true; }
namespace second { template<class T> concept Impled = true; }
#define METHODS \\
  template<class T> requires first::Impled<T> void macro_member();
struct Sample {
  METHODS
  template<class T> requires second::Impled<T> void source_member();
  template<class T> void trailing() requires first::Impled<T>;
};
template<class T> requires (first::Impled<T> && second::Impled<T>)
void global_function();
''')
    request = {
        "format": "litodoc-extract", "version": 1, "request_id": "concepts",
        "package": {"name": "concepts", "identity": "path+concepts"},
        "package_root": str(root),
        "target": {"name": "concepts", "kind": "lib"},
        "unit": {"identity": "concepts", "kind": "source", "is_interface": False,
                 "source": str(source), "module": "concepts"},
        "invocation": {"cwd": str(root), "arguments": [sys.argv[2], "-std=c++20", str(source)]},
        "compiler": {"identity": "clang", "target": "x86_64-unknown-linux-gnu"},
        "imported_artifacts": [],
    }
    request_path, response_path = root / "request.json", root / "response.json"
    request_path.write_text(json.dumps(request))
    subprocess.run([sys.argv[1], "extract", "--request", str(request_path),
                    "--response", str(response_path)], check=True)
    response = json.loads(response_path.read_text())
    declarations = response["declarations"]
    for item in declarations:
        if item["kind"] == "concept":
            for field in ("signature", "scope_signature"):
                assert ">\nconcept Impled" in item[field], item
    concepts = {item["qualified_name"]: item["semantic_identity"]
                for item in declarations if item["kind"] == "concept"}
    expected = {"macro_member": ["first::Impled"], "source_member": ["second::Impled"],
                "trailing": ["first::Impled"],
                "global_function": ["first::Impled", "second::Impled"]}
    for item in declarations:
        if item["name"] not in expected:
            continue
        for field in ("signature", "scope_signature"):
            refs = [r for r in item[field + "_references"] if r["kind"] == "concept"]
            assert [r["semantic_identity"] for r in refs] == [concepts[n] for n in expected[item["name"]]], item
            raw = item[field].encode()
            assert all(raw[r["begin"]:r["end"]] == b"Impled" for r in refs), item
        del expected[item["name"]]
    assert not expected, expected
    print("PASS: concept template line breaks, source, macro, trailing and qualified same-name references")
