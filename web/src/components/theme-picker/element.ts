import {
  applyTheme,
  parseTheme,
  readBrowserTheme,
  storeBrowserTheme,
  THEME_STORAGE_KEY,
  type ThemeMode,
} from "../../theme";

export class LitoDocThemePickerElement extends HTMLElement {
  #events?: AbortController;
  #button?: HTMLButtonElement;
  #preference?: MediaQueryList;
  #mode: ThemeMode = "auto";

  connectedCallback(): void {
    if (this.#events) return;
    const button = this.querySelector("[data-theme-toggle]");
    if (!(button instanceof HTMLButtonElement)) {
      console.error("lito-doc-theme-picker requires a toggle button");
      return;
    }

    const events = new AbortController();
    const preference = window.matchMedia("(prefers-color-scheme: dark)");
    this.#events = events;
    this.#button = button;
    this.#preference = preference;
    this.apply(readBrowserTheme(), false);
    button.addEventListener(
      "click",
      () => {
        const mode = this.#resolvedTheme() === "dark" ? "light" : "dark";
        this.apply(mode, true);
        this.dispatchEvent(
          new CustomEvent<ThemeMode>("lito-theme-change", {
            bubbles: true,
            composed: true,
            detail: mode,
          }),
        );
      },
      { signal: events.signal },
    );
    preference.addEventListener(
      "change",
      () => {
        if (this.#mode === "auto") this.#syncButton();
      },
      { signal: events.signal },
    );
    window.addEventListener(
      "storage",
      (event) => {
        if (event.key === THEME_STORAGE_KEY || event.key === null) {
          this.apply(parseTheme(event.newValue), false);
        }
      },
      { signal: events.signal },
    );
    this.toggleAttribute("data-ready", true);
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.#button = undefined;
    this.#preference = undefined;
    this.removeAttribute("data-ready");
    this.removeAttribute("data-current-theme");
  }

  get mode(): ThemeMode {
    return this.#mode;
  }

  set mode(mode: ThemeMode) {
    this.apply(parseTheme(mode), true);
  }

  apply(mode: ThemeMode, persist: boolean): void {
    this.#mode = parseTheme(mode);
    applyTheme(document.documentElement, this.#mode);
    if (persist) storeBrowserTheme(this.#mode);
    this.#syncButton();
  }

  #resolvedTheme(): Exclude<ThemeMode, "auto"> {
    if (this.#mode !== "auto") return this.#mode;
    return this.#preference?.matches ? "dark" : "light";
  }

  #syncButton(): void {
    const current = this.#resolvedTheme();
    const target = current === "dark" ? "light" : "dark";
    this.dataset.currentTheme = current;
    this.#button?.setAttribute("aria-label", `Use ${target} theme`);
    this.#button?.setAttribute("title", `Use ${target} theme`);
  }
}
