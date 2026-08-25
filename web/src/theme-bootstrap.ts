import { applyTheme, readBrowserTheme } from "./theme";

const root = document.documentElement;
root.toggleAttribute("data-document-pending", true);
window.setTimeout(() => root.removeAttribute("data-document-pending"), 3000);
applyTheme(root, readBrowserTheme());
