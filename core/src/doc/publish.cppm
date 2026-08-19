export module lito.doc:publish;

import rstd;
import rstd.json;
import :model;
import :data;
import lito.site;
import :render;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::doc {

using PublishJson = rstd::json::Value;
using PublishJsonArray = rstd::json::Array;
using PublishJsonMap = rstd::json::Map;

auto publish_json_string(ref<str> value) -> PublishJson {
  return PublishJson::String(String::make(value));
}

auto publish_json_number(usize value) -> PublishJson {
  return PublishJson::Number(
      rstd::json::Number::from_u64(u64(value.to_primitive())));
}

auto publish_json_text(PublishJson value) -> String {
  auto result = rstd::json::to_string(
      value, rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  result.push_ascii('\n');
  return result;
}

auto publication_file_json(const PublicationFile &file) -> PublishJson {
  auto object = PublishJsonMap::make();
  object.insert(String::make("path"_str),
                publish_json_string(file.path.as_str()));
  object.insert(String::make("size"_str), publish_json_number(file.size));
  object.insert(String::make("sha256"_str),
                publish_json_string(file.sha256.as_str()));
  object.insert(String::make("media_type"_str),
                publish_json_string(file.media_type.as_str()));
  object.insert(String::make("cache"_str),
                publish_json_string(file.cache.as_str()));
  return PublishJson::Object(rstd::move(object));
}

auto package_publication_json(const Package &package,
                              const FrontendBundle &frontend,
                              ref<str> data_digest,
                              const Vec<PublicationFile> &files) -> String {
  auto root = PublishJsonMap::make();
  root.insert(String::make("format"_str),
              publish_json_string("litodoc-package-publication"_str));
  root.insert(String::make("version"_str), publish_json_number(usize(1)));
  auto package_value = PublishJsonMap::make();
  package_value.insert(String::make("name"_str),
                       publish_json_string(package.name.as_str()));
  package_value.insert(String::make("version"_str),
                       publish_json_string(package.version.as_str()));
  package_value.insert(String::make("root_module"_str),
                       publish_json_string(package.root_module.as_str()));
  root.insert(String::make("package"_str),
              PublishJson::Object(rstd::move(package_value)));
  auto source = PublishJsonMap::make();
  source.insert(String::make("identity"_str),
                publish_json_string(package.source_identity.as_str()));
  root.insert(String::make("source"_str),
              PublishJson::Object(rstd::move(source)));
  auto generator = PublishJsonMap::make();
  generator.insert(String::make("litodoc"_str),
                   publish_json_string("0.1.0"_str));
  generator.insert(String::make("frontend"_str),
                   publish_json_string(frontend.identity.as_str()));
  generator.insert(String::make("frontend_digest"_str),
                   publish_json_string(frontend.digest.as_str()));
  generator.insert(String::make("data_digest"_str),
                   publish_json_string(data_digest));
  generator.insert(String::make("data_api"_str), publish_json_number(usize(3)));
  generator.insert(String::make("template_api"_str),
                   publish_json_number(usize(1)));
  generator.insert(String::make("toolchain_version"_str),
                   publish_json_string(package.toolchain_version.as_str()));
  generator.insert(String::make("toolchain_target"_str),
                   publish_json_string(package.toolchain_target.as_str()));
  generator.insert(String::make("language_standard"_str),
                   publish_json_string(package.language_standard.as_str()));
  root.insert(String::make("generator"_str),
              PublishJson::Object(rstd::move(generator)));
  root.insert(String::make("entry"_str), publish_json_string("index.html"_str));
  auto file_values = PublishJsonArray::make();
  for (const auto &file : files)
    file_values.push(publication_file_json(file));
  root.insert(String::make("files"_str),
              PublishJson::Array(rstd::move(file_values)));
  return publish_json_text(PublishJson::Object(rstd::move(root)));
}

auto publication_set_json(const Vec<PackagePublicationSummary> &packages)
    -> String {
  auto root = PublishJsonMap::make();
  root.insert(String::make("format"_str),
              publish_json_string("litodoc-publication-set"_str));
  root.insert(String::make("version"_str), publish_json_number(usize(1)));
  auto values = PublishJsonArray::make();
  for (const auto &package : packages) {
    auto object = PublishJsonMap::make();
    object.insert(String::make("name"_str),
                  publish_json_string(package.name.as_str()));
    object.insert(String::make("version"_str),
                  publish_json_string(package.version.as_str()));
    object.insert(String::make("directory"_str),
                  publish_json_string(package.name.as_str()));
    object.insert(
        String::make("manifest"_str),
        publish_json_string(
            rstd::format("{}/publication.json", package.name).as_str()));
    values.push(PublishJson::Object(rstd::move(object)));
  }
  root.insert(String::make("packages"_str),
              PublishJson::Array(rstd::move(values)));
  return publish_json_text(PublishJson::Object(rstd::move(root)));
}

auto copy_data_summary(const DataSummary &source) -> DataSummary {
  auto result = DataSummary{
      .root = source.root.clone(),
      .manifest = source.manifest.clone(),
      .digest = source.digest.clone(),
  };
  for (const auto &package : source.packages) {
    result.packages.push(PackageDataSummary{
        .name = package.name.clone(),
        .json = package.json.clone(),
    });
  }
  return result;
}

auto data_json_for(const DataSummary &data, ref<str> package)
    -> rstd::path::PathBuf {
  for (const auto &entry : data.packages) {
    if (entry.name.as_str() == package)
      return entry.json.clone();
  }
  return {};
}

auto summary_for(ref<rstd::path::Path> output, const Dataset &dataset,
                 const DataSummary &data) -> Summary {
  auto summary = Summary{
      .output = rstd::path::PathBuf::from(output),
      .index = rstd::path::PathBuf::from(output).join(
          rstd::path::PathBuf::from("index.html"_str).as_path()),
      .data = copy_data_summary(data),
  };
  for (const auto &package : dataset.packages) {
    auto directory =
        rstd::path::PathBuf::from(output)
            .join(rstd::path::PathBuf::from("package"_str).as_path())
            .join(rstd::path::PathBuf::from(package.name.as_str()).as_path());
    auto package_summary = PackageSummary{
        .name = package.name.clone(),
        .directory = directory.clone(),
        .json =
            directory.join(rstd::path::PathBuf::from("doc.json"_str).as_path()),
        .data_json = data_json_for(data, package.name.as_str()),
        .index = directory.join(
            rstd::path::PathBuf::from("index.html"_str).as_path()),
        .symbols = package.symbols.len(),
        .documented = package.documented,
        .undocumented = package.undocumented,
        .unsupported = package.unsupported,
        .diagnostics = package.diagnostics.len(),
    };
    for (const auto &diagnostic : package.diagnostics) {
      package_summary.diagnostic_details.push(Diagnostic{
          .severity = diagnostic.severity,
          .code = diagnostic.code.clone(),
          .message = diagnostic.message.clone(),
          .path = diagnostic.path.clone(),
          .line = diagnostic.line,
      });
    }
    summary.packages.push(rstd::move(package_summary));
  }
  return summary;
}

auto publish_site(ref<rstd::path::Path> output, const Dataset &dataset,
                  const FrontendBundle &frontend, const DataSummary &data)
    -> Result<Summary, String> {
  auto publication = begin_publication(output, "doc-site"_str);
  if (publication.is_err())
    return Err(rstd::move(publication).unwrap_err());
  auto rendered = render_site(publication->staging.as_path(), dataset, frontend,
                              data.digest.as_str());
  if (rendered.is_err()) {
    (void)abort_publication(*publication);
    return Err(rstd::move(rendered).unwrap_err());
  }
  auto committed = commit_publication(*publication);
  if (committed.is_err())
    return Err(rstd::move(committed).unwrap_err());
  auto summary = summary_for(output, dataset, data);
  summary.site_generated = true;
  return Ok(rstd::move(summary));
}

auto publish_package_set(ref<rstd::path::Path> output, const Dataset &dataset,
                         const FrontendBundle &frontend,
                         const DataSummary &data) -> Result<Summary, String> {
  auto publication = begin_publication(output, "doc-package-set"_str);
  if (publication.is_err())
    return Err(rstd::move(publication).unwrap_err());
  auto publication_summary = PublicationSetSummary{
      .root = rstd::path::PathBuf::from(output),
      .manifest = rstd::path::PathBuf::from(output).join(
          rstd::path::PathBuf::from("publication-set.json"_str).as_path()),
  };
  for (const auto &package : dataset.packages) {
    if (package.source_identity.is_empty()) {
      (void)abort_publication(*publication);
      return Err(
          rstd::format("package '{}' has no source identity for publication",
                       package.name.as_str()));
    }
    auto directory = publication->staging.join(
        rstd::path::PathBuf::from(package.name.as_str()).as_path());
    auto files = Vec<PublicationFile>::make();
    auto package_digest = data_digest(package_json(package).as_str());
    auto rendered =
        render_package_site(directory.as_path(), dataset, package, frontend,
                            package_digest.as_str(), files);
    if (rendered.is_err()) {
      (void)abort_publication(*publication);
      return Err(rstd::move(rendered).unwrap_err());
    }
    auto manifest = package_publication_json(package, frontend,
                                             package_digest.as_str(), files);
    auto written = write_doc_file(directory.as_path(), "publication.json"_str,
                                  manifest.as_str());
    if (written.is_err()) {
      (void)abort_publication(*publication);
      return Err(rstd::move(written).unwrap_err());
    }
    auto final_directory = rstd::path::PathBuf::from(output).join(
        rstd::path::PathBuf::from(package.name.as_str()).as_path());
    publication_summary.packages.push(PackagePublicationSummary{
        .name = package.name.clone(),
        .version = package.version.clone(),
        .directory = final_directory.clone(),
        .manifest = final_directory.join(
            rstd::path::PathBuf::from("publication.json"_str).as_path()),
        .index = final_directory.join(
            rstd::path::PathBuf::from("index.html"_str).as_path()),
        .files = rstd::move(files),
    });
  }
  auto set_manifest = publication_set_json(publication_summary.packages);
  auto written =
      write_doc_file(publication->staging.as_path(), "publication-set.json"_str,
                     set_manifest.as_str());
  if (written.is_err()) {
    (void)abort_publication(*publication);
    return Err(rstd::move(written).unwrap_err());
  }
  auto committed = commit_publication(*publication);
  if (committed.is_err())
    return Err(rstd::move(committed).unwrap_err());
  auto summary = summary_for(output, dataset, data);
  summary.site_generated = true;
  summary.index = {};
  summary.publication_set = Some(rstd::move(publication_summary));
  return Ok(rstd::move(summary));
}

} // namespace lito::doc
