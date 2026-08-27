module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <rstd/macro.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module litodoc.executable;

import rstd;
import licrypto;
import lito.doc;
import lito.doc.web;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace lito::doc::tool {

auto as_rstd(llvm::StringRef value) -> String {
  return String::make(ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(value.data()), usize(value.size())));
}

auto as_std(ref<str> value) -> std::string {
  return std::string(reinterpret_cast<const char *>(value->data()),
                     value.len().to_primitive());
}

auto json_text(llvm::json::Value value) -> String {
  auto text = llvm::formatv("{0:2}\n", rstd::move(value)).str();
  return as_rstd(text);
}

template <typename T> auto failure(String message) -> Result<T, String> {
  return Err(rstd::move(message));
}

template <typename T> auto failure(ref<str> message) -> Result<T, String> {
  return Err(String::make(message));
}

auto parse_json(ref<str> contents, ref<rstd::path::Path> path, ref<str> context)
    -> Result<llvm::json::Value, String> {
  auto text = as_std(contents);
  auto parsed = llvm::json::parse(text);
  if (!parsed) {
    return failure<llvm::json::Value>(
        rstd::format("invalid {} JSON '{}': {}", context, path,
                     as_rstd(llvm::toString(parsed.takeError()))));
  }
  return Ok(rstd::move(*parsed));
}

auto read_json(ref<rstd::path::Path> path, ref<str> context)
    -> Result<llvm::json::Value, String> {
  auto contents = rstd::fs::read_to_string(path);
  if (contents.is_err()) {
    return failure<llvm::json::Value>(
        rstd::format("cannot read {} '{}': {}", context, path,
                     rstd::move(contents).unwrap_err()));
  }
  return parse_json(contents->as_str(), path, context);
}

auto read_json(ref<rstd::path::Path> path, ref<str> expected_digest,
               ref<str> context) -> Result<llvm::json::Value, String> {
  auto contents = rstd::fs::read_to_string(path);
  if (contents.is_err()) {
    return failure<llvm::json::Value>(
        rstd::format("cannot read {} '{}': {}", context, path,
                     rstd::move(contents).unwrap_err()));
  }
  auto digest = licrypto::sha256_hex(contents->as_str());
  if (digest.as_str() != expected_digest) {
    return failure<llvm::json::Value>(
        rstd::format("{} '{}' has digest '{}', expected '{}'", context, path,
                     digest, expected_digest));
  }
  return parse_json(contents->as_str(), path, context);
}

auto json_object(const llvm::json::Value &value, ref<str> context)
    -> Result<const llvm::json::Object *, String> {
  auto object = value.getAsObject();
  if (object == nullptr)
    return failure<const llvm::json::Object *>(
        rstd::format("{} must be an object", context));
  return Ok(object);
}

auto required_string(const llvm::json::Object &object, llvm::StringRef name,
                     ref<str> context) -> Result<String, String> {
  auto value = object.getString(name);
  if (!value || value->empty()) {
    return failure<String>(rstd::format("{}.{} must be a non-empty string",
                                        context, as_rstd(name)));
  }
  return Ok(as_rstd(*value));
}

auto optional_string(const llvm::json::Object &object, llvm::StringRef name,
                     ref<str> context) -> Result<Option<String>, String> {
  auto member = object.get(name);
  if (member == nullptr || member->getAsNull().has_value())
    return Ok(None());
  auto value = member->getAsString();
  if (!value) {
    return failure<Option<String>>(
        rstd::format("{}.{} must be a string or null", context, as_rstd(name)));
  }
  return Ok(Some(as_rstd(*value)));
}

auto required_integer(const llvm::json::Object &object, llvm::StringRef name,
                      ref<str> context) -> Result<usize, String> {
  auto value = object.getInteger(name);
  if (!value || *value < 0) {
    return failure<usize>(rstd::format("{}.{} must be an unsigned integer",
                                       context, as_rstd(name)));
  }
  return Ok(usize(static_cast<size_t>(*value)));
}

auto required_bool(const llvm::json::Object &object, llvm::StringRef name,
                   ref<str> context) -> Result<bool, String> {
  auto value = object.getBoolean(name);
  if (!value) {
    return failure<bool>(
        rstd::format("{}.{} must be a boolean", context, as_rstd(name)));
  }
  return Ok(*value);
}

auto required_object(const llvm::json::Object &object, llvm::StringRef name,
                     ref<str> context)
    -> Result<const llvm::json::Object *, String> {
  auto value = object.getObject(name);
  if (value == nullptr) {
    return failure<const llvm::json::Object *>(
        rstd::format("{}.{} must be an object", context, as_rstd(name)));
  }
  return Ok(value);
}

auto required_array(const llvm::json::Object &object, llvm::StringRef name,
                    ref<str> context)
    -> Result<const llvm::json::Array *, String> {
  auto value = object.getArray(name);
  if (value == nullptr) {
    return failure<const llvm::json::Array *>(
        rstd::format("{}.{} must be an array", context, as_rstd(name)));
  }
  return Ok(value);
}

auto validate_header(const llvm::json::Object &object, llvm::StringRef format,
                     int64_t version, ref<str> context)
    -> Result<empty, String> {
  auto actual_format = object.getString("format");
  auto actual_version = object.getInteger("version");
  if (!actual_format || *actual_format != format) {
    return failure<empty>(rstd::format("{} has unsupported format", context));
  }
  if (!actual_version || *actual_version != version) {
    return failure<empty>(rstd::format("{} has unsupported version", context));
  }
  return Ok(empty{});
}

struct ExtractionRequest {
  struct ImportedArtifact {
    String logical_module;
    PathBuf path;
    String identity;
  };

  String request_id;
  String package_name;
  String package_identity;
  PathBuf package_root;
  String target_name;
  String target_kind;
  String unit_identity;
  String unit_kind;
  bool is_interface{false};
  PathBuf source;
  String logical_module;
  PathBuf working_directory;
  std::vector<std::string> arguments;
  String compiler_identity;
  String compiler_target;
  std::vector<ImportedArtifact> imported_artifacts;
};

