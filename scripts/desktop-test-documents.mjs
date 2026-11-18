// Writes the documents that the desktop's reader tests build at test time,
// so the emulator tests can open the same files. Each one comes from the
// desktop's own generator in tests/reader/, with its default settings, and
// is written as the generator made it, with one change: JSZip dates the
// folder entries it adds ("word/") by the clock, so they take the date the
// generator gave its files. The same desktop commit then always gives the
// same bytes.
//
//   node scripts/desktop-test-documents.mjs /path/to/leandoc OUT_DIR
//
// The desktop checkout needs `npm ci` first: the generators use its JSZip.
import { mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [desktop, out] = process.argv.slice(2);
if (!desktop || !out) {
  console.error("usage: node scripts/desktop-test-documents.mjs /path/to/leandoc OUT_DIR");
  process.exit(2);
}

async function generator(file) {
  return import(pathToFileURL(path.resolve(desktop, "tests/reader", file)).href);
}

const { makeFloatsDocx } = await generator("make-floats-docx.mjs");
const { makeColumnsDocx, makeTablesDocx } = await generator("make-columns-docx.mjs");
const { makeNumberedDocx } = await generator("make-numbered-docx.mjs");

// File name, then what the desktop's tests check in it.
const documents = {
  // A two-column paper as Word saves an IEEE paper: a table floated to the
  // top of page 2 across both columns, a figure floated to the foot of page
  // 3, and references at a hanging indent.
  "two-column-floats.docx": () => makeFloatsDocx(),
  // Two-column sections around a full-width table, as pandoc writes a paper.
  "two-column-sections.docx": () => makeColumnsDocx(),
  // A four-column table of long text, and a table that runs over pages.
  "wide-tables.docx": () => makeTablesDocx(),
  // Numbered headings, and a contents line with a dot leader to a
  // right-aligned page number.
  "contents-leaders.docx": () => makeNumberedDocx()
};

// Gives every folder entry the DOS time and date of the first file entry,
// in the central directory and in its local header.
function fixFolderDates(bytes) {
  const data = Buffer.from(bytes);
  let end = data.length - 22;
  while (end >= 0 && data.readUInt32LE(end) !== 0x06054b50) {
    end -= 1;
  }
  if (end < 0) {
    throw new Error("no end of central directory");
  }
  const count = data.readUInt16LE(end + 10);
  let entry = data.readUInt32LE(end + 16);
  const entries = [];
  for (let index = 0; index < count; index += 1) {
    if (data.readUInt32LE(entry) !== 0x02014b50) {
      throw new Error("bad central directory entry");
    }
    const nameLength = data.readUInt16LE(entry + 28);
    const name = data.toString("utf8", entry + 46, entry + 46 + nameLength);
    entries.push({ entry, name, local: data.readUInt32LE(entry + 42) });
    entry += 46 + nameLength + data.readUInt16LE(entry + 30) + data.readUInt16LE(entry + 32);
  }
  const file = entries.find((item) => !item.name.endsWith("/"));
  const time = data.readUInt16LE(file.entry + 12);
  const date = data.readUInt16LE(file.entry + 14);
  for (const item of entries.filter((each) => each.name.endsWith("/"))) {
    data.writeUInt16LE(time, item.entry + 12);
    data.writeUInt16LE(date, item.entry + 14);
    data.writeUInt16LE(time, item.local + 10);
    data.writeUInt16LE(date, item.local + 12);
  }
  return data;
}

mkdirSync(out, { recursive: true });
for (const [name, make] of Object.entries(documents)) {
  const bytes = fixFolderDates((await make()).bytes);
  writeFileSync(path.join(out, name), bytes);
  console.log(`${name} ${bytes.length} bytes`);
}
