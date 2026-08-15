export module lito.doc:model;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::doc
{

enum class DocumentationCommentKind
{
    Outer,
    Inner,
};

enum class DeclarationKind
{
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

enum class DeclarationAccess
{
    Public,
    Protected,
    Private,
};

enum class DocumentationSeverity
{
    Warning,
    Error,
};

struct DocumentationSpan {
    rstd::path::PathBuf path;
    usize               begin_line {};
    usize               begin_column {};
    usize               end_line {};
    usize               end_column {};
};

struct DocumentationComment {
    DocumentationCommentKind kind { DocumentationCommentKind::Outer };
    String                   text;
    DocumentationSpan        span;
};

struct DeclarationOutline {
    String                       semantic_identity;
    DeclarationKind              kind { DeclarationKind::Variable };
    String                       name;
    String                       qualified_name;
    String                       namespace_name;
    String                       signature;
    bool                         is_definition { false };
    bool                         exported { false };
    DeclarationAccess            access { DeclarationAccess::Public };
    Option<usize>                parent;
    Option<String>               group;
    Option<DocumentationComment> comment;
    DocumentationSpan            spelling_span;
    DocumentationSpan            expansion_span;
};

struct DocumentationReexport {
    String            logical_module;
    DocumentationSpan span;
};

struct DocumentationDiagnostic {
    DocumentationSeverity severity { DocumentationSeverity::Warning };
    String                code;
    String                message;
    DocumentationSpan     span;
};

struct DocumentationUnit {
    rstd::path::PathBuf          source;
    String                       source_contents;
    String                       logical_module;
    bool                         is_interface { false };
    Vec<DeclarationOutline>      declarations;
    Vec<DocumentationReexport>   reexports;
    Option<DocumentationComment> module_comment;
    Vec<DocumentationDiagnostic> diagnostics;
    usize                        documented {};
    usize                        undocumented {};
    usize                        unsupported {};
};

struct PackageInput {
    String                 name;
    String                 version;
    String                 root_module;
    String                 profile;
    rstd::path::PathBuf    root;
    String                 toolchain_version;
    String                 toolchain_target;
    String                 language_standard;
    Vec<DocumentationUnit> units;
};

struct SiteInput {
    String                      title;
    rstd::path::PathBuf         output;
    rstd::path::PathBuf         data_output;
    Option<rstd::path::PathBuf> frontend;
    bool                        data_only { false };
    Vec<PackageInput>           packages;
};

struct RenderInput {
    rstd::path::PathBuf         data;
    rstd::path::PathBuf         output;
    Option<rstd::path::PathBuf> frontend;
};

struct Source {
    String path;
    String page;
    String contents;
};

struct Symbol {
    String          key;
    String          page;
    String          module;
    String          module_page;
    DeclarationKind kind { DeclarationKind::Variable };
    String          name;
    String          qualified_name;
    String          namespace_name;
    String          signature;
    bool            is_definition { false };
    Option<String>  parent_key;
    Option<String>  group;
    Option<String>  comment;
    String          source_page;
    String          source_path;
    usize           source_line {};
    usize           source_column {};
    usize           source_end_line {};
    usize           source_end_column {};
};

struct Module {
    String         name;
    String         page;
    Option<String> comment;
    Vec<String>    reexports;
};

struct Diagnostic {
    DocumentationSeverity severity { DocumentationSeverity::Warning };
    String                code;
    String                message;
    String                path;
    usize                 line {};
};

struct Package {
    String          name;
    String          version;
    String          root_module;
    String          profile;
    String          toolchain_version;
    String          toolchain_target;
    String          language_standard;
    Vec<Module>     modules;
    Vec<Symbol>     symbols;
    Vec<Source>     sources;
    Vec<Diagnostic> diagnostics;
    usize           documented {};
    usize           undocumented {};
    usize           unsupported {};
};

struct Database {
    Vec<Package> packages;
};

struct Dataset {
    String       title;
    Vec<Package> packages;
};

struct PackageDataSummary {
    String              name;
    rstd::path::PathBuf json;
};

struct DataSummary {
    rstd::path::PathBuf     root;
    rstd::path::PathBuf     manifest;
    String                  digest;
    Vec<PackageDataSummary> packages;
};

struct PackageSummary {
    String              name;
    rstd::path::PathBuf directory;
    rstd::path::PathBuf json;
    rstd::path::PathBuf data_json;
    rstd::path::PathBuf index;
    usize               symbols {};
    usize               documented {};
    usize               undocumented {};
    usize               unsupported {};
    usize               diagnostics {};
    Vec<Diagnostic>     diagnostic_details;
};

struct Summary {
    rstd::path::PathBuf output;
    rstd::path::PathBuf index;
    bool                site_generated { false };
    DataSummary         data;
    Vec<PackageSummary> packages;
};

auto declaration_kind_name(DeclarationKind kind) -> ref<str>;
auto declaration_kind_slug(DeclarationKind kind) -> ref<str>;

} // namespace lito::doc

namespace lito::doc
{

auto declaration_kind_name(DeclarationKind kind) -> ref<str> {
    switch (kind) {
    case DeclarationKind::Module: return "module"_str;
    case DeclarationKind::Namespace: return "namespace"_str;
    case DeclarationKind::Record: return "record"_str;
    case DeclarationKind::Enum: return "enum"_str;
    case DeclarationKind::Concept: return "concept"_str;
    case DeclarationKind::Alias: return "alias"_str;
    case DeclarationKind::Function: return "function"_str;
    case DeclarationKind::Variable: return "variable"_str;
    case DeclarationKind::Field: return "field"_str;
    }
    __builtin_unreachable();
}

auto declaration_kind_slug(DeclarationKind kind) -> ref<str> {
    switch (kind) {
    case DeclarationKind::Module: return "mod"_str;
    case DeclarationKind::Namespace: return "ns"_str;
    case DeclarationKind::Record: return "type"_str;
    case DeclarationKind::Enum: return "enum"_str;
    case DeclarationKind::Concept: return "concept"_str;
    case DeclarationKind::Alias: return "alias"_str;
    case DeclarationKind::Function: return "fn"_str;
    case DeclarationKind::Variable: return "var"_str;
    case DeclarationKind::Field: return "field"_str;
    }
    __builtin_unreachable();
}

} // namespace lito::doc
