/* Floating command cheat sheet. The screen stack is updated from the
 * editor's key loops. The panel does not take keyboard focus. */
#ifndef COMMAND_REFERENCE_H
#define COMMAND_REFERENCE_H

enum {
	STED_SCR_NONE = 0,
	STED_SCR_COMMAND,
	STED_SCR_TRACK_EDIT,
	STED_SCR_RHYTHM_EDIT,
	STED_SCR_VISUAL,
	STED_SCR_TRACK_SET,
	STED_SCR_TRACK_SEL,
	STED_SCR_TIMEKEY,
	STED_SCR_MEMO,
	STED_SCR_OPTION,
	STED_SCR_PART,
	STED_SCR_RHYTHM_AS,
	STED_SCR_FILTER,
	STED_SCR_USER_EXC,
	STED_SCR_TRK_EXC,
	STED_SCR_DOT,
	STED_SCR_CM64,
	STED_SCR_GS,
	STED_SCR_STGT,
	STED_SCR_INPMON,
	STED_SCR_FILESEL,
	STED_SCR_TONESEL,
	STED_SCR_TAG,
	STED_SCR_CMDSEL,
	STED_SCR_REPLSEL,
	STED_SCR_CHORD,
	STED_SCR_SCALE,
	STED_SCR_COUNT
};

enum {
	STED_LAYER_NORMAL = 0,
	STED_LAYER_SHIFT = 1,
	STED_LAYER_ST = 2,
	STED_LAYER_GT = 3,
	STED_LAYER_VEL = 4
};

void CommandReferenceShow(void);
void STedScreenPush(int id);
void STedScreenPop(void);
void STedScreenLayer(int layer);
int STedScreenCurrent(void);

static inline void STedScreenPopCleanup(int *unused) __attribute__((unused));
static inline void STedScreenPopCleanup(int *unused)
{
	(void)unused;
	STedScreenPop();
}

/* Push on entry and pop on every return, including early returns. */
#define STED_SCREEN(id) \
	int sted_screen_scope __attribute__((unused, cleanup(STedScreenPopCleanup))) = \
		(STedScreenPush(id), 0)

#endif
