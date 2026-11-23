#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import licrypto;
import lito.doc;
import lito.doc.web;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto child(ref<rstd::path::Path> root, ref<str> relative) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(relative).as_path());
}

auto source_span(const PathBuf& source) -> lito::doc::DocumentationSpan {
    return {
        .path         = source.clone(),
        .begin_line   = usize(3),
        .begin_column = usize(1),
        .end_line     = usize(3),
        .end_column   = usize(18),
    };
}

auto source_span(const PathBuf& source, usize line) -> lito::doc::DocumentationSpan {
    return {
        .path         = source.clone(),
        .begin_line   = line,
        .begin_column = usize(1),
        .end_line     = line,
        .end_column   = usize(24),
    };
}

auto comment(const PathBuf& source, usize line, ref<str> text)
    -> Option<lito::doc::DocumentationComment> {
    return Some(lito::doc::DocumentationComment {
        .text = String::make(text),
        .span = source_span(source, line),
    });
}

auto prepare_unit(lito::doc::DocumentationUnit& unit, ref<str> module) -> void {
    unit.named_module = String::make(module);
    for (auto& declaration : unit.declarations) {
        declaration.named_module = String::make(module);
        auto interface     = unit.kind == lito::doc::DocumentationUnitKind::PrimaryInterface ||
                             unit.kind == lito::doc::DocumentationUnitKind::InterfacePartition;
        declaration.origin = interface ? lito::doc::DeclarationOrigin::Interface
                                       : lito::doc::DeclarationOrigin::Implementation;
        if (! interface) {
            declaration.exported_redeclaration = declaration.lexical_export;
            declaration.lexical_export         = false;
        }
    }
}

auto publication_package(ref<rstd::path::Path> root, ref<str> name, ref<str> module)
    -> lito::doc::PackageInput {
    auto source = child(root, rstd::format("src/{}.cppm", name).as_str());
    auto unit   = lito::doc::DocumentationUnit {
        .source = source.clone(),
        .source_contents =
            rstd::format("export module {};\n\nexport auto value() -> int;\n", module),
        .logical_module = String::make(module),
        .kind           = lito::doc::DocumentationUnitKind::PrimaryInterface,
        .module_comment = Some(lito::doc::DocumentationComment {
            .text = String::make("Package documentation."_str),
        }),
        .documented     = usize(1),
    };
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = rstd::format("{}::value", module),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("value"_str),
        .qualified_name    = rstd::format("{}::value", module),
        .signature         = String::make("auto value() -> int;"_str),
        .scope_signature   = String::make("auto value() -> int;"_str),
        .is_definition     = false,
        .lexical_export    = true,
        .spelling_span     = source_span(source),
        .expansion_span    = source_span(source),
    });
    auto package = lito::doc::PackageInput {
        .name              = String::make(name),
        .version           = String::make("1.2.3"_str),
        .source_identity   = rstd::format("git+https://example.test/{}.git#abc", name),
        .root_module       = String::make(module),
        .profile           = String::make("release"_str),
        .root              = PathBuf::from(root),
        .toolchain_version = String::make("clang 22"_str),
        .toolchain_target  = String::make("x86_64-unknown-linux-gnu"_str),
        .language_standard = String::make("c++20"_str),
    };
    package.units.push(rstd::move(unit));
    for (auto& unit : package.units) prepare_unit(unit, package.root_module.as_str());
    return package;
}

auto append_module(lito::doc::PackageInput& package, ref<rstd::path::Path> root, ref<str> module)
    -> void {
    package.units.push(lito::doc::DocumentationUnit {
        .source          = child(root, "src/child.cppm"_str),
        .source_contents = rstd::format("export module {};\n", module),
        .logical_module  = String::make(module),
        .kind            = lito::doc::DocumentationUnitKind::InterfacePartition,
        .module_comment  = Some(lito::doc::DocumentationComment {
            .text = String::make("Child module documentation."_str),
        }),
    });
    prepare_unit(package.units[package.units.len() - usize(1)], package.root_module.as_str());
    package.units[usize {}].reexports.push({ .logical_module = String::make(module),
                                             .named_module   = package.root_module.clone(),
                                             .is_partition   = true });
}

