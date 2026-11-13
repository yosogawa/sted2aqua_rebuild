/* rcdcheck.c

   RCD stay check functions
   compatible to rcdcheck.s

   Made by Studio Breeze. 1998

*/

#include "sted.h"
#include "rcp_transport.h"
#include <signal.h>

#ifndef NO_SHM
# include <sys/ipc.h>
# include <sys/shm.h>
#endif /* NO_SHM */

extern int rcptomid( char *, int, char ** );
#include "rcpconv.h"

#ifdef __APPLE__  /* Jun.08.2003 Toshi Nagata */
#include "CoreMIDIRCPPlayer.h"
#endif

extern int open_midi_port( char * );
extern int get_midi_data( void );
int    step_cluc2(int,int,int);
int    meas_add(int);
int    add_set(int tr, int *add);

#define MAX_BAR      2048            /* max size of measure is 2048 */
int STed_InitMeasureConversion( void );
int STed_MeasureConversion( int track );
static int bar_st[TRK_NUM][MAX_BAR];
static int bar_total_st[TRK_NUM]; /* track length in steps; past this → inactive */

void rcd_check( void );
struct RCD_HEAD *rcd;

char player_name[STED_NAMEBUF];          /* midi player name */
char midi_port_name[STED_NAMEBUF];       /* midi_port device name */
int player_flag=0;               /* bit0 : rcp_capable
				    bit1 : sted controllable
				    bit2 : data from shared memory */

#define RCP_CAPABLE (player_flag&(1<<0))
#define STED_CONTROLLABLE (player_flag&(1<<1))
#define DATA_FROM_FILE (player_flag&(1<<2))
#define B_DATA_FROM_FILE (1<<2)
#define CONTROL_FROM_PIPE (player_flag&(1<<3))

static char *_data_adr, *_tone_adr, *_word_adr, *_gsd_adr, *_smf_adr;
static char tmp_file_name[STED_NAMEBUF];

static int pipe_in_fd, pipe_out_fd, shmid=0;

char *zero_ptr="\0";

int add_set(int, int *);

static void _play_external_player( void );
static void _stop_external_player( void );
static void _exit_external_player( int );
static void request_midi_data( uint8_t * );

static void install_sighandler( void );
static void release_sighandler( void );

static pid_t player_pid=0;

static void null_func ( void ) {};
static void null_func_char ( uint8_t a) {};

static const int signals[]={SIGHUP,SIGINT,SIGQUIT,SIGILL,SIGABRT,SIGFPE,
                            SIGBUS,SIGSEGV,SIGPIPE,SIGALRM,SIGTERM,0};

void Exit( int sig ) {
  static volatile sig_atomic_t exiting;
  int i;

  if ( exiting )
    _exit( 128 + (sig > 0 ? sig : 1) );
  exiting = 1;

  for ( i=0 ; signals[i]!=0 ; i++ )
    signal( signals[i], SIG_DFL );

  /* Fatal faults must not run ObjC / MIDI cleanup (it re-enters Exit). */
  if ( sig==SIGSEGV || sig==SIGBUS || sig==SIGILL ||
       sig==SIGFPE || sig==SIGABRT )
    _exit( 128 + sig );

  _exit_external_player( sig );
}

static void install_sighandler( void ) {
  int i;

  for ( i=0 ; signals[i]!=0 ; i++ ) {
    signal( signals[i], Exit );
  }
  return;
}

static void release_sighandler( void ) {
  int i;

  for ( i=0 ; signals[i]!=0 ; i++ ) {
    signal( signals[i], SIG_DFL );
  }
  return;
}

