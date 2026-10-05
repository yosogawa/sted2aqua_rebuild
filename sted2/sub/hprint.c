/*
  hprint.c

  functions for compatibility in hprint.s
  "extremely fast print functions"

  H_INIT , H_PRINT , H_PRINT2

  Made by Studio Breeze. 1998

 */

#include "sted.h"

void curses_cls_eol(void);
void XSTed_cls_eol(void);

static int
hprint_len(const char *s)
{
  return (s != NULL && s[0] != '\0') ? (int)strlen(s) : 0;
}

static void
hprint_put(const char *s, int color)
{
  if (s == NULL || s[0] == '\0')
    return;
  B_COLOR(color);
  B_PRINT(s);
}

void
H_INIT(void)
{
  return;
}

void
H_PRINT(char *s1, char *s2, char *s3)
{
  int x, y;

  x = B_LOCATE(-1, 0);
  y = x & 0xffff;
  x = x >> 16;

  B_LOCATE(0, y);
  if (isconsole)
    curses_cls_eol();
  else if (isGUI)
    XSTed_cls_eol();

  B_LOCATE(x, y);
  hprint_put(s1, 3);
  hprint_put(s2, 2);
  hprint_put(s3, 1);
}

void
H_PRINT2(char *s1, char *s2, char *s3)
{
  int x, y, n;

  x = B_LOCATE(-1, 0);
  y = x & 0xffff;
  x = x >> 16;
  B_LOCATE(x, y);

  n = hprint_len(s1) + hprint_len(s2) + hprint_len(s3);
  hprint_put(s1, 3);
  hprint_put(s2, 2);
  hprint_put(s3, 1);
  /* Original hprint.s always wrote two text cells. */
  if (n == 0)
    B_PRINT("  ");
}
