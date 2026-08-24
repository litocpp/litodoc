export module lito.site:frontend_manifest;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site {

struct FrontendManifestTemplates {
  Option<String> root;
  Option<String> package;
  Option<String> module;
  Option<String> symbol;
  Option<String> source;
  Option<String> book_root;
  Option<String> book_page;
};

struct FrontendManifestAsset {
  String path;
  String media_type;
};

struct FrontendManifest {
  String format;
  usize version{};
  usize template_api{};
  Option<usize> data_api;
  Option<usize> book_data_api;
  Option<Vec<String>> capabilities;
  Option<FrontendManifestTemplates> templates;
  Option<Vec<String>> partials;
  Option<Vec<FrontendManifestAsset>> assets;
};

} // namespace lito::site

export namespace rstd {

template <>
struct Impl<serde::Deserialize, lito::site::FrontendManifestTemplates> {
  template <typename Deserializer>
  static auto deserialize(Deserializer &deserializer)
      -> Result<lito::site::FrontendManifestTemplates,
                typename Deserializer::error_type> {
    auto root = serde::OptionalField<String>("root"_str);
    auto package = serde::OptionalField<String>("package"_str);
    auto module = serde::OptionalField<String>("module"_str);
    auto symbol = serde::OptionalField<String>("symbol"_str);
    auto source = serde::OptionalField<String>("source"_str);
    auto book_root = serde::OptionalField<String>("book-root"_str);
    auto book_page = serde::OptionalField<String>("book-page"_str);
    auto result = serde::deserialize_record(
        deserializer,
        [&](ref<str> name,
            auto &input) -> Result<empty, typename Deserializer::error_type> {
          if (root.matches(name))
            return root.assign(deserializer, input);
          if (package.matches(name))
            return package.assign(deserializer, input);
          if (module.matches(name))
            return module.assign(deserializer, input);
          if (symbol.matches(name))
            return symbol.assign(deserializer, input);
          if (source.matches(name))
            return source.assign(deserializer, input);
          if (book_root.matches(name))
            return book_root.assign(deserializer, input);
          if (book_page.matches(name))
            return book_page.assign(deserializer, input);
          return Err(deserializer.unknown_field(name));
        });
    if (result.is_err())
      return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(lito::site::FrontendManifestTemplates{
        .root = root.take(),
        .package = package.take(),
        .module = module.take(),
        .symbol = symbol.take(),
        .source = source.take(),
        .book_root = book_root.take(),
        .book_page = book_page.take(),
    });
  }
};

template <> struct Impl<serde::Deserialize, lito::site::FrontendManifestAsset> {
  template <typename Deserializer>
  static auto deserialize(Deserializer &deserializer)
      -> Result<lito::site::FrontendManifestAsset,
                typename Deserializer::error_type> {
    auto path = serde::RequiredField<String>("path"_str);
    auto media_type = serde::RequiredField<String>("media-type"_str);
    auto result = serde::deserialize_record(
        deserializer,
        [&](ref<str> name,
            auto &input) -> Result<empty, typename Deserializer::error_type> {
          if (path.matches(name))
            return path.assign(deserializer, input);
          if (media_type.matches(name))
            return media_type.assign(deserializer, input);
          return Err(deserializer.unknown_field(name));
        });
    if (result.is_err())
      return Err(rstd::move(result).unwrap_err_unchecked());
    auto path_value = path.take(deserializer);
    if (path_value.is_err())
      return Err(rstd::move(path_value).unwrap_err_unchecked());
    auto media_type_value = media_type.take(deserializer);
    if (media_type_value.is_err())
      return Err(rstd::move(media_type_value).unwrap_err_unchecked());
    return Ok(lito::site::FrontendManifestAsset{
        .path = rstd::move(path_value).unwrap_unchecked(),
        .media_type = rstd::move(media_type_value).unwrap_unchecked(),
    });
  }
};

template <> struct Impl<serde::Deserialize, lito::site::FrontendManifest> {
  template <typename Deserializer>
  static auto deserialize(Deserializer &deserializer)
      -> Result<lito::site::FrontendManifest,
                typename Deserializer::error_type> {
    auto format = serde::RequiredField<String>("format"_str);
    auto version = serde::RequiredField<usize>("version"_str);
    auto template_api = serde::RequiredField<usize>("template-api"_str);
    auto data_api = serde::OptionalField<usize>("data-api"_str);
    auto book_data_api = serde::OptionalField<usize>("book-data-api"_str);
    auto capabilities = serde::OptionalField<Vec<String>>("capabilities"_str);
    auto templates =
        serde::OptionalField<lito::site::FrontendManifestTemplates>(
            "templates"_str);
    auto partials = serde::OptionalField<Vec<String>>("partials"_str);
    auto assets = serde::OptionalField<Vec<lito::site::FrontendManifestAsset>>(
        "assets"_str);
    auto result = serde::deserialize_record(
        deserializer,
        [&](ref<str> name,
            auto &input) -> Result<empty, typename Deserializer::error_type> {
          if (format.matches(name))
            return format.assign(deserializer, input);
          if (version.matches(name))
            return version.assign(deserializer, input);
          if (template_api.matches(name))
            return template_api.assign(deserializer, input);
          if (data_api.matches(name))
            return data_api.assign(deserializer, input);
          if (book_data_api.matches(name))
            return book_data_api.assign(deserializer, input);
          if (capabilities.matches(name))
            return capabilities.assign(deserializer, input);
          if (templates.matches(name))
            return templates.assign(deserializer, input);
          if (partials.matches(name))
            return partials.assign(deserializer, input);
          if (assets.matches(name))
            return assets.assign(deserializer, input);
          return Err(deserializer.unknown_field(name));
        });
    if (result.is_err())
      return Err(rstd::move(result).unwrap_err_unchecked());
    auto format_value = format.take(deserializer);
    if (format_value.is_err())
      return Err(rstd::move(format_value).unwrap_err_unchecked());
    auto version_value = version.take(deserializer);
    if (version_value.is_err())
      return Err(rstd::move(version_value).unwrap_err_unchecked());
    auto template_api_value = template_api.take(deserializer);
    if (template_api_value.is_err())
      return Err(rstd::move(template_api_value).unwrap_err_unchecked());
    return Ok(lito::site::FrontendManifest{
        .format = rstd::move(format_value).unwrap_unchecked(),
        .version = rstd::move(version_value).unwrap_unchecked(),
        .template_api = rstd::move(template_api_value).unwrap_unchecked(),
        .data_api = data_api.take(),
        .book_data_api = book_data_api.take(),
        .capabilities = capabilities.take(),
        .templates = templates.take(),
        .partials = partials.take(),
        .assets = assets.take(),
    });
  }
};

} // namespace rstd
