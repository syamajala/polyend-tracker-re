// [I] Checks the music theory test cases (tests/theory_cases.inc) against the
// Tonal library, so the expected chords don't come from our own code.
//   cd tools/theory_check && npm ci && node check.mjs
import { readFileSync } from "node:fs";
import { Chord, Key, Note } from "tonal";

const here = new URL(".", import.meta.url);
const text = readFileSync(new URL("../../tests/theory_cases.inc", here), "utf8");

// Our suffixes -> Tonal chord types.
const kType = { "": "M", m: "m", dim: "dim", aug: "aug", 7: "7", maj7: "maj7", m7: "m7", m7b5: "m7b5",
                dim7: "dim7", sus2: "sus2", sus4: "sus4" };
const kNum = ["i", "ii", "iii", "iv", "v", "vi", "vii"];

// A numeral as the file format writes it: accidental, degree 1..7, case, suffix.
function numeral(tok) {
  const m = /^([b#]?)([IViv]+)(.*)$/.exec(tok);
  if (!m) throw new Error(`bad numeral ${tok}`);
  const degree = kNum.indexOf(m[2].toLowerCase()) + 1;
  const upper = m[2] === m[2].toUpperCase();
  let suffix = m[3];
  if (suffix === "") suffix = upper ? "" : "m";
  else if (suffix === "7") suffix = upper ? "7" : "m7";
  return { acc: m[1], degree, suffix };
}

let bad = 0, rows = 0;
const re = /^PROG_CASE\("([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)"\)/gm;
for (const [, tonic, key, line, names, notes] of text.matchAll(re)) {
  ++rows;
  const k = key === "minor" ? Key.minorKey(tonic) : Key.majorKey(tonic);
  const chords = line.split(/\s+/).map(numeral);
  const wantNames = names.split(" "), wantNotes = notes.split("|").map((s) => s.trim());
  chords.forEach((c, i) => {
    // The degree's note in the key; in minor the leading-tone chords on harmonic minor's 7th.
    const lead = key === "minor" && c.degree === 7 && ["dim", "dim7", "m7b5"].includes(c.suffix) && !c.acc;
    const scale = key === "minor" ? (lead ? k.harmonic.scale : k.natural.scale) : k.scale;
    let root = scale[c.degree - 1];
    if (c.acc) root = Note.transpose(root, c.acc === "b" ? "-1A" : "1A");
    const ch = Chord.getChord(kType[c.suffix], root);
    const name = root + c.suffix, got = ch.notes.join(" ");
    if (ch.empty || name !== wantNames[i] || got !== wantNotes[i]) {
      console.log(`FAIL ${tonic} ${key} "${line}" chord ${i + 1}: Tonal ${name} (${got}), case ${wantNames[i]} (${wantNotes[i]})`);
      ++bad;
    }
  });
}
console.log(`${rows} rows, ${bad} mismatches`);
process.exit(bad || !rows ? 1 : 0);
