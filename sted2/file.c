/*
 * STed: file.c (file & play)
 */

#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>

#if 0
#include	"basic0.h"
#include	"BASIC.h"
#endif

#include	"iocslib.h"
#include	"doslib.h"
#include	"sted.h"
#include	"rcp_transport.h"

#include        "rcpconv.h"

/* 123456789012345678901234567890 */
/*static char	sted_id[]="STedV2.0(C)TURBO\r\n";*/		/* 18byte */

static char	rcp_id[] ="RCM-PC98V2.0(C)COME ON MUSIC\r\n";	/* 30byte */
static char	trk_id[] ="STedV2.0TRACK\r\n";			/* 15byte */
static char	prt_id[] ="RCPPR";				/*  5byte */

static char	rex_id[] ="RCM-PC98 USER EXCLUSIVE\r\n";	/* 25byte */
static char	res_id[] ="STedV2.0U_EXC\r\n";			/* 15byte */

static char	ras_id[] ="STedV2.0R_ASS\r\n";			/* 15byte */

static char     sted_id[] = "STEDDATAx.xxDedicatedToTURBO\r\n"; /* 30bytes */

/*prt rex -> grt gex v2.5g*/











/* ================================================== file */
static int smfload( char *fna ) {
  int  i,j,size2,ln,md2=0,stra=0,max;
  long size;
  FILE *fp;
  uint8_t *smfbuf, *rcpbuf;
  size_t ptr;
  int rcpsize;

  fp = fopen2(fna,"rb");
  if ( fp == NULL ) {msg(_("File not found."));return(-1);}
  fseek(fp,0,SEEK_END);
  size=ftell(fp);
  fseek(fp,0,SEEK_SET);

  smfbuf = (uint8_t *)malloc(sizeof(uint8_t)*size);
  if ( smfbuf==NULL ) {fclose(fp);return(-1);}
  fread(smfbuf, 1, size, fp);
  fclose(fp);

  rcpbuf = (uint8_t *)malloc(sizeof(uint8_t)*DATA_ADR_SIZE);
  if ( rcpbuf == NULL ) {free(smfbuf);return(-1);}
  
  if ( (rcpsize=itor( smfbuf, rcpbuf )) == 0 ) {
    free(rcpbuf);free(smfbuf);return(-1);
  }
  free(smfbuf);

  ptr=0;
  memmove( hed, rcpbuf+ptr, 1414 );
  ptr+=1414;
  if ( strcmp(sread(0,30),rcp_id)!=0 ) {
    msg(_("Invalid file format."));free(rcpbuf);return(-1);
  }
  if(hed[0x1e6]==36){md2=1;}else{md2=0;}
  if(md2==0){max=18;}else{max=36;}
  dinit();hedread();

  for(i=stra;i<max;i++){
    memmove(hed,rcpbuf+ptr,44);size=thedread(i);size2=0;
    ptr+=44;
    if( size>TRACK_SIZE ){size2=(int)size;size=TRACK_SIZE;}

    if(size_change(i,(int)size)){break;}
    ln=(int)size;
    memmove(trk[i],rcpbuf+ptr,size);
    if( rcpsize<ptr+size ) ln=0;
    ptr+=size;

    if( ln!=size ){if(i==18){break;}msg(_("Invalid file."));size=ln;break;}

    size=gomi_cut(trk[i],(int)size);
    tr_len[i]=(int)size;trk[i][size-4]=0xfe;

    if( size2>0){
      msg(_("Track buffer exhausted."));size2-=(int)size;
      while( size2>0 ){
	if( size2>TRACK_SIZE ){j=TRACK_SIZE;}else{j=size2;}
	memmove(dat,rcpbuf+ptr,j);ptr+=j;
	size2=size2-j;
      }
    }
  }

  free(rcpbuf);
  return(0);
}

int	dload(char *fna,int md)
{
  int	i,j,size,size2,ln,md2=0,stra=0,max;
  FILE	*fp;

  if(str_search(fna,".MID")>0 || str_search(fna,".mid")>0 )
    {return(smfload(fna));}

  if(str_search(fna,".R36")>0 || str_search(fna,".r36")>0){md2=1;}

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}

  fread(hed,1,1414,fp);

  if( strcmp(sread(0,30),rcp_id)!=0 ){
    msg(_("Invalid file format."));fclose(fp);return(-1);}

  if(hed[0x1e6]==36){md2=1;}else{md2=0;}
  if(md2==0){max=18;}else{max=36;}
  if(md==1){stra=18;max=36;}else{dinit();hedread();}

  for(i=stra;i<max;i++){
    fread(hed,1,44,fp);size=thedread(i);size2=0;
    if( size>TRACK_SIZE ){size2=size;size=TRACK_SIZE;}

    if(size_change(i,size)){break;}
    ln=fread(trk[i],1,size,fp);

    if( ln!=size ){if(i==18){break;}msg(_("Invalid file."));size=ln;break;}

    size=gomi_cut(trk[i],size);
    tr_len[i]=size;trk[i][size-4]=0xfe;

    if( size2>0){
      msg(_("Track buffer exhausted."));size2-=size;
      while( size2>0 ){
	if( size2>TRACK_SIZE ){j=TRACK_SIZE;}else{j=size2;}
	fread(dat,1,j,fp);size2=size2-j;
      }
    }
  }
  fclose(fp);
  trksize_c();
  return(0);
}

/***************************/
static int smfsave(char *fna) {
  int	i,md2=0,max;
  int   smfsize, rcpsize, ptr;
  uint8_t *smfdata, *rcpdata;
  FILE	*fp;

  for(i=18;i<36;i++){
    if(tr_len[i]>4 || trkmemo[i][0]!=0){md2=1;}
  }

  if(md2==0){max=18;}else{max=36;}

  rcpsize = 1414;
  for ( i=0 ; i<max ; i++ ) {
    rcpsize+=(44+tr_len[i]);
  }
  rcpdata = (uint8_t *)malloc(sizeof(uint8_t)*rcpsize);
  if ( rcpdata == NULL ) return(-1);

  hedset(max);
  memcpy( rcpdata, hed, 1414 );
  ptr=1414;
  for ( i=0 ; i<max ; i++ ) {
    thedset( i, tr_len[i]+44 );
    memcpy( rcpdata+ptr, hed, 44 ); ptr+=44;
    memcpy( rcpdata+ptr, trk[i], tr_len[i] ); ptr+=tr_len[i];
  }

  smfsize = rcpconv( rcpdata, rcpsize, &smfdata, NULL );
  free( rcpdata );
  if ( smfsize<0 ) return(-1);

  if ((fp=fopen2(fna,"wb"))==NULL) {
    msg(_("Cannot open file."));
    free(smfdata);
    return (-1);
  }
  i=fwrite( smfdata, 1, smfsize, fp );
  fclose(fp);
  free( smfdata );

  if ( i!=smfsize ) {
    msg(_("Cannot write file."));
    unlink(fna);
    return(-1);
  }

  return(0);
}

