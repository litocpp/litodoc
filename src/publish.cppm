export module lito.doc:publish;

import rstd;
import :model;
import :frontend;
import :publication;
import :render;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::doc
{

auto copy_data_summary(const DataSummary& source) -> DataSummary {
    auto result = DataSummary {
        .root     = source.root.clone(),
        .manifest = source.manifest.clone(),
        .digest   = source.digest.clone(),
    };
    for (const auto& package : source.packages) {
        result.packages.push(PackageDataSummary {
            .name = package.name.clone(),
            .json = package.json.clone(),
        });
    }
    return result;
}

auto data_json_for(const DataSummary& data, ref<str> package) -> rstd::path::PathBuf {
    for (const auto& entry : data.packages) {
        if (entry.name.as_str() == package) return entry.json.clone();
    }
    return {};
}

auto summary_for(ref<rstd::path::Path> output, const Dataset& dataset, const DataSummary& data)
    -> Summary {
    auto summary = Summary {
        .output = rstd::path::PathBuf::from(output),
        .index  = rstd::path::PathBuf::from(output).join(
            rstd::path::PathBuf::from("index.html"_str).as_path()),
        .data = copy_data_summary(data),
    };
    for (const auto& package : dataset.packages) {
        auto directory = rstd::path::PathBuf::from(output)
                             .join(rstd::path::PathBuf::from("package"_str).as_path())
                             .join(rstd::path::PathBuf::from(package.name.as_str()).as_path());
        auto package_summary = PackageSummary {
            .name         = package.name.clone(),
            .directory    = directory.clone(),
            .json         = directory.join(rstd::path::PathBuf::from("doc.json"_str).as_path()),
            .data_json    = data_json_for(data, package.name.as_str()),
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

auto publish_site(ref<rstd::path::Path> output,
                  const Dataset&        dataset,
                  const FrontendBundle& frontend,
                  const DataSummary&    data) -> Result<Summary, String> {
    auto publication = begin_publication(output, "doc-site"_str);
    if (publication.is_err()) return Err(rstd::move(publication).unwrap_err());
    auto rendered =
        render_site(publication->staging.as_path(), dataset, frontend, data.digest.as_str());
    if (rendered.is_err()) {
        (void)abort_publication(*publication);
        return Err(rstd::move(rendered).unwrap_err());
    }
    auto committed = commit_publication(*publication);
    if (committed.is_err()) return Err(rstd::move(committed).unwrap_err());
    auto summary           = summary_for(output, dataset, data);
    summary.site_generated = true;
    return Ok(rstd::move(summary));
}

} // namespace lito::doc