auto record_package(ref<rstd::path::Path> root) -> lito::doc::PackageInput {
    auto source = child(root, "src/box.cppm"_str);
    auto unit   = lito::doc::DocumentationUnit {
        .source          = source.clone(),
        .source_contents = String::make("export module records;\n\n"
                                        "export template <typename T> class Box;\n"_str),
        .logical_module  = String::make("records"_str),
        .kind            = lito::doc::DocumentationUnitKind::PrimaryInterface,
    };
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("records::Box"_str),
        .kind              = lito::doc::DeclarationKind::Record,
        .name              = String::make("Box"_str),
        .qualified_name    = String::make("Box"_str),
        .signature         = String::make("template <typename T>\nclass Box {};"_str),
        .scope_signature   = String::make("template <typename T>\nclass Box {};"_str),
        .record_keyword    = Some(String::make("class"_str)),
        .record_header     = Some(String::make("template <typename T>\nclass Box"_str)),
        .is_definition     = true,
        .lexical_export    = true,
        .comment           = comment(source, usize(3), "Stores a value."_str),
        .spelling_span     = source_span(source, usize(3)),
        .expansion_span    = source_span(source, usize(3)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Box::value"_str),
        .kind                 = lito::doc::DeclarationKind::Field,
        .name                 = String::make("value"_str),
        .qualified_name       = String::make("Box::value"_str),
        .signature            = String::make("T Box::value{};"_str),
        .scope_signature      = String::make("T value{};"_str),
        .is_definition        = true,
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .comment              = comment(source, usize(5), "Stored value."_str),
        .spelling_span        = source_span(source, usize(5)),
        .expansion_span       = source_span(source, usize(5)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Box::flags"_str),
        .kind                 = lito::doc::DeclarationKind::Field,
        .name                 = String::make("flags"_str),
        .qualified_name       = String::make("Box::flags"_str),
        .signature            = String::make("unsigned int Box::flags : 3 = 1;"_str),
        .scope_signature      = String::make("unsigned int flags : 3 = 1;"_str),
        .is_definition        = true,
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .spelling_span        = source_span(source, usize(7)),
        .expansion_span       = source_span(source, usize(7)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("records::Box::get#const"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("get"_str),
        .qualified_name    = String::make("Box::get"_str),
        .signature = String::make("template <typename U>\n"
                                  "    requires Numeric<U>\n"
                                  "[[nodiscard]] constexpr auto Box::get(U fallback = U{}) const & "
                                  "noexcept -> T;"_str),
        .scope_signature =
            String::make("template <typename U>\n"
                         "    requires Numeric<U>\n"
                         "[[nodiscard]] constexpr auto get(U fallback = U{}) const & noexcept "
                         "-> T;"_str),
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .comment              = comment(source, usize(9), "Returns the value."_str),
        .spelling_span        = source_span(source, usize(9)),
        .expansion_span       = source_span(source, usize(9)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Box::get#rvalue"_str),
        .kind                 = lito::doc::DeclarationKind::Function,
        .name                 = String::make("get"_str),
        .qualified_name       = String::make("Box::get"_str),
        .signature            = String::make("auto Box::get() && -> Box<T>;"_str),
        .scope_signature      = String::make("auto get() && -> Box<T>;"_str),
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .spelling_span        = source_span(source, usize(11)),
        .expansion_span       = source_span(source, usize(11)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Box::clear"_str),
        .kind                 = lito::doc::DeclarationKind::Function,
        .name                 = String::make("clear"_str),
        .qualified_name       = String::make("Box::clear"_str),
        .signature            = String::make("void Box::clear();"_str),
        .scope_signature      = String::make("void clear();"_str),
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .spelling_span        = source_span(source, usize(12)),
        .expansion_span       = source_span(source, usize(12)),
    });
    unit.declarations[usize(4)].scope_signature.references.push(lito::doc::DeclarationReference {
        .begin             = usize(17),
        .end               = usize(20),
        .semantic_identity = String::make("records::Box"_str),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Box::Nested"_str),
        .kind                 = lito::doc::DeclarationKind::Record,
        .name                 = String::make("Nested"_str),
        .qualified_name       = String::make("Box::Nested"_str),
        .signature            = String::make("struct Box::Nested {};"_str),
        .scope_signature      = String::make("struct Nested {};"_str),
        .record_keyword       = Some(String::make("struct"_str)),
        .record_header        = Some(String::make("struct Nested"_str)),
        .is_definition        = true,
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .spelling_span        = source_span(source, usize(13)),
        .expansion_span       = source_span(source, usize(13)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("records::make_box"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("make_box"_str),
        .qualified_name    = String::make("make_box"_str),
        .signature         = String::make("auto make_box() -> Box<int>;"_str),
        .scope_signature   = String::make("auto make_box() -> Box<int>;"_str),
        .lexical_export    = true,
        .spelling_span     = source_span(source, usize(15)),
        .expansion_span    = source_span(source, usize(15)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("records::Hidden"_str),
        .kind              = lito::doc::DeclarationKind::Record,
        .name              = String::make("Hidden"_str),
        .qualified_name    = String::make("Hidden"_str),
        .signature         = String::make("struct Hidden {};"_str),
        .scope_signature   = String::make("struct Hidden {};"_str),
        .record_keyword    = Some(String::make("struct"_str)),
        .record_header     = Some(String::make("struct Hidden"_str)),
        .is_definition     = true,
        .lexical_export    = true,
        .access            = lito::doc::DeclarationAccess::Private,
        .spelling_span     = source_span(source, usize(17)),
        .expansion_span    = source_span(source, usize(17)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("records::Hidden::legacy"_str),
        .kind                 = lito::doc::DeclarationKind::Field,
        .name                 = String::make("legacy"_str),
        .qualified_name       = String::make("Hidden::legacy"_str),
        .signature            = String::make("int legacy;"_str),
        .scope_signature      = String::make("int legacy;"_str),
        .is_definition        = true,
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(8)),
        .spelling_span        = source_span(source, usize(18)),
        .expansion_span       = source_span(source, usize(18)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .alias_target         = Some(String::make("T"_str)),
        .semantic_identity    = String::make("records::Box::value_type"_str),
        .kind                 = lito::doc::DeclarationKind::Alias,
        .name                 = String::make("value_type"_str),
        .qualified_name       = String::make("Box::value_type"_str),
        .signature            = String::make("using value_type = T;"_str),
        .scope_signature      = String::make("using value_type = T;"_str),
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(0)),
        .comment              = comment(source, usize(19), "The stored type."_str),
        .spelling_span        = source_span(source, usize(19)),
        .expansion_span       = source_span(source, usize(19)),
    });
    auto definition = lito::doc::DocumentationUnit {
        .source          = child(root, "src/box.cpp"_str),
        .source_contents = String::make("auto Box<int>::get() && -> int {}\n"_str),
        .logical_module  = String::make("records"_str),
        .kind            = lito::doc::DocumentationUnitKind::Implementation,
    };
    definition.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("records::Box::get#rvalue"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("get"_str),
        .qualified_name    = String::make("Box::get"_str),
        .signature         = String::make("auto Box<int>::get() && -> int;"_str),
        .scope_signature   = String::make("auto get() && -> int;"_str),
        .is_definition     = true,
        .lexical_export    = true,
        .comment           = comment(definition.source, usize(1), "Definition docs."_str),
        .spelling_span     = source_span(definition.source, usize(1)),
        .expansion_span    = source_span(definition.source, usize(1)),
    });
    auto package = lito::doc::PackageInput {
        .name              = String::make("records"_str),
        .version           = String::make("1.0.0"_str),
        .source_identity   = String::make("path+records"_str),
        .root_module       = String::make("records"_str),
        .profile           = String::make("release"_str),
        .root              = PathBuf::from(root),
        .toolchain_version = String::make("clang 22"_str),
        .toolchain_target  = String::make("x86_64-unknown-linux-gnu"_str),
        .language_standard = String::make("c++20"_str),
    };
    package.units.push(rstd::move(unit));
    package.units.push(rstd::move(definition));
    for (auto& unit : package.units) prepare_unit(unit, package.root_module.as_str());
    return package;
}

auto namespace_package(ref<rstd::path::Path> root) -> lito::doc::PackageInput {
    auto source = child(root, "src/namespaced.cppm"_str);
    auto unit   = lito::doc::DocumentationUnit {
        .source          = source.clone(),
        .source_contents = String::make("export module namespaced;\n\n"
                                        "export namespace alpha {\n"
                                        "struct Widget { auto get() const -> int; };\n"
                                        "auto make_widget() -> Widget;\n"
                                        "}\n"
                                        "export auto global_helper() -> int;\n"_str),
        .logical_module  = String::make("namespaced"_str),
        .kind            = lito::doc::DocumentationUnitKind::PrimaryInterface,
    };
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("namespaced::alpha"_str),
        .kind              = lito::doc::DeclarationKind::Namespace,
        .name              = String::make("alpha"_str),
        .qualified_name    = String::make("alpha"_str),
        .signature         = String::make("namespace alpha;"_str),
        .scope_signature   = String::make("namespace alpha;"_str),
        .is_definition     = true,
        .lexical_export    = true,
        .comment           = comment(source, usize(3), "Namespace documentation."_str),
        .spelling_span     = source_span(source, usize(3)),
        .expansion_span    = source_span(source, usize(3)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("namespaced::alpha::Widget"_str),
        .kind              = lito::doc::DeclarationKind::Record,
        .name              = String::make("Widget"_str),
        .qualified_name    = String::make("alpha::Widget"_str),
        .namespace_name    = String::make("alpha"_str),
        .signature         = String::make("struct alpha::Widget {};"_str),
        .scope_signature   = String::make("struct Widget {};"_str),
        .record_keyword    = Some(String::make("struct"_str)),
        .record_header     = Some(String::make("struct Widget"_str)),
        .is_definition     = true,
        .lexical_export    = true,
        .parent            = Some(usize(0)),
        .comment           = comment(source, usize(4), "Widget documentation."_str),
        .spelling_span     = source_span(source, usize(4)),
        .expansion_span    = source_span(source, usize(4)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity    = String::make("namespaced::alpha::Widget::get"_str),
        .kind                 = lito::doc::DeclarationKind::Function,
        .name                 = String::make("get"_str),
        .qualified_name       = String::make("alpha::Widget::get"_str),
        .namespace_name       = String::make("alpha"_str),
        .signature            = String::make("auto alpha::Widget::get() const -> int;"_str),
        .scope_signature      = String::make("auto get() const -> int;"_str),
        .is_scope_declaration = true,
        .lexical_export       = true,
        .parent               = Some(usize(1)),
        .comment              = comment(source, usize(4), "Method documentation."_str),
        .spelling_span        = source_span(source, usize(4)),
        .expansion_span       = source_span(source, usize(4)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("namespaced::alpha::make_widget"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("make_widget"_str),
        .qualified_name    = String::make("alpha::make_widget"_str),
        .namespace_name    = String::make("alpha"_str),
        .signature         = String::make("auto alpha::make_widget() -> Widget;"_str),
        .scope_signature   = String::make("auto make_widget() -> Widget;"_str),
        .lexical_export    = true,
        .parent            = Some(usize(0)),
        .comment           = comment(source, usize(5), "Function documentation."_str),
        .spelling_span     = source_span(source, usize(5)),
        .expansion_span    = source_span(source, usize(5)),
    });
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = String::make("namespaced::global_helper"_str),
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = String::make("global_helper"_str),
        .qualified_name    = String::make("global_helper"_str),
        .signature         = String::make("auto global_helper() -> int;"_str),
        .scope_signature   = String::make("auto global_helper() -> int;"_str),
        .lexical_export    = true,
        .comment           = comment(source, usize(7), "Global helper documentation."_str),
        .spelling_span     = source_span(source, usize(7)),
        .expansion_span    = source_span(source, usize(7)),
    });
    auto package = lito::doc::PackageInput {
        .name              = String::make("namespaced"_str),
        .version           = String::make("1.0.0"_str),
        .source_identity   = String::make("path+namespaced"_str),
        .root_module       = String::make("namespaced"_str),
        .profile           = String::make("release"_str),
        .root              = PathBuf::from(root),
        .toolchain_version = String::make("clang 22"_str),
        .toolchain_target  = String::make("x86_64-unknown-linux-gnu"_str),
        .language_standard = String::make("c++20"_str),
    };
    package.units.push(rstd::move(unit));
    for (auto& unit : package.units) prepare_unit(unit, package.root_module.as_str());
    return package;
}

auto symbol_page_containing(ref<rstd::path::Path> package_directory, ref<str> needle)
    -> Result<String, String> {
    auto directory = PathBuf::from(package_directory).join(PathBuf::from("symbol"_str).as_path());
    auto opened    = rstd::fs::read_dir(directory.as_path());
    if (opened.is_err())
        return Err(rstd::format(
            "cannot enumerate '{}': {}", directory.as_path(), rstd::move(opened).unwrap_err()));
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto entry = rstd::move(next).unwrap();
        if (entry.is_err())
            return Err(rstd::format(
                "cannot enumerate '{}': {}", directory.as_path(), rstd::move(entry).unwrap_err()));
        auto contents = rstd::fs::read_to_string(entry->path().as_path());
        if (contents.is_err())
            return Err(rstd::format("cannot read '{}': {}",
                                    entry->path().as_path(),
                                    rstd::move(contents).unwrap_err()));
        if (contents->as_str().contains(needle)) return Ok(rstd::move(contents).unwrap());
    }
    return Err(rstd::format("cannot find symbol page containing '{}'", needle));
}

auto occurrences(ref<str> contents, ref<str> needle) -> usize {
    auto count     = usize {};
    auto remaining = contents;
    while (true) {
        auto position = remaining.find(needle);
        if (position.is_none()) return count;
        ++count;
        remaining = remaining.get(*position + needle.len(), remaining.len()).unwrap();
    }
}

auto independent_doc_theme(ref<str> root_template = ""_str)
    -> Result<lito::site::FrontendBundle, String> {
    constexpr auto page =
        "<!doctype html><html><head><link rel=\"stylesheet\" "
        "href=\"{{page.asset_prefix}}skin.css\">"
        "</head><body data-kind=\"{{page.kind}}\" data-output=\"{{page.output}}\" "
        "data-url=\"{{page.url}}\">"
        "<a href=\"{{page.root_url}}\">Own home</a><h1>{{page.title}}</h1>"
        "{{#if site.brand}}{{site.brand.label}}{{else}}Own brand{{/if}}"
        "{{#if page.icon_url}}Configured icon{{else}}No icon{{/if}}"
        "{{#if page.package}}<p>Package {{page.package}}</p>{{/if}}"
        "{{#if page.module}}<p>Module {{page.module}}</p>{{/if}}"
        "{{#if navigation.module_index}}<a href=\"{{navigation.module_index.href}}\">Index</a>"
        "<a href=\"{{navigation.module_index.fallback_href}}\">Package home</a>{{/if}}"
        "{{#if symbol}}<article>{{#each symbol.signature_fragments}}"
        "{{#if href}}<a href=\"{{href}}\">{{{html}}}</a>{{else}}{{{html}}}{{/if}}{{/each}}"
        "{{#if symbol.documentation}}{{{symbol.documentation}}}{{/if}}"
        "{{#if symbol.parent}}<a href=\"{{symbol.parent.href}}\">{{symbol.parent.name}}</a>{{/if}}"
        "{{#each member_types}}{{#if inline}}{{#if alias_target}}"
        "<p>{{name}} =&gt; "
        "{{{alias_target.html}}}</p>{{else}}{{{signature_html}}}{{/if}}{{/if}}{{/each}}"
        "</article>{{/if}}<footer>{{#each site.navigation}}"
        "<a href=\"{{href}}\">{{label}}</a>{{/each}}"
        "{{#if site.repository}}<a href=\"{{site.repository.href}}\">Source</a>{{/if}}</footer></body></html>"_str;
    const lito::site::FrontendResourceInput resources[] = {
        { .path       = "frontend.json"_str,
          .media_type = "application/json"_str,
          .contents =
              "{\"format\":\"lito-doc-frontend\",\"version\":2,\"template-api\":1,"
              "\"doc-template-api\":2,\"data-api\":4,\"capabilities\":[\"api\"],"
              "\"templates\":{\"root\":\"root.html\",\"package\":\"package.html\","
              "\"module\":\"module.html\",\"symbol\":\"symbol.html\"},"
              "\"partials\":[],\"assets\":[{\"path\":\"skin.css\",\"media-type\":\"text/css\"}]}"_str
                  .as_bytes() },
        { .path       = "root.html"_str,
          .media_type = "text/html"_str,
          .contents   = (root_template.is_empty() ? page : root_template).as_bytes() },
        { .path = "package.html"_str, .media_type = "text/html"_str, .contents = page.as_bytes() },
        { .path = "module.html"_str, .media_type = "text/html"_str, .contents = page.as_bytes() },
        { .path = "symbol.html"_str, .media_type = "text/html"_str, .contents = page.as_bytes() },
        { .path       = "skin.css"_str,
          .media_type = "text/css"_str,
          .contents   = "body { color: navy; }"_str.as_bytes() },
    };
    return lito::site::load_frontend_resources(
        "independent-doc"_str,
        slice<lito::site::FrontendResourceInput>::from_raw_parts(
            resources, usize(sizeof(resources) / sizeof(resources[0]))));
}

} // namespace

TEST(DocPublication, UsesAnIndependentApiTwoThemeForEveryPageKind) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root  = temporary->path();
    auto theme = independent_doc_theme();
    ASSERT_TRUE(theme.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = "Own documentation"_Str,
        .output      = child(root, "site"_str),
        .data_output = child(root, "data"_str),
    };
    input.packages.push(record_package(root));
    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(theme).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    auto overview = rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path()).unwrap();
    EXPECT_TRUE(overview.as_str().contains(
        "data-kind=\"root\" data-output=\"index.html\" data-url=\"index.html\""_str));
    EXPECT_TRUE(overview.as_str().contains("Own brandNo icon"_str));
    EXPECT_TRUE(overview.as_str().contains("href=\"index.html\">Own home"_str));
    EXPECT_FALSE(overview.as_str().contains("Litocpp"_str));
    EXPECT_FALSE(overview.as_str().contains("lito-doc-shell"_str));
    EXPECT_FALSE(overview.as_str().contains("static/style.css"_str));
    EXPECT_FALSE(rstd::fs::exists(child(root, "site/static/favicon.svg"_str).as_path()).unwrap());
    EXPECT_TRUE(rstd::fs::exists(child(root, "site/skin.css"_str).as_path()).unwrap());
    auto package =
        rstd::fs::read_to_string(child(root, "site/package/records/index.html"_str).as_path())
            .unwrap();
    EXPECT_TRUE(package.as_str().contains("data-kind=\"package\""_str));
    EXPECT_TRUE(package.as_str().contains("href=\"../../index.html\">Own home"_str));
    auto modules =
        rstd::fs::read_dir(child(root, "site/package/records/module"_str).as_path()).unwrap();
    auto module_count = usize {};
    for (auto entry = modules.next(); entry.is_some(); entry = modules.next()) {
        ASSERT_TRUE(entry->is_ok());
        auto html = rstd::fs::read_to_string(entry->unwrap().path().as_path()).unwrap();
        EXPECT_TRUE(html.as_str().contains("data-kind=\"module\""_str));
        EXPECT_TRUE(html.as_str().contains("href=\"../../../skin.css\""_str));
        ++module_count;
    }
    EXPECT_TRUE(module_count > usize {});
    auto symbol =
        symbol_page_containing(generated->packages[usize {}].directory.as_path(), "class Box"_str);
    ASSERT_TRUE(symbol.is_ok());
    EXPECT_TRUE(symbol->as_str().contains("data-kind=\"symbol\""_str));
    EXPECT_TRUE(symbol->as_str().contains("href=\"../../../index.html\">Own home"_str));
    EXPECT_TRUE(symbol->as_str().contains("value_type =&gt; T"_str));
    auto theme_again = independent_doc_theme();
    ASSERT_TRUE(theme_again.is_ok());
    auto rendered =
        lito::doc::render({ .data = child(root, "data"_str), .output = child(root, "copy"_str) },
                          Some(rstd::move(theme_again).unwrap()));
    ASSERT_TRUE(rendered.is_ok());
    auto copy = rstd::fs::read_to_string(child(root, "copy/index.html"_str).as_path()).unwrap();
    EXPECT_EQ(copy.as_str(), overview.as_str());
}

TEST(DocPublication, RejectsOldContextFieldsWithoutReplacingTheSite) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root  = temporary->path();
    auto theme = independent_doc_theme();
    ASSERT_TRUE(theme.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = "Own documentation"_Str,
        .output      = child(root, "site"_str),
        .data_output = child(root, "data"_str),
    };
    input.packages.push(record_package(root));
    ASSERT_TRUE(lito::doc::generate(rstd::move(input), Some(rstd::move(theme).unwrap())).is_ok());
    auto previous = rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path()).unwrap();
    for (auto field : array<ref<str>, 8> { "site.has_default_brand"_str,
                                           "site.has_repository"_str,
                                           "page.has_outline"_str,
                                           "page.search_package"_str,
                                           "navigation.show_modules"_str,
                                           "navigation.has_packages"_str,
                                           "navigation.scroll_state_url"_str,
                                           "page.is_not_found"_str }) {
        auto body = String::make("{{#if "_str);
        body.push_str(field);
        body.push_str("}}old{{/if}}"_str);
        auto old_theme = independent_doc_theme(body.as_str());
        ASSERT_TRUE(old_theme.is_ok());
        auto rendered = lito::doc::render(
            { .data = child(root, "data"_str), .output = child(root, "site"_str) },
            Some(rstd::move(old_theme).unwrap()));
        EXPECT_TRUE(rendered.is_err());
        EXPECT_EQ(rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path())
                      .unwrap()
                      .as_str(),
                  previous.as_str());
    }
    for (auto field : array<ref<str>, 6> { "symbol.has_documentation"_str,
                                           "symbol.has_namespace"_str,
                                           "symbol.has_parent"_str,
                                           "symbol.parent_name"_str,
                                           "symbol.has_methods"_str,
                                           "symbol.has_fields"_str }) {
        auto old_theme = independent_doc_theme();
        ASSERT_TRUE(old_theme.is_ok());
        auto body = String::make("{{#if "_str);
        body.push_str(field);
        body.push_str("}}old{{/if}}"_str);
        auto document = lito::site::parse_template("symbol.html"_str, body.as_str());
        ASSERT_TRUE(document.is_ok());
        old_theme->templates.documents.insert("symbol.html"_Str, rstd::move(document).unwrap());
        EXPECT_TRUE(lito::doc::render(
                        { .data = child(root, "data"_str), .output = child(root, "site"_str) },
                        Some(rstd::move(old_theme).unwrap()))
                        .is_err());
        EXPECT_EQ(rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path()).unwrap(),
                  previous);
    }
    auto incomplete = independent_doc_theme();
    ASSERT_TRUE(incomplete.is_ok());
    incomplete->symbol_template = "absent.html"_Str;
    EXPECT_TRUE(
        lito::doc::render({ .data = child(root, "data"_str), .output = child(root, "site"_str) },
                          Some(rstd::move(incomplete).unwrap()))
            .is_err());
    EXPECT_EQ(rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path()).unwrap(),
              previous);
    auto mutated = independent_doc_theme();
    ASSERT_TRUE(mutated.is_ok());
    mutated->doc_template_api = usize(1);
    EXPECT_TRUE(
        lito::doc::render({ .data = child(root, "data"_str), .output = child(root, "site"_str) },
                          Some(rstd::move(mutated).unwrap()))
            .is_err());
    EXPECT_EQ(
        rstd::fs::read_to_string(child(root, "site/index.html"_str).as_path()).unwrap().as_str(),
        previous.as_str());
}

