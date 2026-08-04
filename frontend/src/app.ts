import { defineTenonDocComponents } from "./components/registry";
import { TenonDocSearchElement } from "./components/search/element";
import { readSearchCatalog } from "./search/catalog";

let entries: ReturnType<typeof readSearchCatalog> = [];
try {
  entries = readSearchCatalog(window.__TENON_DOC_SEARCH__);
} catch (error) {
  console.error(error);
}

defineTenonDocComponents();

for (const element of document.querySelectorAll("tenon-doc-search")) {
  if (element instanceof TenonDocSearchElement) {
    element.entries = entries;
  }
}
