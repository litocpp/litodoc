import assert from "node:assert/strict";
import test from "node:test";

import { readSearchCatalog, searchCatalog } from "../src/search/catalog";
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
  ]);
  assert.deepEqual(searchCatalog(catalog, "  MUTEX  "), [catalog[1]]);
  assert.deepEqual(searchCatalog(catalog, "rstd", 1), [catalog[0]]);
  assert.deepEqual(searchCatalog(catalog, ""), []);
  assert.throws(
    () => readSearchCatalog([{ package: "rstd" }]),
    /search entry 0 has no string 'module'/,
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
  const { defineTenonDocComponents } = await import("../src/components/registry");
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
  defineTenonDocComponents(registry as CustomElementRegistry);
  defineTenonDocComponents(registry as CustomElementRegistry);
  assert.equal(registry.definitions.size, 4);

  const conflict = new Registry();
  conflict.define("tenon-doc-shell", class extends HTMLElement {});
  assert.throws(
    () => defineTenonDocComponents(conflict as CustomElementRegistry),
    /custom element 'tenon-doc-shell' is already registered by another frontend/,
  );
});
