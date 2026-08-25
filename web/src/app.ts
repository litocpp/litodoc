import { defineLitoDocComponents } from "./components/registry";
import { LitoDocNavigationElement } from "./components/navigation/element";

defineLitoDocComponents();

const navigationReady = Array.from(
  document.querySelectorAll<LitoDocNavigationElement>(
    "lito-doc-navigation[module-navigation-url]",
  ),
  (navigation) => navigation.ready,
);
void Promise.all(navigationReady).then(() => {
  document.documentElement.removeAttribute("data-document-pending");
});