int	dsave(char *fna)
{
  int	i,ef=0,md2=0,max;
  FILE	*fp;

  dclr();
  if(str_search(fna,".MID")>0 || str_search(fna,".mid")>0 )
    {return(smfsave(fna));}

  if(str_search(fna,".R36")>0 || str_search(fna,".r36")>0){md2=1;}

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}

  for(i=18;i<36;i++){
    if(tr_len[i]>4 || trkmemo[i][0]!=0){md2=1;}
  }

  if(md2==0){max=18;}else{max=36;}

  hedset(max);fwrite(hed,1,1414,fp);

  i=0;
  while(i<max && ef==0){
    thedset(i,tr_len[i]+44);fwrite(hed,1,44,fp);
    if(fwrite(trk[i],1,tr_len[i],fp)!=tr_len[i]){ef=1;break;}
    i++;
  }
  fclose(fp);

  if(ef!=0){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/***************************/
int	part_load(char *fna)
{
  int	ln,sz;
  FILE	*fp;

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}
  fread(hed,1,8,fp);ln=fread(dat,1,work_size,fp);fclose(fp);
  if( strcmp(sread(0,5),prt_id)!=0 ){
    msg(_("Invalid file format."));return(-1);}
  sz=hed[7]*256+hed[6];

  if( ln<sz ){msg(_("Invalid file."));sz=-1;}
  return(sz);
}

/***************************/
int	part_save(char *fna,int ln)
{
  int	fr;
  FILE	*fp;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}
  sset(prt_id,0,5);hed[5]=track+1;hed[6]=ln&0xff;hed[7]=ln>>8;
  fwrite(hed,1,8,fp);fr=fwrite(dat,1,ln,fp);fclose(fp);

  if( fr!=ln ){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/* Body length of one RCP track header. Sizes are a multiple of 4, so the
 * low 2 bits of the first byte are the high bits of an 18-bit length.
 */
static int rcp_body_size(const uint8_t *h)
{
  return (h[0] & 0xfc) + (((h[0] & 3) * 256 + h[1]) * 256) - 44;
}

static int track_has_note(const uint8_t *p, int n)
{
  int i;

  for (i = 0; i + 4 <= n; i += 4) {
    if (p[i] == 0xfe)
      break;
    if (p[i] > 0 && p[i] < 0x80)
      return 1;
  }
  return 0;
}

static int scale_time(int v, int src, int dst)
{
  if (v <= 0 || src <= 0 || src == dst)
    return v;
  return (int)(((long)v * dst + src / 2) / src);
}

static int emit4(uint8_t *out, int n, int cap, int a, int b, int c, int d)
{
  if (n < 0 || n + 4 > cap)
    return -1;
  out[n] = a;
  out[n + 1] = b;
  out[n + 2] = c;
  out[n + 3] = d;
  return n + 4;
}

/* Scale step (and, for notes, gate) from the MIDI file's RCP timebase to
 * the song's. Values that no longer fit in one byte are split. Markers,
 * same-measure pointers and continuation bytes are left unchanged; same_cluc
 * rewrites same-measure addresses after the track is installed.
 */
static int scale_track(const uint8_t *src, int n, uint8_t *out, int cap,
		       int tb_src, int tb_dst)
{
  int i, o;

  o = 0;
  for (i = 0; i + 4 <= n; i += 4) {
    int ev = src[i];
    int step, gate;

    if (ev == 0xfe) {
      o = emit4(out, o, cap, 0xfe, 0, 0, 0);
      return o;
    }
    if (ev >= 0xf0) {
      o = emit4(out, o, cap, ev, src[i + 1], src[i + 2], src[i + 3]);
      if (o < 0)
	return -1;
      continue;
    }
    step = scale_time(src[i + 1], tb_src, tb_dst);
    gate = src[i + 2];
    if (ev > 0 && ev < 0x80)
      gate = scale_time(gate, tb_src, tb_dst);
    while (step > 255) {
      o = emit4(out, o, cap, 0x00, 255, 0, 0);
      if (o < 0)
	return -1;
      step -= 255;
    }
    if (ev > 0 && ev < 0x80) {
      while (gate > 255) {
	o = emit4(out, o, cap, ev, step, 255, src[i + 3]);
	if (o < 0)
	  return -1;
	step = 255;
	gate -= 255;
      }
      o = emit4(out, o, cap, ev, step, gate, src[i + 3]);
    } else {
      o = emit4(out, o, cap, ev, step, src[i + 2], src[i + 3]);
    }
    if (o < 0)
      return -1;
  }
  return emit4(out, o, cap, 0xfe, 0, 0, 0);
}

static void copy_track_memo(char *dst, const uint8_t *src)
{
  int n, i;

  n = 36;
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == 0))
    n--;
  for (i = 0; i < n; i++)
    dst[i] = src[i];
  dst[n] = 0;
}

/***************************/
int	trk_load(char *fna)
{
  int	ln,sz;
  FILE	*fp;

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}

  fread(hed,1,16,fp);
  if( strcmp(sread(0,15),trk_id)!=0 ){
    msg(_("Invalid file format."));fclose(fp);return(-1);}

  fread(hed,1,44,fp);ln=fread(dat,1,work_size,fp);fclose(fp);
  sz=hed[0]+hed[1]*256-44;
  if( ln<sz ){msg(_("Invalid file."));return(-1);}

  sz=gomi_cut(dat,sz);
  if(size_change(track,sz)){return -1;}

  thedread(track);memcpy_l(trk[track],dat,ln);tr_len[track]=sz;
  tr_pos[track][0][0]=0;tr_pos[track][0][1]=0;tr_pos[track][0][2]=0;
  tr_pos[track][1][0]=0;tr_pos[track][1][1]=0;tr_pos[track][1][2]=0;

  return(0);
}

