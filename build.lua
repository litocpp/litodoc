local esbuild = lito.tool("esbuild")

local function copy(input, output)
  lito.configure_file({
    package = "litodoc",
    input = input,
    output = output,
    values = {},
  })
end

local frontend_sources = {
  "src/app.ts",
  "src/theme-bootstrap.ts",
  "src/theme.ts",
  "src/search/catalog.ts",
  "src/components/registry.ts",
  "src/components/module-identity/element.ts",
  "src/components/module-identity/style.css",
  "src/components/search/element.ts",
  "src/components/search/style.css",
  "src/components/shell/element.ts",
  "src/components/shell/style.css",
  "src/components/theme-picker/element.ts",
  "src/components/theme-picker/style.css",
  "src/styles/base.css",
  "src/styles/content.css",
  "src/styles/source.css",
  "src/styles/style.css",
  "src/styles/tokens.css",
}

local function bundle(entry, output, target)
  local args = {
    entry,
    "--bundle",
    "--minify",
    "--outfile=@OUTPUT@",
  }
  if target then
    args[#args + 1] = "--target=" .. target
  end
  lito.run({
    tool = esbuild,
    package = "litodoc",
    cwd = "frontend",
    args = args,
    inputs = frontend_sources,
    outputs = { output },
  })
end

bundle("src/app.ts", "frontend/default/static/app.js", "es2020")
bundle("src/theme-bootstrap.ts", "frontend/default/static/theme-bootstrap.js", "es2020")
bundle("src/styles/style.css", "frontend/default/static/style.css")

copy("frontend/frontend.json", "frontend/default/frontend.json")
copy("frontend/src/static/favicon.svg", "frontend/default/static/favicon.svg")
copy("frontend/src/templates/root.html", "frontend/default/templates/root.html")
copy("frontend/src/templates/package.html", "frontend/default/templates/package.html")
copy("frontend/src/templates/module.html", "frontend/default/templates/module.html")
copy("frontend/src/templates/symbol.html", "frontend/default/templates/symbol.html")
copy("frontend/src/templates/source.html", "frontend/default/templates/source.html")
copy("frontend/src/templates/partials/head.html", "frontend/default/templates/partials/head.html")
copy("frontend/src/templates/partials/foot.html", "frontend/default/templates/partials/foot.html")
