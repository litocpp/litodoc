(() => {
  const saved = window.localStorage.getItem("tenon-doc-theme");
  if (saved === "light" || saved === "dark") {
    document.documentElement.dataset.theme = saved;
  }
})();
