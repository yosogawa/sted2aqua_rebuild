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
  return nkf_conv(s, disp_text, disp_codeset);
}

/*
  void strmfe( char dest, *char *node , char *extention )

   destination = node.ext
 */

void strmfe( char *dest, const char *node, const char *ext ) {

  char temp[PATHLEN];
  const char *last = strrchr(node, '.');

  /* Prefix is "name." — strncpy() of that length omits the NUL. */
  if ( last != NULL && last != node && last[-1] != '.' && last[1] != '/' ) {
    size_t prefix = (size_t)(last - node + 1);
    if ( prefix >= PATHLEN )
      prefix = PATHLEN - 1;
    memcpy(temp, node, prefix);
    temp[prefix] = '\0';
    strncat(temp, ext, PATHLEN - strlen(temp) - 1);
  } else {
    snprintf(temp, sizeof(temp), "%s.%s", node, ext);
  }

  strcpy( dest, temp );
  return;
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

/* system initialize */

void STed_system_initialize( int *argc, char ***argv ) {
    
    char *p, *buf;
    
    /* init parameters */
    
    rcd = NULL;
    /* May.06.2001 NAGANO Daisuke <breeze.nagano@nifty.ne.jp> */
    issted3 = 1;
    
#ifdef __APPLE__
    // ogawa 2025/12/11 switching local console & GUI
    char *env = getenv("TERM_PROGRAM");
    if ( env != NULL && strcmp(env, "Apple_Terminal") == 0 ) {
#if USE_CURSES
        isconsole = 1;
        isGUI = 0;
#else
        exit(-1);
#endif
    }
    else {
#if defined(USE_MACGUI)
      isconsole = 0;
      isGUI = 1;
#else
      exit(-1);
#endif
    }
#elif __unix__
  if ( getenv("DISPLAY")==NULL ) {
#if USE_CURSES
    isconsole = 1;
    isGUI = 0;
#else /* USE_CURSES */
    exit(-1);
#endif /* USE_CURSES */
  } else {
    isconsole = 0;
    isGUI = 1;
  }
#endif

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
