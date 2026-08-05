export type SearchEntry = {
  package: string;
  module: string;
  kind: string;
  name: string;
  "qualified-name": string;
  url: string;
};

export type SearchContext = {
  package?: string;
  module?: string;
};

export type SearchRequest = (
  url: string,
) => Promise<{ ok: boolean; status: number; json(): Promise<unknown> }>;

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

export async function loadSearchCatalog(
  url: string,
  request: SearchRequest = fetch,
): Promise<SearchEntry[]> {
  const response = await request(url);
  if (!response.ok) {
    throw new Error(`search catalog request failed with status ${response.status}`);
  }
  return readSearchCatalog(await response.json());
}

const compareText = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

const matchRank = (entry: SearchEntry, query: string): number | undefined => {
  const name = entry.name.toLowerCase();
  const qualifiedName = entry["qualified-name"].toLowerCase();
  const module = entry.module.toLowerCase();
  const packageName = entry.package.toLowerCase();
  const kind = entry.kind.toLowerCase();
  if (qualifiedName === query) return 0;
  if (name === query) return 1;
  if (qualifiedName.startsWith(query)) return 2;
  if (name.startsWith(query)) return 3;
  if (qualifiedName.includes(query)) return 4;
  if (module.includes(query) || packageName.includes(query) || kind.includes(query)) return 5;
  return undefined;
};

const contextRank = (entry: SearchEntry, context: SearchContext): number => {
  if (context.module && entry.module === context.module) return 0;
  if (context.package && entry.package === context.package) return 1;
  return 2;
};

const kindRank = (kind: string): number => {
  switch (kind) {
    case "record": return 0;
    case "enum": return 1;
    case "concept": return 2;
    case "function": return 3;
    case "alias": return 4;
    case "variable": return 5;
    case "namespace": return 6;
    case "field": return 7;
    default: return 8;
  }
};

export function searchCatalog(
  entries: readonly SearchEntry[],
  query: string,
  limit = 12,
  context: SearchContext = {},
): SearchEntry[] {
  const normalized = query.trim().toLowerCase();
  if (!normalized || limit <= 0) return [];
  return entries
    .map((entry) => ({ entry, rank: matchRank(entry, normalized) }))
    .filter((candidate): candidate is { entry: SearchEntry; rank: number } =>
      candidate.rank !== undefined,
    )
    .sort((left, right) =>
      left.rank - right.rank ||
      contextRank(left.entry, context) - contextRank(right.entry, context) ||
      kindRank(left.entry.kind) - kindRank(right.entry.kind) ||
      compareText(left.entry["qualified-name"], right.entry["qualified-name"]) ||
      compareText(left.entry.package, right.entry.package) ||
      compareText(left.entry.url, right.entry.url),
    )
    .map((candidate) => candidate.entry)
    .slice(0, limit);
}
