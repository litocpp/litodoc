export module tenon.doc;

export import :model;

import rstd;
import :database;
import :json;
import :publish;

using namespace rstd::prelude;

export namespace tenon::doc
{

auto generate(SiteInput input) -> Result<Summary, String> {
    auto output   = input.output.clone();
    auto database = make_database(rstd::move(input.packages));
    if (database.is_err()) return Err(rstd::move(database).unwrap_err());
    return publish(output.as_path(), *database);
}

auto validate_json(ref<str> contents) -> Result<empty, String> {
    return validate_package_json(contents);
}

} // namespace tenon::doc
