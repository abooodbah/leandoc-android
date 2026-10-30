#!/usr/bin/env node
// Checks the LeanDoc for Android site with Node.js built-ins only, in the style
// of the desktop sites' site/validate.mjs:
//
//   node site/validate.mjs
//
// It checks the page structure, the closed-test sign-up order, the links the
// page must keep, local assets, image sizes and alt text, that nothing loads
// from another host, and the writing rules.
import { readFile, readdir, stat } from "node:fs/promises";
import { dirname, extname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const APP = "leandoc";
const NAME = "LeanDoc";
const BASE = `https://abooodbah.github.io/${APP}-android/`;
const REPO = `https://github.com/abooodbah/${APP}-android`;
const GROUP = "https://groups.google.com/g/lean-apps-testers";
const PLAY = `https://play.google.com/apps/testing/io.github.abooodbah.${APP}`;
const POLICY = "https://abooodbah.github.io/privacy/android/";

const siteRoot = dirname(fileURLToPath(import.meta.url));
const failures = [];
const fail = (message) => failures.push(message);
function requireMatch(value, pattern, message) {
  if (!pattern.test(value)) fail(message);
}
const escape = (text) => text.replace(/[.*+?^${}()|[\]\\/]/g, "\\$&");

const html = await readFile(resolve(siteRoot, "index.html"), "utf8");
const css = await readFile(resolve(siteRoot, "styles.css"), "utf8");

// --- Required files ----------------------------------------------------------

const required = [
  ".nojekyll",
  "index.html",
  "styles.css",
  "robots.txt",
  "sitemap.xml",
  "site.webmanifest",
  `assets/${APP}-icon.png`,
  `assets/${APP}-phone-light.png`,
  `assets/${APP}-phone-dark.png`,
  "assets/social-preview.png",
];
for (const file of required) {
  try {
    if (!(await stat(resolve(siteRoot, file))).isFile()) fail(`required path is not a file: ${file}`);
  } catch {
    fail(`missing required site file: ${file}`);
  }
}

// --- Document structure -------------------------------------------------------

requireMatch(html, /^<!doctype html>/i, "index.html must start with an HTML5 doctype");
requireMatch(html, /<html\s+lang="en">/i, "the document language must be declared");
requireMatch(html, /<meta\s+charset="utf-8">/i, "the character set must be declared");
requireMatch(html, /<meta\s+name="viewport"\s+content="width=device-width, initial-scale=1">/i,
  "the viewport meta tag is required");
requireMatch(html, /<meta\s+name="color-scheme"\s+content="light dark">/i, "the page must declare light and dark");
requireMatch(html, /<meta\s+name="description"\s+content="[^"]{40,}"/i, "a meta description is required");
requireMatch(html, new RegExp(`<title>[^<]*${NAME} for Android[^<]*</title>`), `the title must name ${NAME} for Android`);
requireMatch(html, new RegExp(`<link\\s+rel="canonical"\\s+href="${escape(BASE)}">`),
  `the canonical URL must be ${BASE}`);
requireMatch(html, new RegExp(`<meta\\s+property="og:image"\\s+content="${escape(BASE)}assets/social-preview\\.png">`),
  "the Open Graph image must be the social preview");
requireMatch(html, /<main\s+id="main-content">/i, "a named main landmark is required");
requireMatch(html, /class="skip-link"\s+href="#main-content"/i, "a skip link to the main content is required");
const h1 = html.match(/<h1>([^<]*)<\/h1>/g) ?? [];
if (h1.length !== 1) fail(`expected exactly one h1; found ${h1.length}`);
else requireMatch(h1[0], new RegExp(`${NAME} for Android`), `the h1 must name ${NAME} for Android`);

// --- Closed-test sign-up -------------------------------------------------------

const buttons = [...html.matchAll(/<a class="button[^"]*\bbutton-primary\b[^"]*" href="([^"]+)">([^<]+)<\/a>/g)];
if (buttons.length === 0) {
  fail("the page needs a primary button");
} else {
  if (buttons[0][1] !== GROUP) fail(`the first primary button must link the testers group ${GROUP}`);
  if (buttons[0][2].trim() !== "Join the testers group") fail('the first primary button must say "Join the testers group"');
  for (const [, href] of buttons) {
    if (href !== GROUP) fail(`every primary button must link the testers group; found ${href}`);
  }
}
const firstGroup = html.indexOf(`href="${GROUP}"`);
const firstPlay = html.indexOf(`href="${PLAY}"`);
if (firstGroup < 0) fail("the page must link the testers group");
if (firstPlay < 0) fail(`the page must link the ${NAME} tester page ${PLAY}`);
if (firstGroup >= 0 && firstPlay >= 0 && firstGroup > firstPlay) {
  fail("the testers group must come before the Play tester page: joining the group is step 1");
}
for (const match of html.matchAll(/href="(https:\/\/play\.google\.com\/[^"]*)"/g)) {
  if (match[1] !== PLAY) fail(`Play link must be the ${NAME} tester page; found ${match[1]}`);
}
requireMatch(html, /<section class="section[^"]*" id="join" aria-labelledby="join-heading">/,
  "the sign-up steps must keep their #join anchor");
