/* deepc.c -- CXP to another segment from more than 64 frames deep */
void say(char *s)
{
    int n;
    n = 0;
    while (s[n])
        n++;
    __cspv(6, 1, s, 0, n, 0, 0);
}

#pragma segment OTHER
int other(int x)
{
    return x + 1;
}

#pragma segment MAIN
int down(int n)
{
    if (n == 0)
        return other(0);        /* CXP to segment OTHER */
    return down(n - 1);
}

int main()
{
    say("depth 60: ");
    if (down(60) == 1) say("ok\r"); else say("bad\r");
    say("depth 70: ");
    if (down(70) == 1) say("ok\r"); else say("bad\r");
    return 0;
}
