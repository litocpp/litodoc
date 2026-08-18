export module lito.doc;

export import :model;

import rstd;
import :database;
import :data;
import :publish;
import lito.site;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::site;

namespace lito::doc {

auto selected_frontend(const Option<rstd::path::PathBuf> &path,
                       Option<FrontendBundle> default_frontend)
    -> Result<FrontendBundle, String> {
  auto frontend = Option<FrontendBundle>{};
  if (path.is_some()) {
    auto loaded = load_frontend_directory(path->as_path());
    if (loaded.is_err())
      return Err(rstd::move(loaded).unwrap_err());
    frontend = Some(rstd::move(loaded).unwrap());
  } else if (default_frontend.is_some()) {
    frontend = rstd::move(default_frontend);
  } else {
    return Err(String::make("default site frontend is required"_str));
  }
  if (!frontend->supports_api)
    return Err(rstd::format("frontend '{}' does not support API documentation",
                            frontend->identity.as_str()));
  return Ok(rstd::move(frontend).unwrap());
}

} // namespace lito::doc

export namespace lito::doc {

auto generate(SiteInput input, Option<FrontendBundle> default_frontend = {})
    -> Result<Summary, String> {
  auto title = rstd::move(input.title);
  auto output = input.output.clone();
  auto data_output = input.data_output.clone();
  auto frontend = rstd::move(input.frontend);
  auto data_only = input.data_only;
  if (title.is_empty())
    return Err(String::make("doc site title must not be empty"_str));
  auto database = make_database(rstd::move(input.packages));
  if (database.is_err())
    return Err(rstd::move(database).unwrap_err());
  auto dataset = make_dataset(rstd::move(title), rstd::move(database).unwrap());
  auto data = publish_dataset(data_output.as_path(), dataset);
  if (data.is_err())
    return Err(rstd::move(data).unwrap_err());
  if (data_only)
    return Ok(summary_for(output.as_path(), dataset, *data));
  auto loaded_frontend =
      selected_frontend(frontend, rstd::move(default_frontend));
  if (loaded_frontend.is_err())
    return Err(rstd::move(loaded_frontend).unwrap_err());
  return publish_site(output.as_path(), dataset, *loaded_frontend, *data);
}

auto render(RenderInput input, Option<FrontendBundle> default_frontend = {})
    -> Result<Summary, String> {
  auto dataset = load_dataset(input.data.as_path());
  if (dataset.is_err())
    return Err(rstd::move(dataset).unwrap_err());
  auto data = summarize_dataset(input.data.as_path(), *dataset);
  auto frontend =
      selected_frontend(input.frontend, rstd::move(default_frontend));
  if (frontend.is_err())
    return Err(rstd::move(frontend).unwrap_err());
  return publish_site(input.output.as_path(), *dataset, *frontend, data);
}

auto validate_json(ref<str> contents) -> Result<empty, String> {
  return validate_package_json(contents);
}

auto validate_data(ref<rstd::path::Path> root) -> Result<DataSummary, String> {
  auto dataset = load_dataset(root);
  if (dataset.is_err())
    return Err(rstd::move(dataset).unwrap_err());
  return Ok(summarize_dataset(root, *dataset));
}

} // namespace lito::doc