auto decode_extraction_request(const llvm::json::Value &value)
    -> Result<ExtractionRequest, String> {
  auto root = rstd_try(json_object(value, "extract request"_str));
  rstd_try(validate_header(*root, "litodoc-extract", 1, "extract request"_str));
  auto package =
      rstd_try(required_object(*root, "package", "extract request"_str));
  auto target =
      rstd_try(required_object(*root, "target", "extract request"_str));
  auto unit = rstd_try(required_object(*root, "unit", "extract request"_str));
  auto invocation =
      rstd_try(required_object(*root, "invocation", "extract request"_str));
  auto compiler =
      rstd_try(required_object(*root, "compiler", "extract request"_str));
  auto arguments = rstd_try(required_array(*invocation, "arguments",
                                           "extract request.invocation"_str));
  auto imported = rstd_try(
      required_array(*root, "imported_artifacts", "extract request"_str));
  auto result = ExtractionRequest{
      .request_id =
          rstd_try(required_string(*root, "request_id", "extract request"_str)),
      .package_name = rstd_try(
          required_string(*package, "name", "extract request.package"_str)),
      .package_identity = rstd_try(
          required_string(*package, "identity", "extract request.package"_str)),
      .package_root = PathBuf::from(rstd_try(
          required_string(*root, "package_root", "extract request"_str))),
      .target_name = rstd_try(
          required_string(*target, "name", "extract request.target"_str)),
      .target_kind = rstd_try(
          required_string(*target, "kind", "extract request.target"_str)),
      .unit_identity = rstd_try(
          required_string(*unit, "identity", "extract request.unit"_str)),
      .unit_kind =
          rstd_try(required_string(*unit, "kind", "extract request.unit"_str)),
      .is_interface = rstd_try(
          required_bool(*unit, "is_interface", "extract request.unit"_str)),
      .source = PathBuf::from(rstd_try(
          required_string(*unit, "source", "extract request.unit"_str))),
      .logical_module =
          rstd_try(optional_string(*unit, "module", "extract request.unit"_str))
              .unwrap_or(String::make()),
      .working_directory = PathBuf::from(rstd_try(required_string(
          *invocation, "cwd", "extract request.invocation"_str))),
      .compiler_identity = rstd_try(required_string(
          *compiler, "identity", "extract request.compiler"_str)),
      .compiler_target = rstd_try(
          required_string(*compiler, "target", "extract request.compiler"_str)),
  };
  for (const auto &argument : *arguments) {
    auto text = argument.getAsString();
    if (!text) {
      return failure<ExtractionRequest>(
          "extract request invocation arguments must be strings"_str);
    }
    result.arguments.emplace_back(text->str());
  }
  if (result.arguments.empty()) {
    return failure<ExtractionRequest>(
        "extract request invocation arguments must not be empty"_str);
  }
  for (const auto &value : *imported) {
    auto artifact =
        rstd_try(json_object(value, "extract request imported artifact"_str));
    result.imported_artifacts.push_back(ExtractionRequest::ImportedArtifact{
        .logical_module = rstd_try(required_string(
            *artifact, "module", "extract request imported artifact"_str)),
        .path = PathBuf::from(rstd_try(required_string(
            *artifact, "path", "extract request imported artifact"_str))),
        .identity = rstd_try(required_string(
            *artifact, "identity", "extract request imported artifact"_str)),
    });
  }
  return Ok(rstd::move(result));
}

auto span_for(const clang::SourceManager &manager, clang::SourceRange range,
              bool expansion = false) -> DocumentationSpan {
  auto begin = expansion ? manager.getExpansionLoc(range.getBegin())
                         : manager.getSpellingLoc(range.getBegin());
  auto end = expansion ? manager.getExpansionLoc(range.getEnd())
                       : manager.getSpellingLoc(range.getEnd());
  auto first = manager.getPresumedLoc(begin);
  auto last = manager.getPresumedLoc(end);
  auto path = first.isValid() ? as_rstd(first.getFilename()) : String::make();
  return DocumentationSpan{
      .path = PathBuf::from(path.as_str()),
      .begin_line = first.isValid() ? usize(first.getLine()) : usize{},
      .begin_column = first.isValid() ? usize(first.getColumn()) : usize{},
      .end_line = last.isValid() ? usize(last.getLine()) : usize{},
      .end_column = last.isValid() ? usize(last.getColumn()) : usize{},
  };
}

auto declaration_kind(const clang::NamedDecl &declaration)
    -> Option<DeclarationKind> {
  if (llvm::isa<clang::NamespaceDecl>(declaration))
    return Some(DeclarationKind::Namespace);
  if (llvm::isa<clang::RecordDecl>(declaration))
    return Some(DeclarationKind::Record);
  if (llvm::isa<clang::EnumDecl>(declaration))
    return Some(DeclarationKind::Enum);
  if (llvm::isa<clang::ConceptDecl>(declaration))
    return Some(DeclarationKind::Concept);
  if (llvm::isa<clang::TypedefNameDecl>(declaration))
    return Some(DeclarationKind::Alias);
  if (llvm::isa<clang::FunctionDecl>(declaration))
    return Some(DeclarationKind::Function);
  if (llvm::isa<clang::FieldDecl>(declaration))
    return Some(DeclarationKind::Field);
  if (llvm::isa<clang::VarDecl>(declaration))
    return Some(DeclarationKind::Variable);
  return None();
}

auto declaration_access(const clang::NamedDecl &declaration)
    -> DeclarationAccess {
  switch (declaration.getAccess()) {
  case clang::AS_protected:
    return DeclarationAccess::Protected;
  case clang::AS_private:
    return DeclarationAccess::Private;
  default:
    return DeclarationAccess::Public;
  }
}

auto declaration_definition(const clang::NamedDecl &declaration) -> bool {
  if (const auto *function = llvm::dyn_cast<clang::FunctionDecl>(&declaration))
    return function->isThisDeclarationADefinition();
  if (const auto *tag = llvm::dyn_cast<clang::TagDecl>(&declaration))
    return tag->isThisDeclarationADefinition();
  if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(&declaration))
    return variable->isThisDeclarationADefinition();
  return true;
}

auto declaration_exported(const clang::NamedDecl &declaration,
                          bool module_interface) -> bool {
  if (!module_interface)
    return true;
  if (declaration.isInExportDeclContext())
    return true;
  auto context = declaration.getDeclContext();
  while (context != nullptr && !context->isTranslationUnit()) {
    const auto *owner = llvm::dyn_cast<clang::Decl>(context);
    if (owner != nullptr && owner->isInExportDeclContext())
      return true;
    context = context->getParent();
  }
  return false;
}

auto semantic_identity(const clang::NamedDecl &declaration,
                       const clang::SourceManager &manager) -> String {
  llvm::SmallVector<char, 128> usr;
  if (!clang::index::generateUSRForDecl(&declaration, usr)) {
    return as_rstd(llvm::StringRef(usr.data(), usr.size()));
  }
  auto location =
      manager.getPresumedLoc(manager.getSpellingLoc(declaration.getLocation()));
  return rstd::format("fallback:{}:{}:{}:{}", declaration.getDeclKindName(),
                      location.isValid() ? location.getFilename() : "",
                      location.isValid() ? location.getLine() : 0,
                      location.isValid() ? location.getColumn() : 0);
}

auto described_template(const clang::NamedDecl &declaration)
    -> const clang::TemplateDecl * {
  if (const auto *function =
          llvm::dyn_cast<clang::FunctionDecl>(&declaration)) {
    if (const auto *owner = function->getDescribedFunctionTemplate())
      return owner;
  }
  if (const auto *record = llvm::dyn_cast<clang::CXXRecordDecl>(&declaration)) {
    if (const auto *owner = record->getDescribedClassTemplate())
      return owner;
  }
  if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(&declaration)) {
    if (const auto *owner = variable->getDescribedVarTemplate())
      return owner;
  }
  if (const auto *alias = llvm::dyn_cast<clang::TypeAliasDecl>(&declaration)) {
    if (const auto *owner = alias->getDescribedAliasTemplate())
      return owner;
  }
  return nullptr;
}

auto printable_declaration(const clang::NamedDecl &declaration)
    -> const clang::Decl & {
  if (const auto *owner = described_template(declaration))
    return *owner;
  return declaration;
}

auto template_parameter_lists(const clang::NamedDecl &declaration)
    -> llvm::SmallVector<const clang::TemplateParameterList *, 4> {
  auto result = llvm::SmallVector<const clang::TemplateParameterList *, 4>{};
  if (const auto *partial =
          llvm::dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(
              &declaration)) {
    result.push_back(partial->getTemplateParameters());
  } else if (const auto *partial =
                 llvm::dyn_cast<clang::VarTemplatePartialSpecializationDecl>(
                     &declaration)) {
    result.push_back(partial->getTemplateParameters());
  }
  if (const auto *function =
          llvm::dyn_cast<clang::FunctionDecl>(&declaration)) {
    for (unsigned index = 0; index < function->getNumTemplateParameterLists();
         ++index)
      result.push_back(function->getTemplateParameterList(index));
  }
  if (const auto *owner = described_template(declaration))
    result.push_back(owner->getTemplateParameters());
  return result;
}

