/***************************************************/
/* *.G36 (Recomposer/98 v2.5g) converter / player  */
/***************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtor.h"
#include "rcpfile.h"
#include "g36struct.h"
#include "rcpdef.h"
#include "rcddef.h"
//#include "x68funcs.h"
//#include "gettext_wrapper.h"
#include "../sub/encoding.h"

#define TRACK_LEN 65536

extern void rcd_check( void );
extern struct RCD_HEAD	*rcd;
extern char version[5];

static int tai_compres( int );
static int same_compres( int );
static int add_set( int ,int * );
static int meas_len( int ,int );

static FILE *fpi,*fpo;
static int opt, comment, stop;
static int sameflag;
static int stamp,stampflag;

static uint8_t *rcpptr, *lastptr, *dataptr;
static uint8_t *inptr, *buf;
static uint8_t *note_data, *note;

static int incount;
static int tc;
static int trk_len,trk_max;
static int ptr;

static char g36_name[256], rcp_name[256];

static const char memoinit[] = "                                    ";
static const char ttlinit[] = "                                                                ";

static int midi_ch;
static int tb_mode,tb_max, ratio,rcp_base;
static int step, stepx;
static int stepsum;

static int trk;
static int len;

static int nbuf[16], vbuf[16], gbuf[16], cbuf[16];

static char	rc_path[256];

void	same_cluc(int );

static int alloc_mem( void )
{
  void *work;

  if( ( work = malloc( TRACK_LEN * 4 ) ) == NULL ) {
    fprintf( stderr, "Memory exhausted.\n" );
    return 1;
  }
  note_data = ( uint8_t * )work;
  buf = ( uint8_t * )( work + TRACK_LEN * 2 );

  if( opt == 3 ) {
    if( ( work = malloc( rcd->totalcount ) ) == NULL ) {
      fprintf( stderr, "Memory exhausted.\n" );
      return 1;
    }
    inptr = ( uint8_t * )work;
  }
}

static void move_buf( void )
{
  movedata( ( char * )rcpptr, ( char *)inptr, rcd->totalcount );
}

static void get_data( uchar *s, int bc )
{
  if( opt != 3 ) {
    if( fread( s, 1, bc, fpi ) != bc ) {
      fprintf( stderr, _("Cannot read file.\n") );
      exit(16);
    }
  } else {
    movedata( ( char *)( inptr + incount ), ( char * )s, bc );
    incount += bc;
  }
}

static void put_data( uchar *s, int bc )
{
  if( opt ) {

    if( lastptr <= rcpptr + bc ) {
      fprintf( stderr, _("Buffer exhausted.\n") );
      exit(16);
    }
    movedata( ( char * )s, ( char * )rcpptr, bc );
    rcpptr += bc;

  } else {

    if( fwrite( s, 1, bc, fpo ) != bc ) {
      fprintf( stderr, _("Cannot write file.\n") );
      fclose(fpo);unlink((char *)rcp_name);
      exit(16);
    }

  }
}

static void strncpy0( uchar *dst, uchar *src, int n )
{
  int i;

  for( i = 0; i < n; i++ ) {
    dst[i] = src[i];
  }
  dst[n] = 0;
}

static void strpaste( uchar *dst, uchar *src )
{
  while( *src ) {
    *dst++ = *src++;
  }
}

static void strnpaste( uchar *dst, uchar *src, int n )
{
  while( n-- ) {
    *dst++ = *src++;
  }
}

static void pickfile( char *dst, char *src )
{
  char *p, *p1;

  p = src;

  if( p <= ( p1 = ( char * )jstrrchr( ( uchar * )src, '\\' ) ) ) {
    p = p1 + 1;
  }
  if( p <= ( p1 = ( char * )jstrrchr( ( uchar * )src, '/' ) ) ) {
    p = p1 + 1;
  }
  if( p <= ( p1 = ( char * )jstrrchr( ( uchar * )src, ':' ) ) ) {
    p = p1 + 1;
  }

  strcpy( dst, p );

}

static char *extptr( char *s )
{
  char *p;

  for( p = s + strlen( s ) - 1; p != s; p-- ) {
    if( *p == '\\' || *p == '/' || *p == ':' ) {
      return( 0 );
    }
    if( *p == '.' ) {
      return( p );
    }
  }
  return( 0 );
}

static void entry( uchar x0, uchar x1, uchar x2, uchar x3 )
{

  note[ len++ ] = x0;
  note[ len++ ] = x1;
  note[ len++ ] = x2;
  note[ len++ ] = x3;

  if( len >= 65492+65536 ) {
    fprintf( stderr, _("Track buffer filled.\n") );
    exit(16);
  }

}

static int get_delta4( int p )
{
  register int val;

  val = ((buf[p+3]*256+buf[p+2])*256+buf[p+1])*256+buf[p];

  return( val );
}
static int get_delta2( int p )
{
  register int val;

  val = buf[p+1]*256+buf[p];

  return( val );
}

/*
void proc_beat( int high, int low )
{
	RCP_HEAD.Beathigh = high;
	RCP_HEAD.Beatlow = low;

	barlen = (RCP_HEAD.Timebase+RCP_HEAD.Timebaseh*256) * 4 * high / low;
}
*/
static void flash( int dt )
{
  int dx, i, j;

  while( dt > 0 ) {

    if( dt <= 0xf0 ) {
      dx = dt;
    } else {
      dx = 0xf0;
    }

    for( i = 0; i < 16; i++ ) {
      if( gbuf[i] ) {

	if(midi_ch!=cbuf[i]){
	  if( midi_ch == -1 ) {
	    TRACK.MIDI_CH = cbuf[i];
	  }else{
	    entry( 0xe6, 0x00, cbuf[i]+1, 0x00 );
	  }
	  midi_ch=cbuf[i];
	}

	/*if( gbuf[i] <= dx ) {*/
	if( gbuf[i] <= 0xf0 ) {
	  entry( nbuf[i], 0, gbuf[i], vbuf[i] );
	  for( j = i; j < 15; j++ ) {
	    nbuf[j] = nbuf[j+1];
	    vbuf[j] = vbuf[j+1];
	    gbuf[j] = gbuf[j+1];
	    cbuf[j] = cbuf[j+1];
	  }
	  nbuf[15] = vbuf[15] = gbuf[15] = cbuf[15] = 0;
	  i--;
	} else {
	  entry( nbuf[i], 0, dx + 1, vbuf[i] );
	  gbuf[i] -= dx;
	}
      } else {
	break;
      }
    }

    if( note[ len - 4 ] !=0xde &&
	note[ len - 4 ] < 0xf0 && ( note[ len - 3 ] + dx ) <= 0xf0 ) {
      note[ len - 3 ] += dx ;
    } else {
      if(note[len-4]==0xf7){
	int ll=len-4;
	while(note[ll]==0xf7){ll-=4;}
	if(note[ll]==0x98 && ( note[ ll + 1 ] + dx ) <= 0xf0 ) {
	  note[ ll + 1 ] += dx ;
	} else {
	  entry( 0x00, dx, 0, 0 );
	}
      }else{
	entry( 0x00, dx, 0, 0 );
      }
    }

    dt -= dx;
  }

  stepsum = 0;
}

