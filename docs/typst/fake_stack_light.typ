#set page(
  width: auto,
  height: auto,
  margin: 16pt,
  fill: none,
)

#set text(
  font: "BigBlueTerm437 Nerd Font Mono",
  fill: rgb("#24292F"),
  size: 14pt,
)

#let col-entry = rgb("#1A7F37")
#let col-reg = rgb("#F6F8FA")
#let col-border = rgb("#D0D7DE")
#let col-ptr = rgb("#24292F")

#let slot(bg, label, note: "") = rect(
  width: 180pt,
  height: 40pt,
  fill: bg,
  stroke: 1pt + col-border,
  align(center + horizon)[
    #text(weight: "bold", label)
    #if note != "" [ \ #text(fill: col-ptr, size: 10pt, note) ]
  ],
)

#grid(
  columns: (auto, auto),
  column-gutter: 12pt,
  align: (right + horizon, left + horizon),

  text(size: 10pt, fill: col-ptr)[high addr \
    #sym.arrow.t],
  slot(col-entry, "entry function", note: "popped by ret"),

  [], slot(col-reg, "%r15"),
  [], slot(col-reg, "%r14"),
  [], slot(col-reg, "%r13"),
  [], slot(col-reg, "%r12"),
  [], slot(col-reg, "%rbp"),
  text(size: 10pt, fill: col-ptr)[low addr \ #sym.arrow.b],
  slot(col-reg, "%rbx", note: "tcb->rsp"),
)
