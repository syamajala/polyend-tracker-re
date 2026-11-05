// [I] #121 Chord progressions in a key, shared by tests/progression_test.cpp and
// tools/theory_check/check.mjs, which checks every row against the Tonal
// music theory library (names spelled by letter, chord notes).
//
// PROG_CASE(tonic, key the line reads as, line, chord names with Auto
//           spelling, chord notes spelled, chords split by |)
PROG_CASE("A", "minor", "iv V", "Dm E", "D F A | E G# B")
PROG_CASE("A", "minor", "iv VII III VI", "Dm G C F", "D F A | G B D | C E G | F A C")
PROG_CASE("A", "minor", "i VI III VII", "Am F C G", "A C E | F A C | C E G | G B D")
PROG_CASE("A", "minor", "iim7b5 V7 i", "Bm7b5 E7 Am", "B D F A | E G# B D | A C E")
PROG_CASE("A", "minor", "V7 i viidim7", "E7 Am G#dim7", "E G# B D | A C E | G# B D F")
PROG_CASE("C", "major", "I V vi IV", "C G Am F", "C E G | G B D | A C E | F A C")
PROG_CASE("C", "major", "ii7 V7 Imaj7", "Dm7 G7 Cmaj7", "D F A C | G B D F | C E G B")
PROG_CASE("C", "major", "I bVII IV #ivdim", "C Bb F F#dim", "C E G | Bb D F | F A C | F# A C")
PROG_CASE("C", "major", "Iaug Isus2 Isus4", "Caug Csus2 Csus4", "C E G# | C D G | C F G")
PROG_CASE("C", "minor", "i bII", "Cm Db", "C Eb G | Db F Ab")
PROG_CASE("E", "minor", "i bII", "Em F", "E G B | F A C")
PROG_CASE("D", "minor", "i iv VI VII", "Dm Gm Bb C", "D F A | G Bb D | Bb D F | C E G")
PROG_CASE("G#", "minor", "i iv V", "G#m C#m D#", "G# B D# | C# E G# | D# F## A#")
PROG_CASE("F#", "major", "Imaj7 IV V7 viidim", "F#maj7 B C#7 E#dim", "F# A# C# E# | B D# F# | C# E# G# B | E# G# B")
PROG_CASE("F#", "major", "iii vi", "A#m D#m", "A# C# E# | D# F# A#")
PROG_CASE("Eb", "major", "I IV V vi", "Eb Ab Bb Cm", "Eb G Bb | Ab C Eb | Bb D F | C Eb G")
PROG_CASE("Db", "major", "ii7 V7 Imaj7", "Ebm7 Ab7 Dbmaj7", "Eb Gb Bb Db | Ab C Eb Gb | Db F Ab C")
