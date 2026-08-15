export module lito.doc:frontend;

import rstd;
import rstd.json;
import :templates;

using namespace rstd::prelude;
using namespace rstd::literals;
using FrontendJson      = rstd::json::Value;
using FrontendJsonArray = rstd::json::Array;

namespace lito::doc
{

struct FrontendResource {
    String path;
    String media_type;
    String contents;
};

struct FrontendAsset {
    String path;
    String media_type;
    String contents;
};

struct FrontendBundle {
    String             identity;
    String             digest;
    String             root_template;
    String             package_template;
    String             module_template;
    String             symbol_template;
    String             source_template;
    TemplateSet        templates;
    Vec<FrontendAsset> assets;
};

inline constexpr uint64_t FRONTEND_FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FRONTEND_FNV_PRIME  = 1099511628211ull;

auto frontend_hex(uint64_t value) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[value & 0xfu];
        value >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto frontend_digest(const rstd::collections::BTreeMap<String, FrontendResource>& resources)
    -> String {
    auto hash   = FRONTEND_FNV_OFFSET;
    auto append = [&hash](ref<str> part) {
        for (auto value : part) {
            hash ^= value.to_primitive();
            hash *= FRONTEND_FNV_PRIME;
        }
        hash ^= 0;
        hash *= FRONTEND_FNV_PRIME;
    };
    auto iterator = resources.iter();
    for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
        append((*(*item).template get<0>()).as_str());
        append((*(*item).template get<1>()).contents.as_str());
    }
    return frontend_hex(hash);
}

auto safe_frontend_path(ref<str> value) -> bool {
    if (value.is_empty() || value.starts_with("/"_str) || value.contains("\\"_str) ||
        value.contains("//"_str))
        return false;
    auto begin = usize {};
    while (begin <= value.len()) {
        auto end = begin;
        while (end < value.len() && value[end] != u8('/')) ++end;
        auto part = value.get(begin, end).unwrap();
        if (part.is_empty() || part == "."_str || part == ".."_str) return false;
        if (end == value.len()) break;
        begin = end + usize(1);
    }
    return true;
}

auto frontend_member(const FrontendJson& value, ref<str> name, ref<str> context)
    -> Result<ref<FrontendJson>, String> {
    auto member = value.get(name);
    if (member.is_none()) return Err(rstd::format("{} is missing '{}'", context, name));
    return Ok(*member);
}

auto frontend_string(const FrontendJson& value, ref<str> name, ref<str> context)
    -> Result<String, String> {
    auto member = frontend_member(value, name, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) return Err(rstd::format("{}.{} must be a string", context, name));
    return Ok(String::make(*text));
}

