import { TenonDocModuleIdentityElement } from "./module-identity/element";
import { TenonDocSearchElement } from "./search/element";
import { TenonDocShellElement } from "./shell/element";
import { TenonDocThemePickerElement } from "./theme-picker/element";

const definitions: ReadonlyArray<readonly [string, CustomElementConstructor]> = [
  ["tenon-doc-shell", TenonDocShellElement],
  ["tenon-doc-search", TenonDocSearchElement],
  ["tenon-doc-theme-picker", TenonDocThemePickerElement],
  ["tenon-doc-module-identity", TenonDocModuleIdentityElement],
];

export function defineTenonDocComponents(registry = window.customElements): void {
  for (const [tag, constructor] of definitions) {
    const existing = registry.get(tag);
    if (existing === undefined) {
      registry.define(tag, constructor);
    } else if (existing !== constructor) {
      throw new Error(`custom element '${tag}' is already registered by another frontend`);
    }
  }
}
