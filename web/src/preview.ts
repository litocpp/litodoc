const script = document.currentScript;
if (script instanceof HTMLScriptElement && script.dataset.generation !== undefined) {
  const generation = script.dataset.generation;
  const events = new EventSource("/__litobook/events");
  let reloading = false;
  events.addEventListener("generation", (event: MessageEvent<string>) => {
    if (reloading || !/^\d+$/.test(event.data) || event.data === generation) return;
    reloading = true;
    events.close();
    window.location.reload();
  });
  window.addEventListener("pagehide", () => events.close(), { once: true });
}