/***************************/
int	trk_save(char *fna)
{
  int	fr,ln;
  FILE	*fp;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}

  dclr();
  sset(trk_id,0,15);hed[15]=0x1a;fwrite(hed,1,16,fp);
  ln=tr_len[track];thedset(track,ln+44);fwrite(hed,1,44,fp);
  fr=fwrite(trk[track],1,ln,fp);fclose(fp);

  if( fr!=ln ){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/* Load one note track from an SMF into the current track.
 * itor turns format 0 and 1 into RCP tracks. A format 0 file is split by
 * MIDI channel. Tempo-only tracks are skipped. Exactly one track that
 * contains notes is copied. More than one is left alone. The song's
 * tempo, timebase, beat and the other tracks stay as they are. Steps and
 * gates are scaled from the file's timebase onto the song's.
 */
int	trk_load_mid(char *fna)
{
  FILE *fp;
  long size;
  int rcpsize, body, notes, tb_src, ch, sz;
  size_t pos, chosen;
  uint8_t *smfbuf, *rcpbuf;
  const uint8_t *hdr;

  fp = fopen2(fna, "rb");
  if (fp == NULL) {
    msg(_("File not found."));
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 0) {
    fclose(fp);
    msg(_("Invalid file."));
    return -1;
  }
  smfbuf = (uint8_t *)malloc(sizeof(uint8_t) * size);
  if (smfbuf == NULL) {
    fclose(fp);
    return -1;
  }
  if (fread(smfbuf, 1, size, fp) != (size_t)size) {
    fclose(fp);
    free(smfbuf);
    msg(_("Invalid file."));
    return -1;
  }
  fclose(fp);

  rcpbuf = (uint8_t *)malloc(sizeof(uint8_t) * DATA_ADR_SIZE);
  if (rcpbuf == NULL) {
    free(smfbuf);
    return -1;
  }
  rcpsize = itor(smfbuf, rcpbuf);
  free(smfbuf);
  if (rcpsize == 0 || rcpsize < 1414 ||
      memcmp(rcpbuf, rcp_id, 30) != 0) {
    free(rcpbuf);
    msg(_("Invalid file format."));
    return -1;
  }

  tb_src = rcpbuf[0x1c0];
  if (rcpbuf[0x1e6] != 0)
    tb_src += rcpbuf[0x1e7] << 8;
  if (tb_src <= 0)
    tb_src = 48;

  notes = 0;
  chosen = 0;
  pos = 1414;
  while (pos + 44 <= (size_t)rcpsize) {
    body = rcp_body_size(rcpbuf + pos);
    if (body < 4 || pos + 44 + body > (size_t)rcpsize)
      break;
    if (track_has_note(rcpbuf + pos + 44, body)) {
      notes++;
      chosen = pos;
    }
    pos += 44 + (size_t)body;
  }
  if (notes == 0) {
    free(rcpbuf);
    msg(_("This MIDI file has no note track."));
    return -1;
  }
  if (notes > 1) {
    free(rcpbuf);
    msg(_("This MIDI file has more than one note track."));
    return -1;
  }

  hdr = rcpbuf + chosen;
  body = rcp_body_size(hdr);
  sz = scale_track(hdr + 44, body, dat, work_size, tb_src, tbase);
  if (sz >= 4)
    sz = gomi_cut(dat, sz);
  if (sz < 4) {
    free(rcpbuf);
    msg(_("Track buffer exhausted."));
    return -1;
  }
  /* size_change reports "Track buffer exhausted." itself. */
  if (size_change(track, sz)) {
    free(rcpbuf);
    return -1;
  }
  memcpy(trk[track], dat, sz);
  tr_len[track] = sz;
  trk[track][sz - 4] = 0xfe;
  same_cluc(track, 0);

  ch = hdr[4] + 1;
  if (ch > 32)
    ch = 0;
  mch[track] = ch;
  copy_track_memo(trkmemo[track], hdr + 8);

  tr_pos[track][0][0] = 0;
  tr_pos[track][0][1] = 0;
  tr_pos[track][0][2] = 0;
  tr_pos[track][1][0] = 0;
  tr_pos[track][1][1] = 0;
  tr_pos[track][1][2] = 0;

  free(rcpbuf);
  return 0;
}

/***************************/
int	part_text_save(char *fna,int ad,int ln)
{
  int	i,j=0,fr,ll;
  FILE	*fp;
  char	tmp0[4096+1024];
  uint8_t a;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}

  i=ad;
  while(ln>0){
    int	lp;
    if(INPOUT(0xff)==27 ){msg(_("Terminated."));j=-1;break;}

    lp=0;tmp0[0]=0;
    while(lp<64 && ln>0){
      a=trk[track][i];

      if(a!=0xf7){
	strcat(tmp0,trk_dis(0,i,1));strcat(tmp0,"\r\n");
      }
      lp++;i+=4;ln-=4;
    }
    ll=strlen(tmp0);fr=fwrite(tmp0,1,ll,fp);

    if(fr<ll){msg(_("Disk exhausted."));j=-1;break;}
  }
  fclose(fp);
  snsclr();return(j);
}

/***************************/
void	uex_read(int po,int no)
{
  int	ad,j;

  ad=po+no*48;if(po==16){ad=po;}

  strcpy(user_exc_memo[no],sread(ad,24));
  if(hed[ad+24]==0 && hed[ad+47]==0){
    user_exc_data[no][0]=0xf7;
  }else{
    for(j=0;j<24;j++){user_exc_data[no][j]=hed[ad+j+24];}
  }
}

void	uex_set(int po,int no)
{
  int	ad,j;

  ad=po+no*48;if(po==16){ad=po;}

  sset(user_exc_memo[no],ad,24);
  for(j=0;j<24;j++){hed[ad+j+24]=user_exc_data[no][j];}
}

/***************************/
int	rex_load(char *fna,int mo)
{
  int	ln,i,sz;
  FILE	*fp;

  if(mo<0){sz=410;}else{sz=48+16;}

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}
  ln=fread(hed,1,sz,fp);fclose(fp);
  if( ln!=sz ){msg(_("Invalid file."));return -1;}

  if(mo<0){
    if( strcmp(sread(0,25),rex_id)!=0 ){
      msg(_("Invalid file format."));return(-1);}

    for(i=0;i<8;i++){uex_read(26,i);}
  }else{
    if( strcmp(sread(0,15),res_id)!=0 ){
      msg(_("Invalid file format."));return(-1);}

    uex_read(16,mo);
  }

  return(0);
}

/***************************/
int	rex_save(char *fna,int mo)
{
  int	ln,i,sz;
  FILE	*fp;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}

  if(mo<0){sz=410;}else{sz=48+16;}

  if(mo<0){
    sset(rex_id,0,25);hed[25]=0x1a;
    for(i=0;i<8;i++){uex_set(26,i);}
  }else{
    sset(res_id,0,15);hed[15]=0x1a;
    uex_set(16,mo);
  }

  ln=fwrite(hed,1,sz,fp);fclose(fp);

  if( ln!=sz ){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/***************************/
int	ras_load(char *fna)
{
  int	ln,i;
  FILE	*fp;

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}
  ln=fread(hed,1,528,fp);fclose(fp);
  if( ln!=528 ){msg(_("Invalid file."));return -1;}

  if( strcmp(sread(0,15),ras_id)!=0 ){
    msg(_("Invalid file format."));return(-1);}

  for(i=0;i<32;i++){
    strcpy(rhyna[i],sread(0x10+i*16,14));
    rhyno[i][0]=hed[0x10+i*16+14];rhyno[i][1]=hed[0x10+i*16+15];
  }

  return(0);
}

/***************************/
int	ras_save(char *fna)
{
  int	ln,i;
  FILE	*fp;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}

  sset(ras_id,0,15);hed[15]=0x1a;

  for(i=0;i<32;i++){
    sset(rhyna[i],0x10+i*16,14);
    hed[0x10+i*16+14]=rhyno[i][0];hed[0x10+i*16+15]=rhyno[i][1];
  }

  ln=fwrite(hed,1,528,fp);fclose(fp);

  if( ln!=528 ){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/***************************/
int	timload(char *fna)
{
  FILE	*fp;

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}
  fread(cm6,1,22601,fp);fclose(fp);
  tim_name_read();asin_change();
  return(0);
}

/***************************/
int	timsave(char *fna)
{
  int	fr;
  FILE	*fp;

  if(!(fp= fopen2(fna,"wb"))){msg(_("Cannot open file."));return(-1);}
  fr=fwrite(cm6,1,22601,fp);fclose(fp);
  if( fr!=22601 ){msg(_("Cannot write file."));unlink(fna);return(-1);}
  return(0);
}

/***************************/
int	gsdload(char *fna)
{
  FILE	*fp;

  if(!(fp= fopen2(fna,"rb"))){msg(_("File not found."));return(-1);}
  fread(gsd,1,4096,fp);fclose(fp);
  return(0);
}

/***************************/
char	*sread(int a,int b)
{
  static	char	s[256];
  int	i;

  for(i=0;i<b;i++){s[i]=hed[i+a];}
  if(knck(s,b-1)){b--;}

  s[b]=0;spcut(s);return(s);
}

/***************************/
void	sset(char *s,int a,int b)
{
  int	i;

  for(i=0;i<b;i++){hed[a+i]=32;}
  while(*s!=0){hed[a++]=*s++;}
}

