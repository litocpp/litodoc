export class LitoDocSiteLinksElement extends HTMLElement {
  #events?: AbortController;

  connectedCallback(): void {
    if (this.#events) return;
    const details = this.querySelector("details");
    const summary = details?.querySelector("summary");
    if (!(details instanceof HTMLDetailsElement) || !(summary instanceof HTMLElement)) return;
    const events = new AbortController();
    this.#events = events;
    this.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && details.open) {
        details.open = false;
        summary.focus();
        event.stopPropagation();
      }
    }, { signal: events.signal });
    document.addEventListener("click", (event) => {
      if (event.target instanceof Node && !this.contains(event.target)) details.open = false;
    }, { signal: events.signal });
    this.addEventListener("focusout", (event) => {
      if (!(event.relatedTarget instanceof Node) || !this.contains(event.relatedTarget)) details.open = false;
    }, { signal: events.signal });
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    const details = this.querySelector("details");
    if (details instanceof HTMLDetailsElement) details.open = false;
  }
}
