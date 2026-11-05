// [I] Checks the music theory test cases (tests/theory_cases.inc) against the
// Tonal library, so the expected chords don't come from our own code.
//   cd tools/theory_check && npm ci && node check.mjs
import { readFileSync } from "node:fs";
import { Chord, Key, Note, Scale } from "tonal";

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

// Chord c of a line in a key (tonic, "major" / "minor"): its root and notes.
function chordIn(tonic, key, c) {
  const k = key === "minor" ? Key.minorKey(tonic) : Key.majorKey(tonic);
  // The degree's note in the key; in minor the leading-tone chords on harmonic minor's 7th.
  const lead = key === "minor" && c.degree === 7 && ["dim", "dim7", "m7b5"].includes(c.suffix) && !c.acc;
  const scale = key === "minor" ? (lead ? k.harmonic.scale : k.natural.scale) : k.scale;
  let root = scale[c.degree - 1];
  if (c.acc) root = Note.transpose(root, c.acc === "b" ? "-1A" : "1A");
  const ch = Chord.getChord(kType[c.suffix], root);
  return { name: root + c.suffix, notes: ch.notes, empty: ch.empty };
}

let bad = 0, rows = 0;
const re = /^PROG_CASE\("([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)"\)/gm;
for (const [, tonic, key, line, names, notes] of text.matchAll(re)) {
  ++rows;
  const chords = line.split(/\s+/).map(numeral);
  const wantNames = names.split(" "), wantNotes = notes.split("|").map((s) => s.trim());
  chords.forEach((c, i) => {
    const ch = chordIn(tonic, key, c), got = ch.notes.join(" ");
    if (ch.empty || ch.name !== wantNames[i] || got !== wantNotes[i]) {
      console.log(`FAIL ${tonic} ${key} "${line}" chord ${i + 1}: Tonal ${ch.name} (${got}), case ${wantNames[i]} (${wantNotes[i]})`);
      ++bad;
    }
  });
}

// Progressions in a project scale: parallel key by the scale's 3rd, the
// other kind in the relative key; which chords leave the scale.
const mode = /^MODE_CASE\("([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)"\)/gm;
for (const [, root, scaleName, key, line, names, outside] of text.matchAll(mode)) {
  ++rows;
  const sc = Scale.get(`${root} ${scaleName}`);
  const scaleMinor = sc.intervals.includes("3m") && !sc.intervals.includes("3M");
  const same = (key === "minor") === scaleMinor;
  const tonic = same ? root : scaleMinor ? Key.minorKey(root).relativeMajor : Key.majorKey(root).minorRelative;
  const chroma = new Set(sc.notes.map(Note.chroma));
  const got = [], out = [];
  for (const c of line.split(/\s+/).map(numeral)) {
    const ch = chordIn(tonic, key, c);
    got.push(ch.name);
    if (ch.notes.some((n) => !chroma.has(Note.chroma(n)))) out.push(ch.name);
  }
  if (got.join(" ") !== names || out.join(" ") !== outside) {
    console.log(`FAIL ${root} ${scaleName} "${line}": Tonal ${got.join(" ")} (outside: ${out.join(" ") || "none"}), ` +
                `case ${names} (outside: ${outside || "none"})`);
    ++bad;
  }
}

console.log(`${rows} rows, ${bad} mismatches`);
process.exit(bad || !rows ? 1 : 0);
