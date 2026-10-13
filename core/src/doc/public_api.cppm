export module lito.doc:public_api;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::doc {

enum class PublicApiReason {
  Public,
  Supplement,
  Duplicate,
  Namespace,
  InternalModule,
  NotExported,
  NonPublicAccess,
  MissingOwner,
  Fragment,
  ImplementationOnly,
};

struct DeclarationPosition {
  usize unit{};
  usize declaration{};
};

struct PublicApiEntity {
  DeclarationPosition interface;
  Option<String> parent;
  bool resolving{false};
  bool resolved{false};
  bool published{false};
};

struct PublicApiDecision {
  String identity;
  PublicApiReason reason{PublicApiReason::NotExported};
};

struct PublicApiIndex {
  rstd::collections::BTreeMap<String, PublicApiEntity> entities;
  rstd::collections::BTreeMap<String, empty> modules;
  Vec<Vec<PublicApiDecision>> decisions;
  Vec<usize> unit_order;
};

auto public_api_reason_name(PublicApiReason reason) -> ref<str> {
  switch (reason) {
  case PublicApiReason::Public:
    return "public"_str;
  case PublicApiReason::Supplement:
    return "supplement"_str;
  case PublicApiReason::Duplicate:
    return "duplicate"_str;
  case PublicApiReason::Namespace:
    return "namespace"_str;
  case PublicApiReason::InternalModule:
    return "internal-module"_str;
  case PublicApiReason::NotExported:
    return "not-exported"_str;
  case PublicApiReason::NonPublicAccess:
    return "non-public-access"_str;
  case PublicApiReason::MissingOwner:
    return "missing-owner"_str;
  case PublicApiReason::Fragment:
    return "fragment"_str;
  case PublicApiReason::ImplementationOnly:
    return "implementation-only"_str;
  }
  __builtin_unreachable();
}

auto declaration_entity_identity(ref<str> module, ref<str> identity) -> String {
  return rstd::format("{}|{}", module, identity);
}

auto declaration_entity_identity(const DocumentationUnit &unit,
                                 const DeclarationOutline &declaration)
    -> String {
  return declaration_entity_identity(declaration.named_module.is_empty()
                                         ? unit.named_module.as_str()
                                         : declaration.named_module.as_str(),
                                     declaration.semantic_identity.as_str());
}

} // namespace lito::doc

namespace lito::doc {

auto interface_unit(DocumentationUnitKind kind) -> bool {
  return kind == DocumentationUnitKind::PrimaryInterface ||
         kind == DocumentationUnitKind::InterfacePartition;
}

auto declaration_owner_identity(const DocumentationUnit &unit,
                                const DeclarationOutline &declaration)
    -> Option<String> {
  if (declaration.owner.is_some()) {
    if (declaration.owner->kind != DeclarationKind::Record)
      return None();
    if (declaration.owner->anonymous_record) {
      for (const auto &ancestor : declaration.record_ancestors) {
        if (!ancestor.anonymous_record)
          return Some(
              declaration_entity_identity(ancestor.named_module.as_str(),
                                          ancestor.semantic_identity.as_str()));
      }
      return None();
    }
    return Some(declaration_entity_identity(
        declaration.owner->named_module.as_str(),
        declaration.owner->semantic_identity.as_str()));
  }
  if (declaration.parent.is_some()) {
    const auto &parent = unit.declarations[*declaration.parent];
    if (parent.kind == DeclarationKind::Record)
      return Some(declaration_entity_identity(unit, parent));
  }
  return None();
}

auto resolve_public_entity(PublicApiIndex &index,
                           const Vec<DocumentationUnit> &units,
                           ref<str> identity) -> Result<bool, String> {
  auto entry = index.entities.get_mut(identity);
  if (entry.is_none())
    return Ok(false);
  auto &entity = **entry;
  if (entity.resolved)
    return Ok(entity.published);
  if (entity.resolving)
    return Err(rstd::format("cyclic declaration owner '{}'", identity));
  entity.resolving = true;
  const auto &unit = units[entity.interface.unit];
  const auto &declaration = unit.declarations[entity.interface.declaration];
  auto reason = PublicApiReason::Public;
  auto published = true;
  if (declaration.access != DeclarationAccess::Public) {
    reason = PublicApiReason::NonPublicAccess;
    published = false;
  }
  for (const auto &ancestor : declaration.record_ancestors) {
    if (ancestor.access != DeclarationAccess::Public) {
      reason = PublicApiReason::NonPublicAccess;
      published = false;
    }
  }
  if (entity.parent.is_some()) {
    auto parent = resolve_public_entity(index, units, entity.parent->as_str());
    if (parent.is_err())
      return Err(rstd::move(parent).unwrap_err());
    if (!parent.unwrap()) {
      if (published) {
        auto owner = index.entities.get(entity.parent->as_str());
        reason = owner.is_some()
                     ? index
                           .decisions[(**owner).interface.unit]
                                     [(**owner).interface.declaration]
                           .reason
                     : PublicApiReason::MissingOwner;
      }
      published = false;
    }
  } else if (declaration.kind == DeclarationKind::Field ||
             declaration.is_scope_declaration) {
    reason = PublicApiReason::MissingOwner;
    published = false;
  } else if (declaration.origin != DeclarationOrigin::Ordinary &&
             !declaration.lexical_export &&
             !declaration.exported_redeclaration) {
    reason = PublicApiReason::NotExported;
    published = false;
  }
  entity.resolving = false;
  entity.resolved = true;
  entity.published = published;
  index.decisions[entity.interface.unit][entity.interface.declaration].reason =
      reason;
  return Ok(published);
}

} // namespace lito::doc

