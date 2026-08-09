import { LitoDocModuleIdentityElement } from "./module-identity/element";
import { LitoDocSearchElement } from "./search/element";
import { LitoDocShellElement } from "./shell/element";
import { LitoDocThemePickerElement } from "./theme-picker/element";

const definitions: ReadonlyArray<readonly [string, CustomElementConstructor]> = [
  ["lito-doc-shell", LitoDocShellElement],
  ["lito-doc-search", LitoDocSearchElement],
  ["lito-doc-theme-picker", LitoDocThemePickerElement],
  ["lito-doc-module-identity", LitoDocModuleIdentityElement],
];

export function defineLitoDocComponents(registry = window.customElements): void {
  for (const [tag, constructor] of definitions) {
    const existing = registry.get(tag);
    if (existing === undefined) {
      registry.define(tag, constructor);
    } else if (existing !== constructor) {
      throw new Error(`custom element '${tag}' is already registered by another frontend`);
    }
  }
}