/***************************/
void	hedset(int max)
{
  int	i;

  hed[30]=0;hed[31]=0;sset(mtitle,0x20,64);
#ifdef HAVE_SUPPORT_STED3
  if ( issted3 ) {
    sset(sted_id,0,30);
    sset(STED3_VERSION, 8,4);
  } else
#endif
    {
      sset(rcp_id,0,30);
    }

  i=str_search(cm6_file,".");if(i>8){cm6_file[8]=0;strmfe(cm6_file,cm6_file,"CM6");}
  i=str_search(gsd_file,".");if(i>8){gsd_file[8]=0;strmfe(gsd_file,gsd_file,"GSD");}

  for(i=0x1b0;i<=0x1bf;i++){hed[i]=0x40;}
  for(i=0x1e8;i<=0x1f5;i++){hed[i]=0;}

  for(i=0x1c6;i<0x1e5;i++){hed[i]=0;}
  i=strlen(cm6_file);if(i!=0){sset(cm6_file,0x1c6,12);}
  i=strlen(gsd_file);if(i!=0){sset(gsd_file,0x1d6,12);}

  hed[0x1e6]=max;
  hed[0x1e7]=tbase>>8;hed[0x1c0]=tbase;

  hed[0x1c1]=mtempo;hed[0x1c2]=beat1;hed[0x1c3]=beat2;
  hed[0x1c4]=bkey;hed[0x1c5]=pbias;
  for(i=0;i<12;i++){sset(memo[i],0x60+i*28,28);}

  for(i=1;i<17;i++){
    int a1=tim_asin[i],a2=tim_asin[i+16];
    if(a1>15){a1=0;}
    if(a2>15){a2=0;}
    hed[0x1f6-1+i]=a2*16+a1;
  }

  for(i=0;i<32;i++){
    sset(rhyna[i],0x206+i*16,14);
    hed[0x206+i*16+14]=rhyno[i][0];hed[0x206+i*16+15]=rhyno[i][1];
  }
  for(i=0;i<8;i++){
    uex_set(0x406,i);
  }
}

/***************************/
void	hedread()
{
  int	i,co;

  strcpy(mtitle,sread(0x20,64));
  strcpy(cm6_file,sread(0x1c6,16));strcpy(gsd_file,sread(0x1d6,16));
  if(strlen(cm6_file)>15){gsd_file[0]=0;}

  tbase= hed[0x1c0];
  if( hed[0x1e6]!=0){tbase+=(hed[0x1e7]<<8);}

  mtempo= hed[0x1c1];beat1= hed[0x1c2];beat2= hed[0x1c3];
  if(beat2<2 || beat2>16){beat2=4;}
  if(beat1<1 || beat1>beat2*2){beat1=beat2;}

  bkey=  hed[0x1c4];pbias= hed[0x1c5];
  for(i=0 ;i<= 11;i++){strcpy(memo[i],sread(0x60+i*28,28));}

  /*	asin_init();*/

  co=0;
  if((cnfflag&0x0080)==0){
    if((hed[0x1f6]&15)!=0 || (hed[0x1f6]==0x70 && hed[0x1f6+1]==0x71)){
      for(i=1 ;i<= 16;i++){
	int	d=hed[0x1f6-1+i];
	if(d!=0){co++;}
	tim_asin[i]=d&15;tim_asin[i+16]=(d>>4)&15;
	/*
	  if((d&15)!=0){tim_asin[i]=d&15;}
	  if((d>>4)!=0){tim_asin[i+16]=d>>4;}
	  */
      }
    }
  }
  if(co==0){asin_init();}

  for(i=0;i<32;i++){
    strcpy(rhyna[i],sread(0x206+i*16,14));
    rhyno[i][0]=hed[0x206+i*16+14];rhyno[i][1]=hed[0x206+i*16+15];
  }
  for(i=0;i<8;i++){
    uex_read(0x406,i);
  }
}

/***************************/
void	thedset(int tr,int sz)
{
  hed[0]=(sz&0xfc)+((sz>>16)&3);hed[1]=(sz>>8)&0xff;
  hed[2]=trno[tr];hed[3]=trrhy[tr];hed[4]=mch[tr]-1;
  hed[6]=trst[tr];hed[5]=trkey[tr];hed[7]=trmod[tr];if(hed[7]==3){hed[7]=4;}
  sset(trkmemo[tr],8,36);
}

/***************************/
int	thedread(int tr)
{
  int	size;

  size=(hed[0]&0xfc)+((hed[0]&3)*256+hed[1])*256-44;
  trno[tr]=hed[2];trrhy[tr]=hed[3];mch[tr]=hed[4]+1;if(mch[tr]>32){mch[tr]=0;}
  trkey[tr]=hed[5];trst[tr]=hed[6];trmod[tr]=hed[7];if(trmod[tr]==3){trmod[tr]=4;}
  strcpy(trkmemo[tr],sread(8,36));
  return(size);
}

/***************************/
int	bufload()
{
  int	bpo=0,i,size,size2,max;

  if( (rcd->data_valid==0 && rcd->tone_valid==0 ) ||(rcd->fmt!=2 && rcd->fmt!=1)){
    msg(_("Buffer is empty."));return(-1);
  }

  if( rcd->data_valid!=0 ){
    bpo=rcp_buf_get(hed,0,1414);
    if( strcmp(sread(0,30),rcp_id)!=0 ){
      msg(_("Buffer is empty."));return(-1);
    }
    dinit();
    hedread();

    if(rcd->fmt==1){max=18;}else{max=36;}
    if(hed[0x1e6]==36){max=36;}else{max=18;}

    for(i=0;i<max;i++){
      bpo=rcp_buf_get(hed,bpo,44);
      if((rcd->totalcount)<bpo){break;}

      size=thedread(i);size2=0;
      if( size>TRACK_SIZE ){size2=size;size=TRACK_SIZE;}
      if(size_change(i,size)){break;}

      bpo=rcp_buf_get(trk[i],bpo,size);
      size=gomi_cut(trk[i],size);
      tr_len[i]=size;trk[i][size-4]=0xfe;
      if( size2>0){msg(_("Track buffer exhausted."));bpo=bpo+(size2-size);}
    }
    strlcpy(rcp_file,rcd->filename,sizeof rcp_file);
    if(rcp_file[0]!=0){
      if(max==18){
	if(str_search(rcp_file,".R36")==0 && str_search(rcp_file,".r36")==0){
	  strmfe(rcp_file,rcp_file,"RCP");
	}
      }else{
	if(str_search(rcp_file,".RCP")==0 && str_search(rcp_file,".rcp")==0){
	  strmfe(rcp_file,rcp_file,"R36");
	}
      }
    }
    if( rcd->tone_valid==0 ){cminit();}
    if( rcd->gsd_valid==0 ){gsinit();}
  }

  if( rcd->tone_valid!=0 ){
    tim_var_read();
    if(cm6_file[0]!=0){strmfe(cm6_file,cm6_file,"CM6");asin_change();}
  }
  if( rcd->gsd_valid!=0 ){
    gsd_var_read();
    if(gsd_file[0]!=0){strmfe(gsd_file,gsd_file,"GSD");}
  }
  trksize_c();
  return(0);
}