export namespace lito::doc {

auto build_public_api_index(const Vec<DocumentationUnit> &units,
                            ref<str> root_module)
    -> Result<PublicApiIndex, String> {
  auto index = PublicApiIndex{
      .entities = rstd::collections::BTreeMap<String, PublicApiEntity>::make(),
      .modules = rstd::collections::BTreeMap<String, empty>::make(),
  };
  auto interfaces = rstd::collections::BTreeMap<String, usize>::make();
  auto ordered_units = rstd::collections::BTreeMap<String, usize>::make();
  auto modular = false;
  for (auto u = usize{}; u < units.len(); ++u) {
    const auto &unit = units[u];
    if (unit.kind != DocumentationUnitKind::Ordinary &&
        unit.named_module.is_empty())
      return Err(rstd::format("module unit '{}' has no named module identity",
                              unit.logical_module));
    if (unit.kind != DocumentationUnitKind::Ordinary)
      modular = true;
    if (interface_unit(unit.kind)) {
      if (interfaces.contains_key(unit.logical_module.as_str()))
        return Err(
            rstd::format("duplicate interface unit '{}'", unit.logical_module));
      interfaces.insert(unit.logical_module.clone(), u);
    }
    auto path = unit.source.as_path().to_str();
    if (path.is_none())
      return Err(String::make("documentation source path is not UTF-8"_str));
    ordered_units.insert(
        rstd::format("{}|{}|{}", *path, unit.logical_module, u), u);
  }
  if (modular && !interfaces.contains_key(root_module))
    return Err(rstd::format("public root module '{}' has no interface unit",
                            root_module));
  auto pending = Vec<String>::make();
  if (modular && units[**interfaces.get(root_module)].kind !=
                     DocumentationUnitKind::PrimaryInterface)
    return Err(rstd::format(
        "public root module '{}' is not a primary interface", root_module));
  if (modular)
    pending.push(String::make(root_module));
  while (!pending.is_empty()) {
    auto module = pending.pop().unwrap();
    if (index.modules.contains_key(module.as_str()))
      continue;
    auto position = interfaces.get(module.as_str());
    if (position.is_none()) {
      auto local = false;
      for (const auto &unit : units)
        if (unit.logical_module.as_str() == module.as_str())
          local = true;
      if (local)
        return Err(
            rstd::format("exported module '{}' has no interface unit", module));
      // Other packages' modules remain external graph edges.
      continue;
    }
    index.modules.insert(module.clone(), empty{});
    for (const auto &edge : units[**position].reexports) {
      if (edge.is_partition &&
          edge.named_module.as_str() ==
              units[**interfaces.get(root_module)].named_module.as_str() &&
          !interfaces.contains_key(edge.logical_module.as_str()))
        return Err(rstd::format(
            "exported partition '{}' is missing from extraction input",
            edge.logical_module));
      pending.push(edge.logical_module.clone());
    }
  }
  auto values = ordered_units.values();
  for (auto value = values.next(); value.is_some(); value = values.next())
    index.unit_order.push(usize((**value).to_primitive()));
  for (auto u = usize{}; u < units.len(); ++u)
    index.decisions.push(Vec<PublicApiDecision>::make());
  for (auto u : index.unit_order) {
    const auto &unit = units[u];
    auto decisions = Vec<PublicApiDecision>::make();
    if (unit.kind == DocumentationUnitKind::Ordinary)
      index.modules.insert(String::make(unit.logical_module.is_empty()
                                            ? root_module
                                            : unit.logical_module.as_str()),
                           empty{});
    for (auto d = usize{}; d < unit.declarations.len(); ++d) {
      const auto &declaration = unit.declarations[d];
      if ((unit.kind == DocumentationUnitKind::Ordinary) !=
              (declaration.origin == DeclarationOrigin::Ordinary) ||
          (declaration.origin == DeclarationOrigin::Interface &&
           !interface_unit(unit.kind)) ||
          (declaration.origin == DeclarationOrigin::Implementation &&
           interface_unit(unit.kind)))
        return Err(
            rstd::format("declaration '{}' has inconsistent module origin",
                         declaration.qualified_name));
      if (declaration.parent.is_some() &&
          *declaration.parent >= unit.declarations.len())
        return Err(rstd::format("declaration '{}' has invalid parent",
                                declaration.qualified_name));
      auto parent = declaration.parent;
      auto ancestry = usize{};
      while (parent.is_some()) {
        if (*parent >= unit.declarations.len())
          return Err(String::make("invalid declaration owner index"_str));
        if (++ancestry > unit.declarations.len())
          return Err(String::make("cyclic declaration owner index"_str));
        parent = unit.declarations[*parent].parent;
      }
      if (declaration.parent.is_some() && declaration.owner.is_some()) {
        const auto &local = unit.declarations[*declaration.parent];
        if (declaration.owner->semantic_identity.as_str() !=
                local.semantic_identity.as_str() ||
            declaration.owner->kind != local.kind ||
            declaration.owner->access != local.access)
          return Err(
              rstd::format("declaration '{}' has conflicting owner facts",
                           declaration.qualified_name));
      }
      auto valid =
          declaration.signature.validate(declaration.qualified_name.as_str());
      if (valid.is_err())
        return Err(rstd::move(valid).unwrap_err());
      valid = declaration.scope_signature.validate(
          declaration.qualified_name.as_str());
      if (valid.is_err())
        return Err(rstd::move(valid).unwrap_err());
      if (declaration.semantic_identity.is_empty())
        return Err(rstd::format("declaration '{}' has no semantic identity",
                                declaration.qualified_name));
      auto identity = declaration_entity_identity(unit, declaration);
      auto reason = PublicApiReason::Public;
      if (!is_published_symbol_kind(declaration.kind))
        reason = PublicApiReason::Namespace;
      else if (declaration.origin == DeclarationOrigin::GlobalFragment ||
               declaration.origin == DeclarationOrigin::PrivateFragment)
        reason = PublicApiReason::Fragment;
      else if (declaration.origin == DeclarationOrigin::Implementation)
        reason = PublicApiReason::ImplementationOnly;
      else if (unit.kind != DocumentationUnitKind::Ordinary &&
               !index.modules.contains_key(unit.logical_module.as_str()))
        reason = PublicApiReason::InternalModule;
      if (reason == PublicApiReason::Public) {
        auto existing = index.entities.get_mut(identity.as_str());
        if (existing.is_some()) {
          const auto &previous =
              units[(**existing).interface.unit]
                  .declarations[(**existing).interface.declaration];
          if (previous.kind != declaration.kind)
            return Err(rstd::format("conflicting declaration kinds for '{}'",
                                    identity));
          auto prefer = declaration.is_scope_declaration &&
                        !previous.is_scope_declaration;
          if (declaration.is_scope_declaration == previous.is_scope_declaration)
            prefer = declaration.is_definition && !previous.is_definition;
          if (prefer) {
            auto old = (**existing).interface;
            if (old.unit == u)
              decisions[old.declaration].reason = PublicApiReason::Duplicate;
            else
              index.decisions[old.unit][old.declaration].reason =
                  PublicApiReason::Duplicate;
            (**existing).interface = {u, d};
            (**existing).parent = declaration_owner_identity(unit, declaration);
          } else
            reason = PublicApiReason::Duplicate;
        } else {
          index.entities.insert(
              identity.clone(),
              PublicApiEntity{
                  .interface = {u, d},
                  .parent = declaration_owner_identity(unit, declaration),
              });
        }
      }
      decisions.push({rstd::move(identity), reason});
    }
    index.decisions[u] = rstd::move(decisions);
  }
  auto identities = Vec<String>::make();
  auto keys = index.entities.keys();
  for (auto key = keys.next(); key.is_some(); key = keys.next())
    identities.push((**key).clone());
  for (const auto &identity : identities) {
    auto resolved = resolve_public_entity(index, units, identity.as_str());
    if (resolved.is_err())
      return Err(rstd::move(resolved).unwrap_err());
  }
  for (auto u = usize{}; u < units.len(); ++u) {
    for (auto d = usize{}; d < units[u].declarations.len(); ++d) {
      auto &decision = index.decisions[u][d];
      if (decision.reason != PublicApiReason::Duplicate &&
          decision.reason != PublicApiReason::ImplementationOnly)
        continue;
      auto entity = index.entities.get(decision.identity.as_str());
      if (entity.is_some() && (**entity).published) {
        const auto &interface =
            units[(**entity).interface.unit]
                .declarations[(**entity).interface.declaration];
        if (units[u].declarations[d].kind != interface.kind)
          return Err(rstd::format("conflicting declaration kinds for '{}'",
                                  decision.identity));
        decision.reason = PublicApiReason::Supplement;
      }
    }
  }
  return Ok(rstd::move(index));
}

} // namespace lito::doc