void rcd_check( void ) {

  int i;
  int alloc_size;

  /* memory allocation */

  alloc_size =
    sizeof(struct RCD_HEAD)+
    sizeof(char)*
    (DATA_ADR_SIZE +
     SMF_ADR_SIZE +
     TONE_ADR_SIZE +
     WORD_ADR_SIZE +
     GSD_ADR_SIZE);

#ifndef NO_SHM
  shmid = shmget( IPC_PRIVATE, alloc_size, IPC_CREAT|0600 );
  if (shmid<0) {
    fprintf(stderr, "sted: cannot allocate shared memory\n");
    exit(1);
  }
  rcd = (struct RCD_HEAD *)shmat(shmid, 0, 0);
#else /* NO_SHM */
  shmid=-1;
  rcd = (struct RCD_HEAD *)malloc( alloc_size );
  if ( rcd == NULL ) {
    fprintf(stderr, "sted: cannot allocate shared memory\n");
    exit(1);
  }
#endif /* NO_SHM */
  _data_adr = (char *)rcd + sizeof(struct RCD_HEAD);
  _smf_adr  = _data_adr + DATA_ADR_SIZE;
  _tone_adr = _smf_adr  + SMF_ADR_SIZE;
  _word_adr = _tone_adr + TONE_ADR_SIZE;
  _gsd_adr  = _word_adr + WORD_ADR_SIZE;


  install_sighandler();

  rcd->title[0] = 'R';
  rcd->title[1] = 'C';
  rcd->title[2] = 'D';
  rcd->title[3] = ' ';

  rcd->version[0] = '3';
  rcd->version[1] = '.';
  rcd->version[2] = '0';
  rcd->version[3] = '1';

  rcd->staymark = 0;

  rcd->data_valid = 1;
  rcd->tone_valid = 0;
  rcd->word_valid = 0;
  rcd->gsd_valid = 0;
  rcd->fmt = 1;

  rcd->data_adr = _data_adr;
  rcd->tone_adr = _tone_adr;
  rcd->word_adr = _word_adr;
  rcd->gsd_adr  = _gsd_adr;
  rcd->smf_adr  = _smf_adr;

  rcd->init  = null_func;              /* RCD initialize ? */
  rcd->setup = null_func;              /* RCD setup routine ? */
  rcd->begin = _play_external_player;  /* start playing RCP data */
  rcd->end = _stop_external_player;    /* stop playing RCP data */

  rcd->md_put = put_midi_data;
  rcd->md_get = get_midi_data;
  rcd->md_put2 = null_func_char;
  rcd->mix_out = request_midi_data;

  rcp_transport_attach();

  rcd->dummy1[0] = 0;

  rcd->tempo = 64;
  rcd->basetempo = 64;

  rcd->totalcount = 0;
  rcd->filename[0] = 0;
  rcd->tonename[0] = 0;

  rcd->dummy2[0] = 0;

  rcd->bufcap = DATA_ADR_SIZE;

  rcd->MIDI_avl = 0;
  rcd->MIDI_req[0] = 0;

  for( i=0;i<TRK_NUM;i++ ) {
    rcd->active[i]=1;
    rcd->bar[i]=0;
    rcd->step[i]=0;
  }
  rcd->stepcount = 0;
}

void rcd_open_device( void ) {
  open_midi_port( midi_port_name );
  return;
}

void _play_external_player( void ) {

  /* play with external midi player */
  /* the data located from _data_adr to _data_adr+rcd->totalcound */

  int fd = -1;
  int i=0;
  pid_t p;

  int argc;
  char *argv[128];
  char name[1024];
  char buf[256];
  char *a,*a0;
  int cont_inter[2], inter_cont[2];

  if ( player_pid != 0 ) return;

#ifdef __APPLE__  /* Jun.08.2003 Toshi Nagata */
  play_coremidi_rcpplayer((uint8_t *)rcd->data_adr, rcd->totalcount);
  return;
#endif

  if ( STed_InitMeasureConversion() != 0 ) return;

  if ( STED_CONTROLLABLE ) {
    pipe( cont_inter );
    pipe( inter_cont );
  }

  p=fork();

  if (p < 0) {
    fprintf(stderr, "sted: cannot fork player process\n");
    return;
  }

  if (p == 0) {
    /* child process */

    if ( STED_CONTROLLABLE ) {
      close( cont_inter[1] );
      close( inter_cont[0] );
      pipe_in_fd = cont_inter[0];
      pipe_out_fd = inter_cont[1];
    }

    release_sighandler();

    if ( CONTROL_FROM_PIPE ) {
      close( 0 );
      dup( pipe_in_fd );
      close( pipe_in_fd );
    }

    if ( !RCP_CAPABLE ) {

      /* convert from RCP to SMF format 0 */

      char *output_smf;
      int size;

      size = rcptomid( (char *)rcd->data_adr, rcd->totalcount, &output_smf );
      if ( size < 0 ) {
	fprintf(stderr, "sted: illegal RCP data\n");
	exit(1);
      }

      if ( DATA_FROM_FILE ) {

	FILE *fp;

	strncpy( tmp_file_name, "/tmp/sted.tmp.XXXXXX", sizeof(tmp_file_name) - 1 );
	tmp_file_name[sizeof(tmp_file_name) - 1] = 0;
	fd=mkstemp( tmp_file_name );
	if ( fd<0 ) {
	  fprintf(stderr, "sted: cannot create temporary SMF file\n");
	  exit(1);
	}
	fp = fdopen( fd, "wb" );
	fwrite( output_smf, sizeof(char), size, fp );
	fclose( fp );
	free( output_smf );

      } else {

#ifndef NO_SHM
	/* copy SMF data to shared memory */
	memcpy( _smf_adr, output_smf, size );
	free( output_smf );
#endif /* NO_SHM */

      }

    } else {

      /* RCP capable */

      if ( DATA_FROM_FILE ) {

	FILE *fp;

	strncpy( tmp_file_name, "/tmp/sted.tmp.XXXXXX", sizeof(tmp_file_name) - 1 );
	tmp_file_name[sizeof(tmp_file_name) - 1] = 0;
	fd=mkstemp( tmp_file_name );
	if ( fd<0 ) {
	  fprintf(stderr, "sted: cannot create temporary RCP file\n");
	  exit(1);
	}
	fp = fdopen( fd, "wb" );
	fwrite( _data_adr, sizeof(char), rcd->totalcount, fp );
	fclose( fp );

      }

    }

    a0 = argv[0] = strtok( player_name, " " );
    argc = 1;
    while( (a = strtok( NULL, " " )) != NULL ) {
      if ( strncmp( a, "%s", 2 )==0 ) {
	argv[argc] = tmp_file_name;
      } else {
	argv[argc] = a;
      }
      argc++;
    }
    argv[argc] = NULL;

    execvp( a0, argv );

    /* reached when fail to exec player */
    fprintf(stderr, "sted: fail to exec player\n");
    exit(1);

  } else {

    /* parent process */

    if ( STED_CONTROLLABLE ) {
      close( cont_inter[0] );
      close( inter_cont[1] );
      pipe_in_fd = inter_cont[0];
      pipe_out_fd = inter_cont[1];
    }

    player_pid = p;

  }

  return;

}