static void proc_dummy( int dt )
{
  if( !tb_mode ) {
    step += dt;
    dt = ( step * rcp_base / ratio ) - stepx;
    stepx += dt;
  }

  stepsum += dt;
}

static void ent_buf( int nt, int vl, int gt )
{
  int i;

  /*
    if( !comment ) {
    printf( "<nt=%d vl=%d gt=%d>\n", nt, vl, gt );
    }
    */

  if( gt <= 0xf0 ) {
    entry( nt, 0, gt,vl );return;
  } else {
    entry( nt, 0, 255,vl );
  }

  for( i = 0; i < 16; i++ ) {
    if( !gbuf[i] || (nbuf[i] == nt && cbuf[i] == midi_ch) ) {
      nbuf[i] = nt;
      vbuf[i] = vl;
      gbuf[i] = gt;
      cbuf[i] = midi_ch;
      return;
    }
  }

  for( i = 0; i < 15; i++ ) {
    nbuf[i] = nbuf[i+1];
    vbuf[i] = vbuf[i+1];
    gbuf[i] = gbuf[i+1];
    cbuf[i] = cbuf[i+1];
  }

  nbuf[15] = nt;
  vbuf[15] = vl;
  gbuf[15] = gt;
  cbuf[15] = midi_ch;

  /*
    fprintf( stderr, "ノートオーバフロー\n" );	exit(18);
    */
}

