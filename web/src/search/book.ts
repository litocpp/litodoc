import type { SearchEntry, SearchRequest } from "./catalog";

type JsonObject = Record<string, unknown>;

function objectValue(value: unknown, context: string): JsonObject {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new TypeError(`${context} must be an object`);
  }
  return value as JsonObject;
}

function stringValue(value: JsonObject, name: string, context: string): string {
  const field = value[name];
  if (typeof field !== "string") {
    throw new TypeError(`${context} has no string '${name}'`);
  }
  return field;
}

function arrayValue(value: JsonObject, name: string, context: string): unknown[] {
  const field = value[name];
  if (!Array.isArray(field)) {
    throw new TypeError(`${context} has no array '${name}'`);
  }
  return field;
}

function pageEntry(title: string, url: string): SearchEntry {
  return {
    package: "",
    module: title,
    kind: "page",
    name: title,
    "qualified-name": title,
    url,
  };
}

function headingEntry(
  title: string,
  url: string,
  heading: JsonObject,
  index: number,
): SearchEntry | undefined {
  const level = heading.level;
  if (typeof level !== "number") {
    throw new TypeError(`Book heading ${index} has no number 'level'`);
  }
  if (level === 1) return undefined;
  const text = stringValue(heading, "text", `Book heading ${index}`);
  const anchor = stringValue(heading, "anchor", `Book heading ${index}`);
  return {
    package: "",
    module: title,
    kind: "section",
    name: text,
    "qualified-name": text,
    url: `${url}#${anchor}`,
  };
}

export async function loadBookSearchCatalog(
  url: string,
  request: SearchRequest = fetch,
): Promise<SearchEntry[]> {
  const manifestResponse = await request(url);
  if (!manifestResponse.ok) {
    throw new Error(
      `Book search manifest request failed with status ${manifestResponse.status}`,
    );
  }
  const manifest = objectValue(await manifestResponse.json(), "Book search manifest");
  if (manifest.format !== "lito-book-data" || manifest.version !== 1) {
    throw new TypeError("Book search manifest has an unsupported format");
  }
  const descriptors = arrayValue(manifest, "pages", "Book search manifest");
  const rootUrl = new URL("../", new URL(url, document.baseURI));
  const pages = await Promise.all(
    descriptors.map(async (value, index) => {
      const descriptor = objectValue(value, `Book page descriptor ${index}`);
      const data = stringValue(descriptor, "data", `Book page descriptor ${index}`);
      const response = await request(new URL(data, rootUrl).href);
      if (!response.ok) {
        throw new Error(`Book search page request failed with status ${response.status}`);
      }
      return {
        descriptor,
        page: objectValue(await response.json(), `Book page ${index}`),
      };
    }),
  );

  const entries: SearchEntry[] = [];
  for (const [index, { descriptor, page }] of pages.entries()) {
    const title = stringValue(
      descriptor,
      "title",
      `Book page descriptor ${index}`,
    );
    const pageUrl = stringValue(descriptor, "url", `Book page descriptor ${index}`);
    entries.push(pageEntry(title, pageUrl));
    const headings = arrayValue(page, "headings", `Book page ${index}`);
    for (const [headingIndex, value] of headings.entries()) {
      const heading = objectValue(value, `Book heading ${headingIndex}`);
      const entry = headingEntry(title, pageUrl, heading, headingIndex);
      if (entry) entries.push(entry);
    }
  }
  return entries;
}
