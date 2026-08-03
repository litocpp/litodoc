export module tenon.doc:database;

import rstd;
import tenon.frontend;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::doc
{

inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FNV_PRIME  = 1099511628211ull;

auto add_identity(uint64_t& hash, ref<str> value) -> void {
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= FNV_PRIME;
    }
    hash ^= 0;
    hash *= FNV_PRIME;
}

auto identity_hash(ref<str> recipe, ref<str> value) -> String {
    auto hash = FNV_OFFSET;
    add_identity(hash, recipe);
    add_identity(hash, value);
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto safe_package_name(ref<str> name) -> bool {
    if (name.is_empty()) return false;
    for (auto byte : name) {
        auto valid = (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
                     (byte >= u8('0') && byte <= u8('9')) || byte == u8('-') || byte == u8('_');
        if (! valid) return false;
    }
    return true;
}

auto package_path(ref<rstd::path::Path> root, ref<rstd::path::Path> source)
    -> Result<String, String> {
    auto relative = source.strip_prefix(root);
    if (relative.is_none())
        return Err(rstd::format("doc source '{}' is outside package root '{}'", source, root));
    auto path = relative->to_str();
    if (path.is_none()) return Err(rstd::format("doc source path '{}' is not valid UTF-8", source));
    return Ok(String::make(*path));
}

auto stable_key(ref<str> package, ref<str> module, const frontend::DeclarationOutline& declaration)
    -> String {
    auto signature = declaration.signature.as_str().trim_ascii();
    if (signature.ends_with(";"_str) || signature.ends_with("{"_str))
        signature = signature.get(usize {}, signature.len() - usize(1)).unwrap().trim_ascii();
    return rstd::format("{}|{}|{}|{}|{}",
                        package,
                        module,
                        declaration_kind_name(declaration.kind),
                        declaration.qualified_name.as_str(),
                        signature);
}

auto clone_optional_text(const Option<frontend::DocumentationComment>& comment) -> Option<String> {
    return comment.is_some() ? Some(comment->text.clone()) : Option<String> {};
}

auto make_database(Vec<PackageInput> packages) -> Result<Database, String> {
    auto database      = Database {};
    auto package_names = rstd::collections::BTreeMap<String, empty>::make();
    for (auto& source_package : packages) {
        if (! safe_package_name(source_package.name.as_str())) {
            return Err(rstd::format("doc package name '{}' is not path-safe", source_package.name));
        }
        if (package_names.contains_key(source_package.name.as_str()))
            return Err(rstd::format("duplicate doc package '{}'", source_package.name));
        package_names.insert(source_package.name.clone(), empty {});

        auto package = Package {
            .name              = rstd::move(source_package.name),
            .version           = rstd::move(source_package.version),
            .root_module       = rstd::move(source_package.root_module),
            .profile           = rstd::move(source_package.profile),
            .toolchain_version = rstd::move(source_package.toolchain_version),
            .toolchain_target  = rstd::move(source_package.toolchain_target),
            .language_standard = rstd::move(source_package.language_standard),
        };
        auto modules = rstd::collections::BTreeMap<String, Module>::make();
        auto sources = rstd::collections::BTreeMap<String, Source>::make();
        auto symbols = rstd::collections::BTreeMap<String, Symbol>::make();
        for (auto& unit : source_package.units) {
            auto path = package_path(source_package.root.as_path(), unit.source.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            auto existing_source = sources.get(path->as_str());
            if (existing_source.is_some() && (**existing_source).contents.as_str() !=
                                                 unit.source_snapshot.get()->contents.as_str()) {
                return Err(
                    rstd::format("conflicting doc snapshots for source '{}'", path->as_str()));
            }
            if (existing_source.is_none()) {
                auto source_page =
                    rstd::format("source/src-{}.html",
                                 identity_hash("tenon-doc-source-v1"_str, path->as_str()).as_str());
                sources.insert(path->clone(),
                               Source {
                                   .path     = path->clone(),
                                   .page     = rstd::move(source_page),
                                   .contents = unit.source_snapshot.get()->contents.clone(),
                               });
            }
            auto source_record   = sources.get(path->as_str()).unwrap();
            auto module_name     = unit.logical_module.is_empty() ? package.root_module.as_str()
                                                                  : unit.logical_module.as_str();
            auto existing_module = modules.get_mut(module_name);
            if (existing_module.is_some()) {
                const auto& previous = (**existing_module).comment;
                if (previous.is_some() && unit.module_comment.is_some() &&
                    previous->as_str() != unit.module_comment->text.as_str()) {
                    return Err(
                        rstd::format("conflicting documentation for module '{}'", module_name));
                }
                if (previous.is_none() && unit.module_comment.is_some())
                    (**existing_module).comment = Some(unit.module_comment->text.clone());
            } else {
                auto module_page =
                    rstd::format("module/mod-{}.html",
                                 identity_hash("tenon-doc-module-v1"_str, module_name).as_str());
                modules.insert(String::make(module_name),
                               Module {
                                   .name    = String::make(module_name),
                                   .page    = rstd::move(module_page),
                                   .comment = unit.module_comment.is_some()
                                                  ? Some(unit.module_comment->text.clone())
                                                  : Option<String> {},
                               });
            }
            auto module_record  = modules.get(module_name).unwrap();
            auto mutable_module = modules.get_mut(module_name).unwrap();
            for (const auto& reexport : unit.reexports) {
                auto duplicate = false;
                for (const auto& existing : mutable_module->reexports) {
                    if (existing.as_str() == reexport.logical_module.as_str()) {
                        duplicate = true;
                        break;
                    }
                }
                if (! duplicate) mutable_module->reexports.push(reexport.logical_module.clone());
            }
            package.unsupported += unit.unsupported;
            for (const auto& diagnostic : unit.diagnostics) {
                auto diagnostic_path =
                    package_path(source_package.root.as_path(), diagnostic.span.path.as_path());
                if (diagnostic_path.is_err()) return Err(rstd::move(diagnostic_path).unwrap_err());
                package.diagnostics.push(Diagnostic {
                    .severity = diagnostic.severity,
                    .code     = diagnostic.code.clone(),
                    .message  = diagnostic.message.clone(),
                    .path     = rstd::move(diagnostic_path).unwrap(),
                    .line     = diagnostic.span.begin_line,
                });
            }
            for (const auto& declaration : unit.declarations) {
                if (! declaration.exported ||
                    declaration.access != frontend::DeclarationAccess::Public)
                    continue;
                auto key      = stable_key(package.name.as_str(), module_name, declaration);
                auto comment  = clone_optional_text(declaration.comment);
                auto existing = symbols.get_mut(key.as_str());
                if (existing.is_some()) {
                    const auto& previous = (**existing).comment;
                    auto        conflict = previous.is_some() && comment.is_some() &&
                                           previous->as_str() != comment->as_str();
                    auto        prefer_incoming =
                        declaration.is_definition && ! (**existing).is_definition;
                    if (conflict) {
                        auto message = rstd::format(
                            "conflicting documentation for '{}'; kept the first entry at the "
                            "same precedence",
                            declaration.qualified_name.as_str());
                        if (prefer_incoming) {
                            message = rstd::format(
                                "definition documentation replaced declaration documentation "
                                "for '{}'",
                                declaration.qualified_name.as_str());
                        } else if ((**existing).is_definition && ! declaration.is_definition) {
                            message =
                                rstd::format("definition documentation retained over declaration "
                                             "documentation for '{}'",
                                             declaration.qualified_name.as_str());
                        }
                        package.diagnostics.push(Diagnostic {
                            .severity = frontend::DocumentationSeverity::Warning,
                            .code     = String::make("conflicting-symbol-documentation"_str),
                            .message  = rstd::move(message),
                            .path     = path->clone(),
                            .line     = declaration.span.begin_line,
                        });
                    }
                    if (prefer_incoming) {
                        (**existing).is_definition = true;
                        (**existing).signature     = declaration.signature.clone();
                        if (comment.is_some()) (**existing).comment = rstd::move(comment);
                        if (declaration.group.is_some())
                            (**existing).group = Some(declaration.group->clone());
                        (**existing).source_page       = source_record->page.clone();
                        (**existing).source_path       = path->clone();
                        (**existing).source_line       = declaration.span.begin_line;
                        (**existing).source_column     = declaration.span.begin_column;
                        (**existing).source_end_line   = declaration.span.end_line;
                        (**existing).source_end_column = declaration.span.end_column;
                    } else if (previous.is_none() && comment.is_some()) {
                        (**existing).comment = rstd::move(comment);
                    }
                    continue;
                }
                auto parent = Option<String> {};
                if (declaration.parent.is_some() && *declaration.parent < unit.declarations.len()) {
                    parent = Some(stable_key(package.name.as_str(),
                                             module_name,
                                             unit.declarations[*declaration.parent]));
                }
                auto page =
                    rstd::format("symbol/{}-{}.html",
                                 declaration_kind_slug(declaration.kind),
                                 identity_hash("tenon-doc-symbol-v1"_str, key.as_str()).as_str());
                symbols.insert(key.clone(),
                               Symbol {
                                   .key               = rstd::move(key),
                                   .page              = rstd::move(page),
                                   .module            = String::make(module_name),
                                   .module_page       = module_record->page.clone(),
                                   .kind              = declaration.kind,
                                   .name              = declaration.name.clone(),
                                   .qualified_name    = declaration.qualified_name.clone(),
                                   .signature         = declaration.signature.clone(),
                                   .is_definition     = declaration.is_definition,
                                   .parent_key        = rstd::move(parent),
                                   .group             = declaration.group.is_some()
                                                            ? Some(declaration.group->clone())
                                                            : Option<String> {},
                                   .comment           = rstd::move(comment),
                                   .source_page       = source_record->page.clone(),
                                   .source_path       = path->clone(),
                                   .source_line       = declaration.span.begin_line,
                                   .source_column     = declaration.span.begin_column,
                                   .source_end_line   = declaration.span.end_line,
                                   .source_end_column = declaration.span.end_column,
                               });
            }
        }
        auto module_values = modules.values_mut();
        for (auto item = module_values.next(); item.is_some(); item = module_values.next())
            package.modules.push(rstd::move(**item));
        auto source_values = sources.values_mut();
        for (auto item = source_values.next(); item.is_some(); item = source_values.next())
            package.sources.push(rstd::move(**item));
        auto symbol_values = symbols.values_mut();
        for (auto item = symbol_values.next(); item.is_some(); item = symbol_values.next()) {
            if ((**item).comment.is_some())
                ++package.documented;
            else
                ++package.undocumented;
            package.symbols.push(rstd::move(**item));
        }
        database.packages.push(rstd::move(package));
    }
    return Ok(rstd::move(database));
}

} // namespace tenon::doc
