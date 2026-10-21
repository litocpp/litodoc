local esbuild = lito.tool("esbuild")
local embed = require("@lito.resource")
local target = lito.target({ kind = "lib", name = "litodoc-web" })

local frontend_sources = {
  "static/fonts/IBMPlexSans-Bold-Latin1.woff2",
  "static/fonts/IBMPlexMono-Bold-Latin1.woff2",
  "app.ts",
  "theme-bootstrap.ts",
  "theme.ts",
  "search/catalog.ts",
  "search/book.ts",
  "navigation/data.ts",
  "components/registry.ts",
  "components/module-identity/element.ts",
  "components/module-identity/style.css",
  "components/navigation/element.ts",
  "components/navigation/style.css",
  "components/page-navigation/element.ts",
  "components/page-navigation/style.css",
  "components/prose/element.ts",
  "components/prose/highlight.ts",
  "vendor/highlight.js/core.cjs",
  "vendor/highlight.js/cpp.cjs",
  "styles/syntax.css",
  "components/prose/style.css",
  "components/search/element.ts",
  "components/search/style.css",
  "components/shell/element.ts",
  "components/shell/style.css",
  "components/theme-picker/element.ts",
  "components/theme-picker/style.css",
  "styles/base.css",
  "styles/content.css",
  "styles/style.css",
  "styles/tokens.css",
}

local resources = {
  { path = "static/highlight-LICENSE.txt", source = "src/vendor/highlight.js/LICENSE" },
  { path = "static/fonts/LICENSE.txt", source = "src/static/fonts/LICENSE.txt" },
  { path = "frontend.json", source = "frontend.json" },
  { path = "static/app.js", entry = "app.ts", target = "es2020" },
  { path = "static/favicon.svg", source = "src/static/favicon.svg" },
  { path = "static/style.css", entry = "styles/style.css" },
  { path = "static/theme-bootstrap.js", entry = "theme-bootstrap.ts", target = "es2020" },
  { path = "static/theme-icons.svg", source = "src/static/theme-icons.svg" },
  { path = "templates/book-page.html", source = "src/templates/book-page.html" },
  { path = "templates/book-root.html", source = "src/templates/book-root.html" },
  { path = "templates/module.html", source = "src/templates/module.html" },
  { path = "templates/package.html", source = "src/templates/package.html" },
  { path = "templates/partials/foot.html", source = "src/templates/partials/foot.html" },
  { path = "templates/partials/head.html", source = "src/templates/partials/head.html" },
  { path = "templates/root.html", source = "src/templates/root.html" },
  { path = "templates/symbol.html", source = "src/templates/symbol.html" },
}

local files = {}
for index = 1, #resources do
  local resource = resources[index]
  local output = "frontend/default/" .. resource.path
  local generated
  if resource.entry then
    local args = {
      resource.entry,
      "--bundle",
      "--minify",
      "--loader:.woff2=dataurl",
      "--outfile=@OUTPUT@",
    }
    if resource.target then
      args[#args + 1] = "--target=" .. resource.target
    end
    generated = lito.run({
      tool = esbuild,
      cwd = "src",
      args = args,
      inputs = frontend_sources,
      outputs = { output },
    })
    generated = generated.outputs[1]
  else
    generated = lito.copy({
      input = resource.source,
      output = output,
    })
    generated = generated.output
  end
  files[#files + 1] = { input = generated, path = resource.path }
end
embed.embed({
  target = target, module = "lito.doc.web", namespace = "lito::doc::web",
  name = "embedded_resources", files = files,
})