static void note_off( int p )
{
  /* do nothing */
}

static void note_on( int p )
{
  int gt;

  if( buf[p+1] ) {
    if( !( gt = ( buf[p+4]+buf[p+5]*256) ) ) {
      return;
    }
    if( !tb_mode ) {
      /*			if( !( gt = gt * rcp_base / ratio ) ) {*/
      if( !( gt = (gt * rcp_base+(ratio-1)) / ratio ) ) {
	gt = 1;
      }
    }
    flash( stepsum );
    ent_buf( buf[p], buf[p+1], gt );
  }
}

static void proc_next( void )
{
  int code, length;
  int c1,c2,c3,c4,c5,cc,i;
  uchar	text[4096];

  ptr = 0;
  length = trk_len;
  step = stepx = 0;

  while(ptr<length) {
    code = buf[ptr];
    c1 = buf[ptr+1];c2 = buf[ptr+2];c3 = buf[ptr+3];c4 = buf[ptr+4];
    c5 = buf[ptr+5];

    if( code < 0x80 ){
      note_on(ptr);
      proc_dummy( get_delta2(ptr+2) );
      ptr += 6;
    }else{
      flash(stepsum);

      if( code < 0xf0 ){
	proc_dummy( get_delta2(ptr+2) );
      }

      switch( code ) {
      case 0x90:/* UserExc */
      case 0x91:
      case 0x92:
      case 0x93:
      case 0x94:
      case 0x95:
      case 0x96:
      case 0x97:
	entry( code, 0x00, c4, c1 ); ptr += 6; break;

      case 0x98:/* Tr.Exclu */
	cc=0;
	ptr += 6;
	entry( code, 0x00, c4, c1 );

	while( buf[ptr] == 0xf7 ){
	  c1 = buf[ptr+1];c2 = buf[ptr+2];c3 = buf[ptr+3];
	  c4 = buf[ptr+4];c5 = buf[ptr+5];
	  ptr += 6;

	  text[cc++]=c1;text[cc++]=c2;text[cc++]=c3;
	  text[cc++]=c4;text[cc++]=c5;
	}

	text[cc]=0xf7;
	for(i=0;i<cc;i+=2){
	  entry( 0xf7, 0x00, text[i], text[i+1] );
	  if( text[i]==0xf7 || text[i+1]==0xf7 ){break;}
	}
	break;

      case 0xf6:/* comment */
	cc=0;
	text[cc++]=c1;text[cc++]=c2;text[cc++]=c3;
	text[cc++]=c4;text[cc++]=c5;
	ptr += 6;

	while( buf[ptr] == 0xf7 ){
	  c1 = buf[ptr+1];c2 = buf[ptr+2];c3 = buf[ptr+3];
	  c4 = buf[ptr+4];c5 = buf[ptr+5];
	  ptr += 6;

	  text[cc++]=c1;text[cc++]=c2;text[cc++]=c3;
	  text[cc++]=c4;text[cc++]=c5;
	}
	text[cc]=0;/*if(cc>20){cc=20;}*/
	entry( code, 0x00, text[0], text[1] );
	for(i=2;i<cc;i+=2){
	  if( text[i]==0 ){break;}
	  entry( 0xf7, 0x00, text[i], text[i+1] );
	  if( text[i+1]==0 ){break;}
	}
	break;

      case 0xdd:/* Rol.Base */
      case 0xde:/* Rol.Para */;
      case 0xdf:/* Rol.Dev# */
      case 0xe2:/* BankProg */
      case 0xe7:/* TEMPO */
      case 0xeb:/* CONTROL */
      case 0xed:/* AFTER K. */
      case 0xee:/* PITCH */

      case 0xc0:/* DX7FUNC */
      case 0xc1:/* DX.PARA */
      case 0xc2:/* DX.PREF */
      case 0xc3:/* TX.FUNC */
      case 0xc5:/* FB-01 P */
      case 0xc6:/* FB-01 S */
      case 0xc7:/* TX81Z V */
      case 0xc8:/* TX81Z A */
      case 0xc9:/* TX81Z P */
      case 0xca:/* TX81Z S */
      case 0xcb:/* TX81Z E */
      case 0xcc:/* DX7-2 R */
      case 0xcd:/* DX7-2 A */
      case 0xce:/* DX7-2 P */
      case 0xcf:/* TX802 P */
      case 0xdc:/* MKS-7   */
      case 0xe5:/* KeyScan */
      case 0xef:/* CMU-800 */

	entry( code, 0x00, c4, c1 );  ptr += 6; break;

      case 0xe1:
	entry( 0xeb, 0x00, 0x20,c4 );
	entry( 0xec, 0x00, c1, c1 ); ptr += 6; break;

      case 0xec:/* PROGRAM */
      case 0xea:/* AFTER C. */
      case 0xe6:/* MIDI CH. */
	midi_ch=c4;
	entry( code, 0x00, c4, 0x00 );  ptr += 6; break;

      case 0xf5:/* Music Key */
      case 0xf9:/* repeat[ */
      case 0xf8:/* repeat] */
	entry( code, c2, 0x00, 0x00 );  ptr += 6; break;

      case 0xfc:/*same meas*/
	entry( code, c2, c3&3, 0x00 );  ptr += 6; break;

      case 0xfd:/*meas end*/
	entry( code, 0x00, 0x00, 0x00 );  ptr += 6; break;
      case 0xfe:/*track end*/
	return;  ptr += 6; break;

      default :
	ptr += 6;
	if( !comment ) {
	  printf( "x0 = %02x  x1 = %02x  x2 = %02x  x3 = %02x\n", code,c1,c2,c3 );
	}
      }
    }
  }
}