TEST(DocPublication, PublishesRelocatablePackageSitesFromOneDataset) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root     = temporary->path();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = String::make("Publication fixture"_str),
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    auto alpha_package = publication_package(root, "alpha"_str, "alpha"_str);
    append_module(alpha_package, root, "alpha:child"_str);
    input.packages.push(rstd::move(alpha_package));
    input.packages.push(publication_package(root, "beta"_str, "beta"_str));

    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    ASSERT_TRUE(generated->publication_set.is_some());
    EXPECT_EQ(generated->publication_set->packages.len(), usize(2));
    EXPECT_TRUE(
        rstd::fs::exists(child(root, "publication/publication-set.json"_str).as_path()).unwrap());
    EXPECT_TRUE(
        rstd::fs::exists(child(root, "publication/alpha/index.html"_str).as_path()).unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(child(root, "publication/alpha/package/alpha/index.html"_str).as_path())
            .unwrap());

    const auto& alpha = generated->publication_set->packages[usize(0)];
    ASSERT_TRUE(! alpha.files.is_empty());
    for (const auto& file : alpha.files) {
        EXPECT_FALSE(file.path.as_str().starts_with("source/"_str));
        auto path     = child(root, rstd::format("publication/alpha/{}", file.path).as_str());
        auto contents = rstd::fs::read_to_string(path.as_path());
        ASSERT_TRUE(contents.is_ok());
        EXPECT_EQ(contents->len(), file.size);
        EXPECT_EQ(licrypto::sha256_hex(contents->as_str()).unwrap().as_str(), file.sha256.as_str());
        EXPECT_FALSE(file.path.as_str() == "publication.json"_str);
    }
    auto search =
        rstd::fs::read_to_string(child(root, "publication/alpha/search-index.json"_str).as_path());
    ASSERT_TRUE(search.is_ok());
    EXPECT_FALSE(search->as_str().contains("package/alpha/"_str));
    EXPECT_TRUE(search->as_str().contains("symbol/"_str));
    auto navigation =
        rstd::fs::read_to_string(child(root, "publication/alpha/navigation.json"_str).as_path());
    ASSERT_TRUE(navigation.is_ok());
    EXPECT_TRUE(navigation->as_str().contains("\"format\": \"lito-doc-navigation\""_str));
    EXPECT_TRUE(navigation->as_str().contains("\"package\": \"alpha\""_str));
    EXPECT_TRUE(navigation->as_str().contains("\"label\": \"child\""_str));
    EXPECT_TRUE(navigation->as_str().contains("\"url\": \"module/"_str));
    EXPECT_FALSE(navigation->as_str().contains("package/alpha/"_str));
    auto module_navigation_page = String::make();
    auto root_module_count      = usize {};
    for (const auto& file : alpha.files) {
        if (! file.path.as_str().starts_with("module/"_str) ||
            ! file.path.as_str().ends_with(".html"_str))
            continue;
        auto contents = rstd::fs::read_to_string(
            child(root, rstd::format("publication/alpha/{}", file.path).as_str()).as_path());
        ASSERT_TRUE(contents.is_ok());
        if (contents->as_str().contains("<h1 class=\"page-title\"><code>alpha</code></h1>"_str)) {
            ++root_module_count;
            EXPECT_FALSE(contents->as_str().contains("module-navigation-url"_str));
            EXPECT_FALSE(contents->as_str().contains("data-has-sidebar"_str));
        }
        if (contents->as_str().contains("module-navigation-url"_str))
            module_navigation_page = rstd::move(contents).unwrap();
    }
    EXPECT_EQ(root_module_count, usize(1));
    ASSERT_TRUE(! module_navigation_page.is_empty());
    EXPECT_TRUE(module_navigation_page.as_str().contains(
        "module-navigation-url=\"../navigation.json\""_str));
    EXPECT_TRUE(
        module_navigation_page.as_str().contains("module-navigation-package=\"alpha\""_str));
    EXPECT_TRUE(
        module_navigation_page.as_str().contains("scroll-state-url=\"../navigation.json\""_str));
    EXPECT_TRUE(module_navigation_page.as_str().contains("../static/theme-icons.svg#moon"_str));
    EXPECT_FALSE(module_navigation_page.as_str().contains("sidebar-footer"_str));
    EXPECT_TRUE(module_navigation_page.as_str().contains(
        "<link rel=\"preload\" href=\"../navigation.json\" as=\"fetch\" "
        "crossorigin=\"anonymous\">"_str));
    EXPECT_TRUE(module_navigation_page.as_str().contains(
        "<ul class=\"nav-list module-list\" data-module-navigation>"_str));
    EXPECT_TRUE(module_navigation_page.as_str().contains(
        "<a href=\"../index.html\">View package modules</a>"_str));
    EXPECT_FALSE(module_navigation_page.as_str().contains("<li><a href=\"../module/"_str));
}

