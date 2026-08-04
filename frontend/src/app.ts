type SearchEntry = {
  package: string;
  module: string;
  kind: string;
  name: string;
  "qualified-name": string;
  url: string;
};

declare global {
  interface Window {
    __TENON_DOC_SEARCH__?: SearchEntry[];
  }
}

const theme = document.querySelector<HTMLSelectElement>("#theme-mode");
const savedTheme = window.localStorage.getItem("tenon-doc-theme");
if (theme) {
  theme.value = savedTheme === "light" || savedTheme === "dark" ? savedTheme : "auto";
  theme.addEventListener("change", () => {
    const value = theme.value;
    if (value === "light" || value === "dark") {
      document.documentElement.dataset.theme = value;
      window.localStorage.setItem("tenon-doc-theme", value);
    } else {
      delete document.documentElement.dataset.theme;
      window.localStorage.removeItem("tenon-doc-theme");
    }
  });
}

const menu = document.querySelector<HTMLButtonElement>(".menu-button");
const sidebar = document.querySelector<HTMLElement>("#sidebar");
if (menu && sidebar) {
  const closeSidebar = () => {
    if (!sidebar.classList.contains("is-open")) return;
    sidebar.classList.remove("is-open");
    menu.setAttribute("aria-expanded", "false");
    menu.focus();
  };
  menu.addEventListener("click", () => {
    const open = sidebar.classList.toggle("is-open");
    menu.setAttribute("aria-expanded", String(open));
  });
  sidebar.addEventListener("click", (event) => {
    if (event.target instanceof HTMLAnchorElement && sidebar.classList.contains("is-open")) {
      closeSidebar();
    }
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeSidebar();
  });
}

const search = document.querySelector<HTMLInputElement>("#doc-search");
const results = document.querySelector<HTMLElement>("#search-results");
const rootPrefix = document.body.dataset.rootPrefix ?? "";
const entries = window.__TENON_DOC_SEARCH__ ?? [];

const hideResults = () => {
  if (!results) return;
  results.hidden = true;
  results.replaceChildren();
};

const updateResults = () => {
  if (!search || !results) return;
  const query = search.value.trim().toLocaleLowerCase();
  if (!query) {
    hideResults();
    return;
  }
  const matches = entries
    .filter((entry) =>
      `${entry["qualified-name"]} ${entry.module} ${entry.package} ${entry.kind}`
        .toLocaleLowerCase()
        .includes(query),
    )
    .slice(0, 12);
  results.replaceChildren();
  const status = document.createElement("div");
  status.className = "search-status";
  status.textContent = matches.length ? `${matches.length} matches` : "No matching API";
  results.append(status);
  for (const entry of matches) {
    const link = document.createElement("a");
    link.href = `${rootPrefix}${entry.url}`;
    const identity = document.createElement("span");
    identity.textContent = entry["qualified-name"];
    const context = document.createElement("small");
    context.textContent = `${entry.kind} · ${entry.module}`;
    link.append(identity, context);
    results.append(link);
  }
  results.hidden = false;
};

if (search) {
  search.addEventListener("input", updateResults);
  search.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      search.value = "";
      hideResults();
      search.blur();
    }
  });
  document.addEventListener("keydown", (event) => {
    if ((event.key === "/" || event.key.toLocaleLowerCase() === "s") &&
        !(event.target instanceof HTMLInputElement) &&
        !(event.target instanceof HTMLTextAreaElement)) {
      event.preventDefault();
      search.focus();
    }
  });
}

document.addEventListener("click", (event) => {
  if (results && search && !results.contains(event.target as Node) && event.target !== search) {
    hideResults();
  }
});

export {};
