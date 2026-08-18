export module lito.doc.web;

import rstd;
export import lito.site;

using namespace rstd::prelude;

export namespace lito::doc::web {

auto load_default_frontend() -> Result<site::FrontendBundle, String>;

} // namespace lito::doc::web
