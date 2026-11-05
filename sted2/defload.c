/*
 * STed: defload.c (def file)
 */

#if 0
#include	"basic0.h"
#include	"BASIC.h"
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"iocslib.h"
#include	"doslib.h"
#include	"sted.h"

#if	STED
int	gpal_no[16]={
  0,14*64+28*2048+28*2,28*64+28*2048+14*2,28*64+28*2048+28*2,
  11*64+11*2048+11*2,6*64+6*2048+6*2,0,

  11*2048+14*2,14*2048+17*2, 17*2048,31*2048,31*2048+31*2,
  17*64+17*2048,31*64+31*2048,16*64+16*2048+16*2,31*64+31*2048+31*2};

uint8_t	stgt_def[30]={
  192,144, 96, 72, 48, 36, 24, 18, 12,  6,
  188,140, 92, 68, 45, 34, 22, 16, 10,  5,
  1, 15, 29, 43, 57, 71, 85, 99,113,127};
#endif

char	tim_asin2[33];
char	module_id[64];			/* port A chip: #id= text */
char	module_id_b[64];		/* port B chip: #id= text */
int	def_load_scope = DEF_SCOPE_AB;	/* UNIT SEL A / B / A&B */

/* Part table: SHARED (classic) or SPLIT (after one-time fork). */
enum { PART_SPACE_SHARED = 0, PART_SPACE_SPLIT = 1 };
enum { PART_OWN_NONE = 0, PART_OWN_A = 1, PART_OWN_B = 2 };
int	part_space_mode = PART_SPACE_SHARED;
static unsigned char part_owner[TIM_PART_MAX];
/* Set during defload for each #part= written (0-based). */
static unsigned char part_written[TIM_PART_MAX];

typedef struct DefPartSnap {
	char head[TIM_PART_MAX][24];
	char sym[TIM_PART_MAX][8];
	int16_t top[TIM_PART_MAX];
	char (*names)[TIM_NAME_WIDTH];
	unsigned char owner[TIM_PART_MAX];
	int16_t gs_bank[18 * 128];
	int16_t gs_mode[18];
	int was_shared;
} DefPartSnap;

/* Prefer remap targets above common GS/XG slots (and expand shells 7–14). */
enum { PART_REMAP_BASE0 = 49 }; /* 0-based → part number 50 */

/* Trim leading/trailing spaces into dst (NUL-terminated). */
static void
sf_trim_copy(char *dst, size_t n, const char *src)
{
	size_t len;
	if (dst == NULL || n == 0)
		return;
	if (src == NULL) {
		dst[0] = 0;
		return;
	}
	while (*src == ' ' || *src == '\t')
		src++;
	len = strlen(src);
	while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t'))
		len--;
	if (len >= n)
		len = n - 1;
	memcpy(dst, src, len);
	dst[len] = 0;
}

static void
module_id_from_mdlflag(char *dst, size_t n)
{
	const char *s = "";
	switch (mdlflag & 15) {
	case 1: s = "CM64"; break;
	case 2: s = "SC55"; break;
	case 3: s = "SC88"; break;
	case 4: s = "05RW"; break;
	default: break;
	}
	if (dst == NULL || n == 0)
		return;
	strlcpy(dst, s, n);
}

static void
module_id_store_scope(const char *idtext)
{
	char tmp[64];
	sf_trim_copy(tmp, sizeof tmp, idtext);
	if (def_load_scope != DEF_SCOPE_B)
		strlcpy(module_id, tmp, sizeof module_id);
	if (def_load_scope != DEF_SCOPE_A)
		strlcpy(module_id_b, tmp, sizeof module_id_b);
}
char	defrhyna[32][15];
uint8_t	defrhyno[32][2];

char	def_user_exc_memo[8][25];
uint8_t	def_user_exc_data[8][24];




/* ================================================== file */
#if	STED
void	rhinit()
{
  int	i;

  for(i=0;i<32;i++){rhinit2(i);}
}
void	rhinit2(int i)
{
  strcpy(rhyna[i],defrhyna[i]);
  rhyno[i][0]=defrhyno[i][0];rhyno[i][1]=defrhyno[i][1];
}
void	user_exc_init()
{
  int	i;

  for(i=0;i<8;i++){user_exc_init2(i);}
}
void	user_exc_init2(int i)
{
  int	j;

  strcpy(user_exc_memo[i],def_user_exc_memo[i]);
  for(j=0;j<24;j++){user_exc_data[i][j]=def_user_exc_data[i][j];}
}

/***************************/
void	stgt_init()
{
  int i;

  for(i=0;i<30;i++){stgt_init2(i);}
}
void	stgt_init2(int i)
{
  stgt_tbl[i]=stgt_def[i];
}
/***************************/
void	asin_change()
{
  int	a,i;

  if((mdlflag&3)==1){
    for(i=1;i<=16;i++){if(tim_asin[i]<4){tim_asin[i]=0;}}
    for(i=0x8d;i<=0x8d+8;i++){
      a=cm6[i]+1;
      if(a<17){
	if( tim_asin[a]==0){
	  if(i<0x8d+8){tim_asin[a]=1;}else{tim_asin[a]=2;}
	}
      }
    }
    for(i=0x583c;i<=0x583c+5;i++){
      a=cm6[i]+1;if(a<17){if( tim_asin[a]==0){tim_asin[a]=3;}}
    }
  }
}

/* Copy DEF #group_* (tim_asin2) → runtime PART ASSIGN (tim_asin).
   scope: DEF_SCOPE_A / B / AB. asin_init() keeps full A&B for callers. */
void	asin_init_scope(int scope)
{
  int	i, lo, hi;

  if(scope == DEF_SCOPE_A){lo=1;hi=16;}
  else if(scope == DEF_SCOPE_B){lo=17;hi=32;}
  else{lo=1;hi=32;}
  for(i=lo;i<=hi;i++){tim_asin[i]=tim_asin2[i];}
}

void	asin_init(void)
{
  asin_init_scope(DEF_SCOPE_AB);
}

static void
part_owner_clear(void)
{
  memset(part_owner, 0, sizeof part_owner);
  memset(part_written, 0, sizeof part_written);
  part_space_mode = PART_SPACE_SHARED;
}

static int
part_name_hiwater(void)
{
  int i, hi = 4 * 128;
  for (i = 0; i < TIM_PART_MAX; i++) {
    if (tim_head[i][0] == 0)
      continue;
    if (i <= 3)
      continue;
    if (tim_top[i] > 0 && tim_top[i] + 128 > hi)
      hi = tim_top[i] + 128;
  }
  if (hi > TIM_NAME_MAX)
    hi = TIM_NAME_MAX;
  return hi;
}

static int
part_alloc_names(void)
{
  int hi = part_name_hiwater();
  if (hi + 128 >= TIM_NAME_MAX)
    return -1;
  return hi;
}

