export type ModuleNavigationEntry = {
  label: string;
  url: string;
};

export type ModuleNavigationData = {
  package: string;
  modules: ModuleNavigationEntry[];
};

export type NavigationRequest = (
  url: string,
  init?: RequestInit,
) => Promise<{ ok: boolean; status: number; json(): Promise<unknown> }>;

const objectValue = (value: unknown, context: string): Record<string, unknown> => {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new TypeError(`${context} must be an object`);
  }
  return value as Record<string, unknown>;
};

const stringField = (value: Record<string, unknown>, name: string, context: string): string => {
  const field = value[name];
  if (typeof field !== "string" || !field) {
    throw new TypeError(`${context} has no non-empty string '${name}'`);
  }
  return field;
};

const relativeUrl = (value: Record<string, unknown>, context: string): string => {
  const url = stringField(value, "url", context);
  if (
    url.startsWith("/") ||
    url.includes("\\") ||
    url.includes(":") ||
    url.includes("?") ||
    url.includes("#") ||
    url.includes("%") ||
    url.split("/").some((part) => part === "" || part === "." || part === "..")
  ) {
    throw new TypeError(`${context} has invalid relative URL '${url}'`);
  }
  return url;
};

export function readModuleNavigation(value: unknown): ModuleNavigationData {
  const root = objectValue(value, "module navigation");
  if (root.format !== "lito-doc-navigation" || root.version !== 1) {
    throw new TypeError("module navigation has an unsupported format or version");
  }
  const modules = root.modules;
  if (!Array.isArray(modules)) {
    throw new TypeError("module navigation has no modules array");
  }
  return {
    package: stringField(root, "package", "module navigation"),
    modules: modules.map((value, index) => {
      const context = `module navigation entry ${index}`;
      const entry = objectValue(value, context);
      return {
        label: stringField(entry, "label", context),
        url: relativeUrl(entry, context),
      };
    }),
  };
}

export async function loadModuleNavigation(
  url: string,
  request: NavigationRequest = fetch,
  signal?: AbortSignal,
): Promise<ModuleNavigationData> {
  const response = await request(url, { signal });
  if (!response.ok) {
    throw new Error(`module navigation request failed with status ${response.status}`);
  }
  return readModuleNavigation(await response.json());
}
