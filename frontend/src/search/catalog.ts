export type SearchEntry = {
  package: string;
  module: string;
  kind: string;
  name: string;
  "qualified-name": string;
  url: string;
};

declare global {
  interface Window {
    __TENON_DOC_SEARCH__?: unknown;
  }
}

const stringField = (value: Record<string, unknown>, name: string, index: number): string => {
  const field = value[name];
  if (typeof field !== "string") {
    throw new TypeError(`search entry ${index} has no string '${name}'`);
  }
  return field;
};

export function readSearchCatalog(value: unknown): SearchEntry[] {
  if (value === undefined) return [];
  if (!Array.isArray(value)) throw new TypeError("search catalog must be an array");
  return value.map((entry, index) => {
    if (typeof entry !== "object" || entry === null || Array.isArray(entry)) {
      throw new TypeError(`search entry ${index} must be an object`);
    }
    const object = entry as Record<string, unknown>;
    return {
      package: stringField(object, "package", index),
      module: stringField(object, "module", index),
      kind: stringField(object, "kind", index),
      name: stringField(object, "name", index),
      "qualified-name": stringField(object, "qualified-name", index),
      url: stringField(object, "url", index),
    };
  });
}

export function searchCatalog(
  entries: readonly SearchEntry[],
  query: string,
  limit = 12,
): SearchEntry[] {
  const normalized = query.trim().toLowerCase();
  if (!normalized || limit <= 0) return [];
  return entries
    .filter((entry) =>
      `${entry["qualified-name"]} ${entry.module} ${entry.package} ${entry.kind}`
        .toLowerCase()
        .includes(normalized),
    )
    .slice(0, limit);
}