/* 0-based part indices. */
static void
part_copy_slot(int from0, int to0)
{
  int n, src, dst, k;

  if (from0 < 0 || from0 >= TIM_PART_MAX || to0 < 0 || to0 >= TIM_PART_MAX)
    return;
  memcpy(tim_head[to0], tim_head[from0], sizeof tim_head[0]);
  memcpy(tim_sym[to0], tim_sym[from0], sizeof tim_sym[0]);
  if (from0 <= 3) {
    src = (from0 == 1) ? 192 : from0 * 128;
    dst = (to0 == 1) ? 192 : to0 * 128;
    if (src + 128 <= TIM_NAME_MAX && dst + 128 <= TIM_NAME_MAX)
      memcpy(tim_name[dst], tim_name[src], 128 * TIM_NAME_WIDTH);
    tim_top[to0] = tim_top[from0];
    return;
  }
  src = tim_top[from0];
  dst = part_alloc_names();
  if (dst < 0 || src <= 0 || src >= TIM_NAME_MAX) {
    tim_top[to0] = 0;
    return;
  }
  n = 128;
  if (src + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - src;
  if (dst + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - dst;
  for (k = 0; k < n; k++)
    memcpy(tim_name[dst + k], tim_name[src + k], TIM_NAME_WIDTH);
  tim_top[to0] = (int16_t)dst;
}

static void
part_mark_refs(unsigned char *used /*[TIM_PART_MAX+1] 1-based sense via index*/,
	       int lo, int hi)
{
  int i, p;
  memset(used, 0, TIM_PART_MAX + 1);
  for (i = lo; i <= hi; i++) {
    p = (unsigned char)tim_asin2[i];
    if (p > 0 && p <= TIM_PART_MAX)
      used[p] = 1;
  }
}

static int
part_slot_ok(int i0, const unsigned char *keep1, const unsigned char *block1)
{
  if (i0 < 0 || i0 >= TIM_PART_MAX)
    return 0;
  if (keep1[i0 + 1])
    return 0;
  if (block1 && block1[i0 + 1])
    return 0;
  return 1;
}

static int
part_find_free(const unsigned char *keep /*1-based*/, const unsigned char *block)
{
  int i;
  /* Prefer high slots so expand shells (7/9/11/13) stay usable for the load. */
  for (i = PART_REMAP_BASE0; i < TIM_PART_MAX; i++) {
    if (!part_slot_ok(i, keep, block))
      continue;
    if (tim_head[i][0] == 0)
      return i;
  }
  for (i = PART_REMAP_BASE0; i < TIM_PART_MAX; i++) {
    if (!part_slot_ok(i, keep, block))
      continue;
    if (part_owner[i] != PART_OWN_A && part_owner[i] != PART_OWN_B)
      return i;
  }
  for (i = 14; i < PART_REMAP_BASE0; i++) { /* avoid 0..13 specials */
    if (!part_slot_ok(i, keep, block))
      continue;
    if (tim_head[i][0] == 0)
      return i;
  }
  return -1;
}

/* Copy one part from snapshot into live slot to0 (0-based). */
static void
part_copy_from_snap(const DefPartSnap *snap, int from0, int to0)
{
  int src, dst, k, n;

  if (snap == NULL || from0 < 0 || from0 >= TIM_PART_MAX ||
      to0 < 0 || to0 >= TIM_PART_MAX)
    return;
  memcpy(tim_head[to0], snap->head[from0], sizeof tim_head[0]);
  memcpy(tim_sym[to0], snap->sym[from0], sizeof tim_sym[0]);
  if (from0 <= 3) {
    src = (from0 == 1) ? 192 : from0 * 128;
    dst = (to0 == 1) ? 192 : to0 * 128;
    if (src + 128 <= TIM_NAME_MAX && dst + 128 <= TIM_NAME_MAX)
      memcpy(tim_name[dst], &snap->names[src], 128 * TIM_NAME_WIDTH);
    tim_top[to0] = snap->top[from0];
    return;
  }
  src = snap->top[from0];
  dst = part_alloc_names();
  if (dst < 0 || src <= 0 || src >= TIM_NAME_MAX) {
    tim_top[to0] = 0;
    return;
  }
  n = 128;
  if (src + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - src;
  if (dst + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - dst;
  for (k = 0; k < n; k++)
    memcpy(tim_name[dst + k], &snap->names[src + k], TIM_NAME_WIDTH);
  tim_top[to0] = (int16_t)dst;
}

/*
 * GS #gs_pbank shells (part 7 etc.) keep names in bank-target parts
 * (CAPITAL=16…). After a fork the other port lands on a high slot that
 * no longer uses gs_bank, so flatten bank0 names from the snapshot.
 */
static void
part_flatten_gs_bank0_from_snap(const DefPartSnap *snap, int shell_part1, int to0)
{
  int bp, src, dst, k, n;

  if (snap == NULL || to0 < 0 || to0 >= TIM_PART_MAX)
    return;
  if (shell_part1 < 7 || shell_part1 > 14)
    return;
  if ((snap->gs_mode[shell_part1] & 128) == 0)
    return;
  bp = snap->gs_bank[shell_part1 * 128];
  if (bp < 1 || bp > TIM_PART_MAX)
    return;
  src = snap->top[bp - 1];
  if (src <= 0 || src >= TIM_NAME_MAX)
    return;
  dst = part_alloc_names();
  if (dst < 0)
    return;
  n = 128;
  if (src + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - src;
  if (dst + n > TIM_NAME_MAX)
    n = TIM_NAME_MAX - dst;
  for (k = 0; k < n; k++)
    memcpy(tim_name[dst + k], &snap->names[src + k], TIM_NAME_WIDTH);
  tim_top[to0] = (int16_t)dst;
}

static void
part_restore_from_snap(const DefPartSnap *snap, int idx0)
{
  int src, n;

  if (snap == NULL || idx0 < 0 || idx0 >= TIM_PART_MAX)
    return;
  memcpy(tim_head[idx0], snap->head[idx0], sizeof tim_head[0]);
  memcpy(tim_sym[idx0], snap->sym[idx0], sizeof tim_sym[0]);
  tim_top[idx0] = snap->top[idx0];
  if (idx0 <= 3) {
    src = (idx0 == 1) ? 192 : idx0 * 128;
    if (src + 128 <= TIM_NAME_MAX)
      memcpy(tim_name[src], &snap->names[src], 128 * TIM_NAME_WIDTH);
  } else if (snap->top[idx0] > 0) {
    src = snap->top[idx0];
    n = 128;
    if (src + n > TIM_NAME_MAX)
      n = TIM_NAME_MAX - src;
    if (n > 0)
      memcpy(tim_name[src], &snap->names[src], (size_t)n * TIM_NAME_WIDTH);
  }
}

void *
def_parts_prepare_partial(int scope)
{
  DefPartSnap *snap;
  size_t nbytes;

  if (scope != DEF_SCOPE_A && scope != DEF_SCOPE_B)
    return NULL;
  snap = (DefPartSnap *)calloc(1, sizeof *snap);
  if (snap == NULL)
    return NULL;
  nbytes = (size_t)TIM_NAME_MAX * TIM_NAME_WIDTH;
  snap->names = (char (*)[TIM_NAME_WIDTH])malloc(nbytes);
  if (snap->names == NULL) {
    free(snap);
    return NULL;
  }
  memcpy(snap->head, tim_head, sizeof snap->head);
  memcpy(snap->sym, tim_sym, sizeof snap->sym);
  memcpy(snap->top, tim_top, sizeof snap->top);
  memcpy(snap->names, tim_name, nbytes);
  memcpy(snap->owner, part_owner, sizeof snap->owner);
  memcpy(snap->gs_bank, gs_bank, sizeof snap->gs_bank);
  memcpy(snap->gs_mode, gs_mode, sizeof snap->gs_mode);
  snap->was_shared = (part_space_mode == PART_SPACE_SHARED);
  return snap;
}

void
def_parts_finish_partial(int scope, void *snap_v)
{
  DefPartSnap *snap = (DefPartSnap *)snap_v;
  unsigned char other_used[TIM_PART_MAX + 1], my_group[TIM_PART_MAX + 1];
  unsigned char my_loaded[TIM_PART_MAX + 1], claimed[TIM_PART_MAX + 1];
  int remap_other[TIM_PART_MAX + 1];
  int other_lo, other_hi, my_lo, my_hi;
  int me, other, i, p, q;

  if (snap == NULL)
    return;
  if (scope != DEF_SCOPE_A && scope != DEF_SCOPE_B)
    goto out;

  if (scope == DEF_SCOPE_A) {
    my_lo = 1; my_hi = 16;
    other_lo = 17; other_hi = 32;
    me = PART_OWN_A; other = PART_OWN_B;
  } else {
    my_lo = 17; my_hi = 32;
    other_lo = 1; other_hi = 16;
    me = PART_OWN_B; other = PART_OWN_A;
  }

  part_mark_refs(other_used, other_lo, other_hi);
  part_mark_refs(my_group, my_lo, my_hi);
  memset(my_loaded, 0, sizeof my_loaded);
  memset(claimed, 0, sizeof claimed);
  for (i = 0; i < TIM_PART_MAX; i++) {
    if (part_written[i] || my_group[i + 1])
      my_loaded[i + 1] = 1;
  }
  for (i = 0; i <= TIM_PART_MAX; i++)
    remap_other[i] = i;

  /*
   * Keep the just-loaded port on canonical part numbers (7/8/16… so
   * TONE LIST expand / #bank still work). Move the other port's
   * conflicting parts to high free slots (from the pre-load snapshot).
   */
  for (p = 1; p <= TIM_PART_MAX; p++) {
    int need_other = other_used[p];
    if (!need_other && snap->was_shared == 0 &&
	snap->owner[p - 1] == (unsigned char)other)
      need_other = 1;
    if (!need_other)
      continue;

    if (my_loaded[p]) {
      q = part_find_free(my_loaded, claimed);
      if (q < 0)
	continue;
      part_copy_from_snap(snap, p - 1, q);
      /* High slots skip gs_bank; flatten CAPITAL (bank0) for GS shells. */
      part_flatten_gs_bank0_from_snap(snap, p, q);
      remap_other[p] = q + 1;
      claimed[q + 1] = 1;
      part_owner[q] = (unsigned char)other;
      part_owner[p - 1] = (unsigned char)me;
    } else {
      part_restore_from_snap(snap, p - 1);
      part_owner[p - 1] = (unsigned char)other;
      claimed[p] = 1;
    }
  }

  for (i = other_lo; i <= other_hi; i++) {
    p = (unsigned char)tim_asin2[i];
    if (p > 0 && p <= TIM_PART_MAX)
      tim_asin2[i] = (char)remap_other[p];
  }
  /* Runtime PART ASSIGN must follow remapped #group_* on both ports. */
  for (i = other_lo; i <= other_hi; i++)
    tim_asin[i] = tim_asin2[i];

  for (p = 1; p <= TIM_PART_MAX; p++) {
    if (my_loaded[p])
      part_owner[p - 1] = (unsigned char)me;
  }

  /*
   * Loader brought #bank=/#rbank= (expand_map): leftover #gs_pbank from the
   * other DEF must not keep hijacking part 7 via gs_bank → CAPITAL.
   */
  if (expand_map) {
    gs_mode[7] &= (int16_t)~128;
    gs_mode[9] &= (int16_t)~128;
    gs_mode[11] &= (int16_t)~128;
    gs_mode[13] &= (int16_t)~128;
  }

  if (snap->was_shared)
    part_space_mode = PART_SPACE_SPLIT;

out:
  free(snap->names);
  free(snap);
}
/***************************/
void	cnfinit()
{
  int i;

  inpmode=0;grpmode=0;thrumode=0;vis_reso=100;

  for(i=0;i<16;i++){mplay[i][0]=0;palet_dat[i]=gpal_no[i];}
  strcpy(mplay[1],"rcp -pbs");strcpy(mplay[9],"rcp -ps");

  for(i=0;i<128;i++){keyst_table[0][i]=0;keyst_table[1][i]=0;}
  for(i=0;i<26*3*2;i++){repl_t[i][0]=0;repl_d[i][0]=0;}

  for(i=0;i<16;i++){rhy_vel[i]=0;}
  for(i=1;i<10;i++){rhy_vel[i]=i*14;}
}
#endif
/* XG expand / GM2 expand: rhythm MSB -> part 8/10/12/14 */
static unsigned char	expand_rhy_slot[128];
static int		expand_rhy_used;
static const int	expand_rhy_ids[4]={8,10,12,14};

static int	expand_ints(char *s,int *v,int maxn)
{
  int	n=0;

  while(*s && n<maxn){
    char	tok[32];
    int	i=0;
    while(*s==' ' || *s==','){s++;}
    if(*s==0){break;}
    while(*s && *s!=',' && *s!=' ' && i<30){tok[i++]=*s++;}
    tok[i]=0;
    if(i==0){break;}
    v[n++]=str_val(tok);
  }
  return(n);
}

int	expand_part_of(int owner,int msb,int lsb)
{
  int	i,any;

  /* XG expand / GM2 expand: a row belongs to one melody part.
   * The part's own row wins. A row with no part (owner 0) is the
   * fallback used by defs that put every channel on the same part.
   */
  if(msb<0 || lsb<0 || msb>127 || lsb>127){return(0);}
  any=0;
  for(i=0;i<expand_bank_n;i++){
    if(expand_msb[i]!=msb || expand_lsb[i]!=lsb){continue;}
    if(expand_owner[i]==owner){return(expand_part[i]);}
    if(expand_owner[i]==0 && any==0){any=expand_part[i];}
  }
  return(any);
}

void	expand_write_lsb(int ad)
{
  int	j;

  /* XG expand / GM2 expand: keep CC#32 if the track already has one. */
  if(!expand_map || expand_lsb_pick<0 || expand_lsb_pick>127){return;}
  j=ad;
  while(j>0){
    j-=4;
    if(j<0){break;}
    if(trk[track][j]==0xeb && trk[track][j+2]==32){
      trk[track][j+3]=(uint8_t)expand_lsb_pick;
      return;
    }
  }
}

static void	expand_bank_add(char *s)
{
  int	v[4],n,msb,lsb,part,owner;

  /* XG expand / GM2 expand: #bank= CC#0, CC#32, name-list part [, melody part].
   * The 4th field is 7, 9, 11, or 13. That row is shown only on that part,
   * so two ports can both use CC#0=0. Omitting it keeps the old file: the
   * row is visible on every melody part.
   */
  n=expand_ints(s,v,4);
  if(n<3){return;}
  msb=v[0];lsb=v[1];part=v[2];owner=0;
  if(n>=4){
    owner=v[3];
    if(owner!=7 && owner!=9 && owner!=11 && owner!=13){return;}
  }
  if(msb<0 || msb>127 || lsb<0 || lsb>127){return;}
  if(part<1 || part>=400){return;}
  if(expand_bank_n>=EXPAND_BANK_MAX){return;}
  expand_msb[expand_bank_n]=msb;
  expand_lsb[expand_bank_n]=lsb;
  expand_owner[expand_bank_n]=(unsigned char)owner;
  expand_part[expand_bank_n]=part;
  expand_bank_n++;
  expand_map=1;
  /* Disarm leftover GS #gs_pbank so TONE LIST uses expand_part_of. */
  gs_mode[7]&=(int16_t)~128;
  gs_mode[9]&=(int16_t)~128;
  gs_mode[11]&=(int16_t)~128;
  gs_mode[13]&=(int16_t)~128;
}

static void	expand_rbank_add(char *s)
{
  int	v[5],n,msb,lsb,prog,part,slot,owner;

  /* XG expand / GM2 expand: #rbank= CC#0, CC#32, program, key-name part
   * [, rhythm part]. The 5th field is 8, 10, 12, or 14. The kit list stays
   * on that part, so a bank change does not step onto the other port.
   * Omitting it keeps the old file: the first new CC#0 takes part 8, then
   * 10, 12, 14. Kit choice is the program number.
   */
  n=expand_ints(s,v,5);
  if(n<4){return;}
  msb=v[0];lsb=v[1];prog=v[2];part=v[3];owner=0;
  if(n>=5){
    owner=v[4];
    if(owner!=8 && owner!=10 && owner!=12 && owner!=14){return;}
  }
  if(msb<0 || msb>127 || prog<0 || prog>127){return;}
  if(part<1 || part>=400){return;}
  (void)lsb;
  slot=expand_rhy_slot[msb];
  if(owner!=0 && slot!=0 && slot!=owner){return;}
  if(slot==0){
    if(owner!=0){
      slot=owner;
    }else{
      if(expand_rhy_used>=4){return;}
      slot=expand_rhy_ids[expand_rhy_used++];
    }
    expand_rhy_slot[msb]=(unsigned char)slot;
    gs_bank[17*128+msb]=slot;
    gs_mode[17]|=128;
    gs_mode[slot]|=128+64;
    gs_mode[16]&=~4;
  }
  gs_bank[slot*128+prog]=part;
  expand_map=1;
}

/***************************/
void	definit()
{
  int	i,j;

  strcpy(card_name[0],"NoUse <<PCM CARD>>");
  for(i=1;i<17;i++){strcpy(card_name[i]," --------");}

  for(i=0;i<TIM_NAME_MAX;i++){tim_name[i][0]=0;}
  for(i=0;i<TIM_PART_MAX;i++){tim_top[i]=0;tim_name[i][0]=0;tim_head[i][0]=0;}

  for(i=0;i<8;i++){for(j=0;j<24;j++){
    def_user_exc_memo[i][j]=0;def_user_exc_data[i][j]=0xf7;}}

  init_exc_data[0]=255;

  for(i=0;i<18*128;i++){gs_bank[i]=0;}
  for(i=0;i<18;i++){gs_mode[i]=0;}
  gs_mode[16]=4;

  /* XG expand / GM2 expand */
  expand_bank_n=0;
  expand_map=0;
  expand_lsb_pick=-1;
  expand_rhy_used=0;
  for(i=0;i<128;i++){expand_rhy_slot[i]=0;}

  for(i=0;i<16;i++){rfilt[i][0]=0;rfilt[i][1]=0;rfilt[i+16][0]=128;rfilt[i+16][1]=128;}

  rec_met[0]=16;rec_met[1]=0;
  rec_met[2]=64;rec_met[3]=2;rec_met[4]=117;
  rec_met[5]=64;rec_met[6]=2;rec_met[7]=127;

  rhy_stest[0]=10;rhy_stest[1]=-1;
  rhy_stest[2]=-1;rhy_stest[4]=127;

  player_name[0]=0;       /* Jun.28.1998 Daisuke Nagano */
  midi_port_name[0]=0;    /* Sep.02.1998 Daisuke Nagano */
  font_name[0]=0;         /* Sep.04.1998 */
  player_flag=0;          /* Nov.25.1998 */

  lsp_wait=1;bend_range=12;
  module_id[0]=0;
  module_id_b[0]=0;
  part_owner_clear();
  tim_name_read();
}

/***************************/
/*
  cmdflag=		gs_mode=	mdlfalg=
  00	    00		   00		   00
  84218421 84218421	84218421	84218421
  00000000 00000000	00000000	00000000
  |||||||| ||||||||	||   |||	||||||||
  |||||||| ||||||||	||   |||	||||++++MODULE ID(1=CM-64 2=SC-55
  |||||||| ||||||||	||   |||	++++----	  3=SC-88)
  |||||||| ||||||||	||   ||+bit0	1=capital down on
  |||||||| ||||||||	||   |+-bit1	1=rhy.cap down on
  |||||||| ||||||||	||   +--bit2	0=bank msb 1=bank lsb
  |||||||| ||||||||	||
  |||||||| ||||||||	|+------bit6	1=rhythm mode on
  |||||||| ||||||||	+-------bit7	1=bank mode on
  |||||||| |||||||+TIMBRE NAME
  |||||||| ||||||+-RET&SPC
  |||||||| |||||+--SPC.CTRL SEL
  |||||||| ||||+---BUF LOAD
  |||||||| |||+----BUF SET
  |||||||| ||+-----PROG_MIN
  |||||||| |+------ALL NOTE OFF
  |||||||| +-------RCD POINT PLAY
  |||||||+Rol.Para HEX
  ||||||+-MOUSE CONTROL (0=on 1=off)
  |||||+--RHY AS. check (0=yes/no check 1=all yes)
  ||||+---RHY AS. change(0=on 1=off)
  |||+----timbre select (0=val 1=sel)
  ||+-----timbre select mode(0=normal 1=bank)
  |+------meas play mluti track lsp
  +-------rcp panel mode

  cnfflag=
  00       00
  84218421 84218421
  00000000 00000000
  |||||||| ||||||||
  |||||||| |||||||+file selecter sort
  |||||||| ||||||+-file write check
  |||||||| |||||+--file back up
  |||||||| ||||+---file selecter path move(0=on 1=off)
  |||||||| |||+----
	|||||||| ||+-----
	|||||||| |+------file save mode(0=r36 1=rcp)
	|||||||| +-------file load part assign read
	||||||||
	|||||||+
	||||||+-t.set. option mode(0=option2 1=option1)
	|||||+--t.edit option mode(0=option2 1=option1)
	||||+---
	|||+----replace selecter mode(0=normal 1=select)
	||+-----one touch stgt	(0=normal 1=chord)
	|+------visual edit	(0=normal 1=wide)
	+-------gs bar display	(0=normal 1=reverse)
	*/

/* Items a partial load may leave alone. A text file that omits the key
 * keeps the previous value. A .dfb always stores the block; all-empty
 * is treated as omitted.
 */
struct DefSharedSnap {
  char rec_met[10];
  char rhy_stest[8];
  char rhy_name[32][15];
  uint8_t rhy_no[32][2];
  char exc_memo[8][25];
  uint8_t exc_data[8][24];
  uint8_t init_exc[258];
};
typedef struct DefSharedSnap DefSharedSnap;
static void def_shared_save(DefSharedSnap *s);
static void def_shared_restore_all(const DefSharedSnap *s);
static void def_shared_restore_missing(const DefSharedSnap *s, int saw_rec,
    int saw_rhy, int saw_exc, int saw_init_exc);
static void def_shared_restore_unset(const DefSharedSnap *s);

int	defload(char *fi,int md)
{
  /* Line buffer sized to PATHLEN (same idea as classic MAXPATHLEN). */
  char	tmp0[PATHLEN * 2],tmp1[PATHLEN],li[PATHLEN],li2[PATHLEN];
  int	ad,nm,ln,n,i0=0,i1=0,f,mutef=0,initf=0;
  int	last_no=4*128,last_co=0;
  int	partial=0, saw_rec_met=0, saw_rhy_as=0, saw_user_exc=0, saw_init_exc=0;
  DefSharedSnap snap;
  FILE	*fp;

  if(str_search(fi,".dfb")||str_search(fi,".DFB")>0){return def_bload(fi,md);}

  /*if(!(fp= fopen2(fi,"rb"))){return(-1);}*/

  if ((fp=fopen2(fi,"rb"))==NULL) {
    /*strcpy( tmp0, STED_DEFAULT_PATH );*/
	strcpy( tmp0, s_sted_default_path ); /* Jun.01.2003 by Toshi Nagata */
    if ( tmp0[strlen(tmp0)]!='/' ) strcat(tmp0,"/");
    strcat( tmp0, (strrchr(fi,'/')==NULL)?fi:(strrchr(fi,'/')+1) );
    if ((fp=fopen2( tmp0, "rb" ))==NULL) return(-1);
  } /* Oct.13.1998 by Daisuke Nagano */
  ln=fread(dat,1,work_size,fp);ad=0;nm=99999;
  memset(part_written, 0, sizeof part_written);
  partial = (def_load_scope == DEF_SCOPE_A || def_load_scope == DEF_SCOPE_B);
  if (partial)
    def_shared_save(&snap);

#if	STED
  if(md==0){
    /*B_PRINT("եΥǤ\r\n");*/ /* Oct.17.1998 by Daisuke Nagano */
  }
  if(md==1){
    msg(_("Loading config file.\n"));
  }
#endif
#if	CONV
  B_PRINT(_("Compiling config file.\n"));
#endif

  while(ln>0){
  top:
    n=0;
    while(1){
      uint8_t	a;
    loop:
      a=dat[ad++];
      ln--;if(ln==0){ln=fread(dat,1,work_size,fp);ad=0;if(ln==0){break;}}
      if(a<32){
	/*if(a==13){break;}*/ /* Sep.02.1998 Daisuke Nagano */
	if(a==13||a==0x0a){break;}
	if(a==9){goto loop;}
	a=32;
      }else{
	if(a=='*'){a=0;
	if(n!=0 && li[n-1]=='\\'){n--;a='*';}
	}
	if(a=='=' && li[0]=='#' && n>0){while(n>0 && li[n-1]==' '){n--;}}
      }
      if( n>=PATHLEN-1 ){
#if	STED
	if(md==1){msg(_("Invalid file."));}
	else{B_PRINT(_("Invalid file.\n"));}
#endif
	if(partial){def_shared_restore_all(&snap);}
	fclose(fp);
	return(-1);
      }
      li[n++]=a;
    }
    li[n]=0;
    /*if(dat[ad]==0x0a){ad++;*/ /* Sep.02.1998 Daisuke Nagano */
    if(dat[ad]==0x0a||dat[ad]==0x0d){ad++;
    ln--;if(ln==0){ln=fread(dat,1,work_size,fp);ad=0;}
    }

    spcut(li);
    if( li[0]==0 ){goto top;}

    if( li[0]=='#' ){
      strcpy(li2,li);jstrupr(li2);

      if( str_search(li2,"#END")>0 ){break;}

#if	STED
      if( str_search(li2,"#COMMENT=")>0 ){strlcpy(comment,&li[9],sizeof comment);}

      if( str_search(li2,"#DEF_FILE_A=")>0 ){
	if(def_file_a[0]==0){strcpy(def_file_a,&li[12]);spcut(def_file_a);}}
      else if( str_search(li2,"#DEF_FILE_B=")>0 ){
	if(def_file_b[0]==0){strcpy(def_file_b,&li[12]);spcut(def_file_b);}}
      else if( str_search(li2,"#DEF_FILE=")>0 ){
	if(def_file[0]==0){strcpy(def_file,&li[10]);}}
      if( str_search(li2,"#DEF_PATH=")>0 ){
	if(def_path[0]==0){strcpy(def_path,&li[10]);}}

      if( str_search(li2,"#FON_PATH=")>0 ){
	if(fon_path[0]==0){strcpy(fon_path,&li[10]);}}
      if( str_search(li2,"#HLP_PATH=")>0 ){
	if(hlp_path[0]==0){strcpy(hlp_path,&li[10]);}}

      if( str_search(li2,"#PRT_PATH=")>0 ){
	if(prt_path[0]==0){strcpy(prt_path,&li[10]);}}
      if( str_search(li2,"#TRK_PATH=")>0 ){
	if(trk_path[0]==0){strcpy(trk_path,&li[10]);}}

      if(str_search(li2,"#FSORT=ON"    )>0){cnfflag|=0x0001;}
      if(str_search(li2,"#FCHECK=ON"   )>0){cnfflag|=0x0002;}
      if(str_search(li2,"#FBACKUP=ON"  )>0){cnfflag|=0x0004;}
      if(str_search(li2,"#FSEL_MOV=OFF")>0){cnfflag|=0x0008;}
      if(str_search(li2,"#SAVE_DEF=RCP")>0){cnfflag|=0x0040;}
      if(str_search(li2,"#PAS_READ=OFF")>0){cnfflag|=0x0080;}

      /*			if(str_search(li2,"#MAIN_OPT=ON" )>0){cnfflag|=0x0100;}*/
      if(str_search(li2,"#TSET_OPT=ON" )>0){cnfflag|=0x0200;}
      if(str_search(li2,"#TEDT_OPT=ON" )>0){cnfflag|=0x0400;}
      if(str_search(li2,"#REPL_SEL=ON" )>0){cnfflag|=0x1000;}

      if(str_search(li2,"#ONE_CHORD=ON")>0){cnfflag|=0x2000;}
      if(str_search(li2,"#VIS_WIDE=ON" )>0){cnfflag|=0x4000;}
      if(str_search(li2,"#BAR_REV=ON"  )>0){cnfflag|=0x8000;}

      if(str_search(li2,"#TONENAME=OFF")>0){cmdflag|=0x0001;}
      if(str_search(li2,"#ENTER=ON"    )>0){cmdflag|=0x0002;}
      if(str_search(li2,"#SPC_SEL=ON"  )>0){cmdflag|=0x0004;}
      if(str_search(li2,"#PROG_MIN=1"  )>0){cmdflag|=0x0020;}
      if(str_search(li2,"#NOTEOFF=ON"  )>0){cmdflag|=0x0040;}
      if(str_search(li2,"#ROL_HEX=ON"  )>0){cmdflag|=0x0100;}
      if(str_search(li2,"#RHY_TRK=ON"  )>0){cmdflag|=0x0400;}
      if(str_search(li2,"#RHY_TRK=OFF" )>0){cmdflag|=0x0800;}
      if(str_search(li2,"#TONESEL=OFF" )>0){cmdflag|=0x1000;}
      if(str_search(li2,"#TSELBANK=ON" )>0){cmdflag|=0x2000;}
      if(str_search(li2,"#MP_MLSP=OFF" )>0){cmdflag|=0x4000;}

      /*			if(str_search(li2,"#RHY_SYM=ON"  )>0){cmdflag|=0x20000;}*/
      if(str_search(li2,"#THRUMODE=ON" )>0){thrumode=1;}

      if( str_search(li2,"#TIME_LAG=")>0){
	int n=str_val(&li[10]);if( n>99 && n<65536 ){tm_lag=n;}
      }
      if( str_search(li2,"#EDIT_MODE=")>0){
	int n=str_val(&li[11]);if( n>=0 && n<6 ){inpmode=n;}
      }
      if( str_search(li2,"#GRAP_MODE=")>0){
	int n=str_val(&li[11]);if( n>=0 && n<8 ){grpmode=n;}
      }

      if( str_search(li2,"#VIS_RESO=")>0){
	int n=str_val(&li[10]);if( n>=1 && n<=100 ){vis_reso=n;}
      }

      if( str_search(li2,"#PROGRAM=")>0 ){cnf_prog(li,li2,0);}
      if( str_search(li2,"#SUBPROG=")>0 ){cnf_prog(li,li2,1);}

      if( str_search(li2,"#REPLACE=")>0 ){cnf_repl(li,li2,0);}
      if( str_search(li2,"#DELETE=")>0 ){cnf_repl(li,li2,1);}
      if( str_search(li2,"#FIND=")>0 ){cnf_repl(li,li2,2);}

      if( str_search(li2,"#MPLAY=")>0 ){
	int	c,n;
	n=li2[7]-'0';
	if(n>=0 && n<16){
	  strcpy(tmp1,&li[9]);strcat(tmp1,",");
	  while(n<16){
	    c=str_search(tmp1,",");if( c==0 ){break;}
	    strcpy(tmp0,tmp1);tmp0[c-1]=0;spcut(tmp0);
        memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	    tmp0[15]=0;
	    strcpy(mplay[n],tmp0);
	    n++;
	  }
	}
      }

      if( str_search(li2,"#RHY_VEL=")>0 ){cnf_rvel(li);}

      if( str_search(li2,"#FKEY_ST=")>0 ){cnf_stgt(li,0);}
      if( str_search(li2,"#FKEY_GT=")>0 ){cnf_stgt(li,10);}
      if( str_search(li2,"#FKEY_VE=")>0 ){cnf_stgt(li,20);}

      if( str_search(li2,"#KEYST=")>0 ){
	int	c,n,k=0;
	strcpy(tmp1,&li[7]);strcat(tmp1,",");n=0;
	while(n<3){
	  c=str_search(tmp1,",");if( c==0 ){break;}
	  strcpy(tmp0,tmp1);tmp0[c-1]=0;spcut(tmp0);
      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	  if(n==0){
	    k=ctc(64,tmp0,0);
	    if(k<0||k>127){break;}
	  }else{
	    keyst_table[n-1][k]=str_val(tmp0);
	  }
	  n++;
	}
      }

      if( str_search(li2,"#PALET")>0 ){
	int	c,n;
	c=str_search(li,"=");
	if(c){
	  li2[c]=0;n=str_val(&li2[6]);
	  if(n>=0 && n<16){
	    strcpy(tmp1,&li[c]);strcat(tmp1,",");
	    while(n<16){
	      c=str_search(tmp1,",");if( c==0 ){break;}
	      strcpy(tmp0,tmp1);tmp0[c-1]=0;
	      spcut(tmp0);palet_dat[n]=str_val(tmp0);
          memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
          n++;
	    }
	  }
	}
      }

#endif

      /* #module=: compat only (still kept in module[]); chips use #id=. */
      if( str_search(li2,"#MODULE=")>0 ){
	strlcpy(module,&li[8],sizeof module);
      }

      if( str_search(li2,"#ID=")>0 ){
	strcpy(tmp1,&li2[4]);
	if( str_search(tmp1,"CM64")>0 ){mdlflag|=1;}
	if( str_search(tmp1,"SC55")>0 ){mdlflag|=2;}
	if( str_search(tmp1,"SC88")>0 ){mdlflag|=3;}
	if( str_search(tmp1,"05RW")>0 ){mdlflag|=4;}
	/* Original-case value from li (li2 is uppercased). */
	module_id_store_scope(&li[4]);
      }
      if( str_search(li2,"#MUTE=")>0 ){
	strcpy(tmp1,&li2[6]);mutef=0;
	if( str_search(tmp1,"OFF")>0  ){mutef=1;}
	if( str_search(tmp1,"CM64")>0 ){mutef=2;}
	if( str_search(tmp1,"SC55")>0 ){mutef=3;}
      }
      if( str_search(li2,"#INIT=")>0 ){
	strcpy(tmp1,&li2[6]);initf=0;
	if( str_search(tmp1,"OFF")>0  ){initf=1;}
	if( str_search(tmp1,"CM64")>0 ){initf=2;}
	if( str_search(tmp1,"SC55")>0 ){initf=3;}
	if( str_search(tmp1,"SC88")>0 ){initf=5;}
      }

      if( str_search(li2,"#LSP_WAIT=")>0){
	int n=str_val(&li[10]);if( n>=-1 && n<16 ){lsp_wait=n;}
      }
      if( str_search(li2,"#PB_RANGE=")>0){
	int n=str_val(&li[10]);if( n>0 && n<96 ){bend_range=n;}
      }

      if( str_search(li2,"#GROUP")>0 ){
	int	c,n;
	c=str_search(li,"=");
	if(c!=0){
	  n=0;if(li2[7]=='B'){n=16;}
	  /* Partial UNIT SEL: apply only the selected port's #group_*. */
	  if(!(def_load_scope == DEF_SCOPE_A && n == 16) &&
	     !(def_load_scope == DEF_SCOPE_B && n == 0)){
	    strcpy(tmp1,&li[c]);strcat(tmp1,",");
	    while(n<32){
	      c=str_search(tmp1,",");if(c==0){break;}
	      strcpy(tmp0,tmp1);tmp0[c-1]=0;
	      n++;tim_asin2[n]=str_val(tmp0);
	      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	    }
	  }
	}
      }
      if( str_search(li2,"#CARD_NO=")>0 ){
	int n=str_val(&li[9]);if(n>=0 && n<=16){card_no[0]=n;}
      }

      /* #part= (legacy≡A when shared), #part_a=, #part_b= */
      if( str_search(li2,"#PART_A=")>0 || str_search(li2,"#PART_B=")>0 ||
	  str_search(li2,"#PART=")>0 ){
	int	ch,c,n,skip=0,is_a,is_b,off;
	is_a = (str_search(li2,"#PART_A=")>0);
	is_b = (!is_a && str_search(li2,"#PART_B=")>0);
	if(is_a && def_load_scope == DEF_SCOPE_B) skip=1;
	if(is_b && def_load_scope == DEF_SCOPE_A) skip=1;
	/* Plain #part=: both ports on AB; on partial, applies to that port. */
	if(skip){
	  nm=99999;
	}else{
	  off = is_a ? 8 : (is_b ? 8 : 6);
	  ch=str_val(&li2[off])-1;
	  c=str_search(li,",");
	  if(ch>=0 && ch<400){
	    strcpy(tmp1,&li[c]);strcat(tmp1,",");n=0;
	    while(n<2){
	      c=str_search(tmp1,",");if( c==0 ){break;}
	      strcpy(tmp0,tmp1);tmp0[c-1]=0;
	      if(n==0){strlcpy(tim_head[ch],tmp0,sizeof tim_head[ch]);}
	      else{strlcpy(tim_sym[ch],tmp0,sizeof tim_sym[ch]);}
	      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	      n++;
	    }
	    nm=ch*128;if( ch==1 ){nm=192;}
	    if(tmp1[0]!=0){nm+=str_val(tmp1);}

	    if(ch>3){
	      if(last_no<TIM_NAME_MAX-1){
		tim_name[last_no++][0]=0;
		tim_top[ch]=last_no;last_co=0;
		if(tmp1[0]!=0){last_co=str_val(tmp1);}
	      }
	    }
	    part_written[ch]=1;
	    if(def_load_scope == DEF_SCOPE_A || is_a)
	      part_owner[ch]=PART_OWN_A;
	    else if(def_load_scope == DEF_SCOPE_B || is_b)
	      part_owner[ch]=PART_OWN_B;
	  }
	}
      }
      if( str_search(li2,"#USER_EXC=")>0 ){
	saw_user_exc=1;
	int	ch,c;
	char	tmp9[128];
	ch=str_val(&li2[10]);
	c=str_search(li,",");
	if(ch>=0 && ch<=7){
	  int	i=0,j=0;
	  strcpy(tmp1,&li[c]);
	  c=str_search(tmp1,",");
	  strcpy(tmp0,tmp1);if(c){tmp0[c-1]=0;}
	  spcut(tmp0);

	  while(i<c && j<24 && tmp0[i]!=0){
	    int	a;
	    if(tmp0[i]==32){i++;}else{
	      strlcpy(tmp9,&tmp0[i],3);
	      if(tmp9[1]==0){
		tmp9[1]='H';tmp9[2]=0;
	      }else{
		tmp9[2]='H';tmp9[3]=0;
	      }
	      a=str_val(tmp9);tmp9[2]=0;
	      if(strcmp(tmp9,"GT")==0){a=0x80;}
	      if(strcmp(tmp9,"VE")==0){a=0x81;}
	      if(strcmp(tmp9,"CH")==0){a=0x82;}
	      if(strcmp(tmp9,"CS")==0){a=0x83;}
	      if(strcmp(tmp9,"SS")==0){a=0x84;}

	      def_user_exc_data[ch][j++]=a;
	      i=i+2;
	    }
	  }
	  if(c){
	    strcpy(tmp0,&tmp1[c]);
	    tmp0[24]=0;strcpy(def_user_exc_memo[ch],tmp0);
	  }
	}
      }
      if( str_search(li2,"#INIT_EXC=")>0 ){saw_init_exc=1;def_init(li);}

      if( str_search(li2,"#CARD=")>0 ){
	int	c,ch;
	strcpy(tmp1,&li[6]);
	c=str_search(tmp1,",");
	if(c){
	  strcpy(tmp0,tmp1);tmp0[c-1]=0;ch=str_val(tmp0);
	  strcpy(tmp0,&tmp1[c]);
	  if(ch>0 && ch<17){
	    strlcpy(card_name[ch],tmp0,sizeof card_name[ch]);
	    nm=(ch+15)*128;

	    if(last_no<TIM_NAME_MAX-1){
	      tim_name[last_no++][0]=0;
	      tim_top[ch+15]=last_no;last_co=0;
	    }
	  }
	}
      }

      /* XG expand / GM2 expand. Checked before the GS bank directives. */
      if( str_search(li2,"#BANK=") >0 ){expand_bank_add(strchr(li,'=')+1);}
      if( str_search(li2,"#RBANK=")>0 ){expand_rbank_add(strchr(li,'=')+1);}

      if( str_search(li2,"#GS_PBANK")>0 ){
	gs_mode[ 7]|=128   ;nm=0x10380;
	/* GS bank table replaces expand addressing for this load. */
	expand_map=0;expand_bank_n=0;
      }
      if( str_search(li2,"#GS_RBANK")>0 ){gs_mode[ 8]|=128+64;nm=0x10400;}
      if( str_search(li2,"#GM_PBANK")>0 ){gs_mode[ 9]|=128   ;nm=0x10480;}
      if( str_search(li2,"#GM_RBANK")>0 ){gs_mode[10]|=128+64;nm=0x10500;}
      if( str_search(li2,"#GX_PBANK")>0 ){gs_mode[11]|=128   ;nm=0x10580;}
      if( str_search(li2,"#GX_RBANK")>0 ){gs_mode[12]|=128+64;nm=0x10600;}
      if( str_search(li2,"#GY_PBANK")>0 ){gs_mode[13]|=128   ;nm=0x10680;}
      if( str_search(li2,"#GY_RBANK")>0 ){gs_mode[14]|=128+64;nm=0x10700;}

      if( str_search(li2,"#GS_PGROUP")>0 ){gs_mode[16]|=128   ;nm=0x10800;}
      if( str_search(li2,"#GS_RGROUP")>0 ){gs_mode[17]|=128+64;nm=0x10880;}

      if( str_search(li2,"#GS_GMODE=")>0){
	int	n=str_val(&li[10]);
	if( n>=0 && n<16){gs_mode[16]&=0xf0;gs_mode[16]|=n;}
      }

      if( str_search(li2,"#GS_MODE=")>0){
	int	n=str_val(&li[9]);
	if( n>=0 && n<16){gs_mode[7]&=0xf0;gs_mode[7]|=n;}
      }
      if( str_search(li2,"#GM_MODE=")>0){
	int	n=str_val(&li[9]);
	if( n>=0 && n<16){gs_mode[9]&=0xf0;gs_mode[9]|=n;}
      }
      if( str_search(li2,"#GX_MODE=")>0){
	int	n=str_val(&li[9]);
	if( n>=0 && n<16){gs_mode[11]&=0xf0;gs_mode[11]|=n;}
      }
      if( str_search(li2,"#GY_MODE=")>0){
	int	n=str_val(&li[9]);
	if( n>=0 && n<16){gs_mode[13]&=0xf0;gs_mode[13]|=n;}
      }

      if( str_search(li2,"#RHY_AS=")>0 ){
	saw_rhy_as=1;
	int	c;
	nm=0x10000+128*18;i0=0;i1=0;

	c=str_search(li,"=");
	if(c!=0){
	  int n=0;
	  strcpy(tmp1,&li[c]);strcat(tmp1,",");
	  while(n<6){
	    int cc;
	    c=str_search(tmp1,",");if(c==0){break;}
	    strcpy(tmp0,tmp1);tmp0[c-1]=0;
	    if(n==0){cc=strch(0,tmp0);
	    }else{cc=str_val(tmp0);}
	    if((cc>=0||(n>0 && n<5))&& cc<=127){rhy_stest[n]=cc;}
        memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	    n++;
	  }
	}
      }


      if( str_search(li2,"#REC_MET")>0 ){
	saw_rec_met=1;
	int	c,n;
	c=str_search(li,"=");
	if(c!=0){
	  n=0;
	  strcpy(tmp1,&li[c]);strcat(tmp1,",");
	  while(n<10){
	    c=str_search(tmp1,",");if(c==0){break;}
	    strcpy(tmp0,tmp1);tmp0[c-1]=0;
	    rec_met[n++]=str_val(tmp0);
        memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
	  }
	}
      }

      if( str_search(li2,"#DEFOFF")>0 ){nm=99999;}


      /* the following configurations are introduced only UNIX version */

      if( str_search(li2,"#PLAYER=")>0 ){
	player_flag=li[8]-'0';
	strlcpy(player_name, &li[10], sizeof player_name); }
      /* Jun.28.1998 Daisuke Nagano, Nov.12.1998 kino */

      /*if( str_search(li2,"#ISSMFPLAYER=TRUE")>0 ){issmfplayer=1;}*/
      /* Aug.09.1998 Daisuke Nagano, Nov.12.1998 kino */

      if( str_search(li2,"#MIDI_PORT=")>0 ){
	strlcpy(midi_port_name, &li[11], sizeof midi_port_name); }
      /* Sep.02.1998 Daisuke Nagano */

      if( str_search(li2,"#FONT_NAME=")>0 ){
	strlcpy(font_name, &li[11], sizeof font_name); }
      /* Sep.04.1998 Daisuke Nagano */

#if defined(USE_MACGUI)
	{
	  XSTed_configure_mac_settings(li);
	}
#else
      if ( str_search(li2, "#KEY_XF1=")>0 )
	strlcpy( KEY_XF1, &li[9], sizeof KEY_XF1);
      if ( str_search(li2, "#KEY_XF2=")>0 )
	strlcpy( KEY_XF2, &li[9], sizeof KEY_XF2);
      if ( str_search(li2, "#KEY_XF3=")>0 )
	strlcpy( KEY_XF3, &li[9], sizeof KEY_XF3);
      if ( str_search(li2, "#KEY_XF4=")>0 )
	strlcpy( KEY_XF4, &li[9], sizeof KEY_XF4);
      if ( str_search(li2, "#KEY_XF5=")>0 )
	strlcpy( KEY_XF5, &li[9], sizeof KEY_XF5);
      if ( str_search(li2, "#KEY_KANA=")>0 )
	strlcpy( KEY_KANA, &li[10], sizeof KEY_KANA);
      if ( str_search(li2, "#KEY_KIGO=")>0 )
	strlcpy( KEY_KIGO, &li[10], sizeof KEY_KIGO);
      if ( str_search(li2, "#KEY_TOROKU=")>0 )
	strlcpy( KEY_TOROKU, &li[12], sizeof KEY_TOROKU);
      if ( str_search(li2, "#KEY_INS=")>0 )
	strlcpy( KEY_INS, &li[9], sizeof KEY_INS);
      if ( str_search(li2, "#KEY_DEL=")>0 )
	strlcpy( KEY_DEL, &li[9], sizeof KEY_DEL);
      if ( str_search(li2, "#KEY_HOME=")>0 )
	strlcpy( KEY_HOME, &li[10], sizeof KEY_HOME);
      if ( str_search(li2, "#KEY_UNDO=")>0 )
	strlcpy( KEY_UNDO, &li[10], sizeof KEY_UNDO);
      if ( str_search(li2, "#KEY_RUP=")>0 )
	strlcpy( KEY_RUP, &li[9], sizeof KEY_RUP);
      if ( str_search(li2, "#KEY_RDOWN=")>0 )
	strlcpy( KEY_RDOWN, &li[11], sizeof KEY_RDOWN);
      if ( str_search(li2, "#KEY_OPT1=")>0 )
	strlcpy( KEY_OPT1, &li[10], sizeof KEY_OPT1);
      if ( str_search(li2, "#KEY_OPT2=")>0 )
	strlcpy( KEY_OPT2, &li[10], sizeof KEY_OPT2);
#endif

    }else{
      int	c;
      char	*pp=li,*tmp0;
      strcat(li,",");
      while( nm<99999 ){
	c=str_search(pp,",");if( c==0 ){break;}
	tmp0=pp;tmp0[c-1]=0;pp+=c;

	if(nm<0x10000 ){
	  if(c>15){tmp0[14]=0;}
	  spcut(tmp0);

	  if( nm<0x200 ){
	    strcpy(tim_name[nm],tmp0);
	  }else{
	    if(last_no<TIM_NAME_MAX-1){
	      if(tmp0[0]!=0){
		strcpy(tim_name[last_no],tmp0);
		tim_name[last_no++][14]=last_co;
	      }
	      if(last_co++==127){nm=99999;}
	    }
	  }
	}else{
	  if(nm<0x10900){
	    int ch=str_val(tmp0);
	    if(ch<400){
	      gs_bank[nm-0x10000]=ch;
	    }
	  }else{
	    if(i0<32){
	      if(i1){
		defrhyno[i0][i1-1]=str_val(tmp0);
		/*if(i1==1){
		  defrhyno[i0][0]=ctc(60,tmp0,0);
		  }else{
		  defrhyno[i0][1]=str_val(tmp0);
		  }*/
	      }else{
		if(c>15){tmp0[14]=0;}
		strcpy(defrhyna[i0],tmp0);
	      }
	      i1++;if(i1>=3){i1=0;i0++;}
	    }
	  }
	}
	nm++;
      }
      /*			if(md==1){msg(fstr(last_no,5));}*/
    }
  }
  fclose(fp);
  tim_name[last_no++][0]=0;

#if	STED
  f=(mdlflag&0x7);if(f>2){f=2;}
  if(f>0 && f<4){rcd->moduletype=f-1;}

  if(mutef){rcd->mute_mode=mutef-1;}
  if(initf){rcd->init_mode=initf-1;}

#endif
#if	CONV
  def_bsave(fi,last_no,mutef,initf);
#endif

	/*  2015.3.21. Toshi Nagata  */
	if (def_path[0] == 0) {
		snprintf(def_path, sizeof def_path, "%s/deffile", s_sted_default_path);
	}
	/*  End Toshi Nagata  */
	/* Empty #prt_path uses the support-folder library, same as #def_path.
	 * sted.c still copies rcp_path if this stays empty. */
	if (prt_path[0] == 0 && s_sted_default_path[0] != 0) {
		snprintf(prt_path, sizeof prt_path, "%s/partfile", s_sted_default_path);
	}

  if(partial){
    def_shared_restore_missing(&snap, saw_rec_met, saw_rhy_as,
	saw_user_exc, saw_init_exc);
  }

  return(0);
}

static int
def_load_is_partial(void)
{
  return def_load_scope == DEF_SCOPE_A || def_load_scope == DEF_SCOPE_B;
}

static void
def_shared_save(DefSharedSnap *s)
{
  memcpy(s->rec_met, rec_met, sizeof s->rec_met);
  memcpy(s->rhy_stest, rhy_stest, sizeof s->rhy_stest);
  memcpy(s->rhy_name, defrhyna, sizeof s->rhy_name);
  memcpy(s->rhy_no, defrhyno, sizeof s->rhy_no);
  memcpy(s->exc_memo, def_user_exc_memo, sizeof s->exc_memo);
  memcpy(s->exc_data, def_user_exc_data, sizeof s->exc_data);
  memcpy(s->init_exc, init_exc_data, sizeof s->init_exc);
}

static void
def_shared_restore_all(const DefSharedSnap *s)
{
  memcpy(rec_met, s->rec_met, sizeof rec_met);
  memcpy(rhy_stest, s->rhy_stest, sizeof rhy_stest);
  memcpy(defrhyna, s->rhy_name, sizeof defrhyna);
  memcpy(defrhyno, s->rhy_no, sizeof defrhyno);
  memcpy(def_user_exc_memo, s->exc_memo, sizeof def_user_exc_memo);
  memcpy(def_user_exc_data, s->exc_data, sizeof def_user_exc_data);
  memcpy(init_exc_data, s->init_exc, sizeof s->init_exc);
}

static void
def_shared_restore_missing(const DefSharedSnap *s, int saw_rec, int saw_rhy,
    int saw_exc, int saw_init_exc)
{
  if (!saw_rec)
    memcpy(rec_met, s->rec_met, sizeof rec_met);
  if (!saw_rhy) {
    memcpy(rhy_stest, s->rhy_stest, sizeof rhy_stest);
    memcpy(defrhyna, s->rhy_name, sizeof defrhyna);
    memcpy(defrhyno, s->rhy_no, sizeof defrhyno);
  }
  if (!saw_exc) {
    memcpy(def_user_exc_memo, s->exc_memo, sizeof def_user_exc_memo);
    memcpy(def_user_exc_data, s->exc_data, sizeof def_user_exc_data);
  }
  if (!saw_init_exc)
    memcpy(init_exc_data, s->init_exc, sizeof s->init_exc);
}

static int
def_rec_met_unset(void)
{
  int i;
  for (i = 0; i < 10; i++)
    if (rec_met[i] >= 0)
      return 0;
  return 1;
}

static int
def_rhy_para_unset(void)
{
  int i;
  for (i = 0; i < 6; i++)
    if (rhy_stest[i] >= 0)
      return 0;
  return 1;
}

static int
def_rhy_names_unset(void)
{
  int i;
  for (i = 0; i < 32; i++)
    if (defrhyna[i][0] != 0 || defrhyno[i][0] != 0 || defrhyno[i][1] != 0)
      return 0;
  return 1;
}

static int
def_user_exc_unset(void)
{
  int i, j;
  for (i = 0; i < 8; i++) {
    if (def_user_exc_memo[i][0] != 0)
      return 0;
    for (j = 0; j < 24; j++)
      if (def_user_exc_data[i][j] != 0xf7)
	return 0;
  }
  return 1;
}

/* .dfb always stores these blocks. All-empty means the source had no value. */
static void
def_shared_restore_unset(const DefSharedSnap *s)
{
  if (def_rec_met_unset())
    memcpy(rec_met, s->rec_met, sizeof rec_met);
  if (def_rhy_para_unset())
    memcpy(rhy_stest, s->rhy_stest, sizeof rhy_stest);
  if (def_rhy_names_unset()) {
    memcpy(defrhyna, s->rhy_name, sizeof defrhyna);
    memcpy(defrhyno, s->rhy_no, sizeof defrhyno);
  }
  if (def_user_exc_unset()) {
    memcpy(def_user_exc_memo, s->exc_memo, sizeof def_user_exc_memo);
    memcpy(def_user_exc_data, s->exc_data, sizeof def_user_exc_data);
  }
  if (init_exc_data[0] == 0xff)
    memcpy(init_exc_data, s->init_exc, sizeof s->init_exc);
}

/***************************//*4.66(2.18) 4.00(1.85)*/
/***************************/
#if	STED
void	cnf_stgt(char *li,int nn)
{
  int	c,n;
  char	tmp0[1024];
  char	tmp1[1024];

  c=str_search(li,"=");
  if(c!=0){
    strcpy(tmp1,&li[c]);strcat(tmp1,",");n=nn;
    while(n<30){
      c=str_search(tmp1,",");if(c==0){break;}
      strcpy(tmp0,tmp1);tmp0[c-1]=0;
      stgt_def[n++]=str_val(tmp0);
      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
    }
  }
}

/***************************/
void	cnf_rvel(char *li)
{
  int	c,n;
  char	tmp0[1024];
  char	tmp1[1024];

  c=str_search(li,"=");
  if(c!=0){
    n=0;
    strcpy(tmp1,&li[c]);strcat(tmp1,",");
    while(n<15){
      int cc;
      c=str_search(tmp1,",");if(c==0){break;}
      strcpy(tmp0,tmp1);tmp0[c-1]=0;
      n++;cc=str_val(tmp0);
      if(cc>=0 && cc<=127){rhy_vel[n]=cc;}
      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
    }
  }
}

/***************************/
void	cnf_prog(char *li,char *li2,int md)
{
  int	i,c,n;
  char	tmp0[1024];
  char	tmp1[1024];

  i=li2[9]-'A';

  if(i>=0 && i<26){
    if(md){i+=26;}
    strcpy(tmp1,&li[11]);strcat(tmp1,",");n=0;
    while(n<4){
      c=str_search(tmp1,",");if( c==0 ){break;}

      if(c>1 && tmp1[c-2]=='\\'){
	int d;
	memmove(&tmp1[c-2],&tmp1[c-1],strlen(&tmp1[c-1])+1);

	d=str_search(&tmp1[c-1],",");if( d==0 ){break;}
	c+=d-1;
      }

      strcpy(tmp0,tmp1);tmp0[c-1]=0;spcut(tmp0);
      memmove(tmp1, &tmp1[c], strlen(&tmp1[c]) + 1);
      if((n&1)==0){
	tmp0[38+10]=0;
	strcpy(chcom_s[i][n>>1],tmp0);
      }else{
	tmp0[15]=0;
	strcpy(chcom_c[i][n>>1],tmp0);
      }
      n++;
    }
  }
}

/***************************/
void	cnf_repl(char *li,char *li2,int md)
{
  int	i,c;
  char	tmp0[1024];
  char	tmp1[1024];

  c=9;if(md){c=8;if(md>1){c=6;}}

  i=li2[c]-'A';
  if(li2[c]=='S' && li2[c+1]>='A' && li2[c+1]<='Z'){i=(li2[c+1]-'A')+26;c++;}else{
    if(li2[c]==' '){i=(li2[c+1]-'A');c++;}}

  c=str_search(li,",");if( c==0 ){return;}

  if(i>=0 && i<52){
    strcpy(tmp1,&li[c/*+2*/]);
    c=str_search(tmp1,",");if( c==0 ){return;}
    strcpy(tmp0,tmp1);tmp0[c-1]=0;spcut(tmp0);
    memmove(tmp1,&tmp1[c],strlen(&tmp1[c])+1);spcut(tmp1);

    while(tmp1[0]==32){memmove(tmp1,&tmp1[1],strlen(&tmp1[1])+1);}

    tmp0[35]=0;strcpy(repl_t[i+md*52],tmp0);
    tmp1[35]=0;strcpy(repl_d[i+md*52],tmp1);
  }
}
#endif

/***************************/
void	def_init(char *li)
{
  char	tmp0[1024];
  char	tmp1[1024];
  int	i=0,j=0,sum=0;

  strcpy(tmp0,&li[10]);
  spcut(tmp0);

  while(j<256 && tmp0[i]!=0){
    int	a;
    if(tmp0[i]==32){i++;
    }else{
      strcpy(tmp1,&tmp0[i]);
      if(tmp1[1]==0){
	tmp1[1]='H';tmp1[2]=0;
      }else{
	tmp1[2]='H';tmp1[3]=0;
      }
      a=str_val(tmp1);tmp1[2]=0;
      if(strcmp(tmp1,"CS")==0){sum=0;goto ne;}
      if(strcmp(tmp1,"SS")==0){a=(127-(sum&0x7f)&0x7f);}
      init_exc_data[j++]=a;sum+=a;
    }
  ne:
    i=i+2;
  }
  init_exc_data[j]=0xff;
}

/***************************/
/* .dfb layout (compiled def). Fixed header ends at DFB_FIXED_SIZE;
 * then cc records of DFB_PART_SIZE, then tim_name[top_no..last_no). */
#define DFB_MAGIC		"STED2DEFFILE0.01"
#define DFB_MAGIC_LEN		16
#define DFB_FIXED_SIZE		0x1650
#define DFB_OFF_CC		0x1640	/* u16 BE: part record count */
#define DFB_OFF_LAST_NO		0x1642	/* u16 BE: tim_name end */
#define DFB_OFF_TOP_NO		0x1644	/* u16 BE: tim_name start */
#define DFB_PART_SIZE		32
#define DFB_PART_HEAD_LEN	20
#define DFB_PART_SYM_LEN	8
#define DFB_PART_OFF_INDEX	28	/* u16 BE: part index */
#define DFB_PART_OFF_TOP	30	/* u16 BE: tim_top */

#if	CONV
int	defsave(char *fi,int md,int mutef,int initf)
{
  int	ln,n=0,i,j,k;
  FILE	*fp;

  char gggg[12][12]={"#gs_pbank","#gs_rbank","#gm_pbank","#gm_rbank",
		     "#gx_pbank","#gx_rbank","#gy_pbank","#gy_rbank","",
		     "#gs_pgroup","#gs_rgroup"};

  char	li0[]= "*--------------------------------------------------------------------------------------\r\n";
  char	li1[]= "*======================================================================================\r\n";

  strmfe(fi, PATHLEN, fi, "def");
  if(!(fp=fopen2(fi,"wb"))){B_PRINT("ե뤬ץޤ\r\n");exit(1);}

  n=buf_put(li0,n);

  n=buf_put("\r\n#module=",n);n=buf_put(module,n);n=buf_put("\r\n\r\n",n);


  if(module_id[0]!=0 || (mdlflag&15)){
    n=buf_put("#id=",n);
    if(module_id[0]!=0){
      n=buf_put(module_id,n);
    }else{
      if((mdlflag&15)==1){n=buf_put("cm64",n);}
      if((mdlflag&15)==2){n=buf_put("sc55",n);}
      if((mdlflag&15)==3){n=buf_put("sc88",n);}
      if((mdlflag&15)==4){n=buf_put("05rw",n);}
    }
    n=buf_put("\r\n",n);
  }

  if( mutef>0 ){
    n=buf_put("#mute=",n);
    if(mutef==1){n=buf_put("off",n);}
    if(mutef==2){n=buf_put("cm64",n);}
    if(mutef==3){n=buf_put("sc55",n);}
    n=buf_put("\r\n",n);
  }
  if( initf>0 ){
    n=buf_put("#init=",n);
    if(initf==1){n=buf_put("off",n);}
    if(initf==2){n=buf_put("cm64",n);}
    if(initf==3){n=buf_put("sc55",n);}
    if(initf==5){n=buf_put("sc88",n);}
    n=buf_put("\r\n",n);
  }

  n=buf_put("\r\n",n);
  if(card_no[0]){
    n=buf_put("#card_no=",n);n=buf_put(fstr(card_no[0],2),n);n=buf_put("\r\n",n);
  }
  n=buf_put("#lsp_wait=",n);n=buf_put(fstr(lsp_wait,2),n);n=buf_put("\r\n",n);
  if(bend_range!=12){
    n=buf_put("#pb_range=",n);n=buf_put(fstr(bend_range,2),n);n=buf_put("\r\n",n);
  }
  n=buf_put("\r\n",n);

  n=buf_put(li1,n);

  n=buf_put("\r\n#rec_met=",n);
  for(i=0;i<8;i++){n=buf_put(fstr(rec_met[i],3),n);if(i<7){n=buf_put(",",n);}}
  n=buf_put("\r\n\r\n",n);

  n=buf_put(li1,n);


  for(i=0;i<8;i++){
    n=buf_put("#user_exc=",n);
    n=buf_put(fstr(i,1),n);n=buf_put(",",n);
    n=buf_put(exc_str(def_user_exc_data[i],24,0),n);
    n=buf_put(",",n);
    n=buf_put(def_user_exc_memo[i],n);n=buf_put("\r\n",n);
  }
  n=buf_put("\r\n",n);

  if(init_exc_data[0]!=0xff){
    n=buf_put("#init_exc=",n);
    n=buf_put(exc_str(init_exc_data,256,1),n);
    n=buf_put("\r\n\r\n",n);
  }

  n=buf_put(li1,n);

  n=buf_put("#rhy_as=",n);
  for(i=0;i<5;i++){n=buf_put(fstr(rhy_stest[i],3),n);if(i<4){n=buf_put(",",n);}}
  n=buf_put("\r\n",n);
  for(i=0;i<32;i++){
    n=buf_put(defrhyna[i],n);n=buf_put(",",n);
    n=buf_put(fstr(defrhyno[i][0],3),n);n=buf_put(",",n);
    if(defrhyno[i][1]<100){
      n=buf_put(fstr(defrhyno[i][1],2),n);
    }else{
      n=buf_put(fstr(defrhyno[i][1],3),n);
    }
    if((i&3)==3){n=buf_put("\r\n",n);}else{n=buf_put(",",n);}
  }
  n=buf_put("#defoff\r\n\r\n",n);

  n=buf_put(li1,n);


  n=buf_put("#group_a=",n);
  for(i=1;i<17;i++){n=buf_put(fstr(tim_asin2[i],2),n);if(i<16){n=buf_put(",",n);}}
  n=buf_put("\r\n",n);
  n=buf_put("#group_b=",n);
  for(i=17;i<33;i++){n=buf_put(fstr(tim_asin2[i],2),n);if(i<32){n=buf_put(",",n);}}
  n=buf_put("\r\n\r\n",n);

  if(gs_mode[7]&15){n=buf_put("#gs_mode=",n);
  n=buf_put(fstr(gs_mode[7]&15,3),n);n=buf_put("\r\n\r\n",n);}
  if(gs_mode[9]&15){n=buf_put("#gm_mode=",n);
  n=buf_put(fstr(gs_mode[9]&15,3),n);n=buf_put("\r\n\r\n",n);}
  if(gs_mode[11]&15){n=buf_put("#gx_mode=",n);
  n=buf_put(fstr(gs_mode[11]&15,3),n);n=buf_put("\r\n\r\n",n);}
  if(gs_mode[13]&15){n=buf_put("#gy_mode=",n);
  n=buf_put(fstr(gs_mode[13]&15,3),n);n=buf_put("\r\n\r\n",n);}

  if(gs_mode[16]&128){n=buf_put("#gs_gmode=",n);
  n=buf_put(fstr(gs_mode[16]&15,3),n);n=buf_put("\r\n\r\n",n);}
  n=buf_put("\r\n",n);

  for(i=7;i<18;i++){
    if(gs_mode[i]&0xf0){
      n=buf_put(gggg[i-7],n);n=buf_put("\r\n",n);

      for(j=0;j<128;j++){
	n=buf_put(fstr(gs_bank[i*128+j],3),n);
	if((j&15)!=15){n=buf_put(",",n);}else{n=buf_put("\r\n",n);}
      }
      n=buf_put("\r\n",n);
    }
  }

  n=buf_put(li1,n);

  ln=fwrite(dat,1,n,fp);n=0;

  for(i=1;i<4;i++){
    if( tim_top[i-1]!=0 || tim_head[i-1][0]!=0 ){
      int stp;
      n=buf_put("#part=",n);
      n=buf_put(fstr(i,3),n);n=buf_put(",",n);
      n=buf_put(tim_head[i-1],n);n=buf_put(",",n);
      n=buf_put(tim_sym[i-1],n);n=buf_put("\r\n",n);
      n=buf_put(li0,n);

      stp=0;if( i==2 ){stp=64;}

      for(j=stp;j<128;j++){
	n=buf_put(spadd(tim_names(j+i*128-128),10),n);
	if((j&7)!=7){n=buf_put(",",n);}else{n=buf_put("\r\n",n);}
      }
      n=buf_put(li0,n);
      n=buf_put("#defoff\r\n\r\n",n);
    }
  }

  for(i=4;i<16;i++){
    if( tim_top[i-1]!=0 || tim_head[i-1][0]!=0 ){

      n=buf_put("#part=",n);
      n=buf_put(fstr(i,3),n);n=buf_put(",",n);
      n=buf_put(tim_head[i-1],n);n=buf_put(",",n);
      n=buf_put(tim_sym[i-1],n);n=buf_put("\r\n",n);
      n=buf_put(li0,n);

      for(j=0;j<128;j++){
	n=buf_put(tim_names(j+i*128-128),n);
	if((j&3)!=3){n=buf_put(",",n);}else{n=buf_put("\r\n",n);}
      }
      n=buf_put(li0,n);
      n=buf_put("#defoff\r\n\r\n",n);
    }
  }

  for(i=1;i<17;i++){
    if( card_name[i][0]!=32 ){
      int edp;
      n=buf_put("#card=",n);
      n=buf_put(fstr(i,2),n);n=buf_put(",",n);
      n=buf_put(card_name[i],n);n=buf_put("\r\n",n);
      n=buf_put(li0,n);

      edp=127;while(edp>=0){
	if(tim_names(edp+(15+i)*128)[0]!=0){break;}
	edp--;
      }
      if(edp>=0){
	edp|=7;
	for(j=0;j<=edp;j++){
	  n=buf_put(spadd(tim_names(j+(15+i)*128),10),n);
	  if((j&7)!=7){n=buf_put(",",n);}else{n=buf_put("\r\n",n);}
	}
      }
      n=buf_put(li0,n);
      n=buf_put("#defoff\r\n\r\n",n);
      tim_top[i+15]=0;
    }
  }

  for(i=16;i<400;i++){
    if( tim_top[i-1]!=0 || tim_head[i-1][0]!=0 ){

      n=buf_put("#part=",n);
      n=buf_put(fstr(i,3),n);n=buf_put(",",n);
      n=buf_put(tim_head[i-1],n);n=buf_put(",",n);
      n=buf_put(tim_sym[i-1],n);n=buf_put("\r\n",n);
      n=buf_put(li0,n);

      for(j=0;j<128;j++){
	n=buf_put(tim_names(j+i*128-128),n);
	if((j&3)!=3){n=buf_put(",",n);}else{n=buf_put("\r\n",n);}
      }
      n=buf_put(li0,n);
      n=buf_put("#defoff\r\n\r\n",n);
    }
    if(i==100||i==200||i==300){ln=fwrite(dat,1,n,fp);n=0;}
  }

  n=buf_put("#end\r\n\r\n",n);
  n=buf_put(li0,n);

  ln=fwrite(dat,1,n,fp);

  fclose(fp);

  B_PRINT(_("Compile complete.\n"));
  exit(1);

  return(0);
}

/***************************//*4.66(2.18) 4.00(1.85)*/
char	*exc_str(uint8_t *li,int ln,int md)
{
  char	tmp0[1024];
  int	i=0;

  strcpy(tmp0,"");

  while(i<ln && tmp0[i]!=0){
    int	a;
    a=tmp0[i];

    if(a==0xff){break;}
    if(a==0xf7){if(md!=0){strcat(tmp0,"F7");}break;}

    if(a<0x80){
      strcat(tmp0,hex_s(a,2));
    }else{
      if(a==0xf0){strcat(tmp0,"F0 ");}

      if(a==0x80){strcat(tmp0,"gt ");}
      if(a==0x81){strcat(tmp0,"ve ");}
      if(a==0x82){strcat(tmp0,"ch ");}
      if(a==0x83){strcat(tmp0,"cs ");}
      if(a==0x84){strcat(tmp0,"cs ");}
    }
    i++;
  }
  return tmp0;
}

/***************************/
int	buf_put(char *ptr,int ln)
{
  /*	if((rcd->bufcap)<(po+ln)){return(po+ln);}
   */
  while(*ptr){
    dat[ln++]=*ptr++;
  }
  return(ln);
}

/***************************/
int	def_bsave(char *fi,int last_no,int mutef,int initf)
{
  int	i,ln,n,top_no,cc,ad;
  FILE	*fp;

  strmfe(fi, PATHLEN, fi, "dfb");
  if(!(fp=fopen2(fi,"wb"))){B_PRINT(_("Cannot open file.\n"));exit(1);}

  for(i=0;i<work_size;i++){dat[i]=0;}

  memcpy(&dat[0x0000],DFB_MAGIC,DFB_MAGIC_LEN);
  memcpy(&dat[0x0010],module,32);					/*module*/
  dat[0x30]=mdlflag;						/*id*/
  dat[0x32]=mutef;						/*mute*/
  dat[0x33]=initf;						/*init*/
  n=lsp_wait;if(n<0){n=128;}dat[0x34]=n;				/*lsp_wait*/
  dat[0x36]=bend_range;						/*bend range*/
  dat[0x38]=card_no[0];						/*card_no*/

  for(i=0;i<8;i++){
    memcpy(&dat[0x40+i*48],def_user_exc_memo[i],24);
    memcpy(&dat[0x40+i*48+24],def_user_exc_data[i],24);	/*USER_EXC*/
  }
  memcpy(&dat[0x01c0],init_exc_data,256);				/*INIT_EXC*/

  for(i=0;i<10;i++){n=rec_met[i];if(n<0){n=128;}dat[0x2c0+i]=n;}	/*REC_MET*/
  for(i=0;i<6;i++){n=rhy_stest[i];if(n<0){n=128;}dat[0x2ca+i]=n;}	/*RHY_AS*/

  for(i=0;i<32;i++){
    memcpy(&dat[0x300+i*16],defrhyna[i],14);
    dat[0x300+i*16+14]=defrhyno[i][0];dat[0x300+i*16+15]=defrhyno[i][1];
  }
  for(i=0;i<16;i++){memcpy(&dat[0x500+i*48],card_name[i+1],48);}	/*card*/

  memcpy(&dat[0x800],&tim_asin2[1],32);				/*group*/
  memcpy(&dat[0x820],&gs_mode[4],14*2);				/*gsmode*/
  memcpy(&dat[0x840],&gs_bank[128*4],14*128*2);			/*gsbank*/


  cc=0;for(i=0;i<TIM_PART_MAX;i++){if(tim_top[i]>0 || tim_head[i][0]>0){cc++;}}
  dat[DFB_OFF_CC]=cc>>8;dat[DFB_OFF_CC+1]=cc&0xff;
  top_no=0;while(top_no<last_no){if(tim_name[top_no][0]){break;}top_no++;}
  dat[DFB_OFF_LAST_NO]=last_no>>8;dat[DFB_OFF_LAST_NO+1]=last_no&0xff;
  dat[DFB_OFF_TOP_NO]=top_no>>8;dat[DFB_OFF_TOP_NO+1]=top_no&0xff;

  ad=DFB_FIXED_SIZE;						/*part*/
  for(i=0;i<TIM_PART_MAX;i++){
    if(tim_top[i]>0 || tim_head[i][0]>0){
      memcpy(&dat[ad],tim_head[i],DFB_PART_HEAD_LEN);
      memcpy(&dat[ad+DFB_PART_HEAD_LEN],tim_sym[i],DFB_PART_SYM_LEN);
      dat[ad+DFB_PART_OFF_INDEX]=i>>8;dat[ad+DFB_PART_OFF_INDEX+1]=i&0xff;
      dat[ad+DFB_PART_OFF_TOP]=tim_top[i]>>8;dat[ad+DFB_PART_OFF_TOP+1]=tim_top[i]&0xff;
      ad+=DFB_PART_SIZE;
    }
  }


  ln=fwrite(dat,1,ad,fp);

  ln=last_no-top_no;
  if(ln){ln=fwrite(&tim_name[top_no][0],1,ln*TIM_NAME_WIDTH,fp);}	/*tim_name*/

  fclose(fp);

  B_PRINT(_("Compile complete.\n"));
  exit(1);

  return(0);
}
#endif

/***************************/
int	def_bload(char *fi,int md)
{
  int	i,ln,n,f,top_no,last_no,mutef=0,initf=0,cc,ad;
  int	partial=0, saved=0;
  DefSharedSnap snap;
  FILE	*fp;

  strmfe(fi, PATHLEN, fi, "dfb");
  /*if(!(fp=fopen2(fi,"rb"))){return(-1);}*/
  if (!(fp=fopen2(fi,"rb"))) {
    char tmp0[PATHLEN * 2];
    /*strcpy( tmp0, STED_DEFAULT_PATH );*/
	strcpy( tmp0, s_sted_default_path ); /* Jun.01.2003 by Toshi Nagata */
    strcat( tmp0, "//" );
    strcat( tmp0, (strrchr(fi,'/')==NULL)?fi:(strrchr(fi,'/')+1) );
    if (!(fp=fopen2( tmp0, "rb" ))) return(-1);
  } /* Oct.13.1998 by Daisuke Nagano */

  ln=fread(dat,1,DFB_FIXED_SIZE,fp);
  if( ln!=DFB_FIXED_SIZE ){goto bad;}

#if	STED
  if(md==0){
    B_PRINT(_("Loading config file.\n"));
  }
  if(md==1){
    msg(_("Loading config file.\n"));
  }
#endif
#if	CONV
  B_PRINT(_("Dis-compiling config file.\n"));
#endif

  if( strncmp(&dat[0x0000],DFB_MAGIC,DFB_MAGIC_LEN)!=0 ){goto bad;}

  partial = def_load_is_partial();
  if(partial){def_shared_save(&snap);saved=1;}

  cc=dat[DFB_OFF_CC]*256+dat[DFB_OFF_CC+1];
  last_no=dat[DFB_OFF_LAST_NO]*256+dat[DFB_OFF_LAST_NO+1];
  top_no=dat[DFB_OFF_TOP_NO]*256+dat[DFB_OFF_TOP_NO+1];

  if( cc<0 || cc>TIM_PART_MAX
      || (size_t)cc*(size_t)DFB_PART_SIZE>(size_t)work_size
      || top_no<0 || last_no<0
      || top_no>TIM_NAME_MAX || last_no>TIM_NAME_MAX
      || top_no>last_no ){goto bad;}

  memcpy(module,&dat[0x0010],32);				/*module (compat)*/
  module[32]=0;
  mdlflag=dat[0x30];						/*id flag*/
  /* DFB has no id string — synthesize chip label from mdlflag. */
  {
    char idbuf[64];
    module_id_from_mdlflag(idbuf, sizeof idbuf);
    module_id_store_scope(idbuf);
  }
  mutef=dat[0x32];						/*mute*/
  initf=dat[0x33];						/*init*/
  n=dat[0x34];if(n>127){n=-1;}lsp_wait=n;				/*lsp_wait*/
  bend_range=dat[0x36];						/*bend range*/
  card_no[0]=dat[0x38];						/*card_no*/

  for(i=0;i<8;i++){
    memcpy(def_user_exc_memo[i],&dat[0x40+i*48],24);
    memcpy(def_user_exc_data[i],&dat[0x40+i*48+24],24);	/*USER_EXC*/
  }
  memcpy(init_exc_data,&dat[0x01c0],256);				/*INIT_EXC*/

  for(i=0;i<10;i++){n=dat[0x2c0+i];if(n>127){n=-1;}rec_met[i]=n;}	/*REC_MET*/
  for(i=0;i<6;i++){n=dat[0x2ca+i];if(n>127){n=-1;}rhy_stest[i]=n;}/*RHY_AS*/

  for(i=0;i<32;i++){
    memcpy(defrhyna[i],&dat[0x300+i*16],14);
    defrhyno[i][0]=dat[0x300+i*16+14];
    defrhyno[i][1]=dat[0x300+i*16+15];
  }
  /* Empty blocks are omitted keys. mute and init use 0 the same way. */
  if(partial){def_shared_restore_unset(&snap);}

  for(i=0;i<16;i++){memcpy(card_name[i+1],&dat[0x500+i*48],48);}	/*card*/
  /* group: honor UNIT SEL A / B / A&B */
  if(def_load_scope != DEF_SCOPE_B)
    memcpy(&tim_asin2[1],&dat[0x800],16);			/*group_a*/
  if(def_load_scope != DEF_SCOPE_A)
    memcpy(&tim_asin2[17],&dat[0x800+16],16);			/*group_b*/
  memcpy(&gs_mode[4],&dat[0x820],14*2);				/*gsmode*/
  memcpy(&gs_bank[128*4],&dat[0x840],14*128*2);			/*gsbank*/

  if( cc>0 ){
    ln=fread(dat,1,cc*DFB_PART_SIZE,fp);
    if( ln!=cc*DFB_PART_SIZE ){goto bad;}
  }

  ad=0;								/*part*/
  for(i=0;i<cc;i++){
    int idx=dat[ad+DFB_PART_OFF_INDEX]*256+dat[ad+DFB_PART_OFF_INDEX+1];
    if( idx<0 || idx>=TIM_PART_MAX ){goto bad;}
    memcpy(tim_head[idx],&dat[ad],DFB_PART_HEAD_LEN);
    memcpy(tim_sym[idx],&dat[ad+DFB_PART_HEAD_LEN],DFB_PART_SYM_LEN);
    tim_top[idx]=dat[ad+DFB_PART_OFF_TOP]*256+dat[ad+DFB_PART_OFF_TOP+1];
    ad+=DFB_PART_SIZE;
  }

  ln=last_no-top_no;
  if( ln ){
    size_t name_bytes=(size_t)ln*(size_t)TIM_NAME_WIDTH;
    if( fread(&tim_name[top_no][0],1,name_bytes,fp)!=name_bytes ){goto bad;}
  }

  fclose(fp);

#if	STED
  f=(mdlflag&0x7);if(f>2){f=2;}
  if(f>0 && f<4){rcd->moduletype=f-1;}

  if(mutef){rcd->mute_mode=mutef-1;}
  if(initf){rcd->init_mode=initf-1;}
#endif

#if	CONV
  defsave(fi,md,mutef,initf);
#endif

  return(0);

bad:
  if(saved){def_shared_restore_all(&snap);}
  msg(_("Invalid file."));
  fclose(fp);
  return(-1);
}

