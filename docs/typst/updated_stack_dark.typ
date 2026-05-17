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

#let col-entry = rgb("#238636")
#let col-reg = rgb("#21262D")
#let col-border = rgb("#8B949E")
#let col-ptr = rgb("#A5D6FF")
#let col-pad = rgb("#DA3633")

#let slot(bg, label, note: "") = rect(
  width: 180pt,
  height: 40pt,
  fill: bg,
  stroke: 1pt + col-border,
  align(center + horizon)[
    #text(fill: white, weight: "bold", label)
    #if note != "" [ \ #text(fill: col-ptr, size: 10pt, note) ]
  ],
)

#grid(
  columns: (auto, auto, auto),
  column-gutter: 12pt,
  align: (right + horizon, left + horizon, left + horizon),

  text(size: 10pt, fill: col-ptr)[high addr \ #sym.arrow.t],
  slot(col-pad, "thrd_exit", note: "entered implicitly"),
  [],

  [], slot(col-entry, "entry function", note: "popped by ret"), [],

  [], slot(col-reg, "%r15"), [],
  [], slot(col-reg, "%r14"), [],
  [], slot(col-reg, "%r13"), [],
  [], slot(col-reg, "%r12"), [],
  [], slot(col-reg, "%rbp"), [],

  [],
  slot(col-reg, "%rbx", note: "tcb->rsp"),
  text(size: 10pt, fill: col-ptr)[low addr \ #sym.arrow.b],
)
