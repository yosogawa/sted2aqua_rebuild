#include "spawncmd.h"

#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum { STED_SPAWN_ARGS = 64 };

static int
meta_byte(unsigned char c)
{
  switch (c) {
  case '|':
  case ';':
  case '&':
  case '<':
  case '>':
  case '`':
  case '\n':
  case '\r':
    return 1;
  default:
    return 0;
  }
}

/* Split on spaces. A token that starts with " runs to the next ".
   Shell operators are refused. The pieces are written back into s. */
static int
split_command(char *s, char **argv, int limit)
{
  int n = 0;
  char *w = s;

  for (;;) {
    char *start;

    while (*s == ' ' || *s == '\t')
      s++;
    if (*s == '\0')
      break;
    if (n >= limit - 1)
      return -1;
    start = w;
    if (*s == '"') {
      s++;
      while (*s != '\0' && *s != '"') {
        if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) {
          s++;
          *w++ = *s++;
          continue;
        }
        *w++ = *s++;
      }
      if (*s != '"')
        return -1;
      s++;
      if (*s != '\0' && *s != ' ' && *s != '\t')
        return -1;
    } else {
      while (*s != '\0' && *s != ' ' && *s != '\t') {
        if (*s == '"' || meta_byte((unsigned char)*s))
          return -1;
        if (*s == '$' && s[1] == '(')
          return -1;
        *w++ = *s++;
      }
    }
    /* The separator is still unread. Step over it before planting the NUL,
       or a token that was copied in place would end the line early. */
    if (*s == ' ' || *s == '\t')
      s++;
    *w++ = '\0';
    argv[n++] = start;
  }
  if (n == 0)
    return -1;
  argv[n] = NULL;
  return n;
}

int
sted_spawn_command(const char *line)
{
  char *copy;
  char *argv[STED_SPAWN_ARGS];
  pid_t pid;
  int status;
  int rc;

  if (line == NULL)
    return -1;
  while (*line == ' ' || *line == '\t')
    line++;
  if (*line == '\0')
    return -1;

  copy = strdup(line);
  if (copy == NULL)
    return -1;
  if (split_command(copy, argv, STED_SPAWN_ARGS) < 0) {
    free(copy);
    return -1;
  }

  rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
  free(copy);
  if (rc != 0)
    return -1;

  do {
    rc = waitpid(pid, &status, 0);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0)
    return -1;
  if (WIFEXITED(status))
    return status;
  if (WIFSIGNALED(status) && status > 0)
    return status;
  return -1;
}
