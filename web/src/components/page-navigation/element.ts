export class LitoDocPageNavigationElement extends HTMLElement {
  connectedCallback(): void {
    const navigation = this.querySelector("nav");
    if (!(navigation instanceof HTMLElement)) {
      console.error("lito-doc-page-navigation requires navigation content");
      return;
    }
    navigation.querySelector<HTMLAnchorElement>(".book-previous")?.setAttribute("rel", "prev");
    navigation.querySelector<HTMLAnchorElement>(".book-next")?.setAttribute("rel", "next");
    this.toggleAttribute("data-ready", true);
  }

  disconnectedCallback(): void {
    this.removeAttribute("data-ready");
  }
}
