export module lito.book:model;

import rstd;
export import limarkto;
import lito.site;

using namespace rstd::prelude;

export namespace lito::book
{

struct SourceSpan {
    rstd::path::PathBuf path;
    usize               line { usize(1) };
    usize               column { usize(1) };
};

struct BookAsset {
    rstd::path::PathBuf source;
    String              output;

    auto clone() const -> BookAsset { return { source.clone(), output.clone() }; }
};

struct BookIcon {
    BookAsset asset;
    String    media_type;
};

struct BookSiteLink {
    String               label;
    lito::site::SiteLink target;
};
struct BookEditUrl {
    String prefix;
    String suffix;
};
struct BookPageMetadata {
    String source;
    String description;
};
struct BookRedirect {
    String from;
    String to;
};
struct BookHtmlConfig {
    Option<lito::site::SiteBaseUrl> site_url;
    Option<lito::site::SiteLink>    repository;
    Option<BookEditUrl>             edit;
    Option<BookSiteLink>            brand;
    Vec<BookSiteLink>               navigation;
    Option<String>                  footer_text;
    Vec<BookSiteLink>               footer_links;
    Vec<BookPageMetadata>           pages;
    Vec<BookRedirect>               redirects;
};

struct BookProject {
    rstd::path::PathBuf         root;
    rstd::path::PathBuf         manifest;
    String                      identity;
    String                      name;
    String                      title;
    Option<String>              version;
    Option<String>              language;
    rstd::path::PathBuf         source_root;
    rstd::path::PathBuf         summary;
    rstd::path::PathBuf         output;
    Option<rstd::path::PathBuf> frontend;
    Option<BookIcon>            icon;
    Option<String>              description;
    String                      source_prefix;
    BookHtmlConfig              html;
};

struct SummaryEntry {
    String     title;
    String     source;
    usize      depth {};
    SourceSpan span;
};

struct SummaryDocument {
    rstd::path::PathBuf path;
    Vec<SummaryEntry>   entries;
};

struct BookPage {
    String              identity;
    String              title;
    String              source;
    rstd::path::PathBuf source_path;
    String              output;
    String              url;
    Option<usize>       parent;
    Vec<usize>          children;
    Option<usize>       previous;
    Option<usize>       next;
    Vec<usize>          breadcrumb;
    SourceSpan          span;
};

struct BookGraph {
    Vec<BookPage> pages;
    Vec<usize>    roots;
};

struct MarkdownHeading {
    usize      level {};
    String     text;
    String     anchor;
    SourceSpan span;
};

struct MarkdownLink {
    limarkto::NodeId node;
    String           destination;
    String           resolved_destination;
    bool             image { false };
    SourceSpan       span;
};

struct MarkdownDocument {
    rstd::path::PathBuf                        path;
    limarkto::Document                         syntax;
    rstd::collections::BTreeMap<usize, String> heading_anchors;
    Vec<MarkdownHeading>                       headings;
    Vec<MarkdownLink>                          links;
};

struct BookPageDocument {
    usize            page {};
    MarkdownDocument document;
    String           html;
};

struct BookContent {
    Vec<BookPageDocument> pages;
    Vec<BookAsset>        assets;
};

struct BookDatasetPage {
    String               identity;
    String               title;
    String               source;
    String               output;
    String               url;
    Option<String>       parent;
    Vec<String>          children;
    Option<String>       previous;
    Option<String>       next;
    Vec<String>          breadcrumb;
    Vec<MarkdownHeading> headings;
    String               html;
};

struct BookDataset {
    String               identity;
    String               title;
    Option<String>       version;
    Option<String>       language;
    Vec<String>          roots;
    Vec<BookDatasetPage> pages;
};

struct BookDataPageSummary {
    String              identity;
    rstd::path::PathBuf json;
};

struct BookDataSummary {
    rstd::path::PathBuf      root;
    rstd::path::PathBuf      manifest;
    String                   digest;
    Vec<BookDataPageSummary> pages;
};

struct BookSummary {
    rstd::path::PathBuf output;
    rstd::path::PathBuf index;
    BookDataSummary     data;
    usize               pages {};
    bool                site_generated { false };
};

struct BookCheckInput {
    rstd::path::PathBuf directory;
};

struct BookBuildInput {
    rstd::path::PathBuf         directory;
    Option<rstd::path::PathBuf> output;
    Option<rstd::path::PathBuf> frontend;
};

struct BookCheckSummary {
    BookProject project;
    usize       pages {};
    usize       headings {};
    usize       links {};
};

struct PreparedBook {
    BookProject                project;
    BookGraph                  graph;
    BookContent                content;
    BookDataset                dataset;
    lito::site::FrontendBundle frontend;
    struct HtmlPage {
        String                       output;
        Option<usize>                chapter;
        bool                         not_found {};
        Option<String>               description;
        Option<String>               canonical;
        Option<String>               edit_url;
        Option<String>               source;
        Option<lito::site::SiteLink> redirect;
    };
    struct HtmlPublication {
        Vec<HtmlPage> pages;
        Vec<String>   outputs;
        String        not_found_base;
    } html;
};

struct BookWatchLayout {
    rstd::path::PathBuf      root;
    Vec<rstd::path::PathBuf> roots;
    Vec<rstd::path::PathBuf> excluded;
};

struct BookWatchSnapshot {
    String digest;
};

} // namespace lito::book
