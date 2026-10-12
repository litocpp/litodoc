import core from "../../vendor/highlight.js/core.cjs";
import cpp from "../../vendor/highlight.js/cpp.cjs";

const highlighter = core.newInstance();
highlighter.configure({ classPrefix: "syntax-" });
highlighter.registerLanguage("cpp", cpp);

export function highlightCode(code: HTMLElement): void {
  if (code.dataset.highlighted || code.childElementCount) return;
  const language = Array.from(code.classList).find((name) => name.startsWith("language-"))?.slice(9).toLowerCase();
  if (!language || !["cpp", "c++", "cxx", "cc", "h", "hpp"].includes(language)) return;
  const text = code.textContent ?? "";
  if (text.length > 128 * 1024) return;
  try {
    const result = highlighter.highlight(text, { language: "cpp", ignoreIllegals: true });
    // HTML parsing normalizes literal CRLF, but character references preserve CR.
    code.innerHTML = result.value.replace(/\r/g, "&#13;");
    if (code.textContent !== text) {
      code.textContent = text;
      return;
    }
    code.dataset.highlighted = "true";
  } catch {
    code.textContent = text;
  }
}
