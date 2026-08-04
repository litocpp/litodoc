import { build } from "esbuild";
import {
  mkdir,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const dist = join(root, "dist");
const generated = join(root, "..", "src", "frontend", "generated", "default-bundle.inc");
const check = process.argv.includes("--check");

const templates = {
  root: "templates/root.html",
  package: "templates/package.html",
  module: "templates/module.html",
  symbol: "templates/symbol.html",
  source: "templates/source.html",
};
const partials = ["templates/partials/head.html", "templates/partials/foot.html"];
const assets = [
  { path: "static/style.css", "media-type": "text/css" },
  { path: "static/theme-bootstrap.js", "media-type": "text/javascript" },
  { path: "static/app.js", "media-type": "text/javascript" },
];

const textResources = [
  ...Object.values(templates).map((path) => ({ path, "media-type": "text/html" })),
  ...partials.map((path) => ({ path, "media-type": "text/html" })),
  ...assets,
];

await rm(dist, { recursive: true, force: true });
await mkdir(join(dist, "templates", "partials"), { recursive: true });
await mkdir(join(dist, "static"), { recursive: true });

for (const path of [...Object.values(templates), ...partials]) {
  const source = await readFile(join(root, "src", path), "utf8");
  await writeFile(join(dist, path), source);
}

await build({
  entryPoints: [join(root, "src", "app.ts")],
  bundle: true,
  minify: true,
  target: ["es2020"],
  outfile: join(dist, "static", "app.js"),
});
await build({
  entryPoints: [join(root, "src", "theme-bootstrap.ts")],
  bundle: true,
  minify: true,
  target: ["es2020"],
  outfile: join(dist, "static", "theme-bootstrap.js"),
});
await build({
  entryPoints: [join(root, "src", "styles", "style.css")],
  bundle: true,
  minify: true,
  outfile: join(dist, "static", "style.css"),
});

const manifest = {
  format: "tenon-doc-frontend",
  version: 1,
  "data-api": 1,
  "template-api": 1,
  templates,
  partials,
  assets,
};
await writeFile(join(dist, "frontend.json"), `${JSON.stringify(manifest, null, 2)}\n`);

const resources = [
  { path: "frontend.json", "media-type": "application/json" },
  ...textResources,
].sort((left, right) => (left.path < right.path ? -1 : left.path > right.path ? 1 : 0));

let hash = 14695981039346656037n;
const prime = 1099511628211n;
const mask = (1n << 64n) - 1n;
const encoder = new TextEncoder();
const digestPart = (bytes) => {
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * prime) & mask;
  }
  hash ^= 0n;
  hash = (hash * prime) & mask;
};

const loaded = [];
for (const resource of resources) {
  const contents = new Uint8Array(await readFile(join(dist, resource.path)));
  digestPart(encoder.encode(resource.path));
  digestPart(contents);
  loaded.push({ ...resource, contents });
}
const digest = hash.toString(16).padStart(16, "0");

const lines = [];
lines.push(`inline constexpr auto TENON_DOC_DEFAULT_FRONTEND_DIGEST = "${digest}"_str;`);
loaded.forEach((resource, index) => {
  const bytes = [...resource.contents].map((byte) => `0x${byte.toString(16).padStart(2, "0")}`);
  lines.push(`inline constexpr unsigned char TENON_DOC_FRONTEND_DATA_${index}[] = {`);
  for (let begin = 0; begin < bytes.length; begin += 16) {
    lines.push(`    ${bytes.slice(begin, begin + 16).join(", ")},`);
  }
  lines.push("};");
});
lines.push("inline constexpr EmbeddedFrontendResourceLiteral TENON_DOC_DEFAULT_FRONTEND_RESOURCES[] = {");
loaded.forEach((resource, index) => {
  lines.push(
    `    { "${resource.path}", "${resource["media-type"]}", TENON_DOC_FRONTEND_DATA_${index}, sizeof(TENON_DOC_FRONTEND_DATA_${index}) },`,
  );
});
lines.push("};");
lines.push("");
const include = lines.join("\n");

if (check) {
  let existing = "";
  try {
    existing = await readFile(generated, "utf8");
  } catch {
    process.stderr.write("generated frontend bundle is missing\n");
    process.exitCode = 1;
  }
  if (existing !== include) {
    process.stderr.write("generated frontend bundle is stale\n");
    process.exitCode = 1;
  }
} else {
  await mkdir(dirname(generated), { recursive: true });
  await writeFile(generated, include);
  process.stdout.write(`bundled ${loaded.length} resources (${digest})\n`);
}
