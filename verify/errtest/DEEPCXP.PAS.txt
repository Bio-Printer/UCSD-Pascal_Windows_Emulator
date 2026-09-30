PROGRAM DEEPCXP;
(* A call to a segment procedure (CXP) made more than 64 frames deep.
   Z80 mode prints OK twice; P-Code mode hangs on the second call. *)
VAR R: INTEGER;
SEGMENT FUNCTION OTHER(X: INTEGER): INTEGER;
BEGIN
  OTHER := X + 1
END;
FUNCTION DOWN(N: INTEGER): INTEGER;
BEGIN
  IF N = 0 THEN DOWN := OTHER(0)
  ELSE DOWN := DOWN(N - 1)
END;
BEGIN
  R := DOWN(60); WRITELN('DEPTH 60: ', R);
  R := DOWN(70); WRITELN('DEPTH 70: ', R)
END.
