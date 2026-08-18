module;
#include <rstd/macro.hpp>

#ifndef LITO_RESOURCE_DEFAULT_FRONTEND
#error "litobook requires the default-frontend runtime resource"
#endif

export module litobook.executable:tool;

import rstd;
import rstd.argparse;
import lito.book;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::argparse;
using rstd::ffi::OsString;

namespace lito::book::tool {

inline constexpr auto LITOBOOK_VERSION = LITO_PKG_VERSION;
inline constexpr auto LITOBOOK_VERSION_SIZE =
    sizeof(LITO_PKG_VERSION) - sizeof(char);
inline constexpr auto DEFAULT_FRONTEND = LITO_RESOURCE_DEFAULT_FRONTEND;
inline constexpr auto DEFAULT_FRONTEND_SIZE =
    sizeof(LITO_RESOURCE_DEFAULT_FRONTEND) - sizeof(char);

auto litobook_version() noexcept -> ref<str> {
  return ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(LITOBOOK_VERSION),
      usize(LITOBOOK_VERSION_SIZE));
}

auto default_frontend_path() noexcept -> ref<str> {
  return ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(DEFAULT_FRONTEND),
      usize(DEFAULT_FRONTEND_SIZE));
}

struct CliSchema {
  Parser parser;
  CommandKey build;
  CommandKey check;
  ArgKey<String> directory;
  ArgKey<String> output;
  ArgKey<String> frontend;
};

auto make_cli() -> Result<CliSchema, String> {
  auto build = Command::make("build"_str);
  build.about("Build a Markdown Book"_str);
  auto build_key = build.key();
  auto output = build.add_arg(Arg<String>::value("output"_str, string_parser())
                                  .long_name("out"_str)
                                  .value_name("DIRECTORY"_str)
                                  .help("Override the output directory"_str));
  auto frontend =
      build.add_arg(Arg<String>::value("frontend"_str, string_parser())
                        .long_name("frontend"_str)
                        .value_name("DIRECTORY"_str)
                        .help("Use a local frontend bundle"_str));

  auto check = Command::make("check"_str);
  check.about("Validate a Markdown Book without publishing it"_str);
  auto check_key = check.key();

  auto root = Command::make("litobook"_str);
  root.about("Build static Book documentation from Markdown"_str);
  root.version(litobook_version());
  root.require_subcommand();
  auto directory =
      root.add_arg(Arg<String>::value("directory"_str, string_parser())
                       .short_name(u8('C'))
                       .value_name("DIRECTORY"_str)
                       .help("Change the Book working directory"_str)
                       .default_value("."_str)
                       .global());
  root.add_subcommand(rstd::move(build));
  root.add_subcommand(rstd::move(check));
  auto parser = rstd::move(root).build();
  if (parser.is_err())
    return Err(rstd::format("invalid command definition: {}",
                            rstd::move(parser).unwrap_err()));
  return Ok(CliSchema{
      .parser = rstd::move(parser).unwrap(),
      .build = build_key,
      .check = check_key,
      .directory = directory,
      .output = output,
      .frontend = frontend,
  });
}

auto argument(const Matches &matches, const ArgKey<String> &key)
    -> Result<Option<rstd::path::PathBuf>, String> {
  auto value = matches.get_one(key);
  if (value.is_err())
    return Err(rstd::format("cannot read parsed argument: {}",
                            rstd::move(value).unwrap_err()));
  if (value->is_none())
    return Ok(Option<rstd::path::PathBuf>{});
  return Ok(Some(rstd::path::PathBuf::from((***value).as_str())));
}

auto arguments() -> Vec<OsString> {
  auto result = Vec<OsString>::make();
  result.push(OsString::from("litobook"_str));
  auto input = rstd::env::args_os();
  (void)input.next();
  for (auto value = input.next(); value.is_some(); value = input.next())
    result.push(rstd::move(value).unwrap());
  return result;
}

auto fail(ref<str> message, i32 code = i32(1)) -> int {
  rstd::io::eprintln("litobook: {}", message);
  return code.to_primitive();
}

} // namespace lito::book::tool

export namespace lito::book::tool {

auto run() -> int {
  auto schema = make_cli();
  if (schema.is_err())
    return fail(schema.unwrap_err().as_str());
  auto parsed = schema->parser.parse_from(arguments());
  if (parsed.is_err()) {
    auto report = schema->parser.render_error(rstd::move(parsed).unwrap_err());
    if (report.target() == OutputTarget::Tag::Stderr)
      rstd::io::eprint("{}", report.text());
    else
      rstd::io::print("{}", report.text());
    return report.exit_code().to_primitive();
  }
  auto outcome = rstd::move(parsed).unwrap();
  if (outcome.is_Display()) {
    auto request = rstd::move(outcome).as_Display().request;
    if (request.target() == OutputTarget::Tag::Stderr)
      rstd::io::eprint("{}", request.text());
    else
      rstd::io::print("{}", request.text());
    if (request.kind() == DisplayKind::Tag::Version)
      rstd::io::print("\n");
    return request.exit_code().to_primitive();
  }
  auto matches = rstd::move(outcome).as_Parsed().value;
  auto directory = argument(matches, schema->directory);
  if (directory.is_err())
    return fail(directory.unwrap_err().as_str());
  auto default_frontend = rstd::path::PathBuf::from(default_frontend_path());
  if (matches.subcommand_matches(schema->check).is_some()) {
    auto checked = lito::book::check(BookCheckInput{
        .directory = rstd::move(directory).unwrap().unwrap(),
        .default_frontend = rstd::move(default_frontend),
    });
    if (checked.is_err())
      return fail(checked.unwrap_err().as_str());
    rstd::io::println("checked {}: {} pages, {} headings, {} links",
                      checked->project.title.as_str(), checked->pages,
                      checked->headings, checked->links);
    return 0;
  }
  auto child = matches.subcommand_matches(schema->build);
  if (child.is_none())
    return fail("expected build or check"_str, i32(2));
  auto output = argument(**child, schema->output);
  auto frontend = argument(**child, schema->frontend);
  if (output.is_err())
    return fail(output.unwrap_err().as_str());
  if (frontend.is_err())
    return fail(frontend.unwrap_err().as_str());
  auto built = lito::book::build(BookBuildInput{
      .directory = rstd::move(directory).unwrap().unwrap(),
      .output = rstd::move(output).unwrap(),
      .frontend = rstd::move(frontend).unwrap(),
      .default_frontend = rstd::move(default_frontend),
  });
  if (built.is_err())
    return fail(built.unwrap_err().as_str());
  rstd::io::println("generated {} pages at {}", built->pages,
                    built->output.as_path());
  return 0;
}

} // namespace lito::book::tool
