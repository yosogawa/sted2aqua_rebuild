/*
  x68funcs.h

  prototype dec.s for functions compatible to X680x0

  Made by Studio Breeze. 1998

 */

#ifndef _X68FUNCS_H_
#define _X68FUNCS_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* variables */

extern int iswindowopened;
extern int iocs_graph_home; /* 0 or 1 */

extern char fnc_func[10][32];
extern char fnc_sfunc[10][32];
extern char fnc_key[12][6];
/* ROLL UP, ROLL DOWN, INS, DEL,
   UP, LEFT, RIGHT, DOWN,
   CLR, HELP, HOME, UNDO */

/* misc functions */
extern void keyclr(void);
extern void key_pause(void);

extern void key_wait(void);
/* True when inkey3's 0xff read would return. Modifier make/break does not count. */
extern int key_input_ready(void);

extern void key_mouse_wait(void);


extern void strmfe( char *, size_t, const char *, const char * );
extern void b_striS( char *, int, int );
extern void KNJCTRL( int, int );

/* X-BASIC compatible functions */

extern double val( char * );
extern int child( char * );
extern int dskf( char );
extern int mspos( int *, int * );
extern char *dtasc( unsigned int );
extern char *tmasc( unsigned int );


extern void fill( int, int, int, int, unsigned int );
extern void box( int, int, int, int, unsigned int, unsigned int );
extern int  point( int, int );
extern void line( int, int, int, int, int, int );
extern void symbol( int, int, char *, char, char, char, int, char );

/* original functions */

extern FILE *fopen2( char *, char * );
extern void STed_system_initialize( int *, char *** );
extern void sted_select_ui( int *, char *** );
/* Show the Aqua window once the main screen is drawn. No-op until then. */
extern void XSTed_present_main_window( void );
extern void sted_show_main_window( void );
extern int sted_is_host_arg( const char * );
extern int sted_saved_argc;
extern char **sted_saved_argv;

/* Finder open / window drop. Wake is returned only from com_sel's own inkey3. */
#define STED_FINDER_WAKE (-1)
extern void sted_finder_go_live( void );
extern int sted_finder_take_launch_path( char *dst, size_t n );
extern int sted_finder_receive( const char *path );
extern int sted_finder_path_ok( const char *path );
extern int sted_finder_can_accept( void );
extern int sted_finder_still_safe( void );
extern int sted_finder_request_pending( void );
extern int sted_finder_take_pending( char *dst, size_t n );
extern void sted_finder_set_confirming( int on );
extern void sted_main_idle_set( int on );
extern int sted_main_idle_waiting( void );
extern int XSTed_appkit_request_busy( void );
/* MIDI menu posts this. The emulator thread runs timtra() on its next wait. */
extern void sted_reset_sysex_post( void );
extern void sted_reset_sysex_poll( void );

#endif /* _X68FUNCS_H_ */


#ifdef __cplusplus
}
#endif
