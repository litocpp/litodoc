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
  #select?: HTMLSelectElement;

  connectedCallback(): void {
    if (this.#events) return;
    const select = this.querySelector("[data-theme-select]");
    if (!(select instanceof HTMLSelectElement)) {
      console.error("lito-doc-theme-picker requires a select");
      return;
    }

    const events = new AbortController();
    this.#events = events;
    this.#select = select;
    this.apply(readBrowserTheme(), false);
    select.addEventListener(
      "change",
      () => {
        const mode = parseTheme(select.value);
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
    this.#select = undefined;
    this.removeAttribute("data-ready");
  }

  get mode(): ThemeMode {
    return parseTheme(this.#select?.value);
  }

  set mode(mode: ThemeMode) {
    this.apply(parseTheme(mode), true);
  }

  apply(mode: ThemeMode, persist: boolean): void {
    if (this.#select) this.#select.value = mode;
    applyTheme(document.documentElement, mode);
    if (persist) storeBrowserTheme(mode);
  }
}
