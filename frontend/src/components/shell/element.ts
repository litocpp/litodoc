export class TenonDocShellElement extends HTMLElement {
  #events?: AbortController;
  #narrow?: MediaQueryList;
  #panel?: HTMLElement;
  #trigger?: HTMLButtonElement;

  connectedCallback(): void {
    if (this.#events) return;
    const trigger = this.querySelector("[data-drawer-trigger]");
    const panel = this.querySelector("[data-drawer-panel]");
    if (!(trigger instanceof HTMLButtonElement) || !(panel instanceof HTMLElement)) {
      console.error("tenon-doc-shell requires a drawer trigger and panel");
      return;
    }

    const events = new AbortController();
    const narrow = matchMedia("(max-width: 50rem)");
    this.#events = events;
    this.#narrow = narrow;
    this.#panel = panel;
    this.#trigger = trigger;
    trigger.setAttribute("aria-expanded", "false");
    trigger.addEventListener("click", () => this.toggleDrawer(), { signal: events.signal });
    panel.addEventListener(
      "click",
      (event) => {
        if (event.target instanceof Element && event.target.closest("a") && this.drawerOpen) {
          this.closeDrawer(true);
        }
      },
      { signal: events.signal },
    );
    document.addEventListener(
      "keydown",
      (event) => {
        if (event.key === "Escape") this.closeDrawer(true);
      },
      { signal: events.signal },
    );
    narrow.addEventListener(
      "change",
      () => {
        if (!narrow.matches) this.drawerOpen = false;
        this.#syncPanelAvailability();
      },
      { signal: events.signal },
    );
    this.#syncPanelAvailability();
    this.toggleAttribute("data-ready", true);
    void panel.offsetWidth;
    this.toggleAttribute("data-motion", true);
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.drawerOpen = false;
    if (this.#panel) this.#panel.inert = false;
    this.#narrow = undefined;
    this.#panel = undefined;
    this.#trigger = undefined;
    this.removeAttribute("data-ready");
    this.removeAttribute("data-motion");
  }

  get drawerOpen(): boolean {
    return this.hasAttribute("drawer-open");
  }

  set drawerOpen(open: boolean) {
    this.toggleAttribute("drawer-open", open);
    this.#trigger?.setAttribute("aria-expanded", String(open));
    this.#syncPanelAvailability();
  }

  toggleDrawer(): void {
    this.drawerOpen = !this.drawerOpen;
  }

  closeDrawer(restoreFocus: boolean): void {
    if (!this.drawerOpen) return;
    this.drawerOpen = false;
    if (restoreFocus) this.#trigger?.focus();
  }

  #syncPanelAvailability(): void {
    if (this.#panel) this.#panel.inert = Boolean(this.#narrow?.matches && !this.drawerOpen);
  }
}