/***************************/
int	bufset()
{
  int	bpo,i;

  all_note_off(1);rcd->data_valid=0;dclr();
  hedset(36);bpo=rcp_buf_put(hed,0,1414);

  for(i=0;i<36;i++){
    same_cluc(i,0);
    thedset(i,tr_len[i]+44);bpo=rcp_buf_put(hed,bpo,44);
    bpo=rcp_buf_put(trk[i],bpo,tr_len[i]);
  }

  if((rcd->bufcap)<bpo){msg(_("Too small RCD buffer."));return(-1);}

  rcd->fmt=2;rcd->totalcount=bpo;rcd->data_valid=1;rcd->word_valid=0;
  rcd->tone_valid=0;rcd->gsd_valid=0;
  if(cm6_file[0]!=0){tim_buf_trns();}
  if(gsd_file[0]!=0){gsd_buf_trns();}
  strlcpy(rcd->filename,rcp_file,sizeof rcd->filename);
  strlcpy(rcd->tonename,cm6_file,sizeof rcd->tonename);
  strlcpy(rcd->gsdname,gsd_file,sizeof rcd->gsdname);
  return(0);
}

/***************************/
int	bufload_trk(int tr)
{
  int	bpo=0,size;

  if( (rcd->data_valid==0) || rcd->fmt!=99 ){
    msg(_("Buffer is empty."));return(-1);
  }

  bpo=rcp_buf_get(hed,bpo,44);size=thedread(tr);
  if( size>TRACK_SIZE ){size=TRACK_SIZE;msg(_("Track buffer exhausted."));}
  if(size_change(tr,size)){goto ext;}

  bpo=rcp_buf_get(trk[tr],bpo,size);
  size=gomi_cut(trk[tr],size);
  tr_len[tr]=size;trk[tr][size-4]=0xfe;

  /*	trksize_c();*/
ext:
  rcd->fmt=0;rcd->data_valid=0;rcd->word_valid=0;
  return(0);
}

int	bufset_trk(int tr)
{
  int	bpo=0;

  all_note_off(1);rcd->fmt=0;rcd->data_valid=0;/*dclr();*/

  same_cluc(tr,0);
  thedset(tr,tr_len[tr]+44);bpo=rcp_buf_put(hed,bpo,44);
  bpo=rcp_buf_put(trk[tr],bpo,tr_len[tr]);

  if((rcd->bufcap)<bpo){msg(_("Too small RCD buffer."));return(-1);}

  rcd->fmt=99;rcd->totalcount=bpo;rcd->data_valid=1;rcd->word_valid=0;
  return(0);
}

/***************************/
/* ================================================== play */
void	dplay(int mo,int tr,int meas)
{
  int	bpo,i,f;
  char	tmp1[PATHLEN * 2];
  FILE	*fp;

  if((cmdflag&0x8000)!=0 && mo<2){mo=1-mo;}

  if(rcp_transport_is_active() && mo!=0 && meas==0){
    mo+=8;
    if(mplay[mo][0]=='&'){goto start;}
    if(mplay[mo][0]=='%'){goto resm;}
  }
start:
  all_note_off(1);rcd->data_valid=0;
  msg_clr();poft=0;
  hedset(36);bpo=rcp_buf_put(hed,0,1414);

  for(i=0;i<36;i++){
    if((trmod[i]&1)!=0){
      thedset(i,48);dat[0]=0xfe;
      bpo=rcp_buf_put(hed,bpo,44);
      bpo=rcp_buf_put(dat,bpo,4);
    }else{
      same_cluc(i,0);
      thedset(i,tr_len[i]+44);
      bpo=rcp_buf_put(hed,bpo,44);
      bpo=rcp_buf_put(trk[i],bpo,tr_len[i]);
    }
  }

  if((rcd->bufcap)<bpo){msg(_("Too small RCD buffer."));return;}

  rcd->fmt=2;rcd->totalcount=bpo;
  rcd->data_valid=1;rcd->word_valid=0;
  rcd->tone_valid=0;rcd->gsd_valid=0;

  (*rcd->init)();
  if(cm6_file[0]!=0){tim_buf_trns();}
  if(gsd_file[0]!=0){gsd_buf_trns();}

  if(cm6_file[0]==0 && gsd_file[0]==0){
    if(init_exc_data[0]==255){(*rcd->setup)();
    }else{(*rcd->mix_out)(init_exc_data);twait(5);}
  }

  if(mo!=0 && meas==0){
    strcpy(tmp1,rcp_path);strcat(tmp1,rcp_file);strmfe(tmp1,tmp1,"wrd");
    if(fp=fopen2(tmp1,"rb")){
      int	i,j,sz;
      uint8_t	a;

      sz = rcd->wordcap;
      j=fread(dat,1,work_size,fp);fclose(fp);bpo=0;
      for(i=0;i<j;i++){
	a=dat[i];
	if(a!=0x0a){
	  if(a==0x0d ){a=0;}
	  rcd->word_adr[bpo]=a;bpo++;
	  if(bpo+1>=sz){break;}
	}
      }
      rcd->word_adr[bpo]=26;rcd->word_valid=1;
    }
  resm:
    strlcpy(rcd->filename,rcp_file,sizeof rcd->filename);
    strlcpy(rcd->tonename,cm6_file,sizeof rcd->tonename);
    strlcpy(rcd->gsdname,gsd_file,sizeof rcd->gsdname);

    fnc_dis(-1);C_FNKMOD(0);dpl(0);B_COLOR(3);
    if(mplay[mo][0]=='%' || mplay[mo][0]=='&'){
      strcpy(tmp1,&mplay[mo][1]);
      if(!rcp_transport_is_active() || mplay[mo][0]=='&'){(*rcd->begin)();}
    }else{strcpy(tmp1,mplay[mo]);}
    f=exe(tmp1);
    B_COLOR(3);fnc_dis(-1);C_FNKMOD(3);B_CLR_AL();C_CUROFF();
    CRTMOD(16+256);OnGraphic();dpl(1);tpl(1);
    WINDOW(0,0,1023,1023);

    if( f<0 ){msg(_("Cannot start panel display."));}
  }else{
    if(meas>1){rcp_transport_search(tr,meas-1);}
    (*rcd->begin)();
  }
  return;
}

/* Replace dat[at..at+4) with the source measure and a following 0xfd.
   The bytes after the 0xfc are already the rest of this slice. */
static int
paste_measure(int at, int len, int src, int tr)
{
  int n,p,tail,room;

  if(src<0){src=0;}
  n=0;p=src;
  while(p<tr_len[tr] && trk[tr][p]<0xfc && at+n+8<=work_size){
    n+=4;p+=4;
  }
  tail=len-(at+4);
  if(tail<0){tail=0;}
  room=work_size-(at+4+tail);
  if(room<4){n=0;}
  else if(n>room){n=room&~3;}
  if(n>0 && tail>0){
    memmove(dat+at+4+n,dat+at+4,(size_t)tail);
  }
  for(p=0;p<n;p+=4){
    dat[at+p]=trk[tr][src];dat[at+p+1]=trk[tr][src+1];
    dat[at+p+2]=trk[tr][src+2];dat[at+p+3]=trk[tr][src+3];
    src+=4;
  }
  if(at+n+4<=work_size){
    dat[at+n]=0xfd;dat[at+n+1]=0;dat[at+n+2]=0;dat[at+n+3]=0;
  }
  return(len+n);
}

/* 0xfc still holds the original measure index. The player jumps by a byte
   offset, so rewrite that only after the slice is complete. A target ahead
   of the write position does not exist yet, and a failed lookup must not
   become an offset. lead_fd is the extra 0xfd inserted when po>0. */