static void proc_header( void )
{
  uchar ttl[128];
  int	division;

  get_data( ( uchar * )&G36_HEAD, 0xc98 );

  if( strncmp( ( char * )G36_HEAD.mark,"COME ON MUSIC RECOMPOSER RCP3.0",32-1 ) ) {
    fprintf( stderr, 
	     _("This file format is not a Recomposer/98 V2.5G (%s).\n"),
	     g36_name );
    fclose( fpi );
    exit(16);
  }

  if( opt == 0 ) {
    if(G36_HEAD.trkmax>18){
      fprintf( stderr, _("This file is over 18 tracks. It is converted into .r36 file.\n") );
      strmfe(rcp_name,rcp_name,"r36");
    }

    if( !( fpo = fopen( rcp_name, "wb" ) ) ) {
      fprintf( stderr, _("Cannot create file ( %s ).\n"), rcp_name );
      exit(16);
    }
  }

  if( opt ) {
    pickfile( rcd->filename, g36_name );
    rcd->tonename[0] = 0;
  }

  division = G36_HEAD.Timebaseh*256 + G36_HEAD.Timebase;

  strncpy0( ttl, G36_HEAD.title, 64 );
  ttl[64]=0;

  if( !comment ) {
    printf( "Title = %s\n\n", ttl );
    printf( "Version 3.00 File\n" );
    printf( "division =%4d\n", division );
  }
  strpaste( RCP_HEAD.title, ttl );

  strpaste( RCP_HEAD.memo +  28, ( uchar * )"  This file is converted " );
  strpaste( RCP_HEAD.memo +  84, ( uchar * )"  from RecomposerV2.5G   " );
  strpaste( RCP_HEAD.memo + 140, ( uchar * )"  by   GtoR.x (v1.00)により" );
  strpaste( RCP_HEAD.memo + 151+5, ( uchar * )version );
  strpaste( RCP_HEAD.memo + 196, ( uchar * )"                         " );
  strpaste( RCP_HEAD.memo + 252, ( uchar * )"  Copyright 1996 TURBO" );

  RCP_HEAD.Tempo = G36_HEAD.Tempo;
  RCP_HEAD.Timebase = G36_HEAD.Timebase;
  RCP_HEAD.Timebaseh = G36_HEAD.Timebaseh;
  RCP_HEAD.Beathigh = G36_HEAD.Beathigh;
  RCP_HEAD.Beatlow = G36_HEAD.Beatlow;
  RCP_HEAD.Key = G36_HEAD.Key;
  RCP_HEAD.Playbias = G36_HEAD.Playbias;
  /*	RCP_HEAD.trkmax = G36_HEAD.trkmax;*/

  if(G36_HEAD.trkmax>36){
    G36_HEAD.trkmax=36;
  }

  if(G36_HEAD.trkmax>18){
    RCP_HEAD.trkmax = 36;
  }else{
    RCP_HEAD.trkmax = 18;
  }

  trk_max=G36_HEAD.trkmax;

  /*
    for(i=0;i<12;i++){
    memcpy(&RCP_HEAD.memo+(i*30),&G36_HEAD.memo+(i*32),30);
    }
    */

  memcpy(&RCP_HEAD.Rhythm,&G36_HEAD.Rhythm,16*32 );
  memcpy(&RCP_HEAD.Exclusive,&G36_HEAD.Exclusive,48*8 );


  if( !tb_mode || division > tb_max ) {
    rcp_base=48;if( tb_mode ) {rcp_base=tb_max;}

    RCP_HEAD.Timebase = rcp_base;
    RCP_HEAD.Timebaseh = rcp_base>>8;
    tb_mode = 0;
    ratio = division;

    if( !comment ) {
      if( division > tb_max ) {
	printf( _("Timebase is converted to %d.\n\n"),tb_max );
      }
    }
  } else {
    RCP_HEAD.Timebase = division;
    RCP_HEAD.Timebaseh = division>>8;

    if( !comment ) {
      printf( _("Timebase is reserved.\n\n") );
    }
  }

  /*	proc_beat( RCP_HEAD.Beathigh ,RCP_HEAD.Beatlow );*/


  put_data( RCP_HEAD.mark, 0x0586 );

  tc = 0x0586;

}