auto split_template_declaration(
    llvm::StringRef declaration,
    llvm::ArrayRef<const clang::TemplateParameterList *> parameter_lists,
    const clang::ASTContext &context, const clang::PrintingPolicy &policy)
    -> String {
  auto formatted = llvm::SmallString<256>{};
  auto remaining = declaration;
  for (const auto *parameters : parameter_lists) {
    auto storage = llvm::SmallString<128>{};
    auto stream = llvm::raw_svector_ostream(storage);
    parameters->print(stream, context, policy);
    auto prefix = storage.str();
    if (!prefix.ends_with(" ") || !remaining.starts_with(prefix))
      return as_rstd(declaration);
    auto template_prefix = prefix.drop_back();
    auto requires_clause = llvm::SmallString<128>{};
    if (const auto *clause = parameters->getRequiresClause()) {
      requires_clause.append(" requires ");
      auto requires_stream = llvm::raw_svector_ostream(requires_clause);
      clause->printPretty(requires_stream, nullptr, policy, 0, "\n", &context);
      if (!template_prefix.ends_with(requires_clause.str()))
        return as_rstd(declaration);
      template_prefix = template_prefix.drop_back(requires_clause.size());
    }
    formatted.append(template_prefix);
    formatted.push_back('\n');
    if (!requires_clause.empty()) {
      formatted.append("    ");
      formatted.append(requires_clause.str().drop_front());
      formatted.push_back('\n');
    }
    remaining = remaining.drop_front(prefix.size());
  }
  formatted.append(remaining);
  return as_rstd(formatted.str());
}

auto declaration_signature(const clang::NamedDecl &declaration,
                           const clang::ASTContext &context,
                           bool suppress_scope) -> String {
  auto policy = context.getPrintingPolicy();
  policy.SuppressScope = suppress_scope;
  policy.TerseOutput = true;
  policy.PolishForDeclaration = false;
  policy.Indentation = 0;
  llvm::SmallString<256> printed;
  llvm::raw_svector_ostream stream(printed);
  printable_declaration(declaration)
      .print(stream, policy, /*Indentation=*/0, /*PrintInstantiation=*/false);
  auto result = split_template_declaration(
      printed.str(), template_parameter_lists(declaration), context, policy);
  if (!llvm::isa<clang::NamespaceDecl>(declaration) &&
      !result.as_str().trim_ascii().ends_with(";"_str)) {
    result.push_ascii(';');
  }
  return result;
}

auto record_keyword(const clang::NamedDecl &declaration) -> Option<String> {
  const auto *record = llvm::dyn_cast<clang::RecordDecl>(&declaration);
  return record == nullptr ? Option<String>{}
                           : Some(as_rstd(record->getKindName()));
}

auto record_header(const clang::NamedDecl &declaration,
                   const clang::ASTContext &context) -> Option<String> {
  if (!llvm::isa<clang::RecordDecl>(declaration))
    return Option<String>{};
  auto signature = declaration_signature(declaration, context, true);
  auto text = signature.as_str().trim_ascii();
  if (text.ends_with("{};"_str))
    text = text.get(usize{}, text.len() - usize(3)).unwrap().trim_ascii();
  else if (text.ends_with(";"_str))
    text = text.get(usize{}, text.len() - usize(1)).unwrap().trim_ascii();
  return Some(String::make(text));
}

auto is_scope_declaration(const clang::NamedDecl &declaration) -> bool {
  const auto *semantic =
      llvm::dyn_cast<clang::RecordDecl>(declaration.getDeclContext());
  return semantic != nullptr && declaration.getLexicalDeclContext() == semantic;
}

struct PendingDeclarationReference {
  size_t begin{};
  size_t end{};
  std::string semantic_identity;
};

class DeclarationReferenceWalker {
public:
  DeclarationReferenceWalker(const clang::SourceManager &manager,
                             const clang::LangOptions &language,
                             clang::SourceLocation declaration_begin,
                             size_t declaration_size)
      : manager_(&manager), language_(&language),
        begin_(manager.getDecomposedSpellingLoc(declaration_begin)),
        declaration_size_(declaration_size) {}

  auto traverse(const clang::FunctionDecl &function) -> void {
    if (const auto *information = function.getTypeSourceInfo())
      traverse(information->getTypeLoc());
    if (const auto *information = function.getNameInfo().getNamedTypeInfo())
      traverse(information->getTypeLoc());
  }

  auto finish() -> Vec<DeclarationReference> {
    std::sort(references_.begin(), references_.end(),
              [](const auto &left, const auto &right) {
                if (left.begin != right.begin)
                  return left.begin < right.begin;
                if (left.end != right.end)
                  return left.end < right.end;
                return left.semantic_identity < right.semantic_identity;
              });
    auto result =
        Vec<DeclarationReference>::with_capacity(usize(references_.size()));
    const PendingDeclarationReference *previous = nullptr;
    for (const auto &reference : references_) {
      if (previous != nullptr && previous->begin == reference.begin &&
          previous->end == reference.end &&
          previous->semantic_identity == reference.semantic_identity) {
        continue;
      }
      if (previous != nullptr && reference.begin < previous->end)
        continue;
      result.push(DeclarationReference{
          .begin = usize(reference.begin),
          .end = usize(reference.end),
          .semantic_identity = as_rstd(reference.semantic_identity),
      });
      previous = &reference;
    }
    return result;
  }

private:
  auto traverse(clang::TypeLoc location) -> void {
    if (location.isNull())
      return;
    if (auto value = location.getAs<clang::TagTypeLoc>())
      record(*value.getDecl(), value.getNameLoc());
    if (auto value = location.getAs<clang::TypedefTypeLoc>())
      record(*value.getDecl(), value.getNameLoc());
    if (auto value = location.getAs<clang::UsingTypeLoc>())
      record(*value.getDecl(), value.getNameLoc());
    if (auto value = location.getAs<clang::TemplateSpecializationTypeLoc>()) {
      const auto *declaration =
          value.getTypePtr()->getTemplateName().getAsTemplateDecl(
              /*IgnoreDeduced=*/true);
      if (declaration != nullptr &&
          !llvm::isa<clang::TemplateTemplateParmDecl>(declaration)) {
        record(*declaration, value.getTemplateNameLoc());
      }
      for (unsigned index = 0; index < value.getNumArgs(); ++index) {
        auto argument = value.getArgLoc(index);
        if (const auto *information = argument.getTypeSourceInfo())
          traverse(information->getTypeLoc());
      }
    }
    if (auto value =
            location.getAs<clang::DeducedTemplateSpecializationTypeLoc>()) {
      const auto *declaration =
          value.getTypePtr()->getTemplateName().getAsTemplateDecl(
              /*IgnoreDeduced=*/true);
      if (declaration != nullptr &&
          !llvm::isa<clang::TemplateTemplateParmDecl>(declaration)) {
        record(*declaration, value.getTemplateNameLoc());
      }
    }
    if (auto function = location.getAs<clang::FunctionTypeLoc>()) {
      traverse(function.getReturnLoc());
      for (auto *parameter : function.getParams()) {
        if (parameter != nullptr && parameter->getTypeSourceInfo() != nullptr)
          traverse(parameter->getTypeSourceInfo()->getTypeLoc());
      }
      return;
    }
    auto next = location.getNextTypeLoc();
    if (!next.isNull())
      traverse(next);
  }