static void _stop_external_player( void ) {

#ifdef __APPLE__  /* Jun.08.2003 Toshi Nagata */
  stop_coremidi_rcpplayer();
  return;
#endif

  if ( player_pid == 0 ) return;

  kill( player_pid, SIGTERM );
  player_pid = 0;

  if ( DATA_FROM_FILE ) {
    remove( tmp_file_name );
  }

  return;
}

static void _exit_external_player( int sig ) {

  _stop_external_player();

#ifndef NO_SHM
  if (shmid>0) {
    shmdt( (char *)rcd );
    shmctl( shmid, IPC_RMID, 0 );
  }
#endif /* NO_SHM */

  exit( 0 );
}

static void request_midi_data( uint8_t *a ) {

  /* send midi data to player directly */

  if (a != NULL) {
    int i = 0;
    while (a[i] != 0xff && i < 16)
      i++;
    if (i > 0)
      put_midi_message(a, i);
  }

  if ( STED_CONTROLLABLE ) {

    /* write to pipe */

    write( pipe_out_fd, a, 3 );

  }
  return;

}

int STed_InitMeasureConversion() {

  int i,j;
  int *measure;
  int m;

  for( i=0;i<TRK_NUM;i++ ) {
    rcd->active[i]=1;
    rcd->bar[i]=0;
    rcd->step[i]=0;
  }
  rcd->stepcount = 0;

  measure = (int *)malloc(sizeof(int)*MAX_BAR);
  for ( i=0 ; i<TRK_NUM ; i++ ) {
    m=add_set( i, measure );
    for ( j=0 ; j<m ; j++ ) {
      bar_st[i][j] = step_cluc2( i, 0, measure[j] );
    }
    bar_st[i][m]=-1;
    bar_total_st[i] = step_cluc2( i, 0, tr_len[i] );
  }
  free(measure);

  return 0;
}

/* converts from total step to measure & relative step */
static int stp_ad( int tr, int meas, int step );
static void fold_cross_bar_repeat( int t, int bar, int off, int abs_t, int *bar_io, int *step_io );
int STed_MeasureConversion( int tr ) {

  int i;
  int c=rcd->stepcount;

  /* if ( rcd->bar[tr]>=0 || rcd->step[tr]>=0 ) return 0; */

#if 0
  if ( (poft&0xffff)==0 ) {
    c-=99;
  } else {
    c+=16;  /* too ad hoc... */
  }
#endif
  /* bar_st[] holds measure starts; -1 is the sentinel after the last.
     Hitting -1 means "we are in the last measure", not "track is done".
     Deactivate only once stepcount passes the track's total steps. */
  i=0;
  while (i<MAX_BAR) {
    if ( bar_st[tr][i]>c ) break;
    if ( bar_st[tr][i]<0 ) break;
    i++;
  }
  i--;

  if ( i<0 || c>=bar_total_st[tr] ) {
    rcd->bar[tr]=0;
    rcd->step[tr]=0;
    rcd->active[tr]=0;
  } else {
    int off=c-bar_st[tr][i];
    int bar=i;
    int step=stp_ad(tr, i, off);

    /* A REPEAT that crosses a bar line stores later laps in the 0xF8
       bar. Point those laps back at the 0xF9 bars. */
    fold_cross_bar_repeat(tr, i, off, c, &bar, &step);
    rcd->active[tr]=1;
    rcd->bar[tr] = bar;
    rcd->step[tr] = step;
  }

  return 0;
}