static void track_end( void )
{
  flash( stepsum );

  if(trk){
    while(len>0) {
      if( ( note[ len - 4 ] == 0x00 && note[ len - 2 ] == 0x00 ) ||
	  note[ len - 4 ] == 0xfd ) {
	len -= 4;
      } else {
	break;
      }
    }
  }

  note[ len ] = 0xfe;
  note[ len + 1 ] = 0x00;
  note[ len + 2 ] = 0x00;
  note[ len + 3 ] = 0x00;
  len += 4;

  same_cluc(len);

  len=tai_compres(len);
  if(sameflag){len=same_compres(len);}

  if( len > 65488 ) {
    fprintf( stderr, _("Track buffer filled.\n") );
    /*		len=65488-4;
		entry( 0xfe, 0, 0, 0 );*/
  }

  TRACK.len_high = ( len + 44 ) >> 8;
  TRACK.len_low = (( len + 44 ) >> 16 ) + (( len + 44 ) & 0xff);

}

static void proc_track( void )
{
  int i;
  uchar work[128];

  get_data( ( uchar * )&G36_TRACK, 46 );

  strncpy0( work, G36_TRACK.Memo, 36 );

  work[36] = 0;
  strpaste( TRACK.Memo, work );

  trk_len=(((G36_TRACK.len_high3*256+G36_TRACK.len_high2)*256+
	    G36_TRACK.len_high)*256+G36_TRACK.len_low)-46;

  if( !comment ) {
    printf( "Track= %2d  [%s]\n", trk, work );
  }

  len = 0;
  stepsum = 0;

  midi_ch = TRACK.MIDI_CH;

  for( i = 0; i < 16 ; i++ ) {
    note_data[i] = 0xff;
    nbuf[i] = vbuf[i] = gbuf[i] = cbuf[i] = 0;
  }
  note = note_data + 16;

  TRACK.track_no = G36_TRACK.track_no;
  TRACK.rhythm_flag = G36_TRACK.rhythm_flag;
  TRACK.MIDI_CH = G36_TRACK.MIDI_CH;
  TRACK.Play_bias = G36_TRACK.Play_bias;
  TRACK.ST_offset = G36_TRACK.ST_offset;
  TRACK.Mode = G36_TRACK.Mode;

  get_data( buf,trk_len );
  ptr=0;

  proc_next();

  track_end();

  put_data( ( uchar * )&TRACK.len_low, 44 );
  put_data( ( uchar * )note, len );
  tc += len + 44;
}