  auto record(const clang::NamedDecl &declaration,
              clang::SourceLocation location) -> void {
    if (location.isInvalid() || location.isMacroID())
      return;
    auto spelling = manager_->getSpellingLoc(location);
    if (spelling.isInvalid())
      return;
    auto decomposed = manager_->getDecomposedSpellingLoc(spelling);
    if (decomposed.first != begin_.first || decomposed.second < begin_.second)
      return;
    clang::Token token;
    if (clang::Lexer::getRawToken(spelling, token, *manager_, *language_,
                                  /*IgnoreWhiteSpace=*/true) ||
        (!token.is(clang::tok::identifier) &&
         !token.is(clang::tok::raw_identifier))) {
      return;
    }
    auto begin = static_cast<size_t>(decomposed.second - begin_.second);
    auto end = begin + token.getLength();
    if (begin >= end || end > declaration_size_)
      return;
    llvm::SmallVector<char, 128> usr;
    if (clang::index::generateUSRForDecl(&declaration, usr))
      return;
    references_.push_back(PendingDeclarationReference{
        .begin = begin,
        .end = end,
        .semantic_identity = std::string(usr.data(), usr.size()),
    });
  }

  const clang::SourceManager *manager_{};
  const clang::LangOptions *language_{};
  std::pair<clang::FileID, unsigned> begin_;
  size_t declaration_size_{};
  std::vector<PendingDeclarationReference> references_;
};

auto scope_declaration_text(const clang::NamedDecl &declaration,
                            const clang::ASTContext &context)
    -> Option<DeclarationText> {
  const auto *function = llvm::dyn_cast<clang::FunctionDecl>(&declaration);
  if (function == nullptr || !is_scope_declaration(declaration))
    return None();
  const auto &manager = context.getSourceManager();
  auto begin = printable_declaration(declaration).getBeginLoc();
  auto end = printable_declaration(declaration).getEndLoc();
  if (begin.isInvalid() || end.isInvalid() || begin.isMacroID() ||
      end.isMacroID()) {
    return None();
  }
  auto body_begin = clang::SourceLocation{};
  if (function->doesThisDeclarationHaveABody()) {
    if (!llvm::isa<clang::CXXConstructorDecl>(function)) {
      const auto *body = function->getBody();
      if (body == nullptr || body->getBeginLoc().isInvalid() ||
          body->getBeginLoc().isMacroID()) {
        return None();
      }
      body_begin = body->getBeginLoc();
    }
    auto function_type = function->getFunctionTypeLoc();
    if (function_type.isNull() ||
        function_type.getLocalRangeEnd().isInvalid() ||
        function_type.getLocalRangeEnd().isMacroID()) {
      return None();
    }
    end = function_type.getLocalRangeEnd();
    if (const auto &constraint = function->getTrailingRequiresClause();
        constraint && constraint.ConstraintExpr != nullptr) {
      auto constraint_end = constraint.ConstraintExpr->getEndLoc();
      if (constraint_end.isValid() && !constraint_end.isMacroID() &&
          manager.isBeforeInTranslationUnit(end, constraint_end)) {
        end = constraint_end;
      }
    }
  }
  begin = manager.getSpellingLoc(begin);
  end = manager.getSpellingLoc(end);
  auto begin_location = manager.getDecomposedSpellingLoc(begin);
  auto end_location = manager.getDecomposedSpellingLoc(end);
  if (begin_location.first != end_location.first ||
      end_location.second < begin_location.second) {
    return None();
  }
  auto after_end = body_begin.isValid()
                       ? manager.getSpellingLoc(body_begin)
                       : clang::Lexer::getLocForEndOfToken(
                             end, 0, manager, context.getLangOpts());
  if (after_end.isInvalid())
    return None();
  auto source = clang::Lexer::getSourceText(
      clang::CharSourceRange::getCharRange(begin, after_end), manager,
      context.getLangOpts());
  if (source.empty())
    return None();
  while (!source.empty() && (source.back() == ' ' || source.back() == '\t' ||
                             source.back() == '\n' || source.back() == '\r')) {
    source = source.drop_back();
  }
  auto text = as_rstd(source);
  if (!text.as_str().ends_with(";"_str)) {
    text.push_ascii(';');
  }
  auto walker = DeclarationReferenceWalker(manager, context.getLangOpts(),
                                           begin, source.size());
  walker.traverse(*function);
  auto result = DeclarationText{rstd::move(text)};
  result.references = walker.finish();
  return Some(rstd::move(result));
}

auto namespace_name(const clang::NamedDecl &declaration) -> String {
  std::vector<std::string> names;
  auto context = declaration.getDeclContext();
  while (context != nullptr && !context->isTranslationUnit()) {
    if (const auto *space = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
      if (!space->getName().empty())
        names.push_back(space->getNameAsString());
    }
    context = context->getParent();
  }
  std::string result;
  for (auto item = names.rbegin(); item != names.rend(); ++item) {
    if (!result.empty())
      result.append("::");
    result.append(*item);
  }
  return as_rstd(result);
}

auto declared_in_function(const clang::NamedDecl &declaration) -> bool {
  auto context = declaration.getDeclContext();
  while (context != nullptr && !context->isTranslationUnit()) {
    if (context->isFunctionOrMethod())
      return true;
    context = context->getParent();
  }
  return false;
}

class DocumentationVisitor {
public:
  DocumentationVisitor(clang::ASTContext &context, DocumentationUnit &unit)
      : context_(&context), manager_(&context.getSourceManager()),
        unit_(&unit) {}

  auto traverse(clang::DeclContext &context) -> void {
    for (auto *declaration : context.decls())
      traverse(declaration);
  }

private:
  auto traverse(clang::Decl *declaration) -> void {
    if (declaration == nullptr)
      return;
    if (auto *imported = llvm::dyn_cast<clang::ImportDecl>(declaration))
      visit(*imported);
    if (auto *template_declaration =
            llvm::dyn_cast<clang::TemplateDecl>(declaration)) {
      traverse(template_declaration->getTemplatedDecl());
      return;
    }
    if (auto *named = llvm::dyn_cast<clang::NamedDecl>(declaration))
      visit(*named);
    if (auto *nested = llvm::dyn_cast<clang::DeclContext>(declaration))
      traverse(*nested);
  }

