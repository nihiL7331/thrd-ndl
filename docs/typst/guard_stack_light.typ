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

#let col-usable = rgb("#21262D")
#let col-guard = rgb("#DA3633")
#let col-border = rgb("#D0D7DE")
#let col-ptr = rgb("#24292F")

#let slot(bg, label, note: "", height: 40pt) = rect(
  width: 180pt,
  height: height,
  fill: bg,
  stroke: 1pt + col-border,
  align(center + horizon)[
    #text(fill: white, weight: "bold", label)
    #if note != "" [ \ #text(fill: col-ptr, size: 10pt, note)
    ]
  ],
)

#grid(
  columns: (auto, auto, auto),
  column-gutter: 12pt,
  align: (right + horizon, left + horizon, left + horizon),

  align(right + top, text(size: 10pt, fill: col-ptr)[high addr \ #sym.arrow.t]),
  slot(
    col-usable,
    "usable stack",
    note: sym.arrow.b + " grows down",
    height: 120pt,
  ),
  align(left + top, text(size: 10pt, fill: col-ptr)[#sym.arrow.l top \
    (rsp starts here)]),

  [], slot(col-guard, "GUARD PAGE", note: "PROT_NONE"), [],

  align(right + bottom, text(
    size: 10pt,
    fill: col-ptr,
  )[low addr \ #sym.arrow.b]),
  [],
  align(left + top, text(size: 10pt, fill: col-ptr)[#sym.arrow.l base \
    (ret by os_alloc_stack)]),
)
