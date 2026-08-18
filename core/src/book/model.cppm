export module lito.book:model;

import rstd;

using namespace rstd::prelude;

export namespace lito::book {

struct SourceSpan {
  rstd::path::PathBuf path;
  usize line{usize(1)};
  usize column{usize(1)};
};

struct BookProject {
  rstd::path::PathBuf root;
  rstd::path::PathBuf manifest;
  String identity;
  String name;
  String title;
  Option<String> version;
  Option<String> language;
  rstd::path::PathBuf source_root;
  rstd::path::PathBuf summary;
  rstd::path::PathBuf output;
  Option<rstd::path::PathBuf> frontend;
};

struct SummaryEntry {
  String title;
  String source;
  usize depth{};
  SourceSpan span;
};

struct SummaryDocument {
  rstd::path::PathBuf path;
  Vec<SummaryEntry> entries;
};

struct BookPage {
  String identity;
  String title;
  String source;
  rstd::path::PathBuf source_path;
  String output;
  String url;
  Option<usize> parent;
  Vec<usize> children;
  Option<usize> previous;
  Option<usize> next;
  Vec<usize> breadcrumb;
  SourceSpan span;
};

struct BookGraph {
  Vec<BookPage> pages;
  Vec<usize> roots;
};

enum class MarkdownInlineKind {
  Text,
  Emphasis,
  Strong,
  Code,
  Link,
  Image,
};

struct MarkdownInline {
  MarkdownInlineKind kind{MarkdownInlineKind::Text};
  String text;
  String destination;
  String resolved_destination;
  SourceSpan span;
};

enum class MarkdownBlockKind {
  Heading,
  Paragraph,
  Code,
  ListItem,
  Blockquote,
};

struct MarkdownBlock {
  MarkdownBlockKind kind{MarkdownBlockKind::Paragraph};
  usize level{};
  usize depth{};
  bool ordered{false};
  String language;
  String anchor;
  String literal;
  Vec<MarkdownInline> inlines;
  SourceSpan span;
};

struct MarkdownHeading {
  usize level{};
  String text;
  String anchor;
  SourceSpan span;
};

struct MarkdownLink {
  String destination;
  bool image{false};
  SourceSpan span;
};

struct MarkdownDocument {
  rstd::path::PathBuf path;
  Vec<MarkdownBlock> blocks;
  Vec<MarkdownHeading> headings;
  Vec<MarkdownLink> links;
};

struct BookPageDocument {
  usize page{};
  MarkdownDocument document;
  String html;
};

struct BookAsset {
  rstd::path::PathBuf source;
  String output;
};

struct BookContent {
  Vec<BookPageDocument> pages;
  Vec<BookAsset> assets;
};

struct BookDatasetPage {
  String identity;
  String title;
  String source;
  String output;
  String url;
  Option<String> parent;
  Vec<String> children;
  Option<String> previous;
  Option<String> next;
  Vec<String> breadcrumb;
  Vec<MarkdownHeading> headings;
  String html;
};

struct BookDataset {
  String identity;
  String title;
  Option<String> version;
  Option<String> language;
  Vec<String> roots;
  Vec<BookDatasetPage> pages;
};

struct BookDataPageSummary {
  String identity;
  rstd::path::PathBuf json;
};

struct BookDataSummary {
  rstd::path::PathBuf root;
  rstd::path::PathBuf manifest;
  String digest;
  Vec<BookDataPageSummary> pages;
};

struct BookSummary {
  rstd::path::PathBuf output;
  rstd::path::PathBuf index;
  BookDataSummary data;
  usize pages{};
  bool site_generated{false};
};

struct BookCheckInput {
  rstd::path::PathBuf directory;
};

struct BookBuildInput {
  rstd::path::PathBuf directory;
  Option<rstd::path::PathBuf> output;
  Option<rstd::path::PathBuf> frontend;
};

struct BookCheckSummary {
  BookProject project;
  usize pages{};
  usize headings{};
  usize links{};
};

} // namespace lito::book