TEST(DocPublication, PublishesOrdinaryTranslationUnitsWithoutAModule) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root   = temporary->path();
    auto source = child(root, "src/plain.c"_str);
    auto unit   = lito::doc::DocumentationUnit {
        .source          = source.clone(),
        .source_contents = String::make("\n\nint value(void) { return 7; }\n"_str),
        .kind            = lito::doc::DocumentationUnitKind::Ordinary,
    };
    unit.declarations.push(lito::doc::DeclarationOutline {
        .semantic_identity = "plain-value"_Str,
        .kind              = lito::doc::DeclarationKind::Function,
        .name              = "value"_Str,
        .qualified_name    = "value"_Str,
        .signature         = "int value(void);"_Str,
        .scope_signature   = "int value(void);"_Str,
        .is_definition     = true,
        .origin            = lito::doc::DeclarationOrigin::Ordinary,
        .spelling_span     = source_span(source),
        .expansion_span    = source_span(source),
    });
    unit.diagnostics.push(lito::doc::DocumentationDiagnostic {
        .code    = "external-warning"_Str,
        .message = "Warning in an external header."_Str,
        .span = { .path = PathBuf::from("/system/include/foreign.h"_str), .begin_line = usize(8) },
    });
    unit.diagnostics.push(lito::doc::DocumentationDiagnostic {
        .code    = "locationless-warning"_Str,
        .message = "Warning without a source location."_Str,
    });
    unit.diagnostics.push(lito::doc::DocumentationDiagnostic {
        .code    = "local-warning"_Str,
        .message = "Warning in this package."_Str,
        .span    = source_span(source),
    });
    auto package = lito::doc::PackageInput {
        .name              = "plain"_Str,
        .version           = "1.2.3"_Str,
        .source_identity   = "registry+https://registry.example.test/"_Str,
        .profile           = "release"_Str,
        .root              = PathBuf::from(root),
        .toolchain_version = "clang 22"_Str,
        .toolchain_target  = "x86_64-unknown-linux-gnu"_Str,
        .language_standard = "c17"_Str,
    };
    package.units.push(rstd::move(unit));
    auto input = lito::doc::SiteInput {
        .title       = "Ordinary package"_Str,
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    input.packages.push(rstd::move(package));
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    ASSERT_TRUE(generated->publication_set.is_some());
    ASSERT_EQ(generated->publication_set->packages.len(), usize(1));
    auto manifest =
        rstd::fs::read_to_string(child(root, "publication/plain/publication.json"_str).as_path());
    ASSERT_TRUE(manifest.is_ok());
    EXPECT_TRUE(manifest->as_str().contains("\"root_module\": \"\""_str));
    auto index =
        rstd::fs::read_to_string(child(root, "publication/plain/index.html"_str).as_path());
    ASSERT_TRUE(index.is_ok());
    EXPECT_TRUE(index->as_str().contains("id=\"symbols\""_str));
    EXPECT_FALSE(index->as_str().contains("id=\"modules\""_str));
    auto data = rstd::fs::read_to_string(child(root, "publication/plain/doc.json"_str).as_path());
    ASSERT_TRUE(data.is_ok());
    EXPECT_TRUE(data->as_str().contains("/system/include/foreign.h"_str));
    EXPECT_TRUE(data->as_str().contains("locationless-warning"_str));
    EXPECT_TRUE(data->as_str().contains("\"path\": \"src/plain.c\""_str));
    auto symbols = usize();
    for (const auto& file : generated->publication_set->packages[usize()].files) {
        EXPECT_FALSE(file.path.as_str().starts_with("module/"_str));
        auto path     = child(root, rstd::format("publication/plain/{}", file.path).as_str());
        auto contents = rstd::fs::read_to_string(path.as_path());
        ASSERT_TRUE(contents.is_ok());
        EXPECT_EQ(contents->len(), file.size);
        EXPECT_EQ(licrypto::sha256_hex(contents->as_str()).unwrap().as_str(), file.sha256.as_str());
        if (! file.path.as_str().starts_with("symbol/"_str)) continue;
        ++symbols;
        EXPECT_TRUE(index->as_str().contains(rstd::format("href=\"{}\"", file.path).as_str()));
        EXPECT_TRUE(contents->as_str().contains("href=\"../index.html\">plain</a>"_str));
        EXPECT_FALSE(contents->as_str().contains("href=\"../index.html\"></a>"_str));
    }
    EXPECT_EQ(symbols, usize(1));
    frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto rendered =
        lito::doc::render({ .data = child(root, "data"_str), .output = child(root, "site"_str) },
                          Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(rendered.is_ok());
    auto workspace =
        rstd::fs::read_to_string(child(root, "site/package/plain/index.html"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->as_str().contains("id=\"symbols\""_str));
    EXPECT_TRUE(workspace->as_str().contains(">value</code></a>"_str));
}

TEST(DocPublication, RejectsSourceSnapshotsOutsidePackageRoot) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root                     = temporary->path();
    auto package                  = publication_package(root, "plain"_str, "plain"_str);
    package.units[usize()].source = PathBuf::from("/system/include/foreign.h"_str);
    auto input                    = lito::doc::SiteInput {
        .title       = "Source boundary"_Str,
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    input.packages.push(rstd::move(package));
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_err());
    EXPECT_TRUE(generated.unwrap_err().as_str().contains("outside package root"_str));
    EXPECT_FALSE(
        rstd::fs::exists(child(root, "publication/publication-set.json"_str).as_path()).unwrap());
}

TEST(DocPublication, PackageManifestDoesNotDependOnSiblingPackages) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root = temporary->path();

    auto single_frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(single_frontend.is_ok());
    auto single = lito::doc::SiteInput {
        .title       = String::make("Single package"_str),
        .output      = child(root, "single"_str),
        .data_output = child(root, "single-data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    single.packages.push(publication_package(root, "alpha"_str, "alpha"_str));
    auto single_generated =
        lito::doc::generate(rstd::move(single), Some(rstd::move(single_frontend).unwrap()));
    ASSERT_TRUE(single_generated.is_ok());

    auto workspace_frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(workspace_frontend.is_ok());
    auto workspace = lito::doc::SiteInput {
        .title       = String::make("Workspace"_str),
        .output      = child(root, "workspace"_str),
        .data_output = child(root, "workspace-data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    workspace.packages.push(publication_package(root, "alpha"_str, "alpha"_str));
    workspace.packages.push(publication_package(root, "beta"_str, "beta"_str));
    auto workspace_generated =
        lito::doc::generate(rstd::move(workspace), Some(rstd::move(workspace_frontend).unwrap()));
    ASSERT_TRUE(workspace_generated.is_ok());

    auto single_manifest =
        rstd::fs::read_to_string(child(root, "single/alpha/publication.json"_str).as_path());
    auto workspace_manifest =
        rstd::fs::read_to_string(child(root, "workspace/alpha/publication.json"_str).as_path());
    ASSERT_TRUE(single_manifest.is_ok());
    ASSERT_TRUE(workspace_manifest.is_ok());
    EXPECT_EQ(single_manifest->as_str(), workspace_manifest->as_str());
}

TEST(DocPublication, InlinesRecordFunctionsAndFieldsOnTheRecordPage) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root     = temporary->path();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = String::make("Record fixture"_str),
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    input.packages.push(record_package(root));

    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    if (generated.is_err()) rstd::io::eprintln("{}", generated.unwrap_err());
    ASSERT_TRUE(generated.is_ok());
    ASSERT_TRUE(generated->publication_set.is_some());
    const auto& publication  = generated->publication_set->packages[usize(0)];
    auto        symbol_pages = usize {};
    auto        record_html  = String::make();
    for (const auto& file : publication.files) {
        if (! file.path.as_str().starts_with("symbol/"_str) ||
            ! file.path.as_str().ends_with(".html"_str))
            continue;
        ++symbol_pages;
        auto contents = rstd::fs::read_to_string(
            child(root, rstd::format("publication/records/{}", file.path).as_str()).as_path());
        ASSERT_TRUE(contents.is_ok());
        if (contents->as_str().contains("<h1 class=\"page-title\">Box</h1>"_str))
            record_html = rstd::move(contents).unwrap();
    }
    EXPECT_EQ(symbol_pages, usize(3));
    ASSERT_TRUE(! record_html.is_empty());
    EXPECT_TRUE(record_html.as_str().contains("Stored value."_str));
    EXPECT_TRUE(record_html.as_str().contains("value_type → T"_str));
    EXPECT_TRUE(record_html.as_str().contains("The stored type."_str));
    EXPECT_EQ(occurrences(record_html.as_str(), "id=\"type-"_str), usize(1));
    EXPECT_TRUE(record_html.as_str().contains("Returns the value."_str));
    EXPECT_TRUE(record_html.as_str().contains("Definition docs."_str));
    EXPECT_TRUE(record_html.as_str().contains("public:"_str));
    EXPECT_TRUE(record_html.as_str().contains("template &lt;typename T&gt;\nclass Box {"_str));
    EXPECT_TRUE(record_html.as_str().contains("T value{};"_str));
    EXPECT_TRUE(record_html.as_str().contains("unsigned int flags : 3 = 1;"_str));
    EXPECT_TRUE(record_html.as_str().contains(
        "auto get() &amp;&amp; -&gt; <a class=\"declaration-reference\""_str));
    EXPECT_TRUE(
        record_html.as_str().contains("class=\"declaration-reference\" href=\"../symbol/"_str));
    EXPECT_TRUE(record_html.as_str().contains("\">Box</a>&lt;T&gt;;"_str));
    EXPECT_TRUE(
        record_html.as_str().contains("template &lt;typename U&gt;\n    requires Numeric&lt;U&gt;\n"
                                      "[[nodiscard]] constexpr auto get"_str));
    EXPECT_EQ(occurrences(record_html.as_str(), "class=\"member-summary\""_str), usize(2));
    EXPECT_EQ(occurrences(record_html.as_str(), "<details "_str), usize(2));
    EXPECT_EQ(occurrences(record_html.as_str(), " open>"_str), usize(2));
    EXPECT_EQ(occurrences(record_html.as_str(), "member-summary-static"_str), usize(1));
    EXPECT_FALSE(record_html.as_str().contains("Declared in"_str));
    EXPECT_FALSE(record_html.as_str().contains("../source/"_str));
    EXPECT_EQ(occurrences(record_html.as_str(), "class=\"record-member method-detail\""_str),
              usize(3));
    EXPECT_FALSE(record_html.as_str().contains("<h2><a href=\"#method-"_str));
    EXPECT_EQ(occurrences(record_html.as_str(), "id=\"method-"_str), usize(3));
    EXPECT_EQ(occurrences(record_html.as_str(), "id=\"field-"_str), usize(2));
    EXPECT_FALSE(record_html.as_str().contains("Box&lt;int&gt;::get"_str));
    EXPECT_FALSE(record_html.as_str().contains("box.cpp:1"_str));

    auto search = rstd::fs::read_to_string(
        child(root, "publication/records/search-index.json"_str).as_path());
    ASSERT_TRUE(search.is_ok());
    EXPECT_EQ(occurrences(search->as_str(), "#method-"_str), usize(3));
    EXPECT_EQ(occurrences(search->as_str(), "#field-"_str), usize(2));
    EXPECT_EQ(occurrences(search->as_str(), "#type-"_str), usize(1));
}

TEST(DocPublication, OmitsSourcePagesFromWorkspaceSite) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root     = temporary->path();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = String::make("Records"_str),
        .output      = child(root, "site"_str),
        .data_output = child(root, "data"_str),
    };
    input.packages.push(record_package(root));
    ASSERT_TRUE(
        lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap())).is_ok());
    EXPECT_TRUE(rstd::fs::exists(child(root, "site/index.html"_str).as_path()).unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(child(root, "site/package/records/source"_str).as_path()).unwrap());
}

