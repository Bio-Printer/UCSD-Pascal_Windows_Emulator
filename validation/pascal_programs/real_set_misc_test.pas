PROGRAM REALTEST;
VAR X, Y, Z: REAL; I, J: INTEGER; S: STRING[20]; A, B, C: SET OF 0..40;
BEGIN
X := 1.5; Y := -2.25;
Z := X + Y; WRITELN('X+Y=', Z);
Z := X - Y; WRITELN('X-Y=', Z);
Z := X * Y; WRITELN('X*Y=', Z);
Z := X / Y; WRITELN('X/Y=', Z);
Z := SQR(Y); WRITELN('SQR(Y)=', Z);
Z := ABS(Y); WRITELN('ABS(Y)=', Z);
Z := -X; WRITELN('-X=', Z);
I := 7; Z := I; WRITELN('FLOAT 7=', Z);
Z := I + X; WRITELN('7+X=', Z);
Z := X + I; WRITELN('X+7=', Z);
WRITELN('ROUND(-2.5)=', ROUND(-2.5), ' TRUNC(9.99)=', TRUNC(9.99));
Z := 1.0; FOR J := 1 TO 10 DO Z := Z * 3.0; WRITELN('3^10=', Z);
Z := 1.0 / 3.0; WRITELN('1/3=', Z);
Z := 1.0E30 * 1.0E5; WRITELN('1E35=', Z);
WRITELN('SQR(12)=', SQR(12));
A := [1, 5, 9, 20]; B := [5, 20, 30]; C := A * B;
IF 5 IN C THEN WRITELN('5 IN A*B') ELSE WRITELN('5 MISSING');
IF 9 IN C THEN WRITELN('9 WRONG') ELSE WRITELN('9 NOT IN A*B');
S := 'HELLO'; WRITELN('S[2]=', S[2]);
CASE I OF 1: WRITELN('ONE'); 7: WRITELN('SEVEN') END;
END.
