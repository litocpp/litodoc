# Book themes

A theme is a local frontend bundle: `frontend.json`, templates, optional partials,
and explicitly declared assets. Select it with `output.html.frontend` in
`book.toml`, or `litobook build/watch/serve --frontend <directory>`.
There is no required default header, sidebar, component, script, font or CSS.

## Manifest

```json
{
  "format": "lito-doc-frontend",
  "version": 2,
  "template-api": 1,
  "capabilities": ["book"],
  "book-data-api": 1,
  "book-template-api": 2,
  "templates": {
    "book-root": "root.html",
    "book-page": "page.html",
    "book-not-found": "404.html",
    "book-redirect": "redirect.html"
  },
  "partials": [],
  "assets": []
}
```

All four entry points are required for Book template API 2. Other files must be
declared as partials or assets. Assets include a `path` and `media-type`.
Only the theme's declared assets are published; the default theme is not inherited.

`template-api` identifies the template language and remains 1. `book-template-api`
identifies the Book context. API documentation themes explicitly declare
`doc-template-api: 2` with the `api` capability; combined themes declare both
context versions. Data formats have their own unchanged versions.

## Context

Optional values are null and collections are empty arrays when absent. Guard
optional values before accessing members or printing text. Missing fields are
errors, so misspellings do not silently disappear.

| Value | Meaning |
| --- | --- |
| `site.title`, `site.language` | Book title and resolved HTML language. |
| `site.version`, `site.description` | Optional version and default description. |
| `site.brand` | Optional explicit `{label, href}`. No implicit Litocpp brand. |
| `site.repository` | Optional `{href}`. The theme owns the link label and icon. |
| `site.navigation` | Ordered `{label, href}` links, relative to the current output. |
| `site.footer` | Optional `{text, links}`; text is optional and links use `{label, href}`. |
| `page.kind` | `book-root`, `book-page`, `not-found`, or `redirect`. |
| `page.title`, `page.output`, `page.url` | Content title, output file path and escaped site-relative URL. Synthetic pages use the Book title; error-page wording belongs to the theme. |
| `page.root_url`, `page.asset_prefix` | Relative home URL and prefix for declared root-level assets. |
| `page.icon_url`, `page.icon_media_type` | Optional explicit Book icon URL and media type. The theme owns any fallback icon and its asset. |
| `page.description`, `page.canonical_url` | Optional resolved description and formal canonical URL. |
| `page.edit_url`, `page.source` | Optional edit URL and logical Book-root-relative source path. Absent on synthetic pages. |
| `page.content` | Trusted rendered Markdown HTML; empty on synthetic pages. |
| `page.outline` | Heading entries `{href, label, level}` excluding h1. |
| `page.not_found`, `page.noindex` | Error-page and indexing facts. |
| `page.base_href` | Optional not-found base. Use it before relative assets when an error page is served at an unknown request path. |
| `page.redirect` | Optional `{href}` resolved target for the redirect page. |
| `book.title`, `book.roots` | Book title and root chapter links. |
| `book.breadcrumbs`, `book.ancestors` | Current chapter chain including/excluding the current chapter. |
| `book.previous`, `book.next` | Optional adjacent chapter links `{label, title, href}`. |
| `book.data_digest` | Prepared Book dataset digest. |
| `navigation.pages` | Chapter links with title, href, depth, is_root and has_children. |
| `navigation.data_url` | Relative Book dataset URL, for optional client features. |

`page.is_book` and `page.is_api` identify the content family for themes sharing
partials with API documentation. Book API 2 does not supply default-layout flags
such as `site.has_default_brand` or `site.has_site_links`.

## Templates

`{{value}}` HTML-escapes text. `{{{page.content}}}` inserts trusted Markdown HTML;
raw interpolation of ordinary configuration strings is rejected.

```html
<main><h1>{{page.title}}</h1>{{{page.content}}}</main>
<footer>
  {{#if site.brand}}{{site.brand.label}}{{else}}My project{{/if}}
  {{#each site.navigation}}<a href="{{href}}">{{label}}</a>{{/each}}
  {{#if site.repository}}<a href="{{site.repository.href}}">Source</a>{{/if}}
</footer>
```

The same data can be rendered in a header, footer, dropdown or not rendered at all.
The engine does not inject these elements. `{{else}}` belongs to `if`, not `each`.
Partials use `{{> path/to/partial.html}}` and share the current context.

Conditions also support a single text comparison, for example
`{{#if page.kind == "book-page"}}` or `{{#if left != right}}`. Operands are
field paths or double-quoted text literals; literals support `\"` and `\\`
escapes. Comparisons require text, so guard optional values first. Missing fields
and comparisons involving null, booleans or trusted HTML are errors. Conditions
are parsed with the template, not evaluated as scripts. `each` still takes a
collection path, not a comparison.

Redirect templates should consume `page.redirect.href` for the refresh and
fallback link, and may emit `page.canonical_url`. Not-found templates should use
`page.base_href` (or otherwise build valid root-relative URLs) and may emit
`page.noindex`. The publisher validates targets and output paths, not the theme's
HTML semantics. Preview preserves HTTP 404; production status is the host's job.

Ordinary builds do not inject scripts. Local `serve` adapts HTML responses to add
its reload client; it does not change the theme files or formal canonical URLs.

## Migrating old themes

Only frontend bundle version 2 and Book template API 2 are supported. The
`book-template-api` declaration is mandatory. Missing, old and unknown context
versions are rejected; no compatibility adapter or error/redirect fallback is
provided. Themes must supply all four entry points, including when the current
Book has no redirects. The obsolete `source` template entry is not accepted.

Update old templates as follows:

| Old value | API 2 replacement |
| --- | --- |
| `book.content` | `page.content` |
| `book.title` as the current page title | `page.title`; `book.title` is now the Book title. |
| `site.has_custom_brand`, `site.has_default_brand` | Test `site.brand`; write the fallback in the theme's `else` branch. |
| `site.repository_url`, `site.has_repository` | Guard `site.repository`, then read `site.repository.href`. |
| `site.has_navigation`, `site.has_site_links` | Test `site.navigation` or `site.repository` where the theme needs a control. |
| `site.footer_text`, `site.footer_links`, footer flags | Guard `site.footer`, then use `text` and `links`. |
| `page.has_description`, `page.has_canonical`, `page.has_edit_url` | Test `page.description`, `page.canonical_url`, `page.edit_url`. |
| `page.has_outline`, `book.has_previous`, `book.has_next` | Test `page.outline`, `book.previous`, `book.next`. |
| `book.has_page`, `book.has_breadcrumbs` | Test `page.source`, `book.ancestors`. |
| `navigation.scroll_state_url`, navigation layout flags | Use `navigation.data_url` and `navigation.pages`; choose the layout in the theme. |
| `page.is_not_found` | `page.not_found` |

Optional brand and icon values no longer contain implicit defaults. Old fields
are absent, so references fail instead of silently choosing an old presentation.

## Scope

This contract supports complete local themes. It does not add remote theme
downloads, inheritance, override lookup chains, a marketplace or executable plugins.
Assets retain the existing UTF-8 frontend resource format; extending binary asset
packaging is outside this context-contract change.
