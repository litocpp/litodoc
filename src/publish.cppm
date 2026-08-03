export module tenon.doc:publish;

import rstd;
import :model;
import :render;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::doc
{

auto sibling_path(ref<rstd::path::Path> output, ref<str> suffix)
    -> Result<rstd::path::PathBuf, String> {
    auto parent = output.parent();
    auto name   = output.file_name();
    if (parent.is_none() || name.is_none() || name->to_str().is_none())
        return Err(rstd::format("doc output '{}' must name a directory below a parent", output));
    auto sibling = String::make("."_str);
    sibling.push_str(*name->to_str());
    sibling.push_str(suffix);
    return Ok(rstd::path::PathBuf::from(*parent).join(
        rstd::path::PathBuf::from(rstd::move(sibling)).as_path()));
}

auto remove_owned_directory(ref<rstd::path::Path> path) -> Result<empty, String> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err())
        return Err(rstd::format(
            "cannot inspect doc directory '{}': {}", path, rstd::move(exists).unwrap_err()));
    if (! *exists) return Ok(empty {});
    auto removed = rstd::fs::remove_dir_all(path);
    if (removed.is_err())
        return Err(rstd::format(
            "cannot remove doc directory '{}': {}", path, rstd::move(removed).unwrap_err()));
    return Ok(empty {});
}

auto summary_for(ref<rstd::path::Path> output, const Database& database) -> Summary {
    auto summary = Summary {
        .output = rstd::path::PathBuf::from(output),
        .index  = rstd::path::PathBuf::from(output).join(
            rstd::path::PathBuf::from("index.html"_str).as_path()),
    };
    for (const auto& package : database.packages) {
        auto directory = rstd::path::PathBuf::from(output)
                             .join(rstd::path::PathBuf::from("package"_str).as_path())
                             .join(rstd::path::PathBuf::from(package.name.as_str()).as_path());
        auto package_summary = PackageSummary {
            .name         = package.name.clone(),
            .directory    = directory.clone(),
            .json         = directory.join(rstd::path::PathBuf::from("doc.json"_str).as_path()),
            .index        = directory.join(rstd::path::PathBuf::from("index.html"_str).as_path()),
            .symbols      = package.symbols.len(),
            .documented   = package.documented,
            .undocumented = package.undocumented,
            .unsupported  = package.unsupported,
            .diagnostics  = package.diagnostics.len(),
        };
        for (const auto& diagnostic : package.diagnostics) {
            package_summary.diagnostic_details.push(Diagnostic {
                .severity = diagnostic.severity,
                .code     = diagnostic.code.clone(),
                .message  = diagnostic.message.clone(),
                .path     = diagnostic.path.clone(),
                .line     = diagnostic.line,
            });
        }
        summary.packages.push(rstd::move(package_summary));
    }
    return summary;
}

auto publish(ref<rstd::path::Path> output, const Database& database) -> Result<Summary, String> {
    auto staging = sibling_path(output, ".tenon-doc-staging"_str);
    auto backup  = sibling_path(output, ".tenon-doc-backup"_str);
    if (staging.is_err()) return Err(rstd::move(staging).unwrap_err());
    if (backup.is_err()) return Err(rstd::move(backup).unwrap_err());
    auto cleaned = remove_owned_directory(staging->as_path());
    if (cleaned.is_err()) return Err(rstd::move(cleaned).unwrap_err());
    cleaned = remove_owned_directory(backup->as_path());
    if (cleaned.is_err()) return Err(rstd::move(cleaned).unwrap_err());
    auto created = rstd::fs::create_dir_all(staging->as_path());
    if (created.is_err())
        return Err(rstd::format("cannot create doc staging directory '{}': {}",
                                staging->as_path(),
                                rstd::move(created).unwrap_err()));
    auto rendered = render_database(staging->as_path(), database);
    if (rendered.is_err()) return Err(rstd::move(rendered).unwrap_err());

    auto output_exists = rstd::fs::exists(output);
    if (output_exists.is_err())
        return Err(rstd::format(
            "cannot inspect doc output '{}': {}", output, rstd::move(output_exists).unwrap_err()));
    if (*output_exists) {
        auto moved = rstd::fs::rename(output, backup->as_path());
        if (moved.is_err())
            return Err(rstd::format("cannot preserve previous doc output '{}': {}",
                                    output,
                                    rstd::move(moved).unwrap_err()));
    }
    auto published = rstd::fs::rename(staging->as_path(), output);
    if (published.is_err()) {
        if (*output_exists) (void)rstd::fs::rename(backup->as_path(), output);
        return Err(rstd::format(
            "cannot publish doc output '{}': {}", output, rstd::move(published).unwrap_err()));
    }
    if (*output_exists) {
        cleaned = remove_owned_directory(backup->as_path());
        if (cleaned.is_err()) return Err(rstd::move(cleaned).unwrap_err());
    }
    return Ok(summary_for(output, database));
}

} // namespace tenon::doc