const steps = html.match(/<ol class="steps">([\s\S]*?)<\/ol>/);
if (!steps) {
  fail("the sign-up steps must be an ordered list");
} else {
  const items = steps[1].split("<li>").slice(1);
  if (items.length !== 3) fail(`expected three sign-up steps; found ${items.length}`);
  else {
    if (!items[0].includes(GROUP)) fail("step 1 must link the testers group");
    if (!items[1].includes(PLAY)) fail("step 2 must link the Play tester page");
    if (!/Become a tester/.test(items[1])) fail("step 2 must name the Become a tester button");
    if (!/14 days/.test(items[2])) fail("step 3 must ask testers to keep the app installed for 14 days");
  }
}
requireMatch(html, /Google\s+account\s+you\s+use\s+on\s+your\s+phone/, "step 1 must say to use the Google account on the phone");
requireMatch(html, /try\s+again\s+a\s+day\s+later/, "the page must say to try again a day later when the test is not available");

// --- Links the page must keep ------------------------------------------------------

const mustLink = [
  [POLICY, "the Android privacy policy"],
  [`https://abooodbah.github.io/${APP}/`, "the desktop app site"],
  ["https://abooodbah.github.io/", "the Lean Apps hub"],
  [`${REPO}/releases`, "the GitHub releases"],
  [`${REPO}/issues`, "the GitHub issues for feedback"],
];
for (const [url, label] of mustLink) {
  if (!html.includes(`href="${url}"`)) fail(`the page must link ${label}: ${url}`);
}
requireMatch(html, /no internet permission/i, "the privacy section must say the app has no internet permission");
requireMatch(html, /no permissions at all/i, "the privacy section must say the app asks for no permissions");

// --- Links, assets, images, and scripts -----------------------------------------

const ids = new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));
for (const match of html.matchAll(/href="#([^"]*)"/g)) {
  if (!ids.has(match[1])) fail(`same-page link has no target: #${match[1]}`);
}

