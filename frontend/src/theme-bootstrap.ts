import { applyTheme, readBrowserTheme } from "./theme";

applyTheme(document.documentElement, readBrowserTheme());