TEST(DocPublication, LinksConceptReferencesAcrossPackagesAndDatasetReload) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto  root                       = temporary->path();
    auto  owner                      = publication_package(root, "owner"_str, "owner"_str);
    auto  target                     = publication_package(root, "target"_str, "target"_str);
    auto& concept_symbol             = target.units[usize {}].declarations[usize {}];
    concept_symbol.name              = String::make("Impled"_str);
    concept_symbol.qualified_name    = String::make("rstd::Impled"_str);
    concept_symbol.kind              = lito::doc::DeclarationKind::Concept;
    concept_symbol.semantic_identity = String::make("concept:Impl"_str);
    concept_symbol.signature = String::make("template <typename T> concept Impled = true;"_str);
    concept_symbol.scope_signature = concept_symbol.signature.clone();
    auto& function                 = owner.units[usize {}].declarations[usize {}];
    auto  prefix                   = "template <typename T> requires rstd::"_str;
    function.signature             = rstd::format("{}Impled<T>\nauto value() -> int;", prefix);
    function.signature.references.push({ prefix.len(),
                                         prefix.len() + usize(6),
                                         String::make("concept:Impl"_str),
                                         lito::doc::DeclarationReferenceKind::Concept });
    auto input = lito::doc::SiteInput {
        .title       = String::make("Concept links"_str),
        .output      = child(root, "site"_str),
        .data_output = child(root, "data"_str),
    };
    input.packages.push(rstd::move(owner));
    input.packages.push(rstd::move(target));
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    auto page = symbol_page_containing(generated->packages[usize {}].directory.as_path(),
                                       "<h1 class=\"page-title\">owner::value</h1>"_str);
    ASSERT_TRUE(page.is_ok());
    EXPECT_TRUE(page->as_str().contains(
        "rstd::<a class=\"declaration-reference\" href=\"../../target/symbol/"_str));
    EXPECT_TRUE(page->as_str().contains(">Impled</a>&lt;T&gt;"_str));
    ASSERT_TRUE(lito::doc::validate_data(child(root, "data"_str).as_path()).is_ok());
    frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto rendered = lito::doc::render(
        { .data = child(root, "data"_str), .output = child(root, "reloaded"_str) },
        Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(rendered.is_ok());
    auto reloaded = symbol_page_containing(rendered->packages[usize {}].directory.as_path(),
                                           "<h1 class=\"page-title\">owner::value</h1>"_str);
    ASSERT_TRUE(reloaded.is_ok());
    EXPECT_EQ(page->as_str(), reloaded->as_str());
}

