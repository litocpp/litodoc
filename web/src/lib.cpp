module lito.doc.web;
import rstd;
import liresto;
import lito.site;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::doc::web
{
auto embedded_resources() -> liresto::ResourceBundle;

auto load_default_frontend() -> Result<site::FrontendBundle, String> {
    auto                            bundle = embedded_resources();
    Vec<site::FrontendResourceData> resources;
    for (usize i {}; i < bundle.len(); ++i) {
        auto entry = bundle.at(i).unwrap();
        resources.push(site::FrontendResourceData { entry.path(), entry.contents() });
    }
    return site::load_frontend_data("embedded:litodoc-default"_str, resources.as_slice());
}
} // namespace lito::doc::web