auto frontend_usize(const FrontendJson& value, ref<str> name, ref<str> context)
    -> Result<usize, String> {
    auto member = frontend_member(value, name, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto number = (**member).as_u64();
    if (number.is_none() || *number > u64(usize::MAX.to_primitive()))
        return Err(rstd::format("{}.{} must be an unsigned integer", context, name));
    return Ok(usize(static_cast<size_t>((*number).to_primitive())));
}

auto frontend_array(const FrontendJson& value, ref<str> name, ref<str> context)
    -> Result<ref<FrontendJsonArray>, String> {
    auto member = frontend_member(value, name, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto array = (**member).as_array();
    if (array.is_none()) return Err(rstd::format("{}.{} must be an array", context, name));
    return Ok(*array);
}

auto parse_frontend_manifest(ref<str> contents) -> Result<FrontendJson, String> {
    auto parsed = rstd::json::from_str(contents);
    if (parsed.is_err())
        return Err(rstd::format("invalid frontend manifest JSON: {}", parsed.unwrap_err()));
    if (! parsed->is_object()) return Err(String::make("frontend manifest must be an object"_str));
    auto format       = frontend_string(*parsed, "format"_str, "frontend manifest"_str);
    auto version      = frontend_usize(*parsed, "version"_str, "frontend manifest"_str);
    auto data_api     = frontend_usize(*parsed, "data-api"_str, "frontend manifest"_str);
    auto template_api = frontend_usize(*parsed, "template-api"_str, "frontend manifest"_str);
    if (format.is_err()) return Err(rstd::move(format).unwrap_err());
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (data_api.is_err()) return Err(rstd::move(data_api).unwrap_err());
    if (template_api.is_err()) return Err(rstd::move(template_api).unwrap_err());
    if (format->as_str() != "lito-doc-frontend"_str)
        return Err(rstd::format("unsupported frontend format '{}'", format->as_str()));
    if (*version != usize(1))
        return Err(rstd::format("unsupported frontend bundle version {}", *version));
    if (*data_api != usize(2))
        return Err(rstd::format("frontend requires unsupported doc data API {}", *data_api));
    if (*template_api != usize(1))
        return Err(rstd::format("frontend requires unsupported template API {}", *template_api));
    return Ok(rstd::move(parsed).unwrap());
}

auto frontend_manifest_paths(const FrontendJson& manifest) -> Result<Vec<String>, String> {
    auto templates = frontend_member(manifest, "templates"_str, "frontend manifest"_str);
    auto partials  = frontend_array(manifest, "partials"_str, "frontend manifest"_str);
    auto assets    = frontend_array(manifest, "assets"_str, "frontend manifest"_str);
    if (templates.is_err()) return Err(rstd::move(templates).unwrap_err());
    if (partials.is_err()) return Err(rstd::move(partials).unwrap_err());
    if (assets.is_err()) return Err(rstd::move(assets).unwrap_err());
    auto paths           = Vec<String>::make();
    auto append_template = [&paths, &templates](ref<str> name) -> Result<empty, String> {
        auto path = frontend_string(**templates, name, "frontend templates"_str);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (! safe_frontend_path(path->as_str()))
            return Err(rstd::format("invalid frontend template path '{}'", path->as_str()));
        paths.push(rstd::move(path).unwrap());
        return Ok(empty {});
    };
    auto appended = append_template("root"_str);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    appended = append_template("package"_str);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    appended = append_template("module"_str);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    appended = append_template("symbol"_str);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    appended = append_template("source"_str);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    for (const auto& partial : **partials) {
        auto text = partial.as_str();
        if (text.is_none()) return Err(String::make("frontend partial path must be a string"_str));
        if (! safe_frontend_path(*text))
            return Err(rstd::format("invalid frontend partial path '{}'", *text));
        paths.push(String::make(*text));
    }
    for (const auto& asset : **assets) {
        auto path       = frontend_string(asset, "path"_str, "frontend asset"_str);
        auto media_type = frontend_string(asset, "media-type"_str, "frontend asset"_str);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (media_type.is_err()) return Err(rstd::move(media_type).unwrap_err());
        if (! safe_frontend_path(path->as_str()))
            return Err(rstd::format("invalid frontend asset path '{}'", path->as_str()));
        paths.push(rstd::move(path).unwrap());
    }
    return Ok(rstd::move(paths));
}

auto make_frontend_bundle(String                                                identity,
                          String                                                digest,
                          rstd::collections::BTreeMap<String, FrontendResource> resources,
                          FrontendJson manifest) -> Result<FrontendBundle, String> {
    auto templates = frontend_member(manifest, "templates"_str, "frontend manifest"_str);
    auto partials  = frontend_array(manifest, "partials"_str, "frontend manifest"_str);
    auto assets    = frontend_array(manifest, "assets"_str, "frontend manifest"_str);
    if (templates.is_err()) return Err(rstd::move(templates).unwrap_err());
    if (partials.is_err()) return Err(rstd::move(partials).unwrap_err());
    if (assets.is_err()) return Err(rstd::move(assets).unwrap_err());
    auto root_template    = frontend_string(**templates, "root"_str, "frontend templates"_str);
    auto package_template = frontend_string(**templates, "package"_str, "frontend templates"_str);
    auto module_template  = frontend_string(**templates, "module"_str, "frontend templates"_str);
    auto symbol_template  = frontend_string(**templates, "symbol"_str, "frontend templates"_str);
    auto source_template  = frontend_string(**templates, "source"_str, "frontend templates"_str);
    if (root_template.is_err()) return Err(rstd::move(root_template).unwrap_err());
    if (package_template.is_err()) return Err(rstd::move(package_template).unwrap_err());
    if (module_template.is_err()) return Err(rstd::move(module_template).unwrap_err());
    if (symbol_template.is_err()) return Err(rstd::move(symbol_template).unwrap_err());
    if (source_template.is_err()) return Err(rstd::move(source_template).unwrap_err());

    auto set = TemplateSet {
        .identity  = identity.clone(),
        .documents = rstd::collections::BTreeMap<String, TemplateDocument>::make(),
    };
    auto template_paths = Vec<String>::make();
    template_paths.push(root_template->clone());
    template_paths.push(package_template->clone());
    template_paths.push(module_template->clone());
    template_paths.push(symbol_template->clone());
    template_paths.push(source_template->clone());
    for (const auto& partial : **partials) {
        auto text = partial.as_str();
        if (text.is_none()) return Err(String::make("frontend partial path must be a string"_str));
        template_paths.push(String::make(*text));
    }
    auto seen = rstd::collections::BTreeSet<String>::make();
    for (const auto& path : template_paths) {
        if (! seen.insert(path.clone())) continue;
        auto resource = resources.get(path.as_str());
        if (resource.is_none())
            return Err(rstd::format("frontend '{}' is missing template '{}'", identity, path));
        auto parsed = parse_template(path.as_str(), (**resource).contents.as_str());
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        set.documents.insert(path.clone(), rstd::move(parsed).unwrap());
    }

    auto frontend_assets = Vec<FrontendAsset>::make();
    for (const auto& asset : **assets) {
        auto path       = frontend_string(asset, "path"_str, "frontend asset"_str);
        auto media_type = frontend_string(asset, "media-type"_str, "frontend asset"_str);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (media_type.is_err()) return Err(rstd::move(media_type).unwrap_err());
        auto resource = resources.get(path->as_str());
        if (resource.is_none())
            return Err(rstd::format(
                "frontend '{}' is missing asset '{}'", identity.as_str(), path->as_str()));
        if ((**resource).media_type.as_str() != media_type->as_str())
            return Err(rstd::format("frontend asset '{}' media type mismatch", path->as_str()));
        frontend_assets.push(FrontendAsset {
            .path       = rstd::move(path).unwrap(),
            .media_type = rstd::move(media_type).unwrap(),
            .contents   = (**resource).contents.clone(),
        });
    }
    return Ok(FrontendBundle {
        .identity         = rstd::move(identity),
        .digest           = rstd::move(digest),
        .root_template    = rstd::move(root_template).unwrap(),
        .package_template = rstd::move(package_template).unwrap(),
        .module_template  = rstd::move(module_template).unwrap(),
        .symbol_template  = rstd::move(symbol_template).unwrap(),
        .source_template  = rstd::move(source_template).unwrap(),
        .templates        = rstd::move(set),
        .assets           = rstd::move(frontend_assets),
    });
}

auto read_frontend_file(ref<rstd::path::Path> root, ref<str> relative, ref<str> media_type)
    -> Result<FrontendResource, String> {
    if (! safe_frontend_path(relative))
        return Err(rstd::format("invalid frontend resource path '{}'", relative));
    auto path = rstd::path::PathBuf::from(root).join(rstd::path::PathBuf::from(relative).as_path());
    auto metadata = rstd::fs::symlink_metadata(path.as_path());
    if (metadata.is_err())
        return Err(rstd::format("cannot inspect frontend resource '{}': {}",
                                path.as_path(),
                                rstd::move(metadata).unwrap_err()));
    if (metadata->is_symlink() || ! metadata->is_file())
        return Err(rstd::format("frontend resource '{}' must be a regular file", path.as_path()));
    auto contents = rstd::fs::read_to_string(path.as_path());
    if (contents.is_err())
        return Err(rstd::format("cannot read frontend resource '{}': {}",
                                path.as_path(),
                                rstd::move(contents).unwrap_err()));
    return Ok(FrontendResource {
        .path       = String::make(relative),
        .media_type = String::make(media_type),
        .contents   = rstd::move(contents).unwrap(),
    });
}

auto validate_frontend_directory(ref<rstd::path::Path>                      root,
                                 ref<rstd::path::Path>                      directory,
                                 const rstd::collections::BTreeSet<String>& declared)
    -> Result<empty, String> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err())
        return Err(rstd::format("cannot enumerate frontend directory '{}': {}",
                                directory,
                                rstd::move(opened).unwrap_err()));
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err())
            return Err(rstd::format("cannot enumerate frontend directory '{}': {}",
                                    directory,
                                    rstd::move(item).unwrap_err()));
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err())
            return Err(rstd::format("cannot inspect frontend entry '{}': {}",
                                    entry.path().as_path(),
                                    rstd::move(type).unwrap_err()));
        auto path = entry.path();
        if (type->is_symlink())
            return Err(rstd::format("frontend entry '{}' must not be a symlink", path.as_path()));
        if (type->is_dir()) {
            auto nested = validate_frontend_directory(root, path.as_path(), declared);
            if (nested.is_err()) return nested;
            continue;
        }
        if (! type->is_file())
            return Err(rstd::format("frontend entry '{}' must be a regular file", path.as_path()));
        auto relative = path.as_path().strip_prefix(root);
        if (relative.is_none())
            return Err(rstd::format("frontend entry '{}' escapes its root", path.as_path()));
        auto text = (*relative).to_str();
        if (text.is_none())
            return Err(rstd::format("frontend entry '{}' is not valid UTF-8", path.as_path()));
        if (! declared.contains(*text))
            return Err(
                rstd::format("frontend resource '{}' is not declared by frontend.json", *text));
    }
    return Ok(empty {});
}

