/* X68kView.h */
/* Header file for X68kView, the application controller object for sted2_aqua. */
/* Mar.19, 2015, Toshi Nagata */
/* Copyright 2015 Toshi Nagata. All rights reserved. */

#import <Cocoa/Cocoa.h>

extern int sTextWidth;   /* text visible width in pixels; session-fixed, default 768 */
extern int sTextHeight;  /* text visible height in pixels; session-fixed, default 512 */
#define sGraphWidth 1024
#define sGraphHeight 1024

@interface X68kView : NSView <NSTextFieldDelegate>
{
	//  These bitmaps must be recreated every time the bitmap content is updated
	NSBitmapImageRep *bitmapRep;
}
+ (void)initializeX68k;
+ (unsigned int)getKey: (BOOL)senseOnly;

/* What STed2 would do with this Mac key. modMask: control=2, option=4.
   Returns 1 and writes XF1, KIGOU, TOUROKU, and the other X68000 names. */
int XSTed_viewer_label(unsigned vk, unsigned macChar, int modMask, int commandDown,
		       char *name, size_t nameLen);
/* One loaded binding. Returns 0 past the last entry. */
int XSTed_binding_at(int index, unsigned *macChar, unsigned *needMask,
		     char *name, size_t nameLen);
+ (int)displayScalePercent;
+ (void)applyDisplayScalePercent:(int)percent toView:(NSView *)view;
@end
