import assert from "node:assert/strict";
import test from "node:test";

import { loadSearchCatalog, readSearchCatalog, searchCatalog } from "../src/search/catalog";
import { applyTheme, parseTheme } from "../src/theme";

test("search catalog validates once and filters deterministically", () => {
  const catalog = readSearchCatalog([
    {
      package: "rstd-core",
      module: "rstd.core:mem",
      kind: "function",
      name: "copy",
      "qualified-name": "rstd::mem::copy",
      url: "package/rstd-core/symbol/copy.html",
    },
    {
      package: "rstd-std",
      module: "rstd:sync",
      kind: "type",
      name: "Mutex",
      "qualified-name": "rstd::sync::Mutex",
      url: "package/rstd-std/symbol/mutex.html",
    },
    {
      package: "rstd-core",
      module: "rstd.core:alloc",
      kind: "alias",
      name: "Funcs",
      "qualified-name": "rstd::alloc::Allocator::Funcs",
      url: "package/rstd-core/symbol/allocator-funcs.html",
    },
    {
      package: "rstd-core",
      module: "rstd.core:alloc",
      kind: "record",
      name: "Allocator",
      "qualified-name": "rstd::alloc::Allocator",
      url: "package/rstd-core/symbol/allocator.html",
    },
    {
      package: "rstd-cppstd",
      module: "cppstd",
      kind: "alias",
      name: "allocator",
      "qualified-name": "std::allocator",
      url: "package/rstd-cppstd/symbol/allocator.html",
    },
  ]);
  assert.deepEqual(searchCatalog(catalog, "  MUTEX  "), [catalog[1]]);
  assert.deepEqual(
    searchCatalog(catalog, "Allocator", 12, {
      package: "rstd-core",
      module: "rstd.core:alloc",
    }),
    [catalog[3], catalog[4], catalog[2]],
  );
  assert.deepEqual(searchCatalog(catalog, "rstd::alloc::Allocator"), [catalog[3], catalog[2]]);
  assert.deepEqual(searchCatalog(catalog, ""), []);
  assert.throws(
    () => readSearchCatalog([{ package: "rstd" }]),
    /search entry 0 has no string 'module'/,
  );
});

test("search catalog loads through its published JSON URL", async () => {
  let requested = "";
  const catalog = await loadSearchCatalog("../../search-index.json", async (url) => {
    requested = url;
    return {
      ok: true,
      status: 200,
      async json() {
        return [
          {
            package: "rstd-core",
            module: "rstd.core:alloc",
            kind: "record",
            name: "Allocator",
            "qualified-name": "rstd::alloc::Allocator",
            url: "package/rstd-core/symbol/allocator.html",
          },
        ];
      },
    };
  });
  assert.equal(requested, "../../search-index.json");
  assert.equal(catalog[0]["qualified-name"], "rstd::alloc::Allocator");
  await assert.rejects(
    loadSearchCatalog("missing.json", async () => ({
      ok: false,
      status: 404,
      async json() {
        return [];
      },
    })),
    /status 404/,
  );
});

test("theme parsing and application share one rule", () => {
  assert.equal(parseTheme("light"), "light");
  assert.equal(parseTheme("dark"), "dark");
  assert.equal(parseTheme("sepia"), "auto");
  const root = { dataset: {} } as HTMLElement;
  applyTheme(root, "dark");
  assert.equal(root.dataset.theme, "dark");
  applyTheme(root, "auto");
  assert.equal(root.dataset.theme, undefined);
});

test("component registration is idempotent and detects conflicts", async () => {
  globalThis.HTMLElement = class {} as typeof HTMLElement;
  const { defineLitoDocComponents } = await import("../src/components/registry");
  class Registry {
    definitions = new Map<string, CustomElementConstructor>();

    define(name: string, constructor: CustomElementConstructor): void {
      if (this.definitions.has(name)) throw new Error(`duplicate '${name}'`);
      this.definitions.set(name, constructor);
    }

    get(name: string): CustomElementConstructor | undefined {
      return this.definitions.get(name);
    }
  }

  const registry = new Registry();
  defineLitoDocComponents(registry as CustomElementRegistry);
  defineLitoDocComponents(registry as CustomElementRegistry);
  assert.equal(registry.definitions.size, 4);

  const conflict = new Registry();
  conflict.define("lito-doc-shell", class extends HTMLElement {});
  assert.throws(
    () => defineLitoDocComponents(conflict as CustomElementRegistry),
    /custom element 'lito-doc-shell' is already registered by another frontend/,
  );
});
