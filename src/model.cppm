export module tenon.doc:model;

import rstd;
import tenon.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace tenon::doc
{

struct PackageInput {
    String                           name;
    String                           version;
    String                           root_module;
    String                           profile;
    rstd::path::PathBuf              root;
    String                           toolchain_version;
    String                           toolchain_target;
    String                           language_standard;
    Vec<frontend::DocumentationUnit> units;
};

struct SiteInput {
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
    String                    key;
    String                    page;
    String                    module;
    String                    module_page;
    frontend::DeclarationKind kind { frontend::DeclarationKind::Variable };
    String                    name;
    String                    qualified_name;
    String                    signature;
    bool                      is_definition { false };
    Option<String>            parent_key;
    Option<String>            group;
    Option<String>            comment;
    String                    source_page;
    String                    source_path;
    usize                     source_line {};
    usize                     source_column {};
    usize                     source_end_line {};
    usize                     source_end_column {};
};

struct Module {
    String         name;
    String         page;
    Option<String> comment;
    Vec<String>    reexports;
};

struct Diagnostic {
    frontend::DocumentationSeverity severity { frontend::DocumentationSeverity::Warning };
    String                          code;
    String                          message;
    String                          path;
    usize                           line {};
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

auto declaration_kind_name(frontend::DeclarationKind kind) -> ref<str>;
auto declaration_kind_slug(frontend::DeclarationKind kind) -> ref<str>;

} // namespace tenon::doc

namespace tenon::doc
{

auto declaration_kind_name(frontend::DeclarationKind kind) -> ref<str> {
    using frontend::DeclarationKind;
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

auto declaration_kind_slug(frontend::DeclarationKind kind) -> ref<str> {
    using frontend::DeclarationKind;
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

} // namespace tenon::doc