/* Bar address used by stp_ad. poft shifts playback that starts mid-song. */
static int play_bar_po( int t, int measure )
{
  int tr, po;

  measure+=(poft&0xffff);
  if (( poft&0xffff)==0 ) measure++;
  tr=track;
  track=t;
  po=meas_add(measure);
  track=tr;
  return po;
}

static int match_f9( const uint8_t *bp, int f8 )
{
  int po, depth;

  depth=0;
  for(po=f8-4; po>=0; po-=4){
    if(bp[po]==0xf8){depth++;}
    else if(bp[po]==0xf9){
      if(depth==0){return po;}
      depth--;
    }
  }
  return -1;
}

/* add_set marks whose stored address is in (from, to]. */
static int bar_marks_between( const uint8_t *bp, int from, int to, int len )
{
  int n, j;

  n=0;
  for(j=0; j+4<=to && j+4<=len; j+=4){
    int mark, at;

    mark=(bp[j]>=0xfc || (j+4<len && bp[j+4]==0xfc));
    at=j+4;
    if(mark && at>from && at<=to){n++;}
  }
  return n;
}

/* Step index of the event after 0xF9. Notes earlier in that bar count. */
static int step_at_f9( const uint8_t *bp, int f9, int len )
{
  int start, po, s;

  start=0;
  for(po=0; po+4<=f9 && po+4<=len; po+=4){
    if(bp[po]>=0xfc || (po+4<len && bp[po+4]==0xfc)){start=po+4;}
  }
  s=1;
  for(po=start; po<f9; po+=4){
    if(bp[po]==0xf7){continue;}
    if(bp[po]<0xf8){s++;}
  }
  return s;
}

/* pos is ticks from the event after 0xF9. Inner loops keep counting s. */
static void map_body( int t, int f9, int bar0, int s0, int pos, int *bar, int *step )
{
  uint8_t *bp=trk[t];
  int len=tr_len[t];
  int po=f9+4;
  int lpad[16], lpco[16];
  int lpnn=0;
  int s=s0;
  int b=bar0;
  int guard=0;

  while(pos>0 && po>=0 && po<len && guard<100000){
    uint8_t a=bp[po];
    int ct;

    guard++;
    if(a==0xf7){po+=4;continue;}
    if(a>0xfc){
      if(a==0xfe){break;}
      b++;
      s=1;
      po+=4;
      continue;
    }
    if(a==0xfc){break;}
    if(a<0xf0){
      pos-=bp[po+1];
      if(pos<0){break;}
    }
    if(a<0xf8){s++;}
    po+=4;
    if(a==0xf9 && lpnn<16){
      lpnn++;
      lpad[lpnn]=po;
      lpco[lpnn]=0;
    }
    if(a==0xf8 && lpnn>0){
      lpco[lpnn]+=1;
      ct=bp[po-3];
      if(ct==0 || ct==255){ct=1;}
      if(lpco[lpnn]<ct){
	po=lpad[lpnn];
	continue;
      }
      lpnn--;
    }
  }
  *bar=b;
  *step=s;
}

/* Ticks from the screen walker's start. Bar 0 means "do not skip". */
static int walker_origin( int t )
{
  int tr, po, me;

  me=(poft&0xffff)+1;
  tr=track;
  track=t;
  po=meas_add(me);
  track=tr;
  return po;
}

/* Event index from the walker start, counting every lap. */
static int map_cumulative( int t, int origin, int abs_t )
{
  uint8_t *bp=trk[t];
  int len=tr_len[t];
  int po=origin;
  int s=1;
  int lpad[16], lpco[16];
  int lpnn=0;
  int guard=0;
  int pos=abs_t;

  if(po<0){return 1;}
  while(pos>0 && po>=0 && po<len && guard<100000){
    uint8_t a=bp[po];
    int ct;

    guard++;
    if(a==0xf7){po+=4;continue;}
    if(a==0xfe){break;}
    if(a==0xfc){break;}
    if(a>0xfc){po+=4;continue;}
    if(a<0xf0){
      pos-=bp[po+1];
      if(pos<0){break;}
    }
    if(a<0xf8){s++;}
    po+=4;
    if(a==0xf9 && lpnn<16){
      lpnn++;
      lpad[lpnn]=po;
      lpco[lpnn]=0;
    }
    if(a==0xf8 && lpnn>0){
      lpco[lpnn]+=1;
      ct=bp[po-3];
      if(ct==0 || ct==255){ct=1;}
      if(lpco[lpnn]<ct){
	po=lpad[lpnn];
	continue;
      }
      lpnn--;
    }
  }
  return s;
}

