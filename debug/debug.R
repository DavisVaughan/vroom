devtools::clean_dll()
devtools::load_all()

make_warning <- function() {
  x <- vroom(
    I("a\nx\n"),
    delim = ",",
    col_types = "d",
    altrep = TRUE
  )

  x$a[1]
  #vroom:::force_materialization(x$a)
}

withCallingHandlers(
  expr = make_warning(),
  vroom_parse_issue = function(cnd) {
    rlang::abort("oh no")
    #rlang::warn("hi")
  }
)
