#set page(
  width: auto,
  height: auto,
  margin: 16pt,
  fill: none,
)
#set text(
  font: "BigBlueTerm437 Nerd Font Mono",
  fill: rgb("#C9D1D9"),
  size: 14pt,
)

#let col-running = rgb("#238636")
#let col-blocked = rgb("#9E6A03")
#let col-ready = rgb("#1F6FEB")
#let col-dead = rgb("#DA3633")
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

#let next() = align(center + horizon)[#text(
  fill: col-dim,
  size: 9pt,
  sym.arrow.r,
)]

#let elide(note) = rect(
  width: cell-w,
  height: cell-h,
  stroke: none,
  align(center + horizon)[
    #text(fill: col-dim, size: 9pt, style: "italic", note)
  ],
)

#let annot(body) = rect(
  width: cell-w,
  height: 32pt,
  stroke: none,
  align(center + horizon)[
    #text(fill: col-ptr, size: 9pt, body)
  ],
)

#let blank = rect(width: cell-w, height: 32pt, stroke: none)[]

#let lane(name) = align(right + horizon)[
  #text(fill: col-ptr, weight: "bold", name)
]

#grid(
  columns: (60pt, cell-w, 1pt, cell-w, 1pt, cell-w, 1pt, cell-w, 1pt, cell-w),
  column-gutter: 8pt,
  row-gutter: 4pt,

  lane("Joiner"),
  state(col-running, "RUNNING"),
  next(),
  state(col-blocked, "BLOCKED"),
  next(),
  elide("off-CPU"),
  next(),
  state(col-ready, "READY"),
  next(),
  state(col-running, "RUNNING"),

  [],
  blank,
  [],
  annot[parked on target's \ join_queue],
  blank,
  blank,
  blank,
  annot[#sym.arrow.t #h(2pt) thrd_exit],
  blank,
  blank,

  lane("Target"),
  state(col-running, "RUNNING"),
  next(),
  state(col-running, "RUNNING"),
  next(),
  elide("running..."),
  next(),
  state(col-dead, "DEAD"),
  blank,
  elide("reclaimed"),
)
