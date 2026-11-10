// bench/table-bench.js: the State Table for bench pairs, in code.
//
// bench.html loads this file instead of table.js, so a bench pair's rows live
// here. Same format as table.js: signals are the numbers sense-serial.js writes
// (each 0 to 1, named in its SIGNALS), states are checked top to bottom and the
// first true row wins.
//
// hold is in seconds: how long a rule must stay true before the state changes.
// It is a tradeoff you choose per row. A short hold feels instant but flickers
// when a reading wobbles near its line; a long hold is calm but slow. These
// example rows use 0.5, 1 and 2, the same as the working reference build.
// Keep "nobody" last, so it catches everything the other rows miss.

window.TABLE = {
  // Don't Leave, the same names as the Nano and SYSTEM_CHARTER.md.
  // near > 0.98 is under about 12 cm; breath > 0.5 is the Nano's line of 18.
  signals: ['present', 'near', 'breath'],

  states: [
    { name: 'devoted',  when: s => s.near > 0.98 && s.breath > 0.5, hold: 3 },
    { name: 'deserter', when: s => s.present > 0.5,                 hold: 2 },
    { name: 'nobody',   when: s => s.present < 0.2,                 hold: 5 },
  ],
};