static void dummy_track( void )
{
  len = 0;

  TRACK.track_no = trk;
  TRACK.rhythm_flag = 0x00;
  TRACK.MIDI_CH = 255;
  TRACK.Play_bias = 0x00;
  TRACK.ST_offset = 0x00;
  TRACK.Mode = 0x00;
  strpaste( TRACK.Memo, ( uchar * )memoinit );

  if( !comment ) {
    printf( "Track= %2d  dummy\n", trk );
  }

  track_end();

  put_data( ( uchar * )&TRACK.len_low, 44 );
  put_data( ( uchar * )note, len );
  tc += len + 44;
}

static void set_valid( void )
{
  rcd->fmt = 1;
  if(trk_max>18) {rcd->fmt = 2;}
  rcd->data_valid = 1;
  rcd->tone_valid = 0;
  rcd->gsd_valid = 0;
  rcd->word_valid = 0;

}

#if 0
int exe( char *s1, char *s2, char *s3, char *s4, char *s5, char *s6 )
{
  /*
  int rc;

  char	tmp0[128];

  strcpy(tmp0,rc_path);strcat(tmp0,s1);
  if ( ( rc = execl( tmp0, s2, s3, s4, s5, s6 ) ) > 15 ) {
    if(rc_path[0]==0 || rc>0){
      fprintf( stderr, _("RCP が起動できません。\n") );
    }else{
      if ( ( rc = execl( s1, s2, s3, s4, s5, s6 ) ) > 15 ) {
	fprintf( stderr, _("RCP が起動できません。\n") );
      }
    }
  }
  return( rc );
  */
}

void usage( void )
{
  B_PRINT(_(
	    "Usage: GtoR [-option...] <File Name>\n"
	    "                         If extension omitted, *.g36 is added\n"
	    "Option:  C : File conversion (*.g36 into *.rcp)\n"
	    "	      T : Specifies max value of Timebase\n"
	    "	      S : Compress with SAME MEAS\n"
	    "	      N : Suppress all messages\n" ));
  exit(20);
}

void option( char *s )
{
  char c;

  opt = 0;
  comment = 0;
  stop = 0;
  stampflag = 1;
  sameflag = 0;
  tb_mode = 0;
  tb_max = 48;

  if( !*s ) {
    usage();
  }

  while( (c = *s++) ) {
    switch( c ) {
    case 'C' :
    case 'c' :
      opt = 0;
      break;
    case 'B' :
    case 'b' :
      opt = 1;
      stop = 0;
      break;
    case 'P' :
    case 'p' :
      opt = 2;
      stop = 0;
      break;
    case 'K' :
    case 'k'  :
      opt = 2;
      stop = 1;
      break;
    case 'R' :
    case 'r'  :
      opt = 3;
      break;
    case 'X' :
    case 'x' :
      opt = 4;
      stop = 0;
      break;
    case 'S' :
    case 's' :
      sameflag = 1;
      break;
    case 'T' :
    case 't' :
      tb_mode = 1;
      tb_max = 0;
      while(*s>='0' && *s<='9'){
	tb_max=tb_max*10+((*s++)-'0');
      }
      if(tb_max<48){tb_max=240;}
      break;

    case 'N' :
    case 'n' :
      comment = 1;
      break;
      /*
	case 'M' :
	case 'm' :
	stampflag = 1;
	break;
	*/
    case ' ' :
      opt = 0;
      break;
    default :
      usage();
      break;
    }
  }

}
#endif

/*******************************************************************************
 *
 *
 *
 *******************************************************************************
 **/
int gtor( uint8_t *g36_data, size_t g36_size, uint8_t *rcp_buf )
{
}

