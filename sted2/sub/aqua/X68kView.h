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
+ (int)displayScalePercent;
+ (void)applyDisplayScalePercent:(int)percent toView:(NSView *)view;
@end
