/*
  x68funcs.c

  functions for compatibility to X680x0

  Made by Studio Breeze. 1998

 */

#include "sted.h"

#include <locale.h>
#include <langinfo.h>

/* prototype */
void curses_key_wait(void);
void XSTed_key_wait(void);
void curses_midi_wait(void);
void XSTed_midi_wait(void);


/* global variables */

int iswindowopened = 0;
int isGUI          = 0;
int isconsole      = 0;
int sted_saved_argc = 0;
char **sted_saved_argv = NULL;

char disp_text[1024];
int disp_conv      = 0;
char *disp_codeset = "UTF-8";

int issted3        = 0;

char s_sted_default_path[PATHLEN];	/* Jun.01.2003 Toshi Nagata */

/* Display conversion only. RCP / track memo / title stay Shift_JIS in memory. */
static int
sted_codeset_is_utf8(const char *cs)
{
  return cs != NULL &&
    (strstr(cs, "UTF-8") || strstr(cs, "utf-8") ||
     strstr(cs, "UTF8") || strstr(cs, "utf8"));
}

static int
sted_codeset_is_eucjp(const char *cs)
{
  if (cs == NULL)
    return 0;
  return strstr(cs, "EUC-JP") || strstr(cs, "eucJP") ||
    strstr(cs, "eucjp") || strstr(cs, "ujis") || strstr(cs, "UJIS") ||
    strstr(cs, ".euc") || strstr(cs, ".EUC");
}

static void
sted_init_display_codeset(void)
{
  const char *cs = NULL;
  const char *p;

  disp_conv = 0;
  disp_codeset = "UTF-8";

#ifndef ENABLE_NLS
  setlocale(LC_CTYPE, "");
#endif

  cs = nl_langinfo(CODESET);
  if (cs != NULL && cs[0] != '\0') {
    if (sted_codeset_is_utf8(cs)) {
      disp_conv = 1;
      disp_codeset = "UTF-8";
    } else if (sted_codeset_is_eucjp(cs)) {
      disp_conv = 1;
      disp_codeset = "EUC";
    }
  } else {
    p = getenv("LC_ALL");
    if (p == NULL || p[0] == 0)
      p = getenv("LC_CTYPE");
    if (p == NULL || p[0] == 0)
      p = getenv("LANG");
    if (p != NULL) {
      if (sted_codeset_is_utf8(p)) {
	disp_conv = 1;
	disp_codeset = "UTF-8";
      } else if (sted_codeset_is_eucjp(p)) {
	disp_conv = 1;
	disp_codeset = "EUC";
      }
    }
  }

}

char *
sted_to_locale_display(char *s)
{
  if (s == NULL || s[0] == '\0' || disp_conv == 0)
    return s;
  /* X68000 1/4-width overlay (g_print2), not a character encoding. */
  if ((unsigned char)s[0] == 0xf0)
    return s;
  if (sted_looks_like_utf8(s, (int)strlen(s)))
    return s;
  return encoding_convert(s, disp_text, 1024, NULL, disp_codeset);
}

/*
  void strmfe( char *dest, size_t destsize, const char *node, const char *ext )

   destination = node.ext (truncated to destsize)
 */

void strmfe( char *dest, size_t destsize, const char *node, const char *ext ) {

  char temp[PATHLEN];
  const char *slash = strrchr(node, '/');
  const char *last = strrchr(node, '.');

  if ( dest == NULL || destsize == 0 )
    return;

  /* Only a dot in the final path component is an extension.
     STed2.app/Contents/MacOS/STed2 must not become STed2.cnf. */
  if ( last != NULL && (slash == NULL || last > slash) &&
       last != node && last[-1] != '.' && last[1] != '/' && last[1] != '\0' ) {
    size_t prefix = (size_t)(last - node + 1);
    if ( prefix >= PATHLEN )
      prefix = PATHLEN - 1;
    memcpy(temp, node, prefix);
    temp[prefix] = '\0';
    strncat(temp, ext, PATHLEN - strlen(temp) - 1);
  } else {
    snprintf(temp, sizeof(temp), "%s.%s", node, ext);
  }

  strlcpy( dest, temp, destsize );
}

/*
  double val( char *st )

   converts numeric strings to floating pointted number
 */

double val( char *st ) {

/* original "val" function allows following expressions:
    &Hxx
    &Oxx
    &Boo
 */

  return(atof(st));
}

/*
  child ( char *cl )

   execute specified commands in child process
 */

int child( char *cl ) {

  int ret;
  int l;
  char tmp[1024];

  strcpy( tmp, cl );
  l=strlen( tmp );
  for ( ;l>=2;l-- ) {
    if ( tmp[l]==':'  && isalpha(tmp[l-1]) && (tmp[l-2]==' ' || l==0) ) {
      tmp[l--]=' ';
      tmp[l--]=' ';
    }
  }

  ret=system( tmp );
  return ret;
}

/*
 */

void key_wait( void ){

  if ( isconsole ) {
    curses_key_wait();
  } else if ( isGUI ) {
    XSTed_key_wait();
  }

  return;
}

void key_midi_wait( void ){

  if ( isconsole ) {
    curses_midi_wait();
  } else if ( isGUI ) {
    XSTed_midi_wait();
  }
  return;
}

void key_mouse_wait( void ) {

  if ( isconsole ) {
    curses_key_wait();
  } else if ( isGUI ) {
    XSTed_key_wait();
  }
  return;
}

static int
sted_strieq(const char *a, const char *b)
{
  if (a == NULL || b == NULL)
    return 0;
  while (*a != '\0' && *b != '\0') {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb + 32);
    if (ca != cb)
      return 0;
  }
  return *a == *b;
}