#if 0
static int main( int argc, char **argv )
{
  char s[260];
  char file_name[256], *p;
  int rc;

#ifdef ENABLE_NLS
  setlocale( LC_ALL, "" );
  bindtextdomain( PACKAGE, LOCALEDIR );
  textdomain( PACKAGE );
#endif /* ENABLE_NLS */

  fprintf( stderr,
	   "X68k GtoR: Recomposer/98 V2.5G file converter v%s Copyright 1996 TURBO\n",
	   version );

  rc_path[0]=0;
  if( !GETENV( ( uchar * )"RC_PATH", NULL, ( uchar * )s ) ) {
    int	i;
    strcpy(rc_path,s); 
    i=strlen(rc_path)-1;
    if(i>0 && rc_path[i]!='\\' && rc_path[i]!='/'){strcat(rc_path,"\\");}
  }

  file_name[0]=0;
  switch( argc ) {
  case 2 :
    if( argv[1][0] == '-' ) {
      option( &argv[1][1] );
    } else {
      option( " " );
      strcpy( file_name, argv[1] );
    }
    break;
  case 3 :
    if( argv[1][0] != '-' ) {
      usage();
    }
    option( &argv[1][1] );
    strcpy( file_name, argv[2] );
    break;
  default :
    usage();
    break;
  }

  if( opt ) {
    rcd_check();

    rcpptr = dataptr = ( uchar * )rcd->data_adr;
    lastptr = rcpptr + rcd->bufcap;
    if( rcd->act ) {
      (*rcd->end)();
    }
  }

  if( opt != 3 ) {

    strcpy( g36_name, file_name );
    /*
      if( !stcgfe( ext, file_name ) ) {
      strcat( g36_name, ".G36" );
      }
      */

    if( (p = extptr( file_name )) ) {
      *p = 0;
    }

    if( p == 0 ) {strcat( g36_name, ".g36" );}

    strcpy( rcp_name, file_name );
    strcat( rcp_name, ".rcp" );

    if( !( fpi = fopen( g36_name, "rb" ) ) ) {
      fprintf( stderr, _("Cannot read file ( %s ).\n"), g36_name );
      exit(16);
    }

  } else {
    strcpy( g36_name, rcd->filename );
  }

  alloc_mem();

  if( opt == 3 ) {
    move_buf();
  }

  proc_header();

  for( trk = 0; trk < trk_max; trk++ ) {
    proc_track();
  }

  for( trk = trk_max; trk < RCP_HEAD.trkmax ; trk++ ) {
    dummy_track();
  }

  if( opt != 3 ) {
    stamp=getft(fileno(fpi));
    fclose( fpi );
  }

  MFREE( 0 );

  if( opt ) {
    rcd->totalcount = tc;
  }

  rc = 0; /*warning*/

  switch( opt ) {
  case 0 :
    fclose( fpo );
    if(stampflag!=0){
      fpo=fopen(rcp_name,"ab");chgft(fileno(fpo),stamp);fclose(fpo);
    }
    rc = 0;
    break;
  case 1 :
    set_valid();
    rc = exe( "rcp", "", "", "-B", ( char * ) 0, ( char * ) 0 );
    break;
  case 2 :
    set_valid();
    switch( stop ) {
    case 0:
      rc = exe( "rcp", "", "", "-PB", ( char * ) 0, ( char * ) 0 );
      break;
    case 1:
      rc = exe( "rcp", "", "", "-KB", ( char * ) 0, ( char * ) 0 );
      break;
    }
    break;
  case 3 :
    rc = 0;
    set_valid();
    break;
  case 4 :
    set_valid();
    break;
  }

  exit( rc );
}
#endif

