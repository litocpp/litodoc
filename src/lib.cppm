export module tenon.doc;

export import :model;

import rstd;
import :database;
import :data;
import :frontend;
import :publish;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::doc
{

auto selected_frontend(const Option<rstd::path::PathBuf>& path) -> Result<FrontendBundle, String> {
    return path.is_some() ? load_frontend_directory(path->as_path()) : load_builtin_frontend();
}

} // namespace tenon::doc

export namespace tenon::doc
{

auto generate(SiteInput input) -> Result<Summary, String> {
    auto title       = rstd::move(input.title);
    auto output      = input.output.clone();
    auto data_output = input.data_output.clone();
    auto frontend    = rstd::move(input.frontend);
    auto data_only   = input.data_only;
    if (title.is_empty()) return Err(String::make("doc site title must not be empty"_str));
    auto database = make_database(rstd::move(input.packages));
    if (database.is_err()) return Err(rstd::move(database).unwrap_err());
    auto dataset = make_dataset(rstd::move(title), rstd::move(database).unwrap());
    auto data    = publish_dataset(data_output.as_path(), dataset);
    if (data.is_err()) return Err(rstd::move(data).unwrap_err());
    if (data_only) return Ok(summary_for(output.as_path(), dataset, *data));
    auto loaded_frontend = selected_frontend(frontend);
    if (loaded_frontend.is_err()) return Err(rstd::move(loaded_frontend).unwrap_err());
    return publish_site(output.as_path(), dataset, *loaded_frontend, *data);
}

auto render(RenderInput input) -> Result<Summary, String> {
    auto dataset = load_dataset(input.data.as_path());
    if (dataset.is_err()) return Err(rstd::move(dataset).unwrap_err());
    auto data     = summarize_dataset(input.data.as_path(), *dataset);
    auto frontend = selected_frontend(input.frontend);
    if (frontend.is_err()) return Err(rstd::move(frontend).unwrap_err());
    return publish_site(input.output.as_path(), *dataset, *frontend, data);
}

auto validate_json(ref<str> contents) -> Result<empty, String> {
    return validate_package_json(contents);
}

auto validate_data(ref<rstd::path::Path> root) -> Result<DataSummary, String> {
    auto dataset = load_dataset(root);
    if (dataset.is_err()) return Err(rstd::move(dataset).unwrap_err());
    return Ok(summarize_dataset(root, *dataset));
}

} // namespace tenon::doc
