export module lito.site:publication;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::site {

struct Publication {
  rstd::path::PathBuf output;
  rstd::path::PathBuf staging;
  rstd::path::PathBuf backup;
};

auto publication_sibling(ref<rstd::path::Path> output, ref<str> suffix)
    -> Result<rstd::path::PathBuf, String> {
  auto parent = output.parent();
  auto name = output.file_name();
  if (parent.is_none() || name.is_none() || name->to_str().is_none())
    return Err(rstd::format(
        "doc output '{}' must name a directory below a parent", output));
  auto sibling = String::make("."_str);
  sibling.push_str(*name->to_str());
  sibling.push_str(suffix);
  return Ok(rstd::path::PathBuf::from(*parent).join(
      rstd::path::PathBuf::from(rstd::move(sibling)).as_path()));
}

auto remove_publication_directory(ref<rstd::path::Path> path)
    -> Result<empty, String> {
  auto exists = rstd::fs::exists(path);
  if (exists.is_err())
    return Err(rstd::format("cannot inspect doc directory '{}': {}", path,
                            rstd::move(exists).unwrap_err()));
  if (!*exists)
    return Ok(empty{});
  auto removed = rstd::fs::remove_dir_all(path);
  if (removed.is_err())
    return Err(rstd::format("cannot remove doc directory '{}': {}", path,
                            rstd::move(removed).unwrap_err()));
  return Ok(empty{});
}

auto begin_publication(ref<rstd::path::Path> output, ref<str> owner)
    -> Result<Publication, String> {
  auto staging_suffix = rstd::format(".lito-{}-staging", owner);
  auto backup_suffix = rstd::format(".lito-{}-backup", owner);
  auto staging = publication_sibling(output, staging_suffix.as_str());
  auto backup = publication_sibling(output, backup_suffix.as_str());
  if (staging.is_err())
    return Err(rstd::move(staging).unwrap_err());
  if (backup.is_err())
    return Err(rstd::move(backup).unwrap_err());
  auto cleaned = remove_publication_directory(staging->as_path());
  if (cleaned.is_err())
    return Err(rstd::move(cleaned).unwrap_err());
  cleaned = remove_publication_directory(backup->as_path());
  if (cleaned.is_err())
    return Err(rstd::move(cleaned).unwrap_err());
  auto created = rstd::fs::create_dir_all(staging->as_path());
  if (created.is_err())
    return Err(rstd::format("cannot create doc staging directory '{}': {}",
                            staging->as_path(),
                            rstd::move(created).unwrap_err()));
  return Ok(Publication{
      .output = rstd::path::PathBuf::from(output),
      .staging = rstd::move(staging).unwrap(),
      .backup = rstd::move(backup).unwrap(),
  });
}

auto abort_publication(const Publication &publication)
    -> Result<empty, String> {
  return remove_publication_directory(publication.staging.as_path());
}

auto commit_publication(const Publication &publication)
    -> Result<empty, String> {
  auto output_exists = rstd::fs::exists(publication.output.as_path());
  if (output_exists.is_err())
    return Err(rstd::format("cannot inspect doc output '{}': {}",
                            publication.output.as_path(),
                            rstd::move(output_exists).unwrap_err()));
  if (*output_exists) {
    auto moved = rstd::fs::rename(publication.output.as_path(),
                                  publication.backup.as_path());
    if (moved.is_err())
      return Err(rstd::format("cannot preserve previous doc output '{}': {}",
                              publication.output.as_path(),
                              rstd::move(moved).unwrap_err()));
  }
  auto published = rstd::fs::rename(publication.staging.as_path(),
                                    publication.output.as_path());
  if (published.is_err()) {
    if (*output_exists)
      (void)rstd::fs::rename(publication.backup.as_path(),
                             publication.output.as_path());
    return Err(rstd::format("cannot publish doc output '{}': {}",
                            publication.output.as_path(),
                            rstd::move(published).unwrap_err()));
  }
  if (*output_exists) {
    auto cleaned = remove_publication_directory(publication.backup.as_path());
    if (cleaned.is_err())
      return cleaned;
  }
  return Ok(empty{});
}

} // namespace lito::site