TEST(DocPublication, ComposesSyntaxWithLinksAndPreservesExampleLanguages) {
    struct Highlighter final : lito::doc::DeclarationHighlighter {
        bool invalid { false };
        auto highlight(ref<str> text) -> Vec<lito::doc::SyntaxToken> override {
            auto tokens = Vec<lito::doc::SyntaxToken>::make();
            if (! text.is_empty())
                tokens.push({ usize {},
                              text.len() + (invalid ? usize(1) : usize {}),
                              lito::doc::SyntaxKind::Keyword });
            return tokens;
        }
    } highlighter;
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root     = temporary->path();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = String::make("Syntax"_str),
        .output      = child(root, "site"_str),
        .data_output = child(root, "data"_str),
        .highlighter = &highlighter,
    };
    auto package = record_package(root);
    package.units[usize {}].declarations[usize {}].comment =
        comment(child(root, "src/box.cppm"_str),
                usize(1),
                "```cpp\nauto s = \"<script>&\";\n```\n```unknown\n<unsafe>\n```"_str);
    input.packages.push(rstd::move(package));
    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    auto page = symbol_page_containing(generated->packages[usize {}].directory.as_path(),
                                       "<h1 class=\"page-title\">Box</h1>"_str);
    ASSERT_TRUE(page.is_ok());
    EXPECT_TRUE(
        page->as_str().contains("<span class=\"syntax-keyword\">template &lt;typename T&gt;"_str));
    EXPECT_TRUE(page->as_str().contains("<span class=\"syntax-keyword\">Box</span></a>"_str));
    EXPECT_TRUE(page->as_str().contains(
        "<code class=\"language-cpp\">auto s = &quot;&lt;script&gt;&amp;&quot;;"_str));
    EXPECT_TRUE(page->as_str().contains("<code class=\"language-unknown\">&lt;unsafe&gt;"_str));
    EXPECT_FALSE(page->as_str().contains("<script>&"_str));

    highlighter.invalid = true;
    frontend            = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto rendered = lito::doc::render({ .data        = child(root, "data"_str),
                                        .output      = child(root, "fallback"_str),
                                        .highlighter = &highlighter },
                                      Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(rendered.is_ok());
    page = symbol_page_containing(rendered->packages[usize {}].directory.as_path(),
                                  "<h1 class=\"page-title\">Box</h1>"_str);
    ASSERT_TRUE(page.is_ok());
    EXPECT_FALSE(page->as_str().contains("syntax-keyword"_str));
    EXPECT_TRUE(page->as_str().contains("\">Box</a>&lt;T&gt;;"_str));
}