/* Later laps of a REPEAT that crosses a bar live in the 0xF8 bar's
   expanded length. Report the bar and step of this lap instead.
   Notes after that 0xF8 in the same bar are counted from the start,
   because the screen walker replays the whole loop before them.
   A loop closed inside one bar, and a body that contains SAME MEAS,
   keep the stp_ad result. */
static void fold_cross_bar_repeat( int t, int bar, int off, int abs_t, int *bar_io, int *step_io )
{
  uint8_t *bp=trk[t];
  int len=tr_len[t];
  int start, po, f8, f9, ct, body, tail, extra, pos;

  if(off<0){return;}
  start=play_bar_po(t, bar);
  if(start<0 || start>=len){return;}

  f8=-1;
  f9=-1;
  po=start;
  while(po>=0 && po<len){
    uint8_t a=bp[po];
    int m9;

    if(a==0xf7){po+=4;continue;}
    if(a>=0xfc){break;}
    if(a==0xf8){
      m9=match_f9(bp, po);
      if(m9>=0 && m9<start){
	f8=po;
	f9=m9;
      }
    }
    po+=4;
  }
  if(f8<0){return;}
  for(po=f9; po<f8; po+=4){
    if(po+4<=len && (bp[po]==0xfc || bp[po+4]==0xfc)){return;}
  }
  ct=bp[f8+1];
  if(ct==0 || ct==255){ct=1;}
  if(ct<2){return;}
  body=step_cluc2(t, f9, f8-f9);
  tail=step_cluc2(t, start, f8-start);
  if(body<=0 || tail<0){return;}
  extra=(ct-1)*body;
  if(off<tail){return;}
  if(off>=tail+extra){
    *bar_io=0;
    *step_io=map_cumulative(t, walker_origin(t), abs_t);
    return;
  }
  pos=(off-tail)%body;
  map_body(t, f9, bar-bar_marks_between(bp, f9, start, len),
	   step_at_f9(bp, f9, len), pos, bar_io, step_io);
}

static int stp_ad( int t, int measure, int step ) {

  uint8_t a;
  int po;
  int s;
  uint8_t *bp=trk[t];
  int me_ad[2048];
  int max;
  int retad;
  int lpad[16];
  int lpco[16];
  int lpnn;
  int jj, ii, ct, depth;

  po=play_bar_po(t, measure);

  /* Bar length already expands SAME MEAS and REPEAT. Follow that source,
     and repeat loops the same number of times, so the cursor index moves
     for the whole bar. Edit and score do not consume the index on 0xF8
     or 0xF9, so those events rewind without advancing s. */
  max=add_set(t, me_ad);
  retad=0;
  lpnn=0;

  s=1;
  while (step>0) {
  redis:
    if(po<0 || po>=tr_len[t]) break;
    a=bp[po];
    if(a==0xf7){po+=4;goto redis;}
    if(a>=0xfc && retad!=0){
      po=retad;
      retad=0;
      lpnn=0;
      goto redis;
    }
    if(a==0xfc){
      jj=po;
      for(depth=0; depth<2048; depth++){
	if(jj<0 || jj+2>=tr_len[t]) break;
	ct=bp[jj+1]+(bp[jj+2]&3)*256;
	if(ct<0 || ct>=max) break;
	ii=me_ad[ct];
	if(jj==ii) break;
	jj=ii;
	if(bp[ii]!=0xfc){
	  retad=po+4;
	  po=ii;
	  lpnn=0;
	  goto redis;
	}
      }
      break;
    }
    if(a>0xfc) break;

    if ( a<0xf0 ) {
      step-=bp[po+1];
      if (step<0) break;
    }
    if(a<0xf8){s++;}
    po=po+4;

    if(a==0xf9 && lpnn<16){
      lpnn++;
      lpad[lpnn]=po;
      lpco[lpnn]=0;
    }
    if(a==0xf8 && lpnn>0){
      lpco[lpnn]+=1;
      ct=bp[po-3];
      if(ct==0 || ct==255) ct=1;
      if(lpco[lpnn]<ct){
	po=lpad[lpnn];
	goto redis;
      }
      lpnn--;
    }
  }

  return s;
}