#ifdef USE_CURSES
static int
sted_arg_is_curses(const char *a)
{
  return strcmp(a, "-nw") == 0 || strcmp(a, "-NW") == 0 ||
    strcmp(a, "--tty") == 0 || strcmp(a, "--curses") == 0;
}
#endif

static int
sted_arg_is_gui(const char *a)
{
  return strcmp(a, "-g") == 0 || strcmp(a, "-G") == 0 ||
    strcmp(a, "--gui") == 0;
}

#ifdef USE_CURSES
static int
sted_env_is_curses(const char *s)
{
  return sted_strieq(s, "curses") || sted_strieq(s, "tty") ||
    sted_strieq(s, "console") || sted_strieq(s, "nw");
}
#endif

static int
sted_env_is_gui(const char *s)
{
  return sted_strieq(s, "gui");
}

/*
  LaunchServices / Xcode / Cocoa inject arguments such as
  -psn_0_123 and -NSDocumentRevisionsDebugMode YES.
  Those must not reach STed's filename parser.
*/
int
sted_is_host_arg(const char *a)
{
  if (a == NULL || a[0] != '-')
    return 0;
  return strncmp(a, "-psn", 4) == 0 ||
    strncmp(a, "-NS", 3) == 0 ||
    strncmp(a, "-Apple", 6) == 0;
}

static int
sted_host_arg_takes_value(const char *a)
{
  return a != NULL && strncmp(a, "-psn", 4) != 0;
}

/*
  Decide console vs GUI before Cocoa starts.

  Priority: -nw/--tty (USE_CURSES)  -g/--gui  STED_UI  then platform default
  (macOS = GUI, Unix = DISPLAY ? GUI : curses).
  Last explicit argv flag wins. UI flags are removed from argv.
*/
void
sted_select_ui(int *argc, char ***argv)
{
  static int selected = 0;
  char **av;
  int ac, i, o, want;
  const char *ui;

  if (selected)
    return;
  selected = 1;

  want = -1;
  av = (argv != NULL && *argv != NULL) ? *argv : NULL;
  ac = (argc != NULL) ? *argc : 0;

  if (av != NULL && ac > 0) {
    o = 1;
    for (i = 1; i < ac; i++) {
#ifdef USE_CURSES
      if (sted_arg_is_curses(av[i])) {
	want = 1;
	continue;
      }
#endif
      if (sted_arg_is_gui(av[i])) {
	want = 0;
	continue;
      }
      if (sted_is_host_arg(av[i])) {
	if (sted_host_arg_takes_value(av[i]) &&
	    i + 1 < ac && av[i + 1][0] != '-')
	  i++;
	continue;
      }
      av[o++] = av[i];
    }
    av[o] = NULL;
    *argc = o;
  }

  if (want < 0) {
    ui = getenv("STED_UI");
    if (ui != NULL && ui[0] != '\0') {
#ifdef USE_CURSES
      if (sted_env_is_curses(ui))
	want = 1;
      else
#endif
      if (sted_env_is_gui(ui))
	want = 0;
    }
  }

  if (want < 0) {
#ifdef __APPLE__
    want = 0;
#elif defined(USE_CURSES) && defined(__unix__)
    want = (getenv("DISPLAY") == NULL) ? 1 : 0;
#elif defined(USE_CURSES)
    want = 1;
#else
    want = 0;
#endif
  }

  if (want) {
#if USE_CURSES
    isconsole = 1;
    isGUI = 0;
#else
    exit(-1);
#endif
  } else {
#ifdef __APPLE__
#if defined(USE_MACGUI)
    isconsole = 0;
    isGUI = 1;
#else
    exit(-1);
#endif
#else
    isconsole = 0;
    isGUI = 1;
#endif
  }
}

/* system initialize */

void STed_system_initialize( int *argc, char ***argv ) {
    
    char *p, *buf;
    
    /* init parameters */
    
    rcd = NULL;
    /* May.06.2001 NAGANO Daisuke <breeze.nagano@nifty.ne.jp> */
    issted3 = 1;

    sted_select_ui(argc, argv);

  /* get message catalogues */

#ifdef ENABLE_NLS
# ifdef STED_DEFAULT_PATH
  if ( buf!=NULL ) {
    int l;
    if ( (p=getenv("NLSPATH")) == NULL ) {
      l = strlen(STED_DEFAULT_PATH "/nls/%L/%N.cat")+9;
      buf = (char *)malloc(sizeof(char)*l);
      if( snprintf(buf, l, "NLSPATH=%s", STED_DEFAULT_PATH "/nls/%L/%N.cat") > 0 )
	putenv(buf);
    } else {
      if ( strstr(p, STED_DEFAULT_PATH "/nls/%L/%N.cat") == NULL ) {
	l = strlen(p)+strlen(STED_DEFAULT_PATH "/nls/%L/%N.cat")+10;
	buf = (char *)malloc(sizeof(char)*l);
	if ( snprintf(buf, l, "NLSPATH=%s:%s",p, STED_DEFAULT_PATH "/nls/%L/%N.cat") > 0 )
	  putenv(buf);
      }
    }
  }
# endif /* STED_DEFAULT_PATH */

  setlocale(LC_ALL, "");
  bindtextdomain("sted2", STED_LOCALE_DIR);
  textdomain("sted2");

#endif /* HAVE_NLS */

  /* locale codeset for X11/curses; RCP text stays Shift_JIS */
  sted_init_display_codeset();

  return;
}

void
sted_show_main_window(void)
{
#ifdef __APPLE__
  if (isGUI)
    XSTed_present_main_window();
#endif
}
