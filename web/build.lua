local esbuild = lito.tool("esbuild")

local frontend_sources = {
  "app.ts",
  "theme-bootstrap.ts",
  "theme.ts",
  "search/catalog.ts",
  "components/registry.ts",
  "components/module-identity/element.ts",
  "components/module-identity/style.css",
  "components/search/element.ts",
  "components/search/style.css",
  "components/shell/element.ts",
  "components/shell/style.css",
  "components/theme-picker/element.ts",
  "components/theme-picker/style.css",
  "styles/base.css",
  "styles/content.css",
  "styles/source.css",
  "styles/style.css",
  "styles/tokens.css",
}

local resources = {
  { path = "frontend.json", media_type = "application/json", source = "frontend.json" },
  { path = "static/app.js", media_type = "text/javascript", entry = "app.ts", target = "es2020" },
  { path = "static/favicon.svg", media_type = "image/svg+xml", source = "src/static/favicon.svg" },
  { path = "static/style.css", media_type = "text/css", entry = "styles/style.css" },
  { path = "static/theme-bootstrap.js", media_type = "text/javascript", entry = "theme-bootstrap.ts", target = "es2020" },
  { path = "templates/book-page.html", media_type = "text/html", source = "src/templates/book-page.html" },
  { path = "templates/book-root.html", media_type = "text/html", source = "src/templates/book-root.html" },
  { path = "templates/module.html", media_type = "text/html", source = "src/templates/module.html" },
  { path = "templates/package.html", media_type = "text/html", source = "src/templates/package.html" },
  { path = "templates/partials/foot.html", media_type = "text/html", source = "src/templates/partials/foot.html" },
  { path = "templates/partials/head.html", media_type = "text/html", source = "src/templates/partials/head.html" },
  { path = "templates/root.html", media_type = "text/html", source = "src/templates/root.html" },
  { path = "templates/source.html", media_type = "text/html", source = "src/templates/source.html" },
  { path = "templates/symbol.html", media_type = "text/html", source = "src/templates/symbol.html" },
}

for index = 1, #resources do
  for candidate = index + 1, #resources do
    if resources[candidate].path < resources[index].path then
      local value = resources[index]
      resources[index] = resources[candidate]
      resources[candidate] = value
    end
  end
end

for index = 1, #resources do
  local resource = resources[index]
  if resource.path == "" or resource.media_type == "" then
    error("frontend resources require a path and media type")
  end
  if index > 1 and resources[index - 1].path == resource.path then
    error("frontend resource path is repeated: " .. resource.path)
  end
  local output = "frontend/default/" .. resource.path
  if resource.entry then
    local args = {
      resource.entry,
      "--bundle",
      "--minify",
      "--outfile=@OUTPUT@",
    }
    if resource.target then
      args[#args + 1] = "--target=" .. resource.target
    end
    lito.run({
      tool = esbuild,
      cwd = "src",
      args = args,
      inputs = frontend_sources,
      outputs = { output },
    })
  else
    lito.configure_file({
      input = resource.source,
      output = output,
      values = {},
    })
  end
end

local arrays = ""
local entries = ""
for index = 1, #resources do
  local resource = resources[index]
  arrays = arrays
      .. "static constexpr unsigned char resource_"
      .. index
      .. "[] = {\n#embed \"../frontend/default/"
      .. resource.path
      .. "\"\n};\n\n"
  entries = entries
      .. "      FrontendResourceInput{ .path = \""
      .. resource.path
      .. "\"_str, .media_type = \""
      .. resource.media_type
      .. "\"_str, .contents = embedded_bytes(resource_"
      .. index
      .. ") },\n"
end

lito.configure_file({
  input = "src/embedded.cpp.in",
  output = "src/embedded.cpp",
  values = {
    resource_arrays = arrays,
    resource_entries = entries,
  },
})
