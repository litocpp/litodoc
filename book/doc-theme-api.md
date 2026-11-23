# API documentation themes

API Doc themes use the same frontend bundle, template language and shared semantic
site/page metadata as Book themes, with their own documentation data model.
Only explicitly declared theme assets are published by a theme. The renderer does
not inject a default header, sidebar, brand, icon, font, stylesheet or application
script. Search and module-navigation datasets remain available for optional
client features, independently of whether a theme uses them.

## Manifest

```json
{
  "format": "lito-doc-frontend",
  "version": 2,
  "template-api": 1,
  "capabilities": ["api"],
  "data-api": 4,
  "doc-template-api": 2,
  "templates": {
    "root": "root.html",
    "package": "package.html",
    "module": "module.html",
    "symbol": "symbol.html"
  },
  "partials": [],
  "assets": []
}
```

All four entries are required. A combined Book/Doc theme declares both capabilities
and both context API 2 versions, and supplies each capability's required templates.
`template-api: 1` identifies the template language, not an old context contract.
The Doc data/search formats have not changed as part of the context migration.

Text conditions such as `{{#if symbol.kind == "record"}}` and
`{{#if module.name != package.root_module}}` let a theme choose its own
presentation from domain facts. Both operands must be text; guard null objects
before accessing members. See [Book themes](theme-api.md) for the shared template
syntax and escaping rules.

## Shared context

Optional values are null. Collections are empty arrays when absent. Guard optional
values before accessing members. Missing fields are errors rather than silent
empty strings. Ordinary text interpolation is HTML-escaped; raw interpolation
requires trusted rendered HTML.

| Value | Meaning |
| --- | --- |
| `site.title`, `site.language`, `site.package_count` | Site identity and package count; a standalone package site uses its own package title and count. |
| `site.brand`, `site.repository`, `site.navigation`, `site.footer`, `site.version`, `site.description` | Shared semantic site metadata, currently unconfigured/null or empty for Doc inputs. No implicit Litocpp branding. |
| `page.title`, `page.kind` | Content title and `root`, `package`, `module` or `symbol`. |
| `page.output`, `page.url`, `page.root_url`, `page.asset_prefix` | Output path, encoded site-relative URL, relative site home and root asset prefix. These work in both workspace and standalone package layouts. |
| `page.package`, `page.module` | Optional package and module names identifying the current content. A theme may use these as search filters. |
| `page.outline` | Heading entries `{href, label, level}`. |
| `page.icon_url`, `page.icon_media_type`, `page.description`, `page.canonical_url`, `page.edit_url`, `page.source`, `page.redirect`, `page.base_href` | Shared optional metadata; absent unless the content owner supplies it. Fallback icons belong to the theme. |
| `page.not_found`, `page.noindex`, `page.is_api`, `page.is_book` | Page facts; Doc pages are API pages, not Book/error pages. |
| `navigation.packages`, `navigation.modules` | Package and module links available for navigation; the theme chooses where and whether to render them. |
| `navigation.data_url` | Optional module dataset identity usable by client-side navigation state. |
| `navigation.module_index` | Optional `{href, fallback_href}` for the package module dataset and package home. Available on pages belonging to a package, even when the default theme chooses not to show a sidebar. |

The top-level `package`, `module` and `symbol` are null when not applicable. They
are domain objects, not layout flags. `page.package` and `page.module` are names;
the top-level objects contain the detailed content.

## Documentation data

Root pages expose `packages`. Package pages expose `package`, `modules` and
`symbols`. Module pages expose `module`, `modules`, `reexports`, `symbols` and the
typed declaration collections `structs`, `enums`, `concepts`, `aliases`,
`functions` and `variables`. Symbol pages expose `symbol`, `methods`,
`member_types`, `fields` and `member_values`.

Package/module/symbol objects retain their content names, versions, statistics,
counts and links. Their `documentation` value is null or trusted rendered Markdown.
Collections themselves are the condition for rendering sections; there are no
parallel `has_methods`, `has_structs` or inverse `missing_documentation` flags.

Declarations expose plain `signature`, trusted `signature_html` and ordered
`signature_fragments`. Each fragment has plain `text`, trusted highlighted `html`
and optional `href`. A missing reference target remains plain highlighted text,
not an empty link. Namespace is an optional name, not a separate namespace page.
`symbol.parent` is null or `{name, href}`.

Record members retain their anchor `id`, name, declaration and documentation.
An inline member type's `alias_target` is null or `{text, html}`. `inline` records
actual symbol placement; it does not require a particular HTML element. The theme
can display an alias target, its signature, its documentation, or a different
presentation without asking the renderer to generate page chrome.

```html
{{#if symbol}}
<h1>{{symbol.qualified_name}}</h1>
<pre><code>{{#each symbol.signature_fragments}}{{#if href}}<a href="{{href}}">{{{html}}}</a>{{else}}{{{html}}}{{/if}}{{/each}}</code></pre>
{{#if symbol.documentation}}<article>{{{symbol.documentation}}}</article>{{/if}}
{{/if}}
```

## Migrating old themes

Old bundle version 1, omitted/old Doc or Book context versions, and the removed
source template entry are rejected. There is no compatibility adapter or injected
error/redirect fallback. Update the manifest and templates together.

- Replace `site.has_default_brand` with a theme-owned fallback in `if/else`.
- Replace `page.search_package` / `page.search_module` with guarded
  `page.package` / `page.module`.
- Replace `navigation.show_modules`, `has_scroll_state` and `scroll_state_url`
  with semantic collections, `module_index` and `data_url`, and choose layout in
  the theme.
- Replace `has_documentation` / `missing_documentation` with
  `if documentation ... else ...`.
- Replace `has_href` / `plain` with `if href ... else ...`.
- Replace `has_parent`, `parent_name`, `parent_href` with guarded `parent` members.
- Replace `has_alias_target` / `alias_target_html` with guarded `alias_target`
  and its `text` / `html` members.

This is a context migration, not a new C++ extractor, dataset/search protocol,
theme inheritance chain, remote installer or executable plugin system.