  auto visit(clang::NamedDecl &declaration) -> void {
    if (declaration.isImplicit() ||
        llvm::isa<clang::ParmVarDecl>(declaration) ||
        llvm::isa<clang::EnumConstantDecl>(declaration) ||
        indices_.contains(&declaration)) {
      return;
    }
    auto location = manager_->getExpansionLoc(declaration.getLocation());
    if (location.isInvalid() || !manager_->isWrittenInMainFile(location))
      return;
    auto kind = declaration_kind(declaration);
    if (kind.is_none() || declared_in_function(declaration))
      return;
    auto display_name = declaration.getNameAsString();
    if (display_name.empty())
      return;
    auto name = as_rstd(display_name);
    auto qualified_name = declaration.getQualifiedNameAsString();

    auto parent = Option<usize>{};
    const auto *parent_context = declaration.getDeclContext();
    if (parent_context != nullptr) {
      const auto *parent_decl =
          llvm::dyn_cast<clang::NamedDecl>(parent_context);
      if (parent_decl != nullptr) {
        auto existing = indices_.find(parent_decl);
        if (existing != indices_.end())
          parent = Some(usize(existing->second));
      }
    }

    auto comment = Option<DocumentationComment>{};
    const auto *raw = context_->getRawCommentForAnyRedecl(&declaration);
    if (raw != nullptr && raw->isDocumentation()) {
      auto text = raw->getFormattedText(*manager_, context_->getDiagnostics());
      comment = Some(DocumentationComment{
          .kind = raw->isTrailingComment() ? DocumentationCommentKind::Inner
                                           : DocumentationCommentKind::Outer,
          .text = as_rstd(text),
          .span = span_for(*manager_, raw->getSourceRange()),
      });
    }

    auto index = unit_->declarations.len();
    auto signature =
        DeclarationText{declaration_signature(declaration, *context_, false)};
    auto scope_signature =
        DeclarationText{declaration_signature(declaration, *context_, true)};
    auto source_scope = scope_declaration_text(declaration, *context_);
    if (source_scope.is_some())
      scope_signature = rstd::move(*source_scope);
    unit_->declarations.push(DeclarationOutline{
        .semantic_identity = semantic_identity(declaration, *manager_),
        .kind = *kind,
        .name = name.clone(),
        .qualified_name =
            qualified_name.empty() ? rstd::move(name) : as_rstd(qualified_name),
        .namespace_name = namespace_name(declaration),
        .signature = rstd::move(signature),
        .scope_signature = rstd::move(scope_signature),
        .record_keyword = record_keyword(declaration),
        .record_header = record_header(declaration, *context_),
        .is_definition = declaration_definition(declaration),
        .is_scope_declaration = is_scope_declaration(declaration),
        .exported = declaration_exported(declaration, unit_->is_interface),
        .access = declaration_access(declaration),
        .parent = rstd::move(parent),
        .comment = rstd::move(comment),
        .spelling_span = span_for(*manager_, declaration.getSourceRange()),
        .expansion_span =
            span_for(*manager_, declaration.getSourceRange(), true),
    });
    indices_[&declaration] = index.to_primitive();
  }

  auto visit(clang::ImportDecl &declaration) -> void {
    if (declaration.isImplicit() || !declaration.isInExportDeclContext())
      return;
    auto location = manager_->getExpansionLoc(declaration.getLocation());
    if (location.isInvalid() || !manager_->isWrittenInMainFile(location))
      return;
    const auto *imported = declaration.getImportedModule();
    if (imported == nullptr)
      return;
    unit_->reexports.push(DocumentationReexport{
        .logical_module = as_rstd(imported->getFullModuleName()),
        .span = span_for(*manager_, declaration.getSourceRange()),
    });
  }

  clang::ASTContext *context_{};
  clang::SourceManager *manager_{};
  DocumentationUnit *unit_{};
  llvm::DenseMap<const clang::NamedDecl *, size_t> indices_;
};

class DocumentationConsumer : public clang::ASTConsumer {
public:
  explicit DocumentationConsumer(DocumentationUnit &unit) : unit_(&unit) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    DocumentationVisitor(context, *unit_)
        .traverse(*context.getTranslationUnitDecl());
  }

private:
  DocumentationUnit *unit_{};
};

class DocumentationAction : public clang::ASTFrontendAction {
public:
  explicit DocumentationAction(DocumentationUnit &unit) : unit_(&unit) {}

  auto CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef)
      -> std::unique_ptr<clang::ASTConsumer> override {
    return std::make_unique<DocumentationConsumer>(*unit_);
  }

private:
  DocumentationUnit *unit_{};
};

class DocumentationDiagnostics : public clang::DiagnosticConsumer {
public:
  explicit DocumentationDiagnostics(DocumentationUnit &unit) : unit_(&unit) {}

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &information) override {
    clang::DiagnosticConsumer::HandleDiagnostic(level, information);
    if (level < clang::DiagnosticsEngine::Warning)
      return;
    llvm::SmallString<256> message;
    information.FormatDiagnostic(message);
    auto span = DocumentationSpan{};
    if (information.hasSourceManager() && information.getLocation().isValid()) {
      span = span_for(information.getSourceManager(),
                      clang::SourceRange(information.getLocation()));
    }
    unit_->diagnostics.push(DocumentationDiagnostic{
        .severity = level >= clang::DiagnosticsEngine::Error
                        ? DocumentationSeverity::Error
                        : DocumentationSeverity::Warning,
        .code =
            information.getDiags()
                    ->getDiagnosticIDs()
                    ->getWarningOptionForDiag(information.getID())
                    .empty()
                ? String::make("clang"_str)
                : as_rstd(information.getDiags()
                              ->getDiagnosticIDs()
                              ->getWarningOptionForDiag(information.getID())),
        .message = as_rstd(message),
        .span = rstd::move(span),
    });
  }

private:
  DocumentationUnit *unit_{};
};

auto extract(const ExtractionRequest &request)
    -> Result<DocumentationUnit, String> {
  auto source_contents = rstd::fs::read_to_string(request.source.as_path());
  if (source_contents.is_err()) {
    return failure<DocumentationUnit>(rstd::format(
        "cannot read documentation source '{}': {}", request.source.as_path(),
        rstd::move(source_contents).unwrap_err()));
  }
  auto unit = DocumentationUnit{
      .source = request.source.clone(),
      .source_contents = rstd::move(source_contents).unwrap(),
      .logical_module = request.logical_module.clone(),
      .is_interface = request.is_interface,
  };

  llvm::SmallString<256> old_working_directory;
  auto current_error = llvm::sys::fs::current_path(old_working_directory);
  if (current_error) {
    return failure<DocumentationUnit>(
        rstd::format("cannot read litodoc working directory: {}",
                     as_rstd(current_error.message())));
  }
  auto changed = llvm::sys::fs::set_current_path(
      as_std(request.working_directory.as_path().to_str().unwrap()));
  if (changed) {
    return failure<DocumentationUnit>(rstd::format(
        "cannot enter documentation working directory '{}': {}",
        request.working_directory.as_path(), as_rstd(changed.message())));
  }

  auto adjusted = clang::tooling::getClangSyntaxOnlyAdjuster()(
      request.arguments, as_std(request.source.as_path().to_str().unwrap()));
  adjusted = clang::tooling::getClangStripOutputAdjuster()(
      adjusted, as_std(request.source.as_path().to_str().unwrap()));
  clang::FileSystemOptions file_system_options;
  auto files = llvm::makeIntrusiveRefCnt<clang::FileManager>(
      file_system_options, llvm::vfs::getRealFileSystem());
  DocumentationDiagnostics diagnostics(unit);
  clang::tooling::ToolInvocation invocation(
      rstd::move(adjusted), std::make_unique<DocumentationAction>(unit),
      files.get());
  invocation.setDiagnosticConsumer(&diagnostics);
  auto succeeded = invocation.run();
  auto restored = llvm::sys::fs::set_current_path(old_working_directory);
  if (restored) {
    return failure<DocumentationUnit>(
        rstd::format("cannot restore litodoc working directory: {}",
                     as_rstd(restored.message())));
  }
  if (!succeeded) {
    return failure<DocumentationUnit>(rstd::format(
        "Clang AST extraction failed for '{}'", request.source.as_path()));
  }
  for (const auto &declaration : unit.declarations) {
    if (declaration.comment.is_some())
      ++unit.documented;
    else
      ++unit.undocumented;
  }
  return Ok(rstd::move(unit));
}

