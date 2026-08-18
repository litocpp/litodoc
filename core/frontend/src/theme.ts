export type ThemeMode = "auto" | "light" | "dark";

export const THEME_STORAGE_KEY = "lito-doc-theme";

export function parseTheme(value: unknown): ThemeMode {
  return value === "light" || value === "dark" ? value : "auto";
}

function browserStorage(): Storage | undefined {
  try {
    return window.localStorage;
  } catch {
    return undefined;
  }
}

export function readBrowserTheme(): ThemeMode {
  const storage = browserStorage();
  if (!storage) return "auto";
  try {
    return parseTheme(storage.getItem(THEME_STORAGE_KEY));
  } catch {
    return "auto";
  }
}

export function storeBrowserTheme(mode: ThemeMode): void {
  const storage = browserStorage();
  if (!storage) return;
  try {
    if (mode === "auto") {
      storage.removeItem(THEME_STORAGE_KEY);
    } else {
      storage.setItem(THEME_STORAGE_KEY, mode);
    }
  } catch {
    return;
  }
}

export function applyTheme(root: HTMLElement, mode: ThemeMode): void {
  if (mode === "auto") {
    delete root.dataset.theme;
  } else {
    root.dataset.theme = mode;
  }
}
