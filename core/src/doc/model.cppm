export module lito.doc:model;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::doc {

enum class DocumentationCommentKind {
  Outer,
  Inner,
};

enum class DeclarationKind {
  Module,
  Namespace,
  Record,
  Enum,
  Concept,
  Alias,
  Function,
  Variable,
  Field,
};

enum class DeclarationAccess {
  Public,
  Protected,
  Private,
};

enum class SymbolPlacement {
  Standalone,
  RecordMember,
};

enum class DeclarationReferenceKind {
  Type,
  Concept,
};

enum class DocumentationSeverity {
  Warning,
  Error,
};

struct DocumentationSpan {
  rstd::path::PathBuf path;
  usize begin_line{};
  usize begin_column{};
  usize end_line{};
  usize end_column{};
};

struct DocumentationComment {
  DocumentationCommentKind kind{DocumentationCommentKind::Outer};
  String text;
  DocumentationSpan span;
};

struct DeclarationReference {
  usize begin{};
  usize end{};
  String semantic_identity;
  DeclarationReferenceKind kind{DeclarationReferenceKind::Type};
};

struct DeclarationText {
  String text;
  Vec<DeclarationReference> references;

  DeclarationText() = default;
  DeclarationText(String value) : text(rstd::move(value)) {}

  auto as_str() const -> ref<str> { return text.as_str(); }

  auto clone() const -> DeclarationText {
    auto result = DeclarationText{text.clone()};
    result.references.reserve(references.len());
    for (const auto &reference : references) {
      result.references.push(DeclarationReference{
          .begin = reference.begin,
          .end = reference.end,
          .semantic_identity = reference.semantic_identity.clone(),
          .kind = reference.kind,
      });
    }
    return result;
  }

  auto validate(ref<str> context) const -> Result<empty, String> {
    auto previous_end = usize{};
    for (const auto &reference : references) {
      if (reference.begin >= reference.end || reference.end > text.len() ||
          reference.begin < previous_end ||
          !text.as_str().is_char_boundary(reference.begin) ||
          !text.as_str().is_char_boundary(reference.end) ||
          reference.semantic_identity.is_empty()) {
        return Err(rstd::format("{} has invalid declaration reference [{}, {})",
                                context, reference.begin, reference.end));
      }
      previous_end = reference.end;
    }
    return Ok(empty{});
  }
};

enum class DocumentationUnitKind {
  Ordinary,
  PrimaryInterface,
  InterfacePartition,
  Implementation,
  ImplementationPartition,
};

enum class DeclarationOrigin {
  Ordinary,
  Interface,
  Implementation,
  GlobalFragment,
  PrivateFragment
};

struct DeclarationOwner {
  String semantic_identity;
  String named_module;
  DeclarationKind kind{DeclarationKind::Namespace};
  DeclarationAccess access{DeclarationAccess::Public};
  bool anonymous_record{false};

  auto clone() const -> DeclarationOwner {
    return {semantic_identity.clone(), named_module.clone(), kind, access,
            anonymous_record};
  }
};

auto documentation_unit_kind_name(DocumentationUnitKind kind) -> ref<str> {
  switch (kind) {
  case DocumentationUnitKind::Ordinary:
    return "ordinary"_str;
  case DocumentationUnitKind::PrimaryInterface:
    return "primary-interface"_str;
  case DocumentationUnitKind::InterfacePartition:
    return "interface-partition"_str;
  case DocumentationUnitKind::Implementation:
    return "implementation"_str;
  case DocumentationUnitKind::ImplementationPartition:
    return "implementation-partition"_str;
  }
  __builtin_unreachable();
}

auto declaration_origin_name(DeclarationOrigin origin) -> ref<str> {
  switch (origin) {
  case DeclarationOrigin::Ordinary:
    return "ordinary"_str;
  case DeclarationOrigin::Interface:
    return "interface"_str;
  case DeclarationOrigin::Implementation:
    return "implementation"_str;
  case DeclarationOrigin::GlobalFragment:
    return "global-fragment"_str;
  case DeclarationOrigin::PrivateFragment:
    return "private-fragment"_str;
  }
  __builtin_unreachable();
}

struct DeclarationOutline {
  Option<String> alias_target;
  String semantic_identity;
  String publication_identity;
  String named_module;
  DeclarationKind kind{DeclarationKind::Variable};
  String name;
  String qualified_name;
  String namespace_name;
  DeclarationText signature;
  DeclarationText scope_signature;
  Option<String> record_keyword;
  Option<String> record_header;
  bool is_definition{false};
  bool is_scope_declaration{false};
  DeclarationOrigin origin{DeclarationOrigin::Ordinary};
  bool lexical_export{false};
  bool exported_redeclaration{false};
  DeclarationAccess access{DeclarationAccess::Public};
  Option<usize> parent;
  Option<DeclarationOwner> owner;
  Vec<DeclarationOwner> record_ancestors;
  Option<String> group;
  Option<DocumentationComment> comment;
  DocumentationSpan spelling_span;
  DocumentationSpan expansion_span;
};

struct DocumentationReexport {
  String logical_module;
  String named_module;
  bool is_partition{false};
  DocumentationSpan span;
};

struct DocumentationDiagnostic {
  DocumentationSeverity severity{DocumentationSeverity::Warning};
  String code;
  String message;
  DocumentationSpan span;
};

struct DocumentationUnit {
  rstd::path::PathBuf source;
  String source_contents;
  String logical_module;
  String named_module;
  DocumentationUnitKind kind{DocumentationUnitKind::Ordinary};
  Vec<DeclarationOutline> declarations;
  Vec<DocumentationReexport> reexports;
  Option<DocumentationComment> module_comment;
  Vec<DocumentationDiagnostic> diagnostics;
  usize documented{};
  usize undocumented{};
  usize unsupported{};
};

struct PackageInput {
  String name;
  String version;
  String source_identity;
  String root_module;
  String profile;
  rstd::path::PathBuf root;
  String toolchain_version;
  String toolchain_target;
  String language_standard;
  Vec<DocumentationUnit> units;
};

enum class PublicationKind {
  Site,
  PackageSet,
};

enum class SyntaxKind { Keyword, String, Number, Comment, Punctuation };

struct SyntaxToken {
  usize begin;
  usize end;
  SyntaxKind kind;
};

struct DeclarationHighlighter {
  virtual ~DeclarationHighlighter() = default;
  // Ordered, non-overlapping UTF-8 byte ranges in the supplied text.
  virtual auto highlight(ref<str> text) -> Vec<SyntaxToken> = 0;
};

struct SiteInput {
  String title;
  rstd::path::PathBuf output;
  rstd::path::PathBuf data_output;
  Option<rstd::path::PathBuf> frontend;
  bool data_only{false};
  PublicationKind publication{PublicationKind::Site};
  Vec<PackageInput> packages;
  // Borrowed for the synchronous generate call; never retained in the dataset.
  DeclarationHighlighter *highlighter{nullptr};
};

struct RenderInput {
  rstd::path::PathBuf data;
  rstd::path::PathBuf output;
  Option<rstd::path::PathBuf> frontend;
  DeclarationHighlighter *highlighter{nullptr};
};

struct Source {
  String path;
  String page;
  String contents;
};

struct Symbol {
  Option<String> alias_target;
  String key;
  String semantic_identity;
  String page;
  String module;
  String module_page;
  DeclarationKind kind{DeclarationKind::Variable};
  String name;
  String qualified_name;
  String namespace_name;
  DeclarationText signature;
  DeclarationText scope_signature;
  Option<String> record_keyword;
  Option<String> record_header;
  bool is_definition{false};
  bool is_scope_declaration{false};
  SymbolPlacement placement{SymbolPlacement::Standalone};
  Option<String> anchor;
  usize declaration_order{};
  Option<String> parent_key;
  Option<String> group;
  Option<String> comment;
  String source_page;
  String source_path;
  usize source_line{};
  usize source_column{};
  usize source_end_line{};
  usize source_end_column{};
};

auto symbol_href(const Symbol &symbol) -> String {
  if (symbol.anchor.is_none())
    return symbol.page.clone();
  return rstd::format("{}#{}", symbol.page, symbol.anchor->as_str());
}

struct Module {
  String name;
  String page;
  Option<String> comment;
  Vec<String> reexports;
};

struct Diagnostic {
  DocumentationSeverity severity{DocumentationSeverity::Warning};
  String code;
  String message;
  String path;
  usize line{};
};

struct PublicApiCount {
  String reason;
  usize declarations{};
};

struct Package {
  String name;
  String version;
  String source_identity;
  String root_module;
  String profile;
  String toolchain_version;
  String toolchain_target;
  String language_standard;
  Vec<Module> modules;
  Vec<Symbol> symbols;
  Vec<Source> sources;
  Vec<Diagnostic> diagnostics;
  usize documented{};
  usize undocumented{};
  usize unsupported{};
  // Fresh-generation audit; not part of the serialized public dataset.
  Vec<PublicApiCount> public_api_counts;
};

struct PublishedSymbolStatistics {
  usize total{};
  usize documented{};
  usize undocumented{};
};

struct Database {
  Vec<Package> packages;
};

struct Dataset {
  String title;
  Vec<Package> packages;
};

struct PackageDataSummary {
  String name;
  rstd::path::PathBuf json;
};

struct DataSummary {
  rstd::path::PathBuf root;
  rstd::path::PathBuf manifest;
  String digest;
  Vec<PackageDataSummary> packages;
};

struct PackageSummary {
  String name;
  rstd::path::PathBuf directory;
  rstd::path::PathBuf json;
  rstd::path::PathBuf data_json;
  rstd::path::PathBuf index;
  usize symbols{};
  usize documented{};
  usize undocumented{};
  usize unsupported{};
  usize diagnostics{};
  Vec<Diagnostic> diagnostic_details;
  Vec<PublicApiCount> public_api_counts;
};

struct PublicationFile {
  String path;
  usize size{};
  String sha256;
  String media_type;
  String cache;
};

struct PackagePublicationSummary {
  String name;
  String version;
  rstd::path::PathBuf directory;
  rstd::path::PathBuf manifest;
  rstd::path::PathBuf index;
  Vec<PublicationFile> files;
};

struct PublicationSetSummary {
  rstd::path::PathBuf root;
  rstd::path::PathBuf manifest;
  Vec<PackagePublicationSummary> packages;
};

struct Summary {
  rstd::path::PathBuf output;
  rstd::path::PathBuf index;
  bool site_generated{false};
  DataSummary data;
  Vec<PackageSummary> packages;
  Option<PublicationSetSummary> publication_set;
};

auto declaration_kind_name(DeclarationKind kind) -> ref<str>;
auto declaration_kind_slug(DeclarationKind kind) -> ref<str>;
auto litodoc_version() noexcept -> ref<str>;
auto is_published_symbol_kind(DeclarationKind kind) -> bool;
auto published_symbol_statistics(const Package &package)
    -> PublishedSymbolStatistics;

} // namespace lito::doc