/***************************/
static int tai_compres(int ln)
{
  int	i,ad,ch;
  unsigned char	a,b,c,d;

  ch=(TRACK.MIDI_CH+1)&0xff;
  ad=0;

  for(i=0;i<ln;i+=4){
    a=note[i];b=note[i+1];c=note[i+2];d=note[i+3];

    if(a<128 && c>b && d>0){
      int cc=ch,ad2,sum;
      ad2=i;sum=0;

      while(ad2<ln){
	unsigned char	da=note[ad2];
	if(da==0xe6){cc=note[ad2+2];}

	if(ad2>i && cc==ch && a==da && note[ad2+2]!=0 && note[ad2+3]!=0 ){
	  if(sum+note[ad2+2]<=255){
	    c=note[ad2+2]+sum;
	    note[i+2]=c;
	    note[ad2+2]=0;
	    note[ad2+3]=0;
	  }else{
	    break;
	  }
	}

	if(da<0xf0){sum+=note[ad2+1];if(sum>255 || sum>=c){break;}}

	if(da>=0xfc||da==0xf8||da==0xf9||da==0xe2||da==0xec){break;}
	ad2+=4;
      }
    }

    if(note[i]>=0xfc){ch=-1;}

    if(note[i]==0xe6){
      if(note[i+2]==ch){
	note[i]=0;note[i+2]=0;note[i+3]=0;
      }else{
	ch=note[i+2];

	if(ad && note[ad-4]==0xe6){
	  note[ad-2]=note[i+2];
	  note[i]=0;note[i+2]=0;note[i+3]=0;
	}
      }
    }
    /*
      if(note[i]==0xee){
      if(ad && note[ad-4]==note[i] && note[ad-3]==0){
      note[ad-2]=note[i+2];
      note[i]=0;note[i+2]=0;note[i+3]=0;
      }
      }
      */
    if((note[i]<0x80 &&(note[i+2]==0||note[i+3]==0))&&
       ad>0 && note[ad-4]<0xf0 && note[ad-3]+note[i+1]<=240){
      note[ad-3]+=note[i+1];
    }else{
      note[ad++]=note[i];note[ad++]=note[i+1];
      note[ad++]=note[i+2];note[ad++]=note[i+3];
    }
  }

  return ad;
}

/***************************/
static int same_compres(int size)
{
  int	i,j=0,k,match,count=0;
  int	po=0,m_max,ln,co=0,me=0,ad;
  int	add[1024],add2[1024],leng[1024];

  m_max=add_set(size,add);if(m_max<2 || m_max>1024){return(size);}

  for(i=0;i<m_max;i++){
    po=add[i];ln=meas_len(size,po);match=0;
    if(ln>4 && note[po+ln-4]==0xfc){ln=ln-4;}

    if(note[po]<0xfc && me>0){
      j=0;while(j<me ){
	if(ln==leng[j] && note[add2[j]]<0xfc){
	  match=1;ad=add2[j];
	  k=0;while(k<ln){
	    if(note[po+k]!=note[ad]){match=0;break;}
	    k++;ad++;
	  }
	}
	if(match!=0){break;}
	j++;
      }
    }

    if(match!=0 && j<1024 && add2[j]<65536-44){
      int ad=add2[j]+44;
      if(me<1024){add2[me]=co;leng[me]=4;me++;}
      note[co]=0xfc;
      note[co+1]=j&0xff;note[co+2]=(j>>8)+(ad&0xfc);note[co+3]=ad>>8;
      co=co+4;count++;
    }else{
      if(me<1024){add2[me]=co;leng[me]=ln;me++;}
      for(k=0;k<ln;k++){note[co]=note[po];po++;co++;}
    }
  }
  note[co]=0xfe;co=co+4;
  return(co);
}

static int add_set(int size,int *add)
{
  int	co=0,i=0;

  add[0]=0;
  while(i<size){
    if(note[i]>=0xfc||note[i+4]==0xfc){
      co++;if(co<1024){add[co]=i+4;}
    }

    if(note[i]<128){
      if(note[i+2]==0 || note[i+3]==0){note[i]=0;note[i+2]=0;note[i+3]=0;}
    }
    i=i+4;
  }

  return(co);
}

static int meas_len(int size,int j)
{
  int	co=0,d;

  while(j<size ){
    d=note[j];
    if( d==0xfe ){break;}
    co++;
    if( d>0xfb ){break;}
    j=j+4;
  }
  return(co*4);
}

/***************************/
static void same_cluc(int len)
{
  int	j=0,max;
  unsigned char	a;
  int	add[2048];

  max=add_set(len,add);
  while(j<len-4 ){
    a=note[j];
    if(a==0xfc){
      int no,ad=j;
    resame:
      no=note[ad+1]+(note[ad+2]&3)*256;
      if(no<max){
	ad=add[no];
	if(j!=ad && note[ad]==0xfc ){
	  goto resame;
	}

	note[j+1]=no&0xff;
	note[j+2]=(note[j+2]&0xfc)+((no>>8)&3);

      }else{ad=0;}

      ad+=44;note[j+2]=(note[j+2]&3)+(ad&0xfc);note[j+3]=ad>>8;
    }
    j=j+4;
  }
}

/***************************/