auto load_frontend_directory(ref<rstd::path::Path> root) -> Result<FrontendBundle, String> {
    auto manifest_resource = read_frontend_file(root, "frontend.json"_str, "application/json"_str);
    if (manifest_resource.is_err()) return Err(rstd::move(manifest_resource).unwrap_err());
    auto manifest = parse_frontend_manifest(manifest_resource->contents.as_str());
    if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
    auto paths = frontend_manifest_paths(*manifest);
    if (paths.is_err()) return Err(rstd::move(paths).unwrap_err());
    auto declared = rstd::collections::BTreeSet<String>::make();
    declared.insert(String::make("frontend.json"_str));
    for (const auto& path : *paths) {
        if (! declared.insert(path.clone()))
            return Err(rstd::format("frontend manifest repeats resource '{}'", path.as_str()));
    }
    auto valid_directory = validate_frontend_directory(root, root, declared);
    if (valid_directory.is_err()) return Err(rstd::move(valid_directory).unwrap_err());
    auto resources = rstd::collections::BTreeMap<String, FrontendResource>::make();
    resources.insert(String::make("frontend.json"_str), rstd::move(manifest_resource).unwrap());
    auto assets = frontend_array(*manifest, "assets"_str, "frontend manifest"_str);
    if (assets.is_err()) return Err(rstd::move(assets).unwrap_err());
    auto media = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& asset : **assets) {
        auto path       = frontend_string(asset, "path"_str, "frontend asset"_str);
        auto media_type = frontend_string(asset, "media-type"_str, "frontend asset"_str);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (media_type.is_err()) return Err(rstd::move(media_type).unwrap_err());
        media.insert(rstd::move(path).unwrap(), rstd::move(media_type).unwrap());
    }
    for (const auto& path : *paths) {
        if (resources.contains_key(path.as_str())) continue;
        auto media_type = media.get(path.as_str());
        auto resource   = read_frontend_file(
            root, path.as_str(), media_type.is_some() ? (**media_type).as_str() : "text/html"_str);
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());
        resources.insert(path.clone(), rstd::move(resource).unwrap());
    }
    auto root_text = root.to_str();
    if (root_text.is_none()) return Err(String::make("frontend root is not valid UTF-8"_str));
    auto digest = frontend_digest(resources);
    return make_frontend_bundle(rstd::format("directory:{}", *root_text),
                                rstd::move(digest),
                                rstd::move(resources),
                                rstd::move(manifest).unwrap());
}

} // namespace lito::doc
