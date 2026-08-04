export class TenonDocModuleIdentityElement extends HTMLElement {
  connectedCallback(): void {
    const navigation = this.querySelector("nav");
    if (!(navigation instanceof HTMLElement)) {
      console.error("tenon-doc-module-identity requires navigation content");
      return;
    }
    this.toggleAttribute("data-ready", true);
  }

  disconnectedCallback(): void {
    this.removeAttribute("data-ready");
  }
}