auto encode_span(const DocumentationSpan &span) -> llvm::json::Object {
  return llvm::json::Object{
      {"path", as_std(span.path.as_path().to_str().unwrap_or(""_str))},
      {"begin_line", static_cast<int64_t>(span.begin_line.to_primitive())},
      {"begin_column", static_cast<int64_t>(span.begin_column.to_primitive())},
      {"end_line", static_cast<int64_t>(span.end_line.to_primitive())},
      {"end_column", static_cast<int64_t>(span.end_column.to_primitive())},
  };
}

auto encode_declaration_references(const DeclarationText &declaration)
    -> llvm::json::Array {
  auto result = llvm::json::Array{};
  for (const auto &reference : declaration.references) {
    result.push_back(llvm::json::Object{
        {"begin", static_cast<int64_t>(reference.begin.to_primitive())},
        {"end", static_cast<int64_t>(reference.end.to_primitive())},
        {"semantic_identity", as_std(reference.semantic_identity.as_str())},
        {"kind", "type"},
    });
  }
  return result;
}

auto access_name(DeclarationAccess access) -> llvm::StringRef {
  switch (access) {
  case DeclarationAccess::Public:
    return "public";
  case DeclarationAccess::Protected:
    return "protected";
  case DeclarationAccess::Private:
    return "private";
  }
  llvm_unreachable("unknown declaration access");
}

auto encode_response(const ExtractionRequest &request,
                     const DocumentationUnit &unit) -> llvm::json::Value {
  auto declarations = llvm::json::Array{};
  for (const auto &declaration : unit.declarations) {
    auto object = llvm::json::Object{
        {"semantic_identity", as_std(declaration.semantic_identity.as_str())},
        {"kind", as_std(declaration_kind_name(declaration.kind))},
        {"name", as_std(declaration.name.as_str())},
        {"qualified_name", as_std(declaration.qualified_name.as_str())},
        {"namespace", as_std(declaration.namespace_name.as_str())},
        {"signature", as_std(declaration.signature.as_str())},
        {"scope_signature", as_std(declaration.scope_signature.as_str())},
        {"signature_references",
         encode_declaration_references(declaration.signature)},
        {"scope_signature_references",
         encode_declaration_references(declaration.scope_signature)},
        {"is_definition", declaration.is_definition},
        {"is_scope_declaration", declaration.is_scope_declaration},
        {"exported", declaration.exported},
        {"access", access_name(declaration.access)},
        {"spelling_span", encode_span(declaration.spelling_span)},
        {"expansion_span", encode_span(declaration.expansion_span)},
    };
    if (declaration.parent.is_some())
      object["parent"] =
          static_cast<int64_t>(declaration.parent->to_primitive());
    else
      object["parent"] = nullptr;
    if (declaration.record_keyword.is_some())
      object["record_keyword"] = as_std(declaration.record_keyword->as_str());
    else
      object["record_keyword"] = nullptr;
    if (declaration.record_header.is_some())
      object["record_header"] = as_std(declaration.record_header->as_str());
    else
      object["record_header"] = nullptr;
    if (declaration.comment.is_some()) {
      object["comment"] = llvm::json::Object{
          {"kind", declaration.comment->kind == DocumentationCommentKind::Inner
                       ? "inner"
                       : "outer"},
          {"text", as_std(declaration.comment->text.as_str())},
          {"span", encode_span(declaration.comment->span)},
      };
    } else {
      object["comment"] = nullptr;
    }
    declarations.push_back(rstd::move(object));
  }
  auto reexports = llvm::json::Array{};
  for (const auto &reexport : unit.reexports) {
    reexports.push_back(llvm::json::Object{
        {"module", as_std(reexport.logical_module.as_str())},
        {"span", encode_span(reexport.span)},
    });
  }
  auto diagnostics = llvm::json::Array{};
  for (const auto &diagnostic : unit.diagnostics) {
    diagnostics.push_back(llvm::json::Object{
        {"severity", diagnostic.severity == DocumentationSeverity::Error
                         ? "error"
                         : "warning"},
        {"code", as_std(diagnostic.code.as_str())},
        {"message", as_std(diagnostic.message.as_str())},
        {"span", encode_span(diagnostic.span)},
    });
  }
  return llvm::json::Object{
      {"format", "litodoc-extract-result"},
      {"version", 1},
      {"request_id", as_std(request.request_id.as_str())},
      {"producer",
       llvm::json::Object{
           {"litodoc", "0.1.0"},
           {"clang", clang::getClangFullVersion()},
       }},
      {"package", as_std(request.package_name.as_str())},
      {"package_identity", as_std(request.package_identity.as_str())},
      {"target", as_std(request.target_name.as_str())},
      {"unit_identity", as_std(request.unit_identity.as_str())},
      {"source", as_std(unit.source.as_path().to_str().unwrap())},
      {"source_contents", as_std(unit.source_contents.as_str())},
      {"logical_module", as_std(unit.logical_module.as_str())},
      {"is_interface", unit.is_interface},
      {"declarations", rstd::move(declarations)},
      {"reexports", rstd::move(reexports)},
      {"diagnostics", rstd::move(diagnostics)},
      {"documented", static_cast<int64_t>(unit.documented.to_primitive())},
      {"undocumented", static_cast<int64_t>(unit.undocumented.to_primitive())},
      {"unsupported", static_cast<int64_t>(unit.unsupported.to_primitive())},
  };
}

auto write_json(ref<rstd::path::Path> path, llvm::json::Value value,
                ref<str> context) -> Result<empty, String> {
  auto parent = path.parent();
  if (parent.is_none())
    return failure<empty>(rstd::format("{} '{}' has no parent", context, path));
  auto created = rstd::fs::create_dir_all(*parent);
  if (created.is_err()) {
    return failure<empty>(rstd::format("cannot create {} directory '{}': {}",
                                       context, *parent,
                                       rstd::move(created).unwrap_err()));
  }
  auto text = json_text(rstd::move(value));
  auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
  if (written.is_err()) {
    return failure<empty>(rstd::format("cannot write {} '{}': {}", context,
                                       path, rstd::move(written).unwrap_err()));
  }
  return Ok(empty{});
}

auto execute_extract(ref<rstd::path::Path> request_path,
                     ref<rstd::path::Path> response_path)
    -> Result<empty, String> {
  auto document = rstd_try(read_json(request_path, "extract request"_str));
  auto request = rstd_try(decode_extraction_request(document));
  auto unit = rstd_try(extract(request));
  return write_json(response_path, encode_response(request, unit),
                    "extract response"_str);
}

auto decode_span(const llvm::json::Object &object, ref<str> context)
    -> Result<DocumentationSpan, String> {
  return Ok(DocumentationSpan{
      .path = PathBuf::from(rstd_try(required_string(object, "path", context))),
      .begin_line = rstd_try(required_integer(object, "begin_line", context)),
      .begin_column =
          rstd_try(required_integer(object, "begin_column", context)),
      .end_line = rstd_try(required_integer(object, "end_line", context)),
      .end_column = rstd_try(required_integer(object, "end_column", context)),
  });
}

auto decode_kind(llvm::StringRef value) -> Option<DeclarationKind> {
  if (value == "module")
    return Some(DeclarationKind::Module);
  if (value == "namespace")
    return Some(DeclarationKind::Namespace);
  if (value == "record")
    return Some(DeclarationKind::Record);
  if (value == "enum")
    return Some(DeclarationKind::Enum);
  if (value == "concept")
    return Some(DeclarationKind::Concept);
  if (value == "alias")
    return Some(DeclarationKind::Alias);
  if (value == "function")
    return Some(DeclarationKind::Function);
  if (value == "variable")
    return Some(DeclarationKind::Variable);
  if (value == "field")
    return Some(DeclarationKind::Field);
  return None();
}