int
resolve_same_measures(int len, int meas, int lead_fd, int tr, int *me_ad)
{
  int at,same,index,me,ad;

  at=0;
  while(at+4<=len && at+4<work_size){
    if(dat[at]!=0xfc){at+=4;continue;}
    same=dat[at+1]+(dat[at+2]&3)*256;
    if(meas<same+1){at+=4;continue;}
    len=paste_measure(at,len,me_ad[same],tr);
    while(at+4<=len && dat[at]!=0xfd){at+=4;}
    if(at+4<=len){at+=4;}
  }

  at=0;
  while(at+4<=len && at+4<work_size){
    if(dat[at]!=0xfc){at+=4;continue;}
    same=dat[at+1]+(dat[at+2]&3)*256;
    index=same;
    if(lead_fd){index++;}
    me=index+2-meas;
    ad=dat_add(len,me);
    if(ad<0){
      len=paste_measure(at,len,me_ad[same],tr);
      while(at+4<=len && dat[at]!=0xfd){at+=4;}
      if(at+4<=len){at+=4;}
      continue;
    }
    ad+=44;
    dat[at+2]=ad&0xff;dat[at+3]=ad>>8;
    at+=4;
  }
  return(len);
}

/************************** meas play */
void	cdplay(int tr,int po,int ln,int md)
{
  int	bpo,co,i,j,ad,meas,tempo,pr,dumy=track;
  int	me_ad[2048];

  all_note_off(1+1);rcd->data_valid=0;msg_clr();if(ln==0){return;}
  hedset(36);bpo=rcp_buf_put(hed,0,1414);
  meas=meas_no(po);if((cmdflag&0x4000)==0){pr=lsp_wait_chack(meas);}
  tempo=last_tempo(meas);
  poft=meas-1;if(md==0){if(po>0){poft+=0x10000;}}

  for(i=0;i<36;i++){
    track=i;co=0;

    if((trmod[i]&1)==0){
      if((cmdflag&0x4000)==0 && tr!=i){
	ad=meas_add(meas);if(ad<0){ad=tr_len[i]-4;}
	add_set(i,me_ad);
	co=lsp_para_set(i,ad,me_ad,pr);
	j=ad;
      }

      if( tr==i ){
	add_set(i,me_ad);

	if(md==0){
	  if((cmdflag&0x4000)==0){
	    co=lsp_para_set(i,po,me_ad,pr);
	  }else{
	    co=lsp_para_set(i,po,me_ad,lsp_para_set(i,po,me_ad,-1));
	  }
	}
	j=po;

	if(po>0){
	  if( tempo>=0){
	    dat[co]=0xe7;dat[co+1]=1;dat[co+2]=tempo;dat[co+3]=0;
	    co=co+4;
	  }
	  dat[co]=0xfd;co=co+4;
	}

	while(ln>0){
	  dat[co]=trk[i][j];dat[co+1]=trk[i][j+1];
	  dat[co+2]=trk[i][j+2];dat[co+3]=trk[i][j+3];
	  if(dat[co]==0x98){
	    while(trk[i][j+4]==0xf7 && co<work_size){
	      co+=4;j+=4;ln-=4;
	      dat[co]=trk[i][j];dat[co+1]=trk[i][j+1];
	      dat[co+2]=trk[i][j+2];dat[co+3]=trk[i][j+3];
	    }
	  }
	  if(dat[co]==0xfe || co+4>=work_size ){break;}
	  co=co+4;
	  if(trk[i][j]<0xfc && trk[i][j+4]==0xfc){dat[co]=0xfd;co=co+4;}
	  ln=ln-4;j=j+4;
	}
	co=resolve_same_measures(co,meas,po>0,i,me_ad);
      }
    }
    dat[co]=0xfe;co=co+4;
    thedset(i,co+44);bpo=rcp_buf_put(hed,bpo,44);
    bpo=rcp_buf_put(dat,bpo,co);
  }

  track=dumy;

  if((rcd->bufcap)<bpo){msg(_("Too small RCD buffer."));return;}

  rcd->fmt=2;rcd->totalcount=bpo;rcd->data_valid=1;rcd->word_valid=0;
  rcd->tone_valid=0;rcd->gsd_valid=0;

  (*rcd->init)();
  if(cm6_file[0]==0 && gsd_file[0]==0){
    if(init_exc_data[0]==255){(*rcd->setup)();
    }else{(*rcd->mix_out)(init_exc_data);twait(5);}
  }
  (*rcd->begin)();
  return;
}

/************************** point play */
void	cntplay(int tr,int po)
{
  int	bpo,i,j,co,ad,meas,dumy,pr,tempo;
  int	me_ad[2048];

  if((cmdflag&0x80)!=0){dplay(0,tr,meas_no(po));return;}
  all_note_off(1+1);rcd->data_valid=0;msg_clr();dumy=track;
  hedset(36);bpo=rcp_buf_put(hed,0,1414);
  meas=meas_no(po);pr=lsp_wait_chack(meas);tempo=last_tempo(meas);
  poft=meas-1;if(po>0){poft+=0x10000;}

  for(i=0;i<36;i++){
    co=0;
    if((trmod[i]&1)==0){
      track=i;ad=meas_add(meas);if(ad<0){ad=tr_len[i]-4;}
      add_set(i,me_ad);
      co=lsp_para_set(i,ad,me_ad,pr);
      if(po>0){
	if( tempo>=0){
	  dat[co]=0xe7;dat[co+1]=1;dat[co+2]=tempo;dat[co+3]=0;
	  co=co+4;
	}
	dat[co]=0xfd;co=co+4;
      }
      j=ad;
      while(1){
	dat[co]=trk[i][j];dat[co+1]=trk[i][j+1];
	dat[co+2]=trk[i][j+2];dat[co+3]=trk[i][j+3];
	if(dat[co]==0x98){
	  while(trk[i][j+4]==0xf7 && co<work_size){
	    co+=4;j+=4;
	    dat[co]=trk[i][j];dat[co+1]=trk[i][j+1];
	    dat[co+2]=trk[i][j+2];dat[co+3]=trk[i][j+3];
	  }
	}
	if( dat[co]==0xfe || co+4>=work_size ){break;}
	co=co+4;
	if(trk[i][j]<0xfc && trk[i][j+4]==0xfc){dat[co]=0xfd;co=co+4;}
	j=j+4;
      }
      co=resolve_same_measures(co,meas,po>0,i,me_ad);
    }
    dat[co]=0xfe;co=co+4;
    thedset(i,co+44);bpo=rcp_buf_put(hed,bpo,44);
    bpo=rcp_buf_put(dat,bpo,co);
  }

  track=dumy;

  if((rcd->bufcap)<bpo){msg(_("Too small RCD buffer."));return;}

  rcd->fmt=2;rcd->totalcount=bpo;rcd->data_valid=1;rcd->word_valid=0;
  rcd->tone_valid=0;rcd->gsd_valid=0;

  (*rcd->init)();
  if(cm6_file[0]==0 && gsd_file[0]==0){
    if(init_exc_data[0]==255){(*rcd->setup)();
    }else{(*rcd->mix_out)(init_exc_data);twait(5);}
  }
  (*rcd->begin)();
  return;
}

