import {
  loadSearchCatalog,
  searchCatalog,
  type SearchContext,
  type SearchEntry,
} from "../../search/catalog";
import { loadBookSearchCatalog } from "../../search/book";

export class LitoDocSearchElement extends HTMLElement {
  static observedAttributes = [
    "catalog-url",
    "current-module",
    "current-package",
    "mode",
    "root-prefix",
  ];

  #entries?: readonly SearchEntry[];
  #events?: AbortController;
  #input?: HTMLInputElement;
  #load?: Promise<void>;
  #loadError?: unknown;
  #results?: HTMLElement;

  connectedCallback(): void {
    if (this.#events) return;
    const input = this.querySelector("[data-search-input]");
    const results = this.querySelector("[data-search-results]");
    if (!(input instanceof HTMLInputElement) || !(results instanceof HTMLElement)) {
      console.error("lito-doc-search requires an input and results container");
      return;
    }

    const events = new AbortController();
    this.#events = events;
    this.#input = input;
    this.#results = results;
    input.setAttribute("aria-expanded", "false");
    input.addEventListener("focus", () => void this.loadEntries(), { signal: events.signal });
    input.addEventListener("input", () => void this.updateResults(), { signal: events.signal });
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
        const commandSearch =
          event.key.toLowerCase() === "k" &&
          !event.altKey &&
          !event.shiftKey &&
          (event.ctrlKey || event.metaKey);
        if (
          (event.key === "/" || commandSearch) &&
          !event.altKey &&
          (commandSearch || (!event.ctrlKey && !event.metaKey)) &&
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
    void this.updateResults();
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.#input = undefined;
    this.#results = undefined;
    this.removeAttribute("data-ready");
  }

  attributeChangedCallback(name: string, previous: string | null, current: string | null): void {
    if ((name === "catalog-url" || name === "mode") && previous !== current) {
      this.#entries = undefined;
      this.#load = undefined;
      this.#loadError = undefined;
    }
    void this.updateResults();
  }

  get entries(): readonly SearchEntry[] {
    return this.#entries ?? [];
  }

  set entries(entries: readonly SearchEntry[]) {
    this.#entries = entries;
    this.#load = undefined;
    this.#loadError = undefined;
    void this.updateResults();
  }

  get catalogUrl(): string {
    return this.getAttribute("catalog-url") ?? "";
  }

  get rootPrefix(): string {
    return this.getAttribute("root-prefix") ?? "";
  }

  get searchContext(): SearchContext {
    return {
      package: this.getAttribute("current-package") || undefined,
      module: this.getAttribute("current-module") || undefined,
    };
  }

  hideResults(): void {
    if (!this.#results || !this.#input) return;
    this.#results.hidden = true;
    this.#results.replaceChildren();
    this.#input.setAttribute("aria-expanded", "false");
  }

  async loadEntries(): Promise<void> {
    if (this.#entries || this.#loadError) return;
    if (!this.#load) {
      const url = this.catalogUrl;
      if (!url) {
        this.#loadError = new Error("lito-doc-search requires catalog-url or entries");
        console.error(this.#loadError);
        return;
      }
      const load =
        this.getAttribute("mode") === "book" ? loadBookSearchCatalog : loadSearchCatalog;
      this.#load = load(url)
        .then((entries) => {
          this.#entries = entries;
        })
        .catch((error: unknown) => {
          this.#loadError = error;
          console.error(error);
        });
    }
    await this.#load;
  }

  showStatus(message: string): void {
    if (!this.#results || !this.#input) return;
    this.#results.replaceChildren();
    const status = document.createElement("div");
    status.className = "search-status";
    status.setAttribute("role", "status");
    status.textContent = message;
    this.#results.append(status);
    this.#results.hidden = false;
    this.#input.setAttribute("aria-expanded", "true");
  }

  async updateResults(): Promise<void> {
    if (!this.#input || !this.#results) return;
    if (!this.#input.value.trim()) {
      this.hideResults();
      return;
    }

    if (!this.#entries) {
      this.showStatus("Loading search index…");
      await this.loadEntries();
    }
    if (!this.#input.value.trim()) {
      this.hideResults();
      return;
    }
    if (this.#loadError || !this.#entries) {
      this.showStatus("Search index unavailable");
      return;
    }

    const matches = searchCatalog(
      this.#entries,
      this.#input.value,
      12,
      this.searchContext,
    );

    this.#results.replaceChildren();
    const status = document.createElement("div");
    status.className = "search-status";
    status.setAttribute("role", "status");
    status.textContent = matches.length ? `${matches.length} matches` : "No results found";
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