auto decode_access(llvm::StringRef value) -> Option<DeclarationAccess> {
  if (value == "public")
    return Some(DeclarationAccess::Public);
  if (value == "protected")
    return Some(DeclarationAccess::Protected);
  if (value == "private")
    return Some(DeclarationAccess::Private);
  return None();
}

auto decode_declaration_text(const llvm::json::Object &object,
                             llvm::StringRef text_name,
                             llvm::StringRef references_name, ref<str> context)
    -> Result<DeclarationText, String> {
  auto result =
      DeclarationText{rstd_try(required_string(object, text_name, context))};
  const auto *references = object.getArray(references_name);
  if (references == nullptr)
    return Ok(rstd::move(result));
  for (const auto &item : *references) {
    auto reference = rstd_try(json_object(item, context));
    auto begin = rstd_try(required_integer(*reference, "begin", context));
    auto end = rstd_try(required_integer(*reference, "end", context));
    auto identity =
        rstd_try(required_string(*reference, "semantic_identity", context));
    auto kind = rstd_try(required_string(*reference, "kind", context));
    if (kind.as_str() != "type"_str)
      return failure<DeclarationText>(rstd::format(
          "{} has unsupported reference kind '{}'", context, kind.as_str()));
    result.references.push(DeclarationReference{
        .begin = begin,
        .end = end,
        .semantic_identity = rstd::move(identity),
    });
  }
  auto valid = result.validate(context);
  if (valid.is_err())
    return failure<DeclarationText>(rstd::move(valid).unwrap_err());
  return Ok(rstd::move(result));
}

auto decode_unit(ref<rstd::path::Path> response_path, ref<str> expected_digest)
    -> Result<DocumentationUnit, String> {
  auto value = rstd_try(
      read_json(response_path, expected_digest, "extract response"_str));
  auto root = rstd_try(json_object(value, "extract response"_str));
  rstd_try(validate_header(*root, "litodoc-extract-result", 1,
                           "extract response"_str));
  auto declarations =
      rstd_try(required_array(*root, "declarations", "extract response"_str));
  auto reexports =
      rstd_try(required_array(*root, "reexports", "extract response"_str));
  auto diagnostics =
      rstd_try(required_array(*root, "diagnostics", "extract response"_str));
  auto unit = DocumentationUnit{
      .source = PathBuf::from(
          rstd_try(required_string(*root, "source", "extract response"_str))),
      .source_contents = rstd_try(
          required_string(*root, "source_contents", "extract response"_str)),
      .logical_module = rstd_try(optional_string(*root, "logical_module",
                                                 "extract response"_str))
                            .unwrap_or(String::make()),
      .is_interface = rstd_try(
          required_bool(*root, "is_interface", "extract response"_str)),
      .documented = rstd_try(
          required_integer(*root, "documented", "extract response"_str)),
      .undocumented = rstd_try(
          required_integer(*root, "undocumented", "extract response"_str)),
      .unsupported = rstd_try(
          required_integer(*root, "unsupported", "extract response"_str)),
  };
  for (const auto &item : *declarations) {
    auto object =
        rstd_try(json_object(item, "extract response declaration"_str));
    auto kind_text = rstd_try(
        required_string(*object, "kind", "extract response declaration"_str));
    auto access_text = rstd_try(
        required_string(*object, "access", "extract response declaration"_str));
    auto kind = decode_kind(as_std(kind_text.as_str()));
    auto access = decode_access(as_std(access_text.as_str()));
    if (kind.is_none() || access.is_none())
      return failure<DocumentationUnit>(
          "extract response declaration has unsupported kind or access"_str);
    auto spelling_span = rstd_try(required_object(
        *object, "spelling_span", "extract response declaration"_str));
    auto expansion_span = rstd_try(required_object(
        *object, "expansion_span", "extract response declaration"_str));
    auto parent = Option<usize>{};
    if (auto value = object->getInteger("parent")) {
      if (*value < 0)
        return failure<DocumentationUnit>(
            "extract response declaration parent is invalid"_str);
      parent = Some(usize(static_cast<size_t>(*value)));
    }
    auto comment = Option<DocumentationComment>{};
    if (auto comment_object = object->getObject("comment")) {
      auto comment_span = rstd_try(required_object(
          *comment_object, "span", "extract response comment"_str));
      auto comment_kind = rstd_try(required_string(
          *comment_object, "kind", "extract response comment"_str));
      comment = Some(DocumentationComment{
          .kind = comment_kind.as_str() == "inner"_str
                      ? DocumentationCommentKind::Inner
                      : DocumentationCommentKind::Outer,
          .text = rstd_try(required_string(*comment_object, "text",
                                           "extract response comment"_str)),
          .span = rstd_try(
              decode_span(*comment_span, "extract response comment span"_str)),
      });
    }
    unit.declarations.push(DeclarationOutline{
        .semantic_identity = rstd_try(required_string(
            *object, "semantic_identity", "extract response declaration"_str)),
        .kind = *kind,
        .name = rstd_try(required_string(*object, "name",
                                         "extract response declaration"_str)),
        .qualified_name = rstd_try(required_string(
            *object, "qualified_name", "extract response declaration"_str)),
        .namespace_name =
            rstd_try(optional_string(*object, "namespace",
                                     "extract response declaration"_str))
                .unwrap_or(String::make()),
        .signature = rstd_try(decode_declaration_text(
            *object, "signature", "signature_references",
            "extract response declaration signature"_str)),
        .scope_signature = rstd_try(decode_declaration_text(
            *object, "scope_signature", "scope_signature_references",
            "extract response declaration scope signature"_str)),
        .record_keyword = rstd_try(optional_string(
            *object, "record_keyword", "extract response declaration"_str)),
        .record_header = rstd_try(optional_string(
            *object, "record_header", "extract response declaration"_str)),
        .is_definition = rstd_try(required_bool(
            *object, "is_definition", "extract response declaration"_str)),
        .is_scope_declaration =
            rstd_try(required_bool(*object, "is_scope_declaration",
                                   "extract response declaration"_str)),
        .exported = rstd_try(required_bool(*object, "exported",
                                           "extract response declaration"_str)),
        .access = *access,
        .parent = rstd::move(parent),
        .comment = rstd::move(comment),
        .spelling_span = rstd_try(decode_span(
            *spelling_span, "extract response declaration spelling span"_str)),
        .expansion_span = rstd_try(
            decode_span(*expansion_span,
                        "extract response declaration expansion span"_str)),
    });
  }
  for (const auto &item : *reexports) {
    auto object = rstd_try(json_object(item, "extract response reexport"_str));
    auto span = rstd_try(
        required_object(*object, "span", "extract response reexport"_str));
    unit.reexports.push(DocumentationReexport{
        .logical_module = rstd_try(required_string(
            *object, "module", "extract response reexport"_str)),
        .span =
            rstd_try(decode_span(*span, "extract response reexport span"_str)),
    });
  }
  for (const auto &item : *diagnostics) {
    auto object =
        rstd_try(json_object(item, "extract response diagnostic"_str));
    auto span = rstd_try(
        required_object(*object, "span", "extract response diagnostic"_str));
    auto severity = rstd_try(required_string(
        *object, "severity", "extract response diagnostic"_str));
    unit.diagnostics.push(DocumentationDiagnostic{
        .severity = severity.as_str() == "error"_str
                        ? DocumentationSeverity::Error
                        : DocumentationSeverity::Warning,
        .code = rstd_try(required_string(*object, "code",
                                         "extract response diagnostic"_str)),
        .message = rstd_try(required_string(*object, "message",
                                            "extract response diagnostic"_str)),
        .span = rstd_try(
            decode_span(*span, "extract response diagnostic span"_str)),
    });
  }
  return Ok(rstd::move(unit));
}