/***************************/
int	last_tempo(int meas)
{
  int	i,ad,dumy,tm_me=-1,tmpo=-1,b;

  dumy=track;

  for(i=0;i<36;i++){
    if((trmod[i]&1)==0){
      track=i;ad=meas_add(meas);if(ad<0){ad=tr_len[i]-4;}
      while(ad>=0 ){
	if( trk[i][ad]==0xe7 ){
	  b=meas_no(ad);if(b>tm_me){tmpo=trk[i][ad+2];tm_me=b;}
	  break;
	}
	ad=ad-4;
      }
    }
  }
  track=dumy;
  return(tmpo);
}

/***************************/
int	lsp_para_set(int tr,int po,int *me_ad,int wcc)
{
  int	co=0,j,jj=0,i,pa=-1,exf=0;
  int	wa=0,wa2=0,retad=0;
  uint8_t	d;
  uint8_t	*ptr;
  int	ctad[256];
  int	rba=0;

  if(po==0 || lsp_wait<0){return(0);}
  for(i=0;i<128;i++){ctad[i]=-1;}

  ptr=(uint8_t *)trk[tr];

  if(wcc>0){
    int tst=0;
    wa=(mtempo>>6)*(tbase/48)*lsp_wait+1;wa2=wa>>1;wcc=(wcc>>3)*wa;

    while(jj<po &&(jj==0||tst<=16||ptr[jj]==0xe6||ptr[jj]==0xf6
		   ||(ptr[jj]>=0x90 && ptr[jj]<=0x98) )){

      if(ptr[jj]==0xde||(ptr[jj]>=0x90 && ptr[jj]<=0x98)){exf=1;}

      if(ptr[jj]<0xf0){
	if(ptr[jj]==0xde){jj+=4;break;}

	tst+=ptr[jj+1];
	if(tst>16){break;}
      }

      jj=jj+4;

      while(ptr[jj]==0xf7){jj+=4;}
      if(ptr[jj+1]>2 && exf){break;}
    }
    if(exf==0){dat[0]=0;dat[1]=40;dat[2]=0;dat[3]=0;co=4;}
  }

  for(j=0;j<po;j=j+4){
  top:
    if(exf!=0 && j>=jj){dat[co]=0;dat[co+1]=40;dat[co+2]=0;dat[co+3]=0;co+=4;exf=0;}
    d=ptr[j];

    if( d>127 ){
      if( d==0xeb ){
	d=ptr[j+2];
	if( d==0 || d==32 || d==6 || d==38 || (d>97 && d<102) ){
	  if(co>=0 && dat[co-4]==0xeb && dat[co-2]==d){
	    dat[co-1]=ptr[j+3];
	  }else{
	    dat[co]=0xeb;dat[co+1]=0;dat[co+2]=d;
	    dat[co+3]=ptr[j+3];
	    if(wcc>0){dat[co+1]=wa2;wcc=wcc-wa2;wa2=wa-wa2;}
	    co=co+4;
	  }
	}else{
	  if(d!=121){ctad[d]=j;}
	  else{for(d=0;d<128;d++){ctad[d]=-1;}}
	}
      }else{
	if( d==0xf6 ){
	  while(ptr[j+4]==0xf7){j+=4;}
	  goto top;
	}

	/*
	  41 10 42 12 cs 00 01 01 01 ss F7 <--- PART A01μݡȤBˤ
	  41 10 42 12 cs 00 01 17 00 ss F7 <--- PART B01μݡȤAˤ
	  */
	if( d==0xdd ){rba=ptr[j+2]*256+ptr[j+3];}
	if( d==0xde ){if(rba==1 && ptr[j+2]<0x17+16){goto outp;}}
	if( d==0x98 ){
	  if(ptr[j+6]==0x41 && ptr[j+10]==0x42 &&(
						  (ptr[j+14]==0x83 && ptr[j+15]==0 && ptr[j+18]==0x01 && ptr[j+22]<0x17+16)||
						  (ptr[j+14]==0 && ptr[j+15]==0x01 && ptr[j+18]<0x17+16)
						  )){goto outp;}
	}
	if( d>=0x90 && d<=0x97 ){
	  uint8_t *pt2;
	  pt2=(uint8_t *)user_exc_data[d&0xf];

	  if(pt2[0]==0x41 && pt2[2]==0x42 &&(
					     (pt2[4]==0x83 && pt2[5]==0 && pt2[6]==0x01 && pt2[7]<0x17+16)||
					     (pt2[4]==0 && pt2[5]==0x01 && pt2[6]<0x17+16)
					     )){goto outp;}
	}


	if( d==0xe6 ){
	outp:
	  /*control*/
	  for(i=127;i>0;i--){
	    int	j=ctad[i];
	    if(j>=0){
	      dat[co]=0xeb;dat[co+1]=0;dat[co+2]=i;dat[co+3]=ptr[j+3];
	      if(wcc>0){dat[co+1]=wa2;wcc=wcc-wa2;wa2=wa-wa2;}
	      co=co+4;ctad[i]=-1;
	    }
	  }
	  /*pitch*/
	  if(pa>=0){
	    dat[co]=0xee;dat[co+1]=0;dat[co+2]=ptr[pa+2];dat[co+3]=ptr[pa+3];
	    if(wcc>0){dat[co+1]=wa2;wcc=wcc-wa2;wa2=wa-wa2;}
	    co=co+4;pa=-1;
	  }

	}
	if( d<0xee && d!=0xe7 ){
	  dat[co]=d;dat[co+1]=0;dat[co+2]=ptr[j+2];
	  dat[co+3]=ptr[j+3];
	  if((d==0xdd || d==0xdf || d==0xe6)||(d==0xde && \
					       (ptr[j+4]==0xde && ptr[j+2]+1==ptr[j+6]))){
	    if(wcc<0){goto skip02;}
	    if(wcc>0){goto skip01;}
	  }else{
	    if(wcc>0){
	      if(d==0xde){co+=4;dat[co]=0;dat[co+1]=wa;dat[co+2]=0;dat[co+3]=0;
	      wcc-=wa;goto skip01;}
	      if(wa2>0){dat[co+1]=wa2;}
	    }
	    if(wcc<0){
	      if(d==0xde){co+=4;}
	    }
	  }
	  wcc=wcc-wa2;wa2=wa-wa2;
	skip01:
	  co=co+4;
	}
      skip02:
	if( d>=0xfc && retad!=0){j=retad;retad=0;goto top;}

	if( d==0xfc ){
	  int	ii,jj,ct;
	  jj=j;
	resame:
	  ct=ptr[jj+1]+(ptr[jj+2]&3)*256;ii=me_ad[ct];

	  if(jj!=ii){
	    jj=ii;if(ptr[ii]==0xfc){goto resame;}
	    retad=j+4;j=ii;goto top;
	  }
	}
	if( d==0xee ){pa=j;}

      }
    }
  }
  /*control*/
  for(i=127;i>0;i--){
    int	j=ctad[i];
    if(j>=0){
      dat[co]=0xeb;dat[co+1]=0;dat[co+2]=i;dat[co+3]=ptr[j+3];
      if(wcc>0){dat[co+1]=wa2;wcc=wcc-wa2;wa2=wa-wa2;}
      co=co+4;
    }
  }
  /*pitch*/
  if(pa>=0){
    dat[co]=0xee;dat[co+1]=0;dat[co+2]=ptr[pa+2];dat[co+3]=ptr[pa+3];
    if(wcc>0){dat[co+1]=wa2;wcc=wcc-wa2;wa2=wa-wa2;}
    co=co+4;
  }
  if(wcc>0){
    while(wcc>0){
      if(wcc>255){dat[co+1]=255;wcc=wcc-255;}else{dat[co+1]=wcc;wcc=0;}
      dat[co]=0;dat[co+2]=0;dat[co+3]=0;co=co+4;
    }
  }

  return(co);
}

