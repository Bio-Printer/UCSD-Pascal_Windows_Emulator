PROGRAM REALCMP;
(* Real comparisons: EQU/NEQ/LES/LEQ/GRT/GEQ with type REAL (2).
   Runs in P-Code mode; with interpreter memory reclaimed the first
   comparison stops the engine ("Z80 code needed"). *)
VAR X, Y: REAL; N: INTEGER;
BEGIN
  X := 1.5; Y := 2.25; N := 0;
  IF X < Y THEN N := N + 1;
  IF X <= Y THEN N := N + 1;
  IF Y > X THEN N := N + 1;
  IF Y >= X THEN N := N + 1;
  IF X <> Y THEN N := N + 1;
  IF X = 1.5 THEN N := N + 1;
  IF NOT (X > Y) THEN N := N + 1;
  WRITELN('REALCMP: ', N, ' of 7 comparisons true')
END.
