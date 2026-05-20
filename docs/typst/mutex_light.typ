#set page(width: auto, height: auto, margin: 16pt, fill: none)
#set text(
  font: "BigBlueTerm437 Nerd Font Mono",
  fill: rgb("#C9D1D9"),
  size: 14pt,
)

#let col-running = rgb("#238636")
#let col-blocked = rgb("#9E6A03")
#let col-ready = rgb("#1F6FEB")
#let col-owner-a = rgb("#8957E5")
#let col-owner-b = rgb("#DB61A2")
#let col-unowned = rgb("#30363D")
#let col-border = rgb("#8B949E")
#let col-ptr = rgb("#24292F")
#let col-dim = rgb("#21262D")

#let cell-w = 80pt
#let cell-h = 36pt

#let state(bg, label) = rect(
  width: cell-w,
  height: cell-h,
  fill: bg,
  stroke: 1pt + col-border,
  align(center + horizon)[
    #text(fill: white, weight: "bold", size: 11pt, label)
  ],
)

#let elide(note) = rect(
  width: cell-w,
  height: cell-h,
  stroke: none,
  align(center + horizon)[
    #text(fill: col-dim, size: 9pt, style: "italic", note)
  ],
)

#let next() = align(center + horizon)[#text(
  fill: col-dim,
  size: 9pt,
  sym.arrow.r,
)]

#let annot(body) = rect(
  width: cell-w,
  height: 30pt,
  stroke: none,
  align(center + horizon)[
    #text(fill: col-ptr, size: 9pt, body)
  ],
)

#let blank = rect(width: cell-w, height: 30pt, stroke: none)[]

#let lane(name) = align(right + horizon)[
  #text(fill: col-ptr, weight: "bold", name)
]

#grid(
  columns: (72pt, cell-w, 1pt, cell-w, 1pt, cell-w, 1pt, cell-w, 1pt, cell-w),
  column-gutter: 8pt,
  row-gutter: 4pt,

  lane("thrd a"),
  state(col-running, "RUNNING"),
  next(),
  state(col-running, "RUNNING"),
  next(),
  state(col-running, "RUNNING"),
  next(),
  state(col-running, "RUNNING"),
  blank,
  elide("yields"),

  lane("thrd b"),
  state(col-ready, "READY"),
  next(),
  state(col-running, "RUNNING"),
  next(),
  state(col-blocked, "BLOCKED"),
  next(),
  state(col-ready, "READY"),
  next(),
  state(col-running, "RUNNING"),

  [],
  blank,
  blank,
  annot[B: trylock \ #sym.arrow.r EBUSY],
  blank,
  annot[B parked on \ wait_queue],
  blank,
  annot[#sym.arrow.t handoff \ from unlock],
  blank,
  blank,

  lane("owner"),
  state(col-owner-a, "A"),
  next(),
  state(col-owner-a, "A"),
  next(),
  state(col-owner-a, "A"),
  next(),
  state(col-owner-b, "B"),
  next(),
  state(col-owner-b, "B"),
)