auto execute_generate(ref<rstd::path::Path> manifest_path)
    -> Result<Summary, String> {
  auto value = rstd_try(read_json(manifest_path, "site manifest"_str));
  auto root = rstd_try(json_object(value, "site manifest"_str));
  rstd_try(validate_header(*root, "litodoc-site", 1, "site manifest"_str));
  if (rstd_try(required_integer(*root, "data_api", "site manifest"_str)) !=
      usize(4))
    return failure<Summary>("site manifest requires unsupported data API"_str);
  if (rstd_try(required_integer(*root, "template_api", "site manifest"_str)) !=
      usize(1))
    return failure<Summary>(
        "site manifest requires unsupported template API"_str);
  auto packages =
      rstd_try(required_array(*root, "packages", "site manifest"_str));
  auto frontend =
      rstd_try(optional_string(*root, "frontend", "site manifest"_str))
          .map([](String value) { return PathBuf::from(rstd::move(value)); });
  auto data_only =
      rstd_try(required_bool(*root, "data_only", "site manifest"_str));
  auto publication_name =
      rstd_try(optional_string(*root, "publication", "site manifest"_str))
          .unwrap_or(String::make("site"_str));
  auto publication = PublicationKind::Site;
  if (publication_name.as_str() == "package-set"_str) {
    publication = PublicationKind::PackageSet;
  } else if (publication_name.as_str() != "site"_str) {
    return failure<Summary>(rstd::format(
        "site manifest publication '{}' is unsupported", publication_name));
  }
  auto default_frontend = Option<lito::site::FrontendBundle>{};
  if (frontend.is_none() && !data_only) {
    default_frontend = Some(rstd_try(lito::doc::web::load_default_frontend()));
  }
  auto input = SiteInput{
      .title = rstd_try(required_string(*root, "title", "site manifest"_str)),
      .output = PathBuf::from(
          rstd_try(required_string(*root, "output", "site manifest"_str))),
      .data_output = PathBuf::from(
          rstd_try(required_string(*root, "data_output", "site manifest"_str))),
      .frontend = rstd::move(frontend),
      .data_only = data_only,
      .publication = publication,
  };
  for (const auto &item : *packages) {
    auto package = rstd_try(json_object(item, "site manifest package"_str));
    auto responses = rstd_try(
        required_array(*package, "responses", "site manifest package"_str));
    auto package_input = PackageInput{
        .name = rstd_try(
            required_string(*package, "name", "site manifest package"_str)),
        .version = rstd_try(optional_string(*package, "version",
                                            "site manifest package"_str))
                       .unwrap_or(String::make()),
        .source_identity =
            rstd_try(optional_string(*package, "source_identity",
                                     "site manifest package"_str))
                .unwrap_or(String::make()),
        .root_module = rstd_try(optional_string(*package, "root_module",
                                                "site manifest package"_str))
                           .unwrap_or(String::make()),
        .profile = rstd_try(
            required_string(*package, "profile", "site manifest package"_str)),
        .root = PathBuf::from(rstd_try(
            required_string(*package, "root", "site manifest package"_str))),
        .toolchain_version = rstd_try(required_string(
            *package, "toolchain_version", "site manifest package"_str)),
        .toolchain_target = rstd_try(required_string(
            *package, "toolchain_target", "site manifest package"_str)),
        .language_standard = rstd_try(required_string(
            *package, "language_standard", "site manifest package"_str)),
    };
    for (const auto &response : *responses) {
      auto artifact =
          rstd_try(json_object(response, "site manifest response"_str));
      auto path = PathBuf::from(rstd_try(
          required_string(*artifact, "path", "site manifest response"_str)));
      auto digest = rstd_try(
          required_string(*artifact, "digest", "site manifest response"_str));
      package_input.units.push(
          rstd_try(decode_unit(path.as_path(), digest.as_str())));
    }
    input.packages.push(rstd::move(package_input));
  }
  return generate(rstd::move(input), rstd::move(default_frontend));
}

auto capabilities() -> String {
  auto protocols = llvm::json::Array{};
  protocols.push_back(1);
  auto site_versions = llvm::json::Array{};
  site_versions.push_back(1);
  auto data_versions = llvm::json::Array{};
  data_versions.push_back(4);
  auto features = llvm::json::Array{};
  features.push_back("embedded-default-frontend");
  features.push_back("package-publications-v1");
  return json_text(llvm::json::Object{
      {"format", "litodoc-capabilities"},
      {"version", 1},
      {"extract_protocols", rstd::move(protocols)},
      {"site_manifest_versions", rstd::move(site_versions)},
      {"data_api_versions", rstd::move(data_versions)},
      {"features", rstd::move(features)},
      {"litodoc_build", "0.1.0"},
      {"clang_version", CLANG_VERSION_STRING},
      {"clang_build", clang::getClangFullVersion()},
  });
}

auto argument_value(const Vec<String> &arguments, ref<str> name)
    -> Option<PathBuf> {
  for (usize index{}; index + usize(1) < arguments.len(); ++index) {
    if (arguments[index].as_str() == name)
      return Some(PathBuf::from(arguments[index + usize(1)].as_str()));
  }
  return None();
}

} // namespace lito::doc::tool

namespace lito::doc::tool {

auto run() -> int {
  auto arguments = rstd::env::args().collect<Vec<String>>();
  if (arguments.len() < usize(2)) {
    rstd::io::eprintln("litodoc: expected capabilities, extract, or generate");
    return 2;
  }
  auto command = arguments[usize(1)].as_str();
  if (command == "capabilities"_str) {
    if (arguments.len() != usize(3) ||
        arguments[usize(2)].as_str() != "--json"_str) {
      rstd::io::eprintln("litodoc: usage: litodoc capabilities --json");
      return 2;
    }
    rstd::io::print("{}", capabilities().as_str());
    return 0;
  }

  if (command == "extract"_str) {
    auto request = argument_value(arguments, "--request"_str);
    auto response = argument_value(arguments, "--response"_str);
    if (request.is_none() || response.is_none()) {
      rstd::io::eprintln(
          "litodoc: usage: litodoc extract --request <path> --response <path>");
      return 2;
    }
    auto result = execute_extract(request->as_path(), response->as_path());
    if (result.is_err()) {
      rstd::io::eprintln("litodoc: {}", rstd::move(result).unwrap_err());
      return 1;
    }
    return 0;
  }

  if (command == "generate"_str) {
    auto manifest = argument_value(arguments, "--manifest"_str);
    if (manifest.is_none()) {
      rstd::io::eprintln("litodoc: usage: litodoc generate --manifest <path>");
      return 2;
    }
    auto result = execute_generate(manifest->as_path());
    if (result.is_err()) {
      rstd::io::eprintln("litodoc: {}", rstd::move(result).unwrap_err());
      return 1;
    }
    if (result->publication_set.is_some()) {
      rstd::io::println("generated {}",
                        result->publication_set->manifest.as_path());
    } else {
      rstd::io::println("generated {}", result->output.as_path());
    }
    return 0;
  }

  rstd::io::eprintln("litodoc: unsupported command '{}'", command);
  return 2;
}

} // namespace lito::doc::tool