async function checkLocal(value, from) {
  if (/^(?:https?:|mailto:|data:|#)/i.test(value)) return;
  const path = resolve(siteRoot, value.split(/[?#]/, 1)[0]);
  if (path !== siteRoot && !path.startsWith(siteRoot + sep)) {
    fail(`${from}: local reference escapes site/: ${value}`);
    return;
  }
  try {
    if (!(await stat(path)).isFile()) fail(`${from}: local reference is not a file: ${value}`);
  } catch {
    fail(`${from}: missing local reference: ${value}`);
  }
}
for (const match of html.matchAll(/(?:href|src)="([^"]+)"/g)) await checkLocal(match[1], "index.html");
for (const match of css.matchAll(/url\("?([^")]+)"?\)/g)) await checkLocal(match[1], "styles.css");
const manifest = JSON.parse(await readFile(resolve(siteRoot, "site.webmanifest"), "utf8"));
for (const icon of manifest.icons ?? []) await checkLocal(icon.src, "site.webmanifest");
if (manifest.name !== `${NAME} for Android`) fail(`site.webmanifest must name ${NAME} for Android`);

// Plain links only: the page loads nothing from any of these hosts.
const allowedHosts = new Set(["https://github.com", "https://abooodbah.github.io",
  // The testers group and the Play tester page for the closed test.
  "https://groups.google.com", "https://play.google.com"]);
for (const match of html.matchAll(/(?:href|src|content)="(https?:\/\/[^"/]+)/g)) {
  if (!allowedHosts.has(match[1])) fail(`unexpected external host: ${match[1]}`);
}
if (/<script\b/i.test(html)) fail("the page must not contain any script");
if (/<(?:iframe|object|embed)\b/i.test(html)) fail("the page must not embed other pages");
if (/\son[a-z]+="/i.test(html)) fail("the page must not use inline event handlers");
for (const match of html.matchAll(/<link\b[^>]*rel="(?:stylesheet|preload|preconnect|dns-prefetch)"[^>]*>/gi)) {
  if (/href="https?:/i.test(match[0])) fail(`the page must not load anything from another host: ${match[0]}`);
}
for (const match of html.matchAll(/\ssrc="(https?:[^"]+)"/gi)) {
  fail(`the page must not load anything from another host: ${match[1]}`);
}
if (/@import/i.test(css) || /url\(\s*["']?https?:/i.test(css)) {
  fail("styles.css must not load anything from another host");
}
if (/@font-face/i.test(css)) fail("styles.css must use system fonts only");

for (const match of html.matchAll(/<img\b([^>]*)>/gi)) {
  const attributes = match[1];
  const alt = attributes.match(/\salt="([^"]*)"/);
  const src = attributes.match(/\ssrc="([^"]+)"/)?.[1] ?? "(no src)";
  if (!alt) {
    fail(`image has no alt attribute: ${src}`);
    continue;
  }
  const decorative = /-icon\.png$/.test(src);
  if (!decorative && alt[1].trim().length < 40) fail(`image needs descriptive alt text: ${src}`);
  const declared = [attributes.match(/\swidth="(\d+)"/)?.[1], attributes.match(/\sheight="(\d+)"/)?.[1]].map(Number);
  if (!declared[0] || !declared[1]) {
    fail(`image needs width and height attributes: ${src}`);
    continue;
  }
  if (src.endsWith(".png") && !decorative) {
    try {
      const png = await readFile(resolve(siteRoot, src));
      const width = png.readUInt32BE(16);
      const height = png.readUInt32BE(20);
      if (declared[0] !== width || declared[1] !== height) {
        fail(`${src} is ${width}x${height} but the page declares ${declared[0]}x${declared[1]}`);
      }
    } catch (error) {
      fail(`${src} could not be inspected: ${error.message}`);
    }
  }
}
async function pngSize(file) {
  const png = await readFile(resolve(siteRoot, file));
  return [png.readUInt32BE(16), png.readUInt32BE(20)];
}
try {
  const [w, h] = await pngSize("assets/social-preview.png");
  if (w !== 1024 || h !== 500) fail("assets/social-preview.png must be the 1024x500 feature graphic");
  const [iw, ih] = await pngSize(`assets/${APP}-icon.png`);
  if (iw !== 192 || ih !== 192) fail(`assets/${APP}-icon.png must be 192x192`);
} catch {
  // Reported above as a missing required file.
}
let total = 0;
// What a phone downloads: the icon and the two screenshots. The social
// preview is only fetched by link previews.
for (const file of [`${APP}-icon.png`, `${APP}-phone-light.png`, `${APP}-phone-dark.png`]) {
  total += (await stat(resolve(siteRoot, "assets", file)).catch(() => ({ size: 0 }))).size;
}
if (total > 250 * 1024) fail(`the page images are ${Math.round(total / 1024)} KB; keep them under 250 KB for phones`);

// --- Search engine files -----------------------------------------------------------

const robots = await readFile(resolve(siteRoot, "robots.txt"), "utf8").catch(() => "");
requireMatch(robots, new RegExp(`^Sitemap: ${escape(BASE)}sitemap\\.xml$`, "m"), "robots.txt must name the sitemap");
const sitemap = await readFile(resolve(siteRoot, "sitemap.xml"), "utf8").catch(() => "");
requireMatch(sitemap, new RegExp(`<loc>${escape(BASE)}</loc>`), "sitemap.xml must list the site URL");
requireMatch(sitemap, /<lastmod>\d{4}-\d{2}-\d{2}<\/lastmod>/, "sitemap.xml must give a lastmod date");

// --- Accessibility and responsive CSS --------------------------------------------------

requireMatch(css, /:focus-visible\s*\{[^}]*outline:/s, "visible focus styles are required");
requireMatch(css, /prefers-reduced-motion:\s*reduce/i, "reduced-motion support is required");
requireMatch(css, /prefers-color-scheme:\s*dark/i, "dark appearance support is required");
requireMatch(css, /@media\s*\(max-width:\s*52rem\)/i, "phone layout rules are required");
requireMatch(css, /@media\s*\(max-width:\s*34rem\)/i, "narrow phone layout rules are required");
requireMatch(css, /body\s*\{[^}]*background:/s, "body needs an explicit background");

// --- Writing rules over every text file in site/ ---------------------------------------

// Built from parts so this file does not contain the phrases it bans.
const join = (...parts) => parts.join("");
const banned = [
  join("ultra", "-lightweight"), join("blazing", " fast"), join("zero", " dependency"),
  join("zero", "-dependency"), join("completely", " secure"), join("no network", " access"),
  "lorem ipsum", "TODO", "TBD",
];
const attribution = [join("Co-Authored", "-By"), join("Claude", "-Session"), join("Generated", " with")];
const textTypes = new Set([".html", ".css", ".mjs", ".md", ".txt", ".xml", ".webmanifest", ".svg"]);
async function walk(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) files.push(...(await walk(path)));
    else files.push(path);
  }
  return files;
}
for (const path of await walk(siteRoot)) {
  if (!textTypes.has(extname(path))) continue;
  const name = relative(siteRoot, path);
  const text = await readFile(path, "utf8");
  if (text.includes("\u2014")) fail(`${name} contains an em dash`);
  if (name === "validate.mjs") continue;
  const lower = text.toLowerCase();
  for (const phrase of banned) {
    const hit = phrase === phrase.toUpperCase() ? text.includes(phrase) : lower.includes(phrase.toLowerCase());
    if (hit) fail(`${name} uses the banned phrase "${phrase}"`);
  }
  for (const marker of attribution) {
    if (lower.includes(marker.toLowerCase())) fail(`${name} contains attribution (${marker})`);
  }
  if (/\bsecure\b/i.test(text)) fail(`${name} claims "secure"; state what the code does instead`);
}

if (failures.length > 0) {
  console.error(`${NAME} for Android site validation failed:`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exitCode = 1;
} else {
  console.log(`${NAME} for Android site validation passed (structure, sign-up order, links, assets, and wording).`);
}
