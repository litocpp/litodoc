import { highlightCode } from "../src/components/prose/highlight";
import { LitoDocProseElement } from "../src/components/prose/element";

customElements.define("lito-doc-prose", LitoDocProseElement);

function assert(value: unknown, message: string): void {
  if (!value) throw new Error(message);
}

const samples = [
  'export module demo;\nimport std;\ntemplate<class T> requires true\nT f(T x);',
  '#define VALUE(x) \\\n ((x) + 42)\n// comment\nauto s = R"tag(hello\nworld)tag";',
  '[[nodiscard]] auto f(const Vec<Option<T>>& x) -> Result<T, Error>;',
  'auto 名称 = u8"😀";\r\n/* multiline\ncomment */ char c = \'\\n\';',
  'template<class T\n"unterminated',
  'auto s = "</code><img src=x onerror=alert(1)> &";',
];

try {
  for (const text of samples) {
    const code = document.createElement("code");
    code.className = "language-cpp";
    code.textContent = text;
    highlightCode(code);
    assert(code.textContent === text, "text changed");
    assert(code.querySelector(".syntax-keyword, .syntax-string"), "no tokens");
    assert(!code.querySelector("img, script"), "unsafe HTML");
    const html = code.innerHTML;
    highlightCode(code);
    assert(code.innerHTML === html, "highlighted twice");
  }
  for (const language of ["unknown", "", "text"]) {
    const code = document.createElement("code");
    code.className = `language-${language}`;
    code.textContent = "auto x = 42;";
    highlightCode(code);
    assert(!code.childElementCount, "guessed language");
  }
  const prose = document.createElement("lito-doc-prose");
  prose.innerHTML = '<pre><code class="language-cpp">auto x = 42;</code></pre><pre class="signature"><code class="language-cpp">auto y = 1;</code></pre>';
  document.body.append(prose);
  const html = prose.innerHTML;
  prose.remove();
  document.body.append(prose);
  assert(prose.innerHTML === html, "reconnect changed markup");
  assert(prose.querySelectorAll(".code-frame").length === 1, "duplicate controls");
  assert(!prose.querySelector(".signature .syntax-keyword"), "API declaration reprocessed");
  const result = document.createElement("p");
  result.id = "result";
  result.textContent = "PASS: six C++ samples, escaping, text preservation, unknown languages, reconnect and API isolation";
  document.body.append(result);
} catch (error) {
  document.body.textContent = `FAIL: ${error}`;
  throw error;
}