/*
0xE6:M.CH 0xEC:PROG 0xed:AFT.K 0xea:AFT.C 0xee:PITCH 0xdd:R.BASE 0xde:R.PARA 0xe7:TEMPO
0xeb:CONTROL(1:MODULATION 7:VOLUME 10:PANPOT 11:EXPRESSION 64:HOLD 121:RESET A.C.)
*/

/***************************/
int	lsp_wait_chack(int meas)
{
  int	cc=0,i,a,ad,dummy;
  int	me_ad[2048];

  dummy=track;
  for(i=0;i<36;i++){
    if((trmod[i]&1)==0){
      track=i;add_set(i,me_ad);ad=meas_add(meas);if(ad<0){ad=tr_len[i]-4;}
      a=lsp_para_set(track,ad,me_ad,-1);if( a>cc ){cc=a;}
    }
  }
  track=dummy;
  return(cc);
}

/***************************/
void	tim_buf_trns()
{
  memcpy(rcd->tone_adr,cm6,22601);strlcpy(rcd->tonename,cm6_file,sizeof rcd->tonename);
  rcd->fmt=2;rcd->tone_valid=1;
}

void	tim_var_read()
{
  memcpy(cm6,rcd->tone_adr,22601);strlcpy(cm6_file,rcd->tonename,sizeof cm6_file);
  tim_name_read();
}

void	tim_name_read()
{
  int	i,j;

  for(i=0;i<64;i++){
    tim_name[i+128][10]=0;
    for(j=0;j<10;j++){tim_name[i+128][j]=cm6[0xe34+i*256+j];}
  }
}

void	gsd_buf_trns()
{
  memcpy(rcd->gsd_adr,gsd,4096);strlcpy(rcd->gsdname,gsd_file,sizeof rcd->gsdname);
  rcd->fmt=2;rcd->gsd_valid=1;
}

void	gsd_var_read(void)
{
  memcpy(gsd,rcd->gsd_adr,4096);strlcpy(gsd_file,rcd->gsdname,sizeof gsd_file);
}

/***************************/
int	rcp_buf_put(char *ptr,int po,int ln)
{
  char	*p;
  if((rcd->bufcap)<(po+ln)){return(po+ln);}
  p=rcd->data_adr;p+=po;
  if(ln>1414){memcpy_l(p,ptr,ln);}else{memcpy(p,ptr,ln);}
  po=po+ln;return(po);
}

int	rcp_buf_get(char *ptr,int po,int ln)
{
  char	*p;
  if((rcd->bufcap)<(po+ln)){return(po+ln);}
  p=rcd->data_adr;p+=po;
  if(ln>1414){memcpy_l(ptr,p,ln);}else{memcpy(ptr,p,ln);}
  po=po+ln;return(po);
}

/* ================================================== data */
void	dclr(void)
{
  int	i,j;
  uint8_t	a;

  for(i=0;i<36;i++){
    same_cluc(i,0);
    for(j=0;j<tr_len[i];j=j+4){
      a=trk[i][j];
      if(a>127){
	switch(a){
	case  0xe7:if(trk[i][j+2]==0){trk[i][j+2]=1;}break;

	case  0xe6:
	case  0xea:
	case  0xec:trk[i][j+3]=0;break;

	case  0xf6:
	case  0xf7:trk[i][j+1]=0;break;

	case  0xf8:trk[i][j+2]=0;trk[i][j+3]=0;break;

	case  0xf9:
	case  0xfd:trk[i][j+1]=1;trk[i][j+2]=0;trk[i][j+3]=0;break;

	case  0xfe:trk[i][j+1]=0xfe;trk[i][j+2]=0xfe;trk[i][j+3]=0xfe;
	  tr_len[i]=j+4;break;
	}
      }
    }
  }
}

/***************************/
void	same_cluc(int tr,int ofset)
{
  int	j=0,max;
  uint8_t	a;
  int	add[2048];

  max=add_set(tr,add);
  while(j<tr_len[tr]-4 ){
    a=trk[tr][j];
    if(a==0xfc){
      int no,ad=j;
    resame:
      no=trk[tr][ad+1]+(trk[tr][ad+2]&3)*256;
      if(no<max){
	ad=add[no];
	if(j!=ad && trk[tr][ad]==0xfc ){
	  goto resame;
	}

	trk[tr][j+1]=no&0xff;
	trk[tr][j+2]=(trk[tr][j+2]&0xfc)+((no>>8)&3);

      }else{ad=0;}

      ad+=44+ofset;trk[tr][j+2]=(trk[tr][j+2]&3)+(ad&0xfc);trk[tr][j+3]=ad>>8;
    }
    j=j+4;
  }
}

/***************************/
int	gomi_cut(uint8_t *ptr,int size)
{
  int	ad=0,po=0;
  uint8_t	da,db,dc,dd;

  while(ad<size){
    da=ptr[ad++];db=ptr[ad++];dc=ptr[ad++];dd=ptr[ad++];

    if(da==0x98 || da==0xf6){
      ptr[po++]=da;ptr[po++]=db;
      ptr[po++]=dc;ptr[po++]=dd;

      while(ptr[ad]==0xf7 && ad<size){
	ptr[po++]=ptr[ad++];ptr[po++]=ptr[ad++];
	ptr[po++]=ptr[ad++];ptr[po++]=ptr[ad++];
      }

      while(ptr[ad]==0xfd && ptr[ad+4]==0xf7 && ad<size){
	ptr[po++]=ptr[ad+4];ptr[po++]=ptr[ad+5];
	ptr[po++]=ptr[ad+6];ptr[po++]=ptr[ad+7];

	ptr[ad+4]=0xfd;ad+=4;
      }

    }else{
      if(da!=0xf7){
	ptr[po++]=da;ptr[po++]=db;
	ptr[po++]=dc;ptr[po++]=dd;
      }
    }
  }
  return po;
}

/***************************/
void	dinit()
{
  uint8_t	*ptr=trk[0];

  int	i,j;

  for(i=0;i<24;i++){tag[i][0]=-1;}

  track=0;btrack=0;
  buff_free=buff_size;

  for(i=0;i<36;i++){
    trk[i]=ptr;ptr+=4;tr_alc[i]=4;buff_free-=4;

    trk[i][0]=0xfe;tr_len[i]=4;tr_step[i]=0;

    for(j=0;j<4;j++){tr_pos[i][0][j]=0;tr_pos[i][1][j]=0;}
    trmod[i]=0;trno[i]=i+1;trrhy[i]=0;
    mch[i]=i+1;if(i>15){mch[i]=0;}

    trkey[i]=0;trst[i]=0;trkmemo[i][0]=0;
  }

  trrhy[9]=128;trkey[9]=128;

  for(i=0;i<12;i++){memo[i][0]=0;}
  mtitle[0]=0;cm6_file[0]=0;gsd_file[0]=0;
  mtempo=120;tbase=48;beat1=4;beat2=4;bkey=0;pbias=0;

  cminit();gsinit();rhinit();
  user_exc_init();asin_init();stgt_init();
}

/***************************/
