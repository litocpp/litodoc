module;
#include <rstd/macro.hpp>
export module lito.site:links;
import rstd;
import lihttpto;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site
{
struct SiteLink {
    String         href;
    Option<String> output;
    Option<String> fragment;
};
struct SiteBaseUrl {
    lihttpto::Url url;
};

auto absolute_site_url(ref<str> value) -> Result<lihttpto::Url, String> {
    for (auto byte : value)
        if (byte < u8(32) || byte == u8(127) || byte == u8('\\'))
            return Err("URL contains a control character or backslash"_Str);
    auto parsed = lihttpto::Url::parse_http(value);
    if (parsed.is_err())
        return Err(rstd::format("invalid HTTP URL '{}': {}", value, parsed.unwrap_err()));
    if (parsed->userinfo().is_some()) return Err("URL must not contain credentials"_Str);
    return Ok(rstd::move(parsed).unwrap());
}

auto site_base_url(ref<str> value) -> Result<SiteBaseUrl, String> {
    auto url = rstd_try(absolute_site_url(value));
    if (url.query().is_some() || url.fragment().is_some())
        return Err("site-url must not contain query or fragment"_Str);
    if (! url.path().ends_with("/"_str))
        url = rstd_try(absolute_site_url(rstd::format("{}/", url.as_ref()).as_str()));
    return Ok(SiteBaseUrl { rstd::move(url) });
}

auto encode_site_path(ref<str> path) -> String {
    String encoded;
    for (;;) {
        auto split   = path.split_once("/"_str);
        auto segment = split.is_some() ? split->template get<0>() : path;
        encoded.push_str(lihttpto::encode_component(segment).as_str());
        if (split.is_none()) break;
        encoded.push_ascii('/');
        path = split->template get<1>();
    }
    return encoded;
}

auto decode_site_path(ref<str> path) -> Result<String, String> {
    String decoded;
    while (! path.is_empty()) {
        auto split = path.split_once("/"_str);
        auto raw   = split.is_some() ? split->template get<0>() : path;
        auto part  = lihttpto::decode_component(raw);
        if (part.is_err() || part->is_empty() || part->as_str() == "."_str ||
            part->as_str() == ".."_str)
            return Err("site path contains an invalid segment"_Str);
        for (auto byte : part->as_str())
            if (byte < u8(32) || byte == u8(127) || byte == u8('/') || byte == u8('\\') ||
                byte == u8(':'))
                return Err("site path contains an unsafe segment"_Str);
        if (! decoded.is_empty()) decoded.push_ascii('/');
        decoded.push_str(part->as_str());
        if (split.is_none()) break;
        path = split->template get<1>();
        if (path.is_empty()) decoded.push_ascii('/');
    }
    return Ok(rstd::move(decoded));
}

auto site_link(ref<str> value) -> Result<SiteLink, String> {
    if (value.is_empty()) return Err("link must not be empty"_Str);
    for (auto byte : value)
        if (byte < u8(32) || byte == u8(127) || byte == u8('\\'))
            return Err("invalid link character"_Str);
    if (value.starts_with("/"_str)) return Err("site links must be relative to the Book root"_Str);
    auto absolute = lihttpto::Url::parse(value);
    if (absolute.is_ok()) {
        if (absolute->http_origin().is_none()) return Err("site link must use HTTP or HTTPS"_Str);
        if (absolute->userinfo().is_some()) return Err("URL must not contain credentials"_Str);
        return Ok(SiteLink { String::make(absolute->as_ref()), {}, {} });
    }
    auto hash         = value.split_once("#"_str);
    auto without_hash = hash.is_some() ? hash->template get<0>() : value;
    auto query        = without_hash.split_once("?"_str);
    auto path         = query.is_some() ? query->template get<0>() : without_hash;
    auto base         = lihttpto::Url::parse_http("https://litobook.invalid/"_str).unwrap();
    auto resolved     = base.resolve(value);
    if (resolved.is_err())
        return Err(rstd::format("invalid site link '{}': {}", value, resolved.unwrap_err()));
    auto decoded = rstd_try(decode_site_path(path));
    if (decoded.is_empty() || decoded.as_str().ends_with("/"_str))
        decoded.push_str("index.html"_str);
    auto href = encode_site_path(decoded.as_str());
    if (resolved->query().is_some()) {
        href.push_ascii('?');
        href.push_str(*resolved->query());
    }
    Option<String> fragment;
    if (hash.is_some()) {
        auto part = lihttpto::decode_component(hash->template get<1>());
        if (part.is_err()) return Err("invalid link fragment"_Str);
        for (auto byte : part->as_str())
            if (byte < u8(32) || byte == u8(127)) return Err("invalid link fragment character"_Str);
        fragment.insert(rstd::move(part).unwrap());
        href.push_ascii('#');
        href.push_str(lihttpto::encode_component(fragment->as_str()).as_str());
    }
    return Ok(SiteLink { rstd::move(href), Some(rstd::move(decoded)), rstd::move(fragment) });
}

auto relative_site_link(ref<str> output, const SiteLink& link) -> String {
    if (link.output.is_none()) return link.href.clone();
    String value;
    for (auto byte : output)
        if (byte == u8('/')) value.push_str("../"_str);
    value.push_str(link.href.as_str());
    return value;
}

auto canonical_site_url(const SiteBaseUrl& base, ref<str> output) -> Result<String, String> {
    auto resolved = base.url.resolve(encode_site_path(output).as_str());
    if (resolved.is_err())
        return Err(rstd::format("cannot resolve canonical URL: {}", resolved.unwrap_err()));
    return Ok(String::make(resolved->as_ref()));
}
} // namespace lito::site