namespace lito::doc {

auto litodoc_version() noexcept -> ref<str> {
  return ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(LITO_PKG_VERSION),
      usize(sizeof(LITO_PKG_VERSION) - sizeof(char)));
}

auto declaration_kind_name(DeclarationKind kind) -> ref<str> {
  switch (kind) {
  case DeclarationKind::Module:
    return "module"_str;
  case DeclarationKind::Namespace:
    return "namespace"_str;
  case DeclarationKind::Record:
    return "record"_str;
  case DeclarationKind::Enum:
    return "enum"_str;
  case DeclarationKind::Concept:
    return "concept"_str;
  case DeclarationKind::Alias:
    return "alias"_str;
  case DeclarationKind::Function:
    return "function"_str;
  case DeclarationKind::Variable:
    return "variable"_str;
  case DeclarationKind::Field:
    return "field"_str;
  }
  __builtin_unreachable();
}

auto declaration_kind_slug(DeclarationKind kind) -> ref<str> {
  switch (kind) {
  case DeclarationKind::Module:
    return "mod"_str;
  case DeclarationKind::Namespace:
    return "ns"_str;
  case DeclarationKind::Record:
    return "type"_str;
  case DeclarationKind::Enum:
    return "enum"_str;
  case DeclarationKind::Concept:
    return "concept"_str;
  case DeclarationKind::Alias:
    return "alias"_str;
  case DeclarationKind::Function:
    return "fn"_str;
  case DeclarationKind::Variable:
    return "var"_str;
  case DeclarationKind::Field:
    return "field"_str;
  }
  __builtin_unreachable();
}

auto is_published_symbol_kind(DeclarationKind kind) -> bool {
  return kind != DeclarationKind::Namespace;
}

auto published_symbol_statistics(const Package &package)
    -> PublishedSymbolStatistics {
  auto result = PublishedSymbolStatistics{};
  for (const auto &symbol : package.symbols) {
    if (!is_published_symbol_kind(symbol.kind))
      continue;
    ++result.total;
    if (symbol.comment.is_some())
      ++result.documented;
    else
      ++result.undocumented;
  }
  return result;
}

} // namespace lito::doc