TEST(DocPublication, OmitsNamespaceItemsAndShowsDeclarationNamespace) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root     = temporary->path();
    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto input = lito::doc::SiteInput {
        .title       = String::make("Namespace fixture"_str),
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    input.packages.push(namespace_package(root));

    auto generated = lito::doc::generate(rstd::move(input), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    ASSERT_TRUE(generated->publication_set.is_some());
    ASSERT_EQ(generated->packages.len(), usize(1));
    EXPECT_EQ(generated->packages[usize {}].symbols, usize(4));
    EXPECT_EQ(generated->packages[usize {}].documented, usize(4));
    EXPECT_EQ(generated->packages[usize {}].undocumented, usize {});

    const auto& publication  = generated->publication_set->packages[usize {}];
    auto        symbol_pages = usize {};
    auto        module_html  = String::make();
    for (const auto& file : publication.files) {
        EXPECT_FALSE(file.path.as_str().starts_with("symbol/ns-"_str));
        if (file.path.as_str().starts_with("symbol/"_str) &&
            file.path.as_str().ends_with(".html"_str))
            ++symbol_pages;
        if (! file.path.as_str().starts_with("module/"_str) ||
            ! file.path.as_str().ends_with(".html"_str))
            continue;
        auto contents = rstd::fs::read_to_string(
            child(root, rstd::format("publication/namespaced/{}", file.path).as_str()).as_path());
        ASSERT_TRUE(contents.is_ok());
        module_html = rstd::move(contents).unwrap();
    }
    EXPECT_EQ(symbol_pages, usize(3));
    ASSERT_TRUE(! module_html.is_empty());
    EXPECT_FALSE(module_html.as_str().contains("id=\"namespaces\""_str));
    EXPECT_FALSE(module_html.as_str().contains("Namespace documentation."_str));
    EXPECT_EQ(occurrences(module_html.as_str(), "<span class=\"namespace-label\">alpha</span>"_str),
              usize(2));

    auto record_html   = symbol_page_containing(publication.directory.as_path(),
                                                "<h1 class=\"page-title\">alpha::Widget</h1>"_str);
    auto function_html = symbol_page_containing(
        publication.directory.as_path(), "<h1 class=\"page-title\">alpha::make_widget</h1>"_str);
    auto global_function_html = symbol_page_containing(
        publication.directory.as_path(), "<h1 class=\"page-title\">global_helper</h1>"_str);
    ASSERT_TRUE(record_html.is_ok());
    ASSERT_TRUE(function_html.is_ok());
    ASSERT_TRUE(global_function_html.is_ok());
    constexpr auto namespace_identity =
        "<span class=\"namespace-name\">namespace <code>alpha</code></span>"_str;
    EXPECT_EQ(occurrences(record_html->as_str(), namespace_identity), usize(1));
    EXPECT_EQ(occurrences(function_html->as_str(), namespace_identity), usize(1));
    EXPECT_FALSE(global_function_html->as_str().contains("namespace-name"_str));
    EXPECT_TRUE(record_html->as_str().contains("Method documentation."_str));
    EXPECT_FALSE(record_html->as_str().contains("Namespace documentation."_str));

    auto data =
        rstd::fs::read_to_string(child(root, "publication/namespaced/doc.json"_str).as_path());
    auto search = rstd::fs::read_to_string(
        child(root, "publication/namespaced/search-index.json"_str).as_path());
    ASSERT_TRUE(data.is_ok());
    ASSERT_TRUE(search.is_ok());
    EXPECT_FALSE(data->as_str().contains("\"kind\": \"namespace\""_str));
    EXPECT_FALSE(search->as_str().contains("\"kind\": \"namespace\""_str));
    EXPECT_EQ(occurrences(data->as_str(), "\"namespace\": \"alpha\""_str), usize(3));
}

TEST(DocPublication, RejectsInvalidDeclarationReferenceRanges) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root    = temporary->path();
    auto package = record_package(root);
    package.units[usize {}].declarations[usize(4)].scope_signature.references[usize {}].end =
        usize(1024);
    auto input = lito::doc::SiteInput {
        .title       = String::make("Invalid declaration reference"_str),
        .output      = child(root, "publication"_str),
        .data_output = child(root, "data"_str),
        .data_only   = true,
    };
    input.packages.push(rstd::move(package));

    auto generated = lito::doc::generate(rstd::move(input));
    ASSERT_TRUE(generated.is_err());
    EXPECT_TRUE(
        generated.unwrap_err().as_str().contains("invalid declaration reference [17, 1024)"_str));
}

