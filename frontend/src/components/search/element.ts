import { searchCatalog, type SearchEntry } from "../../search/catalog";

export class TenonDocSearchElement extends HTMLElement {
  static observedAttributes = ["root-prefix"];

  #entries: readonly SearchEntry[] = [];
  #events?: AbortController;
  #input?: HTMLInputElement;
  #results?: HTMLElement;

  connectedCallback(): void {
    if (this.#events) return;
    const input = this.querySelector("[data-search-input]");
    const results = this.querySelector("[data-search-results]");
    if (!(input instanceof HTMLInputElement) || !(results instanceof HTMLElement)) {
      console.error("tenon-doc-search requires an input and results container");
      return;
    }

    const events = new AbortController();
    this.#events = events;
    this.#input = input;
    this.#results = results;
    input.setAttribute("aria-expanded", "false");
    input.addEventListener("input", () => this.updateResults(), { signal: events.signal });
    input.addEventListener(
      "keydown",
      (event) => {
        if (event.key !== "Escape") return;
        input.value = "";
        this.hideResults();
        input.blur();
      },
      { signal: events.signal },
    );
    document.addEventListener(
      "keydown",
      (event) => {
        const target = event.target;
        if (
          (event.key === "/" || event.key.toLowerCase() === "s") &&
          !event.altKey &&
          !event.ctrlKey &&
          !event.metaKey &&
          !(target instanceof HTMLInputElement) &&
          !(target instanceof HTMLTextAreaElement) &&
          !(target instanceof HTMLSelectElement) &&
          !(target instanceof HTMLElement && target.isContentEditable)
        ) {
          event.preventDefault();
          input.focus();
        }
      },
      { signal: events.signal },
    );
    document.addEventListener(
      "click",
      (event) => {
        if (event.target instanceof Node && !this.contains(event.target)) this.hideResults();
      },
      { signal: events.signal },
    );
    this.toggleAttribute("data-ready", true);
    this.updateResults();
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.#input = undefined;
    this.#results = undefined;
    this.removeAttribute("data-ready");
  }

  attributeChangedCallback(): void {
    this.updateResults();
  }

  get entries(): readonly SearchEntry[] {
    return this.#entries;
  }

  set entries(entries: readonly SearchEntry[]) {
    this.#entries = entries;
    this.updateResults();
  }

  get rootPrefix(): string {
    return this.getAttribute("root-prefix") ?? "";
  }

  hideResults(): void {
    if (!this.#results || !this.#input) return;
    this.#results.hidden = true;
    this.#results.replaceChildren();
    this.#input.setAttribute("aria-expanded", "false");
  }

  updateResults(): void {
    if (!this.#input || !this.#results) return;
    const matches = searchCatalog(this.#entries, this.#input.value);
    if (!this.#input.value.trim()) {
      this.hideResults();
      return;
    }

    this.#results.replaceChildren();
    const status = document.createElement("div");
    status.className = "search-status";
    status.setAttribute("role", "status");
    status.textContent = matches.length ? `${matches.length} matches` : "No matching API";
    this.#results.append(status);
    for (const entry of matches) {
      const link = document.createElement("a");
      link.href = `${this.rootPrefix}${entry.url}`;
      const identity = document.createElement("span");
      identity.textContent = entry["qualified-name"];
      const context = document.createElement("small");
      context.textContent = `${entry.kind} · ${entry.module}`;
      link.append(identity, context);
      this.#results.append(link);
    }
    this.#results.hidden = false;
    this.#input.setAttribute("aria-expanded", "true");
  }
}
