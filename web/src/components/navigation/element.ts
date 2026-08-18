function documentUrl(value: string): string {
  const url = new URL(value, document.baseURI);
  url.hash = "";
  url.search = "";
  return url.href;
}

export class LitoDocNavigationElement extends HTMLElement {
  #events?: AbortController;
  #outline: ReadonlyArray<readonly [HTMLElement, HTMLAnchorElement]> = [];
  #updatePending = false;

  connectedCallback(): void {
    if (this.#events) return;
    const events = new AbortController();
    this.#events = events;
    this.#markCurrentPage();
    this.#prepareFolders(events.signal);
    this.#prepareOutline();
    window.addEventListener("scroll", () => this.#scheduleOutlineUpdate(), {
      passive: true,
      signal: events.signal,
    });
    window.addEventListener("resize", () => this.#scheduleOutlineUpdate(), {
      signal: events.signal,
    });
    this.toggleAttribute("data-ready", true);
    requestAnimationFrame(() => {
      this.#centerCurrentPage();
      this.#updateOutline();
    });
  }

  disconnectedCallback(): void {
    this.#events?.abort();
    this.#events = undefined;
    this.#outline = [];
    this.#updatePending = false;
    this.removeAttribute("data-ready");
  }

  #markCurrentPage(): void {
    const current = documentUrl(window.location.href);
    const links = this.querySelectorAll<HTMLAnchorElement>("[data-page-navigation] a[href]");
    for (const link of links) {
      if (documentUrl(link.href) !== current) continue;
      link.setAttribute("aria-current", "page");
      const item = link.closest<HTMLLIElement>("li");
      item?.toggleAttribute("data-current", true);
      const navigation = link.closest<HTMLElement>("[data-page-navigation]");
      if (item && navigation) this.#expandCurrentBranch(navigation, item);
      break;
    }
  }

  #expandCurrentBranch(navigation: HTMLElement, current: HTMLLIElement): void {
    const items = Array.from(navigation.querySelectorAll<HTMLLIElement>("li[data-depth]"));
    const currentIndex = items.indexOf(current);
    const currentDepth = Number.parseInt(current.dataset.depth ?? "", 10);
    if (currentIndex < 0 || !Number.isFinite(currentDepth)) return;
    for (let depth = 0; depth <= currentDepth; ++depth) {
      let ancestorIndex = currentIndex;
      while (ancestorIndex >= 0 && Number.parseInt(items[ancestorIndex].dataset.depth ?? "", 10) !== depth) {
        --ancestorIndex;
      }
      if (ancestorIndex < 0) continue;
      const ancestor = items[ancestorIndex];
      if (depth === 0) ancestor.toggleAttribute("data-current-section", true);
      if (ancestor.hasAttribute("data-folder")) ancestor.toggleAttribute("data-open", true);
      for (let index = ancestorIndex + 1; index < items.length; ++index) {
        const itemDepth = Number.parseInt(items[index].dataset.depth ?? "", 10);
        if (itemDepth <= depth) break;
        if (itemDepth === depth + 1) items[index].toggleAttribute("data-expanded", true);
      }
    }
  }

  #prepareFolders(signal: AbortSignal): void {
    const folders = this.querySelectorAll<HTMLLIElement>(".book-page-list li[data-folder]");
    for (const folder of folders) {
      const button = folder.querySelector<HTMLButtonElement>(":scope > .folder-toggle");
      if (!button) continue;
      this.#syncFolder(folder, button);
      button.addEventListener(
        "click",
        () => {
          const open = !folder.hasAttribute("data-open");
          folder.toggleAttribute("data-open", open);
          this.#setDescendants(folder, open);
          this.#syncFolder(folder, button);
        },
        { signal },
      );
    }
  }

  #setDescendants(folder: HTMLLIElement, open: boolean): void {
    const items = Array.from(
      folder.parentElement?.querySelectorAll<HTMLLIElement>("li[data-depth]") ?? [],
    );
    const folderIndex = items.indexOf(folder);
    const folderDepth = Number.parseInt(folder.dataset.depth ?? "", 10);
    if (folderIndex < 0 || !Number.isFinite(folderDepth)) return;
    for (let index = folderIndex + 1; index < items.length; ++index) {
      const item = items[index];
      const depth = Number.parseInt(item.dataset.depth ?? "", 10);
      if (depth <= folderDepth) break;
      if (open && depth === folderDepth + 1) item.toggleAttribute("data-expanded", true);
      if (!open) {
        item.removeAttribute("data-expanded");
        item.removeAttribute("data-open");
        item
          .querySelector<HTMLButtonElement>(":scope > .folder-toggle")
          ?.setAttribute("aria-expanded", "false");
      }
    }
  }

  #syncFolder(folder: HTMLLIElement, button: HTMLButtonElement): void {
    button.setAttribute("aria-expanded", String(folder.hasAttribute("data-open")));
  }

  #prepareOutline(): void {
    const navigation = this.querySelector<HTMLElement>("[data-outline-navigation]");
    if (!navigation) return;
    const links = Array.from(navigation.querySelectorAll<HTMLAnchorElement>('a[href^="#"]'));
    if (!links.length) {
      navigation.closest<HTMLElement>("[data-navigation-section]")?.toggleAttribute("hidden", true);
      return;
    }
    const outline: Array<readonly [HTMLElement, HTMLAnchorElement]> = [];
    for (const link of links) {
      const id = decodeURIComponent(link.hash.slice(1));
      const heading = document.getElementById(id);
      if (heading) outline.push([heading, link]);
    }
    this.#outline = outline;
  }

  #scheduleOutlineUpdate(): void {
    if (this.#updatePending) return;
    this.#updatePending = true;
    requestAnimationFrame(() => {
      this.#updatePending = false;
      this.#updateOutline();
    });
  }

  #updateOutline(): void {
    if (!this.#outline.length) return;
    const topbar = document.querySelector<HTMLElement>(".topbar")?.getBoundingClientRect().height ?? 72;
    const threshold = topbar + 32;
    let current = this.#outline[0]?.[1];
    for (const [heading, link] of this.#outline) {
      if (heading.getBoundingClientRect().top > threshold) break;
      current = link;
    }
    for (const [, link] of this.#outline) {
      if (link === current) {
        link.setAttribute("aria-current", "location");
      } else {
        link.removeAttribute("aria-current");
      }
    }
  }

  #centerCurrentPage(): void {
    const current = this.querySelector<HTMLElement>('[data-page-navigation] [aria-current="page"]');
    const sidebar = this.closest<HTMLElement>(".sidebar")?.querySelector<HTMLElement>(".sidebar-scroll");
    if (!current || !sidebar || sidebar.scrollHeight <= sidebar.clientHeight) return;
    const target = current.offsetTop - sidebar.clientHeight * 0.35;
    sidebar.scrollTop = Math.max(0, target);
  }
}