TEST(DocPublication, ResolvesOnlyAvailableUnambiguousTypePages) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto root          = temporary->path();
    auto owner_package = [root]() mutable {
        auto package             = record_package(root);
        package.name             = String::make("owner"_str);
        package.source_identity  = String::make("path+owner"_str);
        auto& holder             = package.units[usize {}].declarations[usize {}];
        holder.semantic_identity = String::make("owner::Holder"_str);
        holder.name              = String::make("Holder"_str);
        holder.qualified_name    = String::make("Holder"_str);
        holder.signature         = String::make("template <typename T>\nclass Holder {};"_str);
        holder.scope_signature   = String::make("template <typename T>\nclass Holder {};"_str);
        holder.record_header     = Some(String::make("template <typename T>\nclass Holder"_str));
        append_module(package, root, "records:child"_str);
        for (auto& unit : package.units) prepare_unit(unit, package.root_module.as_str());
        return package;
    };
    auto target_package = [root](ref<str> name) mutable {
        auto package            = record_package(root);
        package.name            = String::make(name);
        package.source_identity = rstd::format("path+{}", name);
        for (auto& unit : package.units) prepare_unit(unit, package.root_module.as_str());
        return package;
    };

    auto frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto workspace = lito::doc::SiteInput {
        .title       = String::make("Workspace links"_str),
        .output      = child(root, "workspace"_str),
        .data_output = child(root, "workspace-data"_str),
    };
    workspace.packages.push(owner_package());
    workspace.packages.push(target_package("target"_str));
    auto generated =
        lito::doc::generate(rstd::move(workspace), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    auto workspace_page = symbol_page_containing(generated->packages[usize {}].directory.as_path(),
                                                 "<h1 class=\"page-title\">Holder</h1>"_str);
    ASSERT_TRUE(workspace_page.is_ok());
    EXPECT_TRUE(workspace_page->as_str().contains("href=\"../../target/symbol/"_str));
    EXPECT_TRUE(workspace_page->as_str().contains(
        "module-navigation-url=\"../../../package/owner/navigation.json\""_str));
    EXPECT_TRUE(workspace_page->as_str().contains(
        "scroll-state-url=\"../../../package/owner/navigation.json\""_str));
    EXPECT_TRUE(workspace_page->as_str().contains(
        "href=\"../../../package/owner/index.html\">View package modules"_str));

    frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto publications = lito::doc::SiteInput {
        .title       = String::make("Package links"_str),
        .output      = child(root, "publications"_str),
        .data_output = child(root, "publications-data"_str),
        .publication = lito::doc::PublicationKind::PackageSet,
    };
    publications.packages.push(owner_package());
    publications.packages.push(target_package("target"_str));
    generated = lito::doc::generate(rstd::move(publications), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    ASSERT_TRUE(generated->publication_set.is_some());
    auto package_page =
        symbol_page_containing(generated->publication_set->packages[usize {}].directory.as_path(),
                               "<h1 class=\"page-title\">Holder</h1>"_str);
    ASSERT_TRUE(package_page.is_ok());
    EXPECT_FALSE(package_page->as_str().contains("href=\"../../target/symbol/"_str));
    EXPECT_TRUE(
        package_page->as_str().contains("module-navigation-url=\"../navigation.json\""_str));

    frontend = lito::doc::web::load_default_frontend();
    ASSERT_TRUE(frontend.is_ok());
    auto ambiguous = lito::doc::SiteInput {
        .title       = String::make("Ambiguous links"_str),
        .output      = child(root, "ambiguous"_str),
        .data_output = child(root, "ambiguous-data"_str),
    };
    ambiguous.packages.push(owner_package());
    ambiguous.packages.push(target_package("target-a"_str));
    ambiguous.packages.push(target_package("target-b"_str));
    generated = lito::doc::generate(rstd::move(ambiguous), Some(rstd::move(frontend).unwrap()));
    ASSERT_TRUE(generated.is_ok());
    auto ambiguous_page = symbol_page_containing(generated->packages[usize {}].directory.as_path(),
                                                 "<h1 class=\"page-title\">Holder</h1>"_str);
    ASSERT_TRUE(ambiguous_page.is_ok());
    EXPECT_FALSE(ambiguous_page->as_str().contains("class=\"declaration-reference\""_str));
}
