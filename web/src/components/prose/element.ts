export class LitoDocProseElement extends HTMLElement {
  #events?: AbortController;

  connectedCallback(): void {
    if (this.#events) return;
    const events = new AbortController();
    this.#events = events;
    this.#upgradeHeadings();
    this.#upgradeCodeBlocks();
    this.#upgradeTables();
    this.addEventListener("click", (event) => void this.#handleClick(event), {
      signal: events.signal,
    });
    this.toggleAttribute("data-ready", true);
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.removeAttribute("data-ready");
  }

  #upgradeHeadings(): void {
    for (const heading of this.querySelectorAll<HTMLElement>("h2[id], h3[id], h4[id], h5[id], h6[id]")) {
      if (heading.querySelector(":scope > .heading-anchor")) continue;
      const anchor = document.createElement("a");
      anchor.className = "heading-anchor";
      anchor.href = `#${heading.id}`;
      anchor.setAttribute("aria-label", `Link to ${heading.textContent?.trim() || "section"}`);
      anchor.textContent = "#";
      heading.append(anchor);
    }
  }

  #upgradeCodeBlocks(): void {
    const blocks = Array.from(this.querySelectorAll<HTMLPreElement>("pre"));
    for (const block of blocks) {
      if (block.classList.contains("signature") || block.classList.contains("source-code")) continue;
      if (block.parentElement?.classList.contains("code-frame")) continue;
      const frame = document.createElement("div");
      frame.className = "code-frame";
      const actions = document.createElement("div");
      actions.className = "code-actions";
      const copy = document.createElement("button");
      copy.type = "button";
      copy.dataset.copyCode = "";
      copy.title = "Copy code";
      copy.setAttribute("aria-label", "Copy code");
      copy.textContent = "Copy";
      actions.append(copy);
      block.before(frame);
      frame.append(actions, block);
      block.tabIndex = 0;
    }
  }

  #upgradeTables(): void {
    const tables = Array.from(this.querySelectorAll<HTMLTableElement>("table"));
    for (const table of tables) {
      if (table.parentElement?.classList.contains("table-frame")) continue;
      const frame = document.createElement("div");
      frame.className = "table-frame";
      table.before(frame);
      frame.append(table);
    }
  }

  async #handleClick(event: Event): Promise<void> {
    if (!(event.target instanceof Element)) return;
    const button = event.target.closest<HTMLButtonElement>("[data-copy-code]");
    if (!button) return;
    const code = button.closest(".code-frame")?.querySelector("code");
    if (!(code instanceof HTMLElement)) return;
    const copied = await this.#copy(code.innerText);
    button.textContent = copied ? "Copied" : "Copy";
    button.toggleAttribute("data-copied", copied);
    window.setTimeout(() => {
      button.textContent = "Copy";
      button.removeAttribute("data-copied");
    }, 1400);
  }

  async #copy(value: string): Promise<boolean> {
    try {
      await navigator.clipboard.writeText(value);
      return true;
    } catch {
      const selection = window.getSelection();
      const code = Array.from(this.querySelectorAll("code")).find((item) => item.innerText === value);
      if (!selection || !code) return false;
      const range = document.createRange();
      range.selectNodeContents(code);
      selection.removeAllRanges();
      selection.addRange(range);
      return false;
    }
  }
}
