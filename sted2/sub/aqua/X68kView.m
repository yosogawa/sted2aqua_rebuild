/* X68kView.m */
/* An X680x0 emulating window for sted2_aqua. */
/* Jun.01.2003 Toshi Nagata */
/* Copyright 2003 Toshi Nagata. All rights reserved. */

#import "X68kView.h"
#import "CoreMIDIRCPPlayer.h"
#import <CoreText/CoreText.h>

#include "sted.h"

NSString *STed2ScreenRedrawNotification = @"STed2 screen redraw";

//
//  Basic design of X68kView
//
//    The X680x0 program runs in a single thread, and the user interface
//  (keyboard/mouse input and screen output) are accessed upon request
//  from the thread. This is in contrast with the event-driven model
//  in most modern GUI operating systems.
//    One simple implementation is to run the X680x0 program in a
//  background thread, and the user interface is handled in the main
//  thread. The main thread and the background thread communicate
//  each other via a shared memory mechanism.
//    Another complication with the X680x0 architecture is the use of
//  two separate graphic screens, namely 'text' and 'graphic'. If the
//  above execution model is used, the contents of these graphic screens
//  can be retained as offscreen memory buffers, and the real window is
//  updated in the main thread as requested from the background thread.

static NSLock *sLock;     //  The lock to access shared memory

//  Offscreen buffers
static uint8_t *sTextVram;         //  The 'text' VRAM data as in X680x0 screen (bit 0-3 = plane 0-3)
static uint8_t *sGraphVram;     //  The 'graphic' screen buffer (4-bit color)
static uint32_t *sCompositeBitmap; //  The composite screen buffer 
static NSRect sTextInvalRect;      //  The rectangle to be redrawn in the main thread
static NSRect sGraphInvalRect;     //  ibid

//  Key inputs
#define kKeyBufferMax 128
/* Low 16 bits: X68k key. Bits 17-18: Ctrl/Opt consumed by that binding. */
static uint32_t sKeyBuffer[kKeyBufferMax];
static int sKeyBufferBase, sKeyBufferCount;

int sTextWidth = 768;   /* session-fixed; do not change after VRAM alloc */
int sTextHeight = 512;  /* session-fixed; do not change after VRAM alloc */

//  The screen coordinate of the upper-left point of the real graphic screen.
static NSPoint sGraphOrigin;

static int sCursorX, sCursorY;
static BOOL sCursorOn;
static int sTextColor = 3;
static int sGraphColor = 15;

static unsigned int sModifierFlags;
/* Ctrl (2) / Opt.1 (4) used to select the binding of the key just taken.
   The next B_SFTSNS hides those bits once, then clears this. */
static unsigned int sKeySenseSuppress;
static uint16_t sLastKeyDown;  //  The last getKeyWithOptions: return value
/* Group 7 bits that follow keyUp: ROLL DOWN, ROLL UP, UNDO, UP, DOWN.
   Other scans stay on sLastKeyDown. */
static unsigned int sHeldGroup7;

/* Mouse snapshot for the X68k thread (same contract as xwin_mouse.c). */
static int sMsX, sMsY, sMsButtons;
static int sMsMinX, sMsMinY, sMsMaxX, sMsMaxY;

static int sRequestAppKitCall;
static void **sRequestAppKitArgs;

//  End of shared memory variables

static NSView *sMainView;
static NSTimer *sTimer;
static int sDisplayScalePercent = 100;

//  The palette values are to be stored in big-endian format
static char sPaletteEndianIsFixed = 0;  //  Endian conversion should be done only once
static uint32_t sTextPalette[4] = {
	0, 0xffff00ff, 0x00ffffff, 0xffffffff
};
static uint32_t sGraphPalette[16] = {
	0, 0x0000ffff, 0xff0000ff, 0xff00ffff, 0x00ff00ff, 0x00ffffff, 0xffff00ff, 0xffffffff,
	0, 0x0000ffff, 0xff0000ff, 0xff00ffff, 0x00ff00ff, 0x00ffffff, 0xffff00ff, 0xffffffff
};

//  8x16 bitmap font data for characters 0x20-0x7f (alpha-numeric)
//  and 0xa0-0xdf (hankaku kana).
//  One character is represented by 16-byte bitmap.

static uint8_t *sFontData;  /* 16*160 */
static uint8_t *sKanjiData; /* 32*47*188 */
static uint8_t *sFontDataBase;
static uint8_t *sKanjiDataBase;
static char sAppliedMacFont[256];
static int sAppliedCellHeight;

#if 1
#define _NSLog NSLog
#else
inline void  _NSLog() {}
#endif

static uint8_t sKeyTable[128] = {
	/*08    19    2a    3b    4c    5d    6e    7f  */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0x10, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x35, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, /*  !"@#$%' */
	0x09, 0x0a, 0x28, 0x27, 0x31, 0x0c, 0x32, 0x33, /* ()*+,-./ */
	0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, /* 01234567 */
	0x09, 0x0a, 0x28, 0x27, 0x31, 0x0c, 0x32, 0x33, /* 89:;<=>? */
	0x1b, 0x1e, 0x2e, 0x2c, 0x20, 0x13, 0x21, 0x22, /* @ABCDEFG */
	0x23, 0x18, 0x24, 0x25, 0x26, 0x30, 0x2f, 0x19, /* HIJKLMNO */
	0x1a, 0x11, 0x14, 0x1f, 0x15, 0x17, 0x2d, 0x12, /* PQRSTUVW */
	0x2b, 0x16, 0x2a, 0x1c, 0x0e, 0x29, 0x0d, 0x34, /* XYZ[\]^_ */
	0x1b, 0x1e, 0x2e, 0x2c, 0x20, 0x13, 0x21, 0x22, /* `abcdefg */
	0x23, 0x18, 0x24, 0x25, 0x26, 0x30, 0x2f, 0x19, /* hijklmno */
	0x1a, 0x11, 0x14, 0x1f, 0x15, 0x17, 0x2d, 0x12, /* pqrstuvw */
	0x2b, 0x16, 0x2a, 0x1c, 0x0e, 0x29, 0x0d, 0x00  /* xyz{|}~  */
};

static uint8_t sPadKeyTable[64] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0x10, 0x00, 0x00, 0x00, 0x4e, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x41, 0x46, 0x00, 0x42, 0x51, 0x40,
	0x4f, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x43,
	0x44, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


enum {
	x68k_up = 0x3c,
	x68k_down = 0x3e,
	x68k_left = 0x3b,
	x68k_right = 0x3d,
	x68k_f1 = 0x63,
	x68k_f2 = 0x64,
	x68k_f3 = 0x65,
	x68k_f4 = 0x66,
	x68k_f5 = 0x67,
	x68k_f6 = 0x68,
	x68k_f7 = 0x69,
	x68k_f8 = 0x6a,
	x68k_f9 = 0x6b,
	x68k_f10 = 0x6c,
	x68k_xf1 = 0x55,
	x68k_xf2 = 0x56,
	x68k_xf3 = 0x57,
	x68k_xf4 = 0x58,
	x68k_xf5 = 0x59,
	x68k_touroku = 0x53,
	x68k_kigou = 0x52,
	x68k_kana = 0x5a,
	x68k_del = 0x37,
	x68k_home = 0x36,
	x68k_rollup = 0x39,
	x68k_rolldown = 0x38,
	x68k_clr = 0x35,
	x68k_ins = 0x5e,
	x68k_undo = 0x3a,
	x68k_opt1 = 0x72,
	x68k_opt2 = 0x73
};

/*  Key combination -> X68k special key conversion table  */
/*  bit 0-15 = Mac key code (unicode)  */
/*  bit 16 = unused, bit 17 = control mask, bit 18 = option mask  */
/*  bit 24-31 = X68k key code  */
static uint32_t s_x68k_special_key[128] = {
	(x68k_up << 24) + NSUpArrowFunctionKey,
	(x68k_down << 24) + NSDownArrowFunctionKey,
	(x68k_left << 24) + NSLeftArrowFunctionKey,
	(x68k_right << 24) + NSRightArrowFunctionKey,
	(x68k_f1 << 24) + NSF1FunctionKey,
	(x68k_f2 << 24) + NSF2FunctionKey,
	(x68k_f3 << 24) + NSF3FunctionKey,
	(x68k_f4 << 24) + NSF4FunctionKey,
	(x68k_f5 << 24) + NSF5FunctionKey,
	(x68k_f6 << 24) + NSF6FunctionKey,
	(x68k_f7 << 24) + NSF7FunctionKey,
	(x68k_f8 << 24) + NSF8FunctionKey,
	(x68k_f9 << 24) + NSF9FunctionKey,
	(x68k_f10 << 24) + NSF10FunctionKey,
	/* ctrl-1..5, option-1..0, end, control-\ come from sted2.cnf.
	   Shift glyphs and Ctrl+Option+1..5 are not bindings. */
	(x68k_kigou << 24) + NSF11FunctionKey,   /* X11 / curses: F11 = 記号 */
	(x68k_touroku << 24) + NSF12FunctionKey, /* X11 / curses: F12 = 登録 */
	(x68k_del << 24) + NSDeleteFunctionKey,
	(x68k_home << 24) + NSHomeFunctionKey,
	(x68k_rollup << 24) + NSPageUpFunctionKey,
	(x68k_rolldown << 24) + NSPageDownFunctionKey,
	(x68k_clr << 24) + NSClearLineFunctionKey
};

/* Map NSView coords (origin bottom-left) to X68k screen (origin top-left). */
static void
s_XSTed_ms_store_view_point(NSPoint p, NSUInteger buttons)
{
	NSRect b;
	int x, y;

	if (sMainView != nil)
		b = [sMainView bounds];
	else
		b = NSMakeRect(0, 0, sTextWidth, sTextHeight);
	if (b.size.width < 1.0)
		b.size.width = sTextWidth;
	if (b.size.height < 1.0)
		b.size.height = sTextHeight;
	x = (int)(p.x * (CGFloat)sTextWidth / b.size.width);
	y = (int)((b.size.height - p.y) * (CGFloat)sTextHeight / b.size.height);
	if (x < sMsMinX)
		x = sMsMinX;
	else if (x > sMsMaxX)
		x = sMsMaxX;
	if (y < sMsMinY)
		y = sMsMinY;
	else if (y > sMsMaxY)
		y = sMsMaxY;
	sMsX = x;
	sMsY = y;
	sMsButtons = 0;
	if (buttons & (1UL << 0))
		sMsButtons |= 0xff00; /* left, same as X11 Button1Mask */
	if (buttons & (1UL << 1))
		sMsButtons |= 0x00ff; /* right, same as X11 Button3Mask */
}

@implementation X68kView

+ (int)displayScalePercent
{
	return sDisplayScalePercent;
}

+ (void)applyDisplayScalePercent:(int)percent toView:(NSView *)view
{
	NSWindow *w;
	NSRect oldFrame, newFrame, vis, content;
	CGFloat s;

	if (percent != 100 && percent != 125 && percent != 150)
		percent = 100;
	sDisplayScalePercent = percent;
	[[NSUserDefaults standardUserDefaults] setInteger:percent forKey:@"DisplayScalePercent"];
	if (view == nil)
		return;
	w = [view window];
	if (w == nil)
		return;
	s = (CGFloat)percent / 100.0;
	content.origin = NSZeroPoint;
	content.size.width = floor((CGFloat)sTextWidth * s + 0.5);
	content.size.height = floor((CGFloat)sTextHeight * s + 0.5);
	oldFrame = [w frame];
	newFrame = [w frameRectForContentRect:content];
	newFrame.origin.x = oldFrame.origin.x;
	newFrame.origin.y = oldFrame.origin.y + oldFrame.size.height - newFrame.size.height;
	vis = [[w screen] visibleFrame];
	if (NSIsEmptyRect(vis) == NO) {
		if (newFrame.size.width > vis.size.width)
			newFrame.size.width = vis.size.width;
		if (newFrame.size.height > vis.size.height)
			newFrame.size.height = vis.size.height;
		if (newFrame.origin.x < vis.origin.x)
			newFrame.origin.x = vis.origin.x;
		if (newFrame.origin.y < vis.origin.y)
			newFrame.origin.y = vis.origin.y;
		if (newFrame.origin.x + newFrame.size.width > vis.origin.x + vis.size.width)
			newFrame.origin.x = vis.origin.x + vis.size.width - newFrame.size.width;
		if (newFrame.origin.y + newFrame.size.height > vis.origin.y + vis.size.height)
			newFrame.origin.y = vis.origin.y + vis.size.height - newFrame.size.height;
	}
	[w setFrame:newFrame display:YES];
	[view setNeedsDisplay:YES];
}

- (void)setFrameSize:(NSSize)newSize
{
	[super setFrameSize:newSize];
	[self setBounds:NSMakeRect(0, 0, sTextWidth, sTextHeight)];
}

- (void)awakeFromNib
{
	int i;
	FILE *fp;

	if (isconsole) {
		[[self window] orderOut:nil];
		return;
	}

	sLock = [[NSLock alloc] init];
	
	sCompositeBitmap = (uint32_t *)calloc(sizeof(uint32_t), sTextWidth * sTextHeight);
	sGraphVram = (uint8_t *)calloc(sizeof(uint8_t), sGraphWidth * sGraphHeight);
	sTextVram = (uint8_t *)calloc(sizeof(uint8_t), sTextWidth * sTextHeight);
	
	if (sPaletteEndianIsFixed == 0) {
		sPaletteEndianIsFixed = 1;
		for (i = 0; i < 4; i++)
			sTextPalette[i] = NSSwapHostIntToBig(sTextPalette[i]);
		for (i = 0; i < 16; i++)
			sGraphPalette[i] = NSSwapHostIntToBig(sGraphPalette[i]);
	}
}

+ (void)initializeX68k
{
	sTextInvalRect = NSZeroRect;
	sGraphInvalRect = NSZeroRect;

	sKeyBufferBase = 0;
	sKeyBufferCount = 0;
	sMsX = 0;
	sMsY = 0;
	sMsButtons = 0;
	sMsMinX = 0;
	sMsMinY = 0;
	sMsMaxX = sTextWidth - 1;
	sMsMaxY = sTextHeight - 1;
}

- (void)timerHandler:(NSTimer *)aTimer
{
	int x1, y1, x2, y2, x, y, xx, yy;
	NSRect rect;

	if (sMainView != nil && [sMainView window] != nil) {
		NSPoint p = [sMainView convertPoint:[[sMainView window] mouseLocationOutsideOfEventStream]
					  fromView:nil];
		[sLock lock];
		s_XSTed_ms_store_view_point(p, [NSEvent pressedMouseButtons]);
		[sLock unlock];
	}

	//  Handle AppKit request
	if (sRequestAppKitCall != 0) {
		void (*func_handler)(void **) = (void (*)(void **))sRequestAppKitArgs[0];
		(*func_handler)(sRequestAppKitArgs + 1);
	}
	
	rect = NSUnionRect(sTextInvalRect, sGraphInvalRect);

	if (rect.size.width != 0.0 && rect.size.height != 0.0) {
		//  Composite the text screen and the graph screen
		uint8_t *tp, *gp;
		uint32_t c;
		
		[sLock lock];
		
		x1 = rect.origin.x;
		y1 = rect.origin.y;
		x2 = rect.origin.x + rect.size.width;
		y2 = rect.origin.y + rect.size.height;
		if (x1 < 0)
			x1 = 0;
		if (x2 > sTextWidth)
			x2 = sTextWidth;
		if (y1 < 0)
			y1 = 0;
		if (y2 > sTextHeight)
			y2 = sTextHeight;
		rect.origin.x = x1;
		rect.origin.y = y1;
		rect.size.width = x2 - x1;
		rect.size.height = y2 - y1;
		for (y = y1; y < y2; y++) {
			if (y >= 0 && y < sTextHeight)
				tp = &sTextVram[y * sTextWidth];
			else tp = NULL;
			yy = y + sGraphOrigin.y;
			if (yy >= 0 && yy < sGraphHeight)
				gp = &sGraphVram[yy * sGraphWidth];
			else gp = NULL;
			for (x = x1; x < x2; x++) {
				xx = x + sGraphOrigin.x;
				if (gp != NULL && xx >= 0 && xx < sGraphWidth)
					c = sGraphPalette[gp[xx] % 16];
				else c = 0;
				if (tp != NULL && x >= 0 && x < sTextWidth)
					c |= sTextPalette[tp[x] % 4];
				sCompositeBitmap[y * sTextWidth + x] = c;
			}
		}

		sTextInvalRect = NSZeroRect;
		sGraphInvalRect = NSZeroRect;
				
		[sLock unlock];

		//  Create a new NSBitmapImageRep
		//  (It is necessary to create a new instance every time when the bitmap is
		//  externally modified.)
		if (bitmapRep != nil)
			[bitmapRep release];
		bitmapRep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&sCompositeBitmap pixelsWide:sTextWidth pixelsHigh:sTextHeight bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:32];
		
		//  Request redraw
		rect.origin.y = sTextHeight - (rect.origin.y + rect.size.height);
		[sMainView setNeedsDisplayInRect:rect];

	//	printf("timerHandler - setNeedsDisplayInRect:(%f,%f,%f,%f)\n", rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
		
		if (0) {  /*  For debug  */
			[[bitmapRep TIFFRepresentation] writeToFile:@"/Users/toshi_n/STed_composedBitmap.tiff" atomically:YES];
			int i;
			for (i = 0; i < sTextWidth * sTextHeight; i++)
				sTextVram[i] *= 64;
			NSBitmapImageRep *textVram = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&sTextVram pixelsWide:sTextWidth pixelsHigh:sTextHeight bitsPerSample:8 samplesPerPixel:1 hasAlpha:NO isPlanar:NO colorSpaceName:NSCalibratedWhiteColorSpace bytesPerRow:0 bitsPerPixel:8];
			[[textVram TIFFRepresentation] writeToFile:@"/Users/toshi_n/STed_textVram.tiff" atomically:YES];
			for (i = 0; i < sTextWidth * sTextHeight; i++)
				sTextVram[i] /= 64;
			for (i = 0; i < sGraphWidth * sGraphHeight; i++)
				sGraphVram[i] *= 16;
			NSBitmapImageRep *graphBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&sGraphVram pixelsWide:sGraphWidth pixelsHigh:sGraphHeight bitsPerSample:8 samplesPerPixel:1 hasAlpha:NO isPlanar:NO colorSpaceName:NSCalibratedWhiteColorSpace bytesPerRow:0 bitsPerPixel:8];
			[[graphBitmap TIFFRepresentation] writeToFile:@"/Users/toshi_n/STed_graphBitmap.tiff" atomically:YES];			
			for (i = 0; i < sGraphWidth * sGraphHeight; i++)
				sGraphVram[i] /= 16;
		}
		
	}
}

- (void)drawRect: (NSRect)rect
{
	if (sMainView == nil) {
		sMainView = self;
		sTimer = [NSTimer scheduledTimerWithTimeInterval:0.05 target:self selector:@selector(timerHandler:) userInfo:nil repeats:YES];
	}
	if (bitmapRep != nil) {
		NSGraphicsContext *gc = [NSGraphicsContext currentContext];
		NSImageInterpolation old = [gc imageInterpolation];
		[gc setImageInterpolation:NSImageInterpolationNone];
		[bitmapRep drawInRect:[self bounds]];
		[gc setImageInterpolation:old];
	}
//	printf("drawRect\n");
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

/*  Bits 0-7: ASCII code
    Bits 8-15: scan code
    Bits 16-18: Ctrl/Opt consumed by the matched binding (2 and 4).
    Shift is never consumed. */
static uint32_t
eventToX68kKeyInfo(NSEvent *theEvent)
{
	unichar theChar = 0;
	unsigned int theMask;
	uint32_t keyCode = 0, keyMask = 0, consumed = 0;
	unsigned short vk;
	int i;
	NSString *chars;

	theMask = [theEvent modifierFlags];
	keyMask = 0;
	if (theMask & NSEventModifierFlagShift)
		keyMask |= 1;
	if (theMask & NSEventModifierFlagControl)
		keyMask |= 2;
	if (theMask & NSEventModifierFlagOption)
		keyMask |= 4;	/* Opt.1 */
	if (theMask & NSEventModifierFlagCapsLock)
		keyMask |= 8;   /* Opt.2 */

	/* F11 = 記号, F12 = 登録. Option+minus / Option+= are not bindings. */
	vk = [theEvent keyCode];
	if ((theMask & NSEventModifierFlagCommand) == 0) {
		if (vk == 0x67)
			keyCode = (uint32_t)x68k_kigou << 8;
		else if (vk == 0x6F)
			keyCode = (uint32_t)x68k_touroku << 8;
	}

	if (keyCode == 0) {
		/* Physical F1-F10, then main-row 1-0 as '1'-'0' (not '!'). */
		switch (vk) {
		case 0x7A: theChar = NSF1FunctionKey; break;
		case 0x78: theChar = NSF2FunctionKey; break;
		case 0x63: theChar = NSF3FunctionKey; break;
		case 0x76: theChar = NSF4FunctionKey; break;
		case 0x60: theChar = NSF5FunctionKey; break;
		case 0x61: theChar = NSF6FunctionKey; break;
		case 0x62: theChar = NSF7FunctionKey; break;
		case 0x64: theChar = NSF8FunctionKey; break;
		case 0x65: theChar = NSF9FunctionKey; break;
		case 0x6D: theChar = NSF10FunctionKey; break;
		case 0x12: theChar = '1'; break;
		case 0x13: theChar = '2'; break;
		case 0x14: theChar = '3'; break;
		case 0x15: theChar = '4'; break;
		case 0x17: theChar = '5'; break;
		case 0x16: theChar = '6'; break;
		case 0x1A: theChar = '7'; break;
		case 0x1C: theChar = '8'; break;
		case 0x19: theChar = '9'; break;
		case 0x1D: theChar = '0'; break;
		default:
			theChar = 0;
			chars = [theEvent charactersIgnoringModifiers];
			if ([chars length] > 0)
				theChar = [chars characterAtIndex: 0];
			break;
		}
		if (theChar != 0) {
			int bestRank = -1;
			int tie = 0;
			uint32_t bestCode = 0, bestNeed = 0;
			for (i = 0; i < (int)(sizeof(s_x68k_special_key) / sizeof(s_x68k_special_key[0])); i++) {
				uint32_t need;
				int rank;
				if (s_x68k_special_key[i] == 0)
					break;
				need = (s_x68k_special_key[i] >> 16) & 6;
				if ((s_x68k_special_key[i] & 0xffff) != theChar)
					continue;
				if ((keyMask & need) != need)
					continue;
				rank = (need == 6) ? 2 : (need != 0);
				if (rank > bestRank) {
					bestRank = rank;
					bestCode = (s_x68k_special_key[i] >> 16) & 0xff00;
					bestNeed = need;
					tie = 0;
				} else if (rank == bestRank) {
					tie = 1;
				}
			}
			/* Equal rank means two bindings claim the chord. Leave it unmatched. */
			if (bestRank >= 0 && !tie) {
				keyCode = bestCode;
				consumed = bestNeed;
			}
		}
	}
	if (keyCode == 0) {
		/* Unmatched letter. Binding search already ran, so option-1 and
		   control-1 are untouched. Shift stays in theChar. */
		unichar base = theChar;
		int letter = (base >= 'A' && base <= 'Z') || (base >= 'a' && base <= 'z');
		int optLetter = letter || base == '@';

		if ((keyMask & 2) && letter) {
			/* Control+A-Z: control code, scan 0. CTRL is not consumed.
			   Scan stays 0 so Ctrl+E does not also match the E key. */
			keyCode = base & 0x1f;
		} else if ((keyMask & 4) && optLetter && base < 128) {
			/* Option+A-Z/@: uppercase letter, physical scan. OPT.1 stays.
			   The option menu accepts 'A'-'Z' only. */
			unichar ascii = base;
			if (base >= 'a' && base <= 'z')
				ascii = (unichar)(base - 'a' + 'A');
			keyCode = ((uint32_t)sKeyTable[ascii] << 8) | ascii;
		} else {
			chars = [theEvent characters];
			if ([chars length] > 0) {
				theChar = [chars characterAtIndex: 0];
				if (theChar > 0 && theChar < 128) {
					if ((theMask & NSEventModifierFlagNumericPad) && theChar < 64) {
						keyCode = sPadKeyTable[theChar];
					} else {
						keyCode = sKeyTable[theChar];
					}
					keyCode = (keyCode << 8) + theChar;
				}
			}
		}
	}
	return (consumed << 16) | keyCode;
}

/* Bit in BITSNS(7) while this scan is physically down. 0 if not one of the five. */
static unsigned int
s_group7_hold_bit(uint32_t keyInfo)
{
	switch ((keyInfo >> 8) & 0xff) {
	case x68k_rolldown: return 1;
	case x68k_rollup:   return 2;
	case x68k_undo:     return 4;
	case x68k_up:       return 16;
	case x68k_down:     return 64;
	default:            return 0;
	}
}

//  Register key down event in the ring buffer.
//  [sLock lock] should have been called.
static void
s_RegisterKeyDown(uint32_t ch)
{
	if ((ch & 0xffff) != 0) {
		if (sKeyBufferCount < kKeyBufferMax) {
			sKeyBuffer[(sKeyBufferBase + sKeyBufferCount) % kKeyBufferMax] = ch;
			sKeyBufferCount++;
		}
	}
//	printf("registerKeyDown %04x\n", ch);
}

- (void)keyDown: (NSEvent *)theEvent
{
	uint32_t keyInfo;
	unsigned int flags, flagsChange;
	[sLock lock];
	flags = [theEvent modifierFlags];
	flagsChange = (sModifierFlags ^ flags);
	if ([theEvent type] == NSEventTypeKeyDown) {
		unsigned int scan;
		int repeat;
		keyInfo = eventToX68kKeyInfo(theEvent);
		scan = (keyInfo >> 8) & 0xff;
		repeat = [theEvent isARepeat];
		/* XF3/XF4 are RUN/STOP. The editor reads the queue later, one key
		   per redraw, so a queued STOP is applied again after RUN and the
		   player stays paused. Repeats are ignored. XF5 stays queued:
		   full stop is the editor's all_note_off. */
		if (scan == x68k_xf3 || scan == x68k_xf4) {
			if (!repeat && rcd != NULL)
				*(volatile int32_t *)&rcd->sts = (scan == x68k_xf4) ? 1 : 0;
		} else
			s_RegisterKeyDown(keyInfo);
		sHeldGroup7 |= s_group7_hold_bit(keyInfo);
	} else if ([theEvent type] == NSEventTypeKeyUp) {
		keyInfo = eventToX68kKeyInfo(theEvent);
		sHeldGroup7 &= ~s_group7_hold_bit(keyInfo);
	} else {
		if (flagsChange != 0) {
			if (flagsChange & NSEventModifierFlagControl)
				s_RegisterKeyDown((0x71 + (flags & NSEventModifierFlagControl ? 0 : 0x80)) << 8);
			if (flagsChange & NSEventModifierFlagShift)
				s_RegisterKeyDown((0x70 + (flags & NSEventModifierFlagShift ? 0 : 0x80)) << 8);
			if (flagsChange & NSEventModifierFlagOption)
				s_RegisterKeyDown((0x72 + (flags & NSEventModifierFlagOption ? 0 : 0x80)) << 8);
			if (flagsChange & NSEventModifierFlagCapsLock)
				s_RegisterKeyDown((0x73 + (flags & NSEventModifierFlagCapsLock ? 0 : 0x80)) << 8);
		}
	}
	sModifierFlags = flags;
	[sLock unlock];
}

- (void)flagsChanged:(NSEvent *)theEvent
{
	[self keyDown:theEvent];
}

- (void)keyUp:(NSEvent *)theEvent
{
	[self keyDown:theEvent];
}

- (BOOL)performKeyEquivalent:(NSEvent *)theEvent
{
	unsigned short vk = [theEvent keyCode];
	NSUInteger flags = [theEvent modifierFlags];
	int take = 0;

	if (vk == 0x67 || vk == 0x6F)
		take = 1;
	if (!take)
		return [super performKeyEquivalent:theEvent];
	if ([theEvent type] == NSEventTypeKeyDown) {
		uint32_t keyInfo;
		[sLock lock];
		keyInfo = eventToX68kKeyInfo(theEvent);
		s_RegisterKeyDown(keyInfo);
		[sLock unlock];
	}
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)theEvent
{
	return YES;
}

- (void)updateMouseFromEvent:(NSEvent *)theEvent
{
	NSPoint p = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[sLock lock];
	s_XSTed_ms_store_view_point(p, [NSEvent pressedMouseButtons]);
	[sLock unlock];
}

- (void)mouseDown:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (void)mouseDragged:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (void)mouseUp:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (void)rightMouseDragged:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (void)rightMouseUp:(NSEvent *)theEvent
{
	[self updateMouseFromEvent:theEvent];
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)command
{
	NSTextField *tf = (NSTextField *)control;
	NSString *str = [tf stringValue];
	char *buf = (char *)sRequestAppKitArgs[1];
	int len = (int)(intptr_t)sRequestAppKitArgs[2];
	if (command == @selector(insertNewline:)) {
		if ([str getCString:buf maxLength:len encoding:NSShiftJISStringEncoding]) {
			[tf removeFromSuperview];
			[[sMainView window] makeFirstResponder:sMainView];
			sRequestAppKitCall = 0;
			return YES;
		} else {
			NSString *mes;
			if ([str canBeConvertedToEncoding:NSShiftJISStringEncoding]) {
				mes = @"The string is too long.";
			} else {
				mes = @"The string includes non-Shift-JIS characters.";
			}
			NSAlert *alert = [NSAlert alertWithMessageText:nil defaultButton:nil alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", mes];
			[alert setAlertStyle:2];
			[alert runModal];
			return YES;
		}
	} else if (command == @selector(cancelOperation:)) {
		//  Esc key is pressed
		//  The original string is not changed, and editing will terminate
		[tf removeFromSuperview];
		[[sMainView window] makeFirstResponder:sMainView];
		sRequestAppKitCall = 0;
		return YES;
	}
	return NO;
}

#define kGetKeyNoWaitOption 1
#define kGetKeyKeepInBufferOption 2
#define kGetKeySenseModifiersOption 4

+ (uint16_t)getKeyWithOptions:(int)optionFlags
{
	uint16_t ch = 0;
	uint32_t raw;
	int pt;
	while (1) {
		[sLock lock];
		raw = 0;
		for (pt = 0; pt < sKeyBufferCount; pt++) {
			raw = sKeyBuffer[(sKeyBufferBase + pt) % kKeyBufferMax];
			ch = (uint16_t)(raw & 0xffff);
			if ((optionFlags & kGetKeySenseModifiersOption) != 0 || ch < 0x7000)
				break;
		}
		if (pt < sKeyBufferCount) {
			if ((optionFlags & kGetKeyKeepInBufferOption) == 0) {
				/* Hide the binding's Ctrl/Opt on the next B_SFTSNS. */
				sKeySenseSuppress = (raw >> 16) & 6;
				sKeyBufferCount -= pt + 1;
				sKeyBufferBase = (sKeyBufferBase + pt + 1) % kKeyBufferMax;
			}
			[sLock unlock];
			break;
		}
		[sLock unlock];
		if (optionFlags & kGetKeyNoWaitOption) {
			ch = 0;
			break;
		}
		usleep(50000);  //  Wait for the main thread to handle the key event
	}
//	if (ch != 0)
//		printf("getKeyWithOptions %04x\n", ch);
	sLastKeyDown = ch;
	return ch;
}

#pragma mark "=== System setup functions ==="
			
void
XSTed_setup_default_path(void)
{
	NSString *pathString, *rsrcPathString;
	NSFileManager *fileManager = [NSFileManager defaultManager];

	pathString = [@"~/Library/Application Support/sted" stringByExpandingTildeInPath];
	if (![fileManager fileExistsAtPath: pathString]) {
		rsrcPathString = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: @"conf"];
		[fileManager copyPath: rsrcPathString toPath: pathString handler: nil];
	}
	if (![pathString getFileSystemRepresentation: s_sted_default_path maxLength: 1000])
		s_sted_default_path[0] = 0;
}

/* Glyph tables stay 16 rows until the text buffer itself is rebuilt.
   cellHeight is accepted so a later display size can call this again. */
#define kFontCellHeight 16
#define kFontHalfBytes (kFontCellHeight * 160)
#define kFontKanjiBytes (kFontCellHeight * 2 * 47 * 188)

static void s_font_capture_base(void)
{
	if (sFontDataBase == NULL) {
		sFontDataBase = (uint8_t *)malloc(kFontHalfBytes);
		sKanjiDataBase = (uint8_t *)malloc(kFontKanjiBytes);
	}
	if (sFontDataBase == NULL || sKanjiDataBase == NULL)
		return;
	memcpy(sFontDataBase, sFontData, kFontHalfBytes);
	memcpy(sKanjiDataBase, sKanjiData, kFontKanjiBytes);
}

static void s_font_restore_base(void)
{
	if (sFontDataBase == NULL || sKanjiDataBase == NULL)
		return;
	memcpy(sFontData, sFontDataBase, kFontHalfBytes);
	memcpy(sKanjiData, sKanjiDataBase, kFontKanjiBytes);
}

static void s_cnf_take_mac_font(const char *path, char *name, size_t nlen)
{
	FILE *fp;
	char line[1024];
	char alt[PATHLEN];
	const char *base;

	fp = fopen2((char *)path, "rb");
	if (fp == NULL && s_sted_default_path[0] != 0) {
		base = strrchr(path, '/');
		snprintf(alt, sizeof alt, "%s/%s", s_sted_default_path, base ? base + 1 : path);
		fp = fopen2(alt, "rb");
	}
	if (fp == NULL)
		return;
	while (fgets(line, sizeof line, fp) != NULL) {
		char *p = line;
		size_t n;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncasecmp(p, "#mac_font=", 10) != 0)
			continue;
		p += 10;
		while (*p == ' ' || *p == '\t')
			p++;
		n = strlen(p);
		while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t'))
			p[--n] = 0;
		if (n >= nlen)
			n = nlen - 1;
		memcpy(name, p, n);
		name[n] = 0;
	}
	fclose(fp);
}

static void s_load_mac_font_setting(int argc, char **argv, char *name, size_t nlen)
{
	char path[PATHLEN];
	const char *argv0;
	const char *home;

	name[0] = 0;
	argv0 = (argc > 0 && argv != NULL && argv[0] != NULL) ? argv[0] : "sted2";
	strmfe(path, argv0, "cnf");
	s_cnf_take_mac_font(path, name, nlen);
	strmfe(path, argv0, "cf2");
	s_cnf_take_mac_font(path, name, nlen);
	home = getenv("HOME");
	if (home != NULL && home[0] != 0) {
		snprintf(path, sizeof path, "%s/.sted2rc", home);
		s_cnf_take_mac_font(path, name, nlen);
	}
}

static CTFontRef s_font_fitted(NSString *psName, CGFloat targetW, CGFloat targetH, UniChar sample)
{
	CTFontRef probe, fitted;
	CGFloat ascent, descent, body, advw, sy, sx, fit;
	CGGlyph glyph;
	CGSize adv;

	probe = CTFontCreateWithName((CFStringRef)psName, targetH, NULL);
	if (probe == NULL)
		return NULL;
	ascent = CTFontGetAscent(probe);
	descent = CTFontGetDescent(probe);
	body = ascent + descent;
	if (body < 1.0)
		body = 1.0;
	advw = targetW;
	glyph = 0;
	if (CTFontGetGlyphsForCharacters(probe, &sample, &glyph, 1) && glyph != 0) {
		CTFontGetAdvancesForGlyphs(probe, kCTFontOrientationHorizontal, &glyph, &adv, 1);
		if (adv.width > 0.5)
			advw = adv.width;
	}
	CFRelease(probe);
	sy = targetH / body;
	sx = targetW / advw;
	fit = (sy < sx) ? sy : sx;
	if (fit <= 0.0)
		fit = 1.0;
	fitted = CTFontCreateWithName((CFStringRef)psName, targetH * fit, NULL);
	return fitted;
}

static int s_draw_cell(CTFontRef font, CGGlyph glyph, int cellW, int cellH, uint8_t *dst)
{
	uint8_t *pix, tmp[16 * 2], *src;
	CGColorSpaceRef cs;
	CGContextRef ctx;
	CGPoint pos;
	int y, x, any, bytesPerRow;

	pix = (uint8_t *)calloc((size_t)cellW * (size_t)cellH, 1);
	if (pix == NULL)
		return 0;
	cs = CGColorSpaceCreateDeviceGray();
	ctx = CGBitmapContextCreate(pix, (size_t)cellW, (size_t)cellH, 8, (size_t)cellW, cs, (CGBitmapInfo)kCGImageAlphaNone);
	CGColorSpaceRelease(cs);
	if (ctx == NULL) {
		free(pix);
		return 0;
	}
	CGContextSetShouldAntialias(ctx, false);
	CGContextSetAllowsAntialiasing(ctx, false);
	CGContextSetShouldSmoothFonts(ctx, false);
	CGContextSetAllowsFontSmoothing(ctx, false);
	CGContextSetGrayFillColor(ctx, 1.0, 1.0);
	pos = CGPointMake(0, CTFontGetDescent(font));
	CTFontDrawGlyphs(font, &glyph, &pos, 1, ctx);
	CGContextRelease(ctx);

	bytesPerRow = (cellW + 7) / 8;
	memset(tmp, 0, sizeof tmp);
	any = 0;
	for (y = 0; y < cellH; y++) {
		src = pix + y * cellW;
		for (x = 0; x < cellW; x++) {
			if (src[x] >= 128) {
				tmp[y * bytesPerRow + x / 8] |= (uint8_t)(1 << (x % 8));
				any = 1;
			}
		}
	}
	free(pix);
	if (any && sLock != nil)
		[sLock lock];
	if (any)
		memcpy(dst, tmp, (size_t)bytesPerRow * (size_t)cellH);
	if (any && sLock != nil)
		[sLock unlock];
	return any;
}

static const char kBundledFontPSName[] = "KH-Dot-Kodenmachou-16";

static int s_nsfont_installed(const char *name)
{
	NSFont *font;
	if (name == NULL || name[0] == 0)
		return 0;
	font = [NSFont fontWithName:[NSString stringWithUTF8String:name] size:(CGFloat)kFontCellHeight];
	return font != nil;
}

/* Use a font already registered under this PostScript name.
   Otherwise register the copy in the application bundle. */
static int s_register_bundled_kodenmachou(void)
{
	NSString *path;
	NSURL *url;
	if (s_nsfont_installed(kBundledFontPSName))
		return 1;
	path = [[NSBundle mainBundle] pathForResource:@"KH-Dot-Kodenmachou-16" ofType:@"ttf"];
	if (path == nil)
		return 0;
	url = [NSURL fileURLWithPath:path];
	CTFontManagerRegisterFontsForURL((CFURLRef)url, kCTFontManagerScopeProcess, NULL);
	return s_nsfont_installed(kBundledFontPSName);
}

static void s_apply_mac_font_main(const char *name, int cellHeight)
{
	NSFont *nsfont;
	NSString *psName;
	CTFontRef halfFont, fullFont;
	int i, j, nHalf, nKanji;
	UniChar sample;

	if (sFontData == NULL || sKanjiData == NULL)
		return;
	if (cellHeight != kFontCellHeight)
		cellHeight = kFontCellHeight;
	if (name == NULL)
		name = "";
	if (strcmp(sAppliedMacFont, name) == 0 && sAppliedCellHeight == cellHeight)
		return;
	strncpy(sAppliedMacFont, name, sizeof sAppliedMacFont - 1);
	sAppliedMacFont[sizeof sAppliedMacFont - 1] = 0;
	sAppliedCellHeight = cellHeight;
	s_font_restore_base();
	if (name[0] == 0)
		return;

	nsfont = [NSFont fontWithName:[NSString stringWithUTF8String:name] size:(CGFloat)cellHeight];
	if (nsfont == nil) {
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setMessageText:@"フォントが見つかりません"];
		[alert setInformativeText:[NSString stringWithFormat:@"「%s」は使えません。", name]];
		[alert setAlertStyle:NSAlertStyleWarning];
		[alert runModal];
		[alert release];
		return;
	}

	psName = [nsfont fontName];
	sample = 'M';
	halfFont = s_font_fitted(psName, 8.0, (CGFloat)cellHeight, sample);
	sample = 0x4E00; /* 一 */
	fullFont = s_font_fitted(psName, 16.0, (CGFloat)cellHeight, sample);
	nHalf = 0;
	nKanji = 0;

	if (halfFont != NULL) {
		for (i = 0; i < 160; i++) {
			UniChar cp;
			CGGlyph glyph = 0;
			if (i < 95)
				cp = (UniChar)(0x20 + i);
			else if (i >= 97 && i <= 159)
				cp = (UniChar)(0xFF61 + (i - 97));
			else
				continue;
			if (!CTFontGetGlyphsForCharacters(halfFont, &cp, &glyph, 1) || glyph == 0)
				continue;
			if (s_draw_cell(halfFont, glyph, 8, cellHeight, sFontData + i * cellHeight))
				nHalf++;
		}
		CFRelease(halfFont);
	}

	if (fullFont != NULL) {
		for (i = 0; i < 47; i++) {
			for (j = 0; j < 188; j++) {
				unsigned char bytes[2];
				int c1, c2, scode;
				NSString *s;
				CGGlyph glyph = 0;
				UniChar cp;
				c1 = (i < 31) ? i + 0x81 : i + (0xe0 - 31);
				c2 = (j < 63) ? j + 0x40 : j + (0x80 - 63);
				scode = (c1 << 8) | c2;
				if ((scode >= 0xeb9f && scode <= 0xebfc) || (scode >= 0xec40 && scode <= 0xec9e))
					continue;
				bytes[0] = (unsigned char)c1;
				bytes[1] = (unsigned char)c2;
				s = [[NSString alloc] initWithBytes:bytes length:2 encoding:NSShiftJISStringEncoding];
				if (s == nil || [s length] != 1) {
					[s release];
					continue;
				}
				cp = [s characterAtIndex:0];
				[s release];
				if (cp == 0xFFFD)
					continue;
				if (!CTFontGetGlyphsForCharacters(fullFont, &cp, &glyph, 1) || glyph == 0)
					continue;
				if (s_draw_cell(fullFont, glyph, 16, cellHeight, sKanjiData + (i * 188 + j) * 32))
					nKanji++;
			}
		}
		CFRelease(fullFont);
	}
	NSLog(@"mac_font \"%s\": halfwidth %d, kanji %d", name, nHalf, nKanji);
}

static char sPendingMacFont[256];

static void s_alert_font(NSString *message, NSString *info)
{
	NSAlert *alert = [[NSAlert alloc] init];
	[alert setMessageText:message];
	[alert setInformativeText:info];
	[alert setAlertStyle:NSAlertStyleWarning];
	[alert runModal];
	[alert release];
}

static void s_use_mac_font_main(const char *requested)
{
	int haveBundled;
	haveBundled = s_register_bundled_kodenmachou();
	if (requested != NULL && requested[0] != 0 && s_nsfont_installed(requested)) {
		s_apply_mac_font_main(requested, kFontCellHeight);
		return;
	}
	if (requested != NULL && requested[0] != 0 && strcmp(requested, kBundledFontPSName) != 0) {
		if (haveBundled) {
			s_alert_font(@"フォントが見つかりません",
				[NSString stringWithFormat:@"「%s」は使えません。同梱の %s で表示します。",
					requested, kBundledFontPSName]);
		} else {
			s_alert_font(@"フォントが見つかりません",
				[NSString stringWithFormat:@"「%s」は使えません。同梱の %s.ttf もありません。",
					requested, kBundledFontPSName]);
		}
	} else if (!haveBundled) {
		s_alert_font(@"フォントが見つかりません",
			[NSString stringWithFormat:@"同梱の %s.ttf がありません。", kBundledFontPSName]);
	}
	if (haveBundled)
		s_apply_mac_font_main(kBundledFontPSName, kFontCellHeight);
}

static void s_use_mac_font(const char *requested)
{
	if (requested == NULL)
		requested = "";
	if ([NSThread isMainThread]) {
		s_use_mac_font_main(requested);
		return;
	}
	strncpy(sPendingMacFont, requested, sizeof sPendingMacFont - 1);
	sPendingMacFont[sizeof sPendingMacFont - 1] = 0;
	dispatch_sync(dispatch_get_main_queue(), ^{
		s_use_mac_font_main(sPendingMacFont);
	});
}

void
XSTed_start_main(int (*mainPtr)(int, char **), int argc, char **argv)
{
	NSArray *args;
	char macFont[256];

	XSTed_setup_default_path();

	/*  Glyph tables start empty. The bundled TTF, or #mac_font=, fills them.  */
	sFontData = (uint8_t *)calloc(kFontHalfBytes, 1);
	sKanjiData = (uint8_t *)calloc(kFontKanjiBytes, 1);
	s_font_capture_base();
	s_load_mac_font_setting(argc, argv, macFont, sizeof macFont);
	s_use_mac_font(macFont);

	/*  Initialize CoreMIDI  */
	CoreMIDIInitIfNeeded();
	CoreMIDISelectDeviceForPort(0, 0);
	CoreMIDISelectDeviceForPort(1, 1);

	args = [NSArray arrayWithObjects:
			[NSValue valueWithPointer: mainPtr],
			[NSNumber numberWithInt: argc],
			[NSValue valueWithPointer: argv], nil];
	
	NSThread *thread = [[[NSThread alloc] initWithTarget:[NSApp delegate] selector:@selector(startX68kThread:) object:args] autorelease];
	[thread setStackSize:4*1024*1024];  //  4MB should be sufficient (the default 512K may be too small)
	[thread start];
}


void
XSTed_init_window(void)
{
	iswindowopened = 1;
}

void
XSTed_close_window(void)
{
}

void
XSTed_register_key(const char *str)
{
	char buf[128];
	char *p, *q, *r;
	int code, mask, n;
	uint32_t ul;
	static struct {
		const char *symbol;
		int code;
	} s_x68k_keysym_table[] = {
		{ "TOROKU", x68k_touroku },
		{ "KIGOU", x68k_kigou },
		{ "KIGO", x68k_kigou },
		{ "KANA", x68k_kana },
		{ "DEL", x68k_del },
		{ "HOME", x68k_home },
		{ "RUP", x68k_rollup },
		{ "RDOWN", x68k_rolldown },
		{ "CLR", x68k_clr },
		{ "INS", x68k_ins },
		{ "UNDO", x68k_undo },
		{ "OPT1", x68k_opt1 },
		{ "OPT2", x68k_opt2 },
		{ NULL, 0 }
	};
		
	strncpy(buf, str, 127);
	buf[127] = 0;
	p = buf + 5;   /*  skip 'KEY_'  */
	code = 0;
	q = strsep(&p, "=");
	while ((r = strrchr(q, ' ')) != NULL)
		*r = 0;
	if (strncasecmp(q, "XF", 2) == 0 && (n = atoi(q + 2)) >= 1 && n <= 5) {
		code = x68k_xf1 + n - 1;
	} else if (strncasecmp(q, "F", 1) == 0 && (n = atoi(q + 1)) >= 1 && n <= 10) {
		code = x68k_f1 + n - 1;
	} else if (strcasecmp(q, "TOROKU") == 0) {
		code = x68k_touroku;
	} else if (strcasecmp(q, "KIGOU") == 0 || strcasecmp(q, "KIGO") == 0) {
		code = x68k_kigou;
	} else if (strcasecmp(q, "KANA") == 0) {
		code = x68k_kana;
	} else if (strcasecmp(q, "OPT1") == 0) {
		code = x68k_opt1;
	} else if (strcasecmp(q, "OPT2") == 0) {
		code = x68k_opt2;
	} else {
		for (n = 0; s_x68k_keysym_table[n].symbol != NULL; n++) {
			if (strcasecmp(q, s_x68k_keysym_table[n].symbol) == 0) {
				code = s_x68k_keysym_table[n].code;
				break;
			}
		}
	}
	
	if (code == 0)
		return;

	while ((q = strsep(&p, " ")) != NULL) {
		if (*q == 0)
			continue;
		mask = 0;
		/* option-- means Option + '-'; only split on '-' when the
		   left token is a modifier. */
		for (;;) {
			char *dash = (q != NULL) ? strchr(q, '-') : NULL;
			if (dash == NULL || dash == q)
				break;
			*dash = 0;
			r = q;
			q = dash + 1;
			if (strcasecmp(r, "c") == 0 || strcasecmp(r, "ctrl") == 0 || strcasecmp(r, "control") == 0)
				mask |= 2;
			else if (strcasecmp(r, "o") == 0 || strcasecmp(r, "opt") == 0 || strcasecmp(r, "option") == 0)
				mask |= 4;
			else {
				*dash = '-';
				q = r;
				break;
			}
		}
		if (q == NULL)
			continue;
		if (*q == 0)
			q = "-";
		if (strcasecmp(q, "space") == 0)
			n = ' ';
		else if (strcasecmp(q, "up") == 0)
			n = NSUpArrowFunctionKey;
		else if (strcasecmp(q, "down") == 0)
			n = NSDownArrowFunctionKey;
		else if (strcasecmp(q, "left") == 0)
			n = NSLeftArrowFunctionKey;
		else if (strcasecmp(q, "right") == 0)
			n = NSRightArrowFunctionKey;
		else if (tolower(*q) == 'f' && (n = atoi(q + 1)) >= 1 && n <= 12)
			n = NSF1FunctionKey - 1 + n;
		else if (strcasecmp(q, "delete") == 0)
			n = NSDeleteFunctionKey;
		else if (strcasecmp(q, "home") == 0)
			n = NSHomeFunctionKey;
		else if (strcasecmp(q, "end") == 0)
			n = NSEndFunctionKey;
		else if (strcasecmp(q, "page_up") == 0)
			n = NSPageUpFunctionKey;
		else if (strcasecmp(q, "page_down") == 0)
			n = NSPageDownFunctionKey;
		else if (strcasecmp(q, "clear") == 0)
			n = NSClearLineFunctionKey;
		else if (strlen(q) == 1)
			n = tolower(*q) & 127;
		else n = 0;
		/*  Look up the table, and replace the entry if present  */
		ul = n | ((uint32_t)mask << 16);
		for (n = 0; n < 127; n++) {
			if (s_x68k_special_key[n] == 0
			|| (s_x68k_special_key[n] & 0x00ffffff) == ul) {
				s_x68k_special_key[n] = (ul | ((uint32_t)code << 24));
				break;
			}
		}
	}		
}

void
XSTed_register_midi_device(const char *str)
{
	char buf[128];
	char *p, *q, *r;
	int n;
	uint8_t input_flag;

	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	
	strncpy(buf, str, 127);
	buf[127] = 0;
	p = buf + 13;  /*  skip "#midi_device_"  */
	q = strsep(&p, "=");
	while ((r = strrchr(q, ' ')) != NULL)
		*r = 0;	
	if (strcasecmp(q, "input") == 0) {
		input_flag = 1;
	} else if (strcasecmp(q, "output") == 0) {
		input_flag = 0;
	} else return;
	
	n = 0;
	while ((q = strsep(&p, ",")) != NULL) {
		while (*q == ' ')
			q++;
		if (*q == 0)
			continue;
		if (input_flag == 0) {
			const char *canon = CoreMIDICanonicalOutputName(q);
			CoreMIDISelectDeviceByNameForPort(canon, n);
			[def setValue:[NSString stringWithUTF8String:canon] forKey:[NSString stringWithFormat:@"midiout%d", n + 1]];
		} else if (input_flag == 1) {
			CoreMIDISelectInputDeviceByName(q);
			[def setValue:[NSString stringWithUTF8String:q] forKey:@"midiin"];
		}
		n++;
	}
}

static float
parse_dot_float(const char *s)
{
	int sign = 1;
	double v = 0.0;
	double place = 0.1;

	if (s == NULL)
		return 0.0f;
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '+')
		s++;
	else if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10.0 + (double)(*s - '0');
		s++;
	}
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') {
			v += (double)(*s - '0') * place;
			place *= 0.1;
			s++;
		}
	}
	return (float)(sign * v);
}

static NSString *
DotFloat(double v, int decimals)
{
	int scale = 1;
	int i, neg, whole, frac;
	double a;

	for (i = 0; i < decimals; i++)
		scale *= 10;
	neg = v < 0.0;
	a = neg ? -v : v;
	a = floor(a * (double)scale + 0.5);
	whole = (int)(a / (double)scale);
	frac = (int)a - whole * scale;
	if (decimals <= 1)
		return [NSString stringWithFormat:@"%s%d.%d", neg ? "-" : "", whole, frac];
	if (decimals >= 3)
		return [NSString stringWithFormat:@"%s%d.%03d", neg ? "-" : "", whole, frac];
	return [NSString stringWithFormat:@"%s%d.%02d", neg ? "-" : "", whole, frac];
}

void
XSTed_configure_mac_settings(const char *line)
{
	if (strncasecmp(line, "#KEY_", 5) == 0)
		XSTed_register_key(line);
	if (strncasecmp(line, "#MIDI_DEVICE_", 13) == 0)
		XSTed_register_midi_device(line);
	if (strncasecmp(line, "#dls_synth_font=", 16) == 0) {
		const char *p = line + 16;
		while (*p == ' ' || *p == '\t')
			p++;
		CoreMIDISetDLSSynthFont(p);
	}
	if (strncasecmp(line, "#dls_synth_use_reverb=", 22) == 0
	 || strncasecmp(line, "#dls_synth_reverbtype=", 22) == 0) {
		DLSInternalReverb bus;
		CoreMIDIGetDLSInternalReverb(&bus);
		if (strncasecmp(line, "#dls_synth_use_reverb=", 22) == 0)
			bus.useReverb = atoi(line + 22);
		else
			bus.roomType = atoi(line + 22);
		CoreMIDISetDLSInternalReverb(&bus);
	}
	if (strncasecmp(line, "#dls_total_effect=", 18) == 0
	 || strncasecmp(line, "#dls_eq_global_gain=", 20) == 0
	 || strncasecmp(line, "#dls_bass_gain=", 15) == 0
	 || strncasecmp(line, "#dls_treble_gain=", 17) == 0
	 || strncasecmp(line, "#dls_reverb_type=", 17) == 0
	 || strncasecmp(line, "#dls_reverb_mix=", 16) == 0
	 || strncasecmp(line, "#dls_limiter_boost=", 19) == 0
	 || strncasecmp(line, "#dls_volume=", 12) == 0) {
		DLSEffectSettings fx;
		CoreMIDIGetDLSEffects(&fx);
		if (strncasecmp(line, "#dls_total_effect=", 18) == 0)
			fx.totalEffect = atoi(line + 18);
		else if (strncasecmp(line, "#dls_eq_global_gain=", 20) == 0)
			fx.globalGain = parse_dot_float(line + 20);
		else if (strncasecmp(line, "#dls_bass_gain=", 15) == 0)
			fx.bassGain = parse_dot_float(line + 15);
		else if (strncasecmp(line, "#dls_treble_gain=", 17) == 0)
			fx.trebleGain = parse_dot_float(line + 17);
		else if (strncasecmp(line, "#dls_reverb_type=", 17) == 0)
			fx.reverbPreset = atoi(line + 17);
		else if (strncasecmp(line, "#dls_reverb_mix=", 16) == 0)
			fx.reverbMix = parse_dot_float(line + 16);
		else if (strncasecmp(line, "#dls_limiter_boost=", 19) == 0)
			fx.limiterBoost = parse_dot_float(line + 19);
		else
			fx.volume = parse_dot_float(line + 12);
		CoreMIDISetDLSEffects(&fx);
	}
	if (strncasecmp(line, "#mac_font=", 10) == 0) {
		const char *p = line + 10;
		char name[256];
		size_t n;
		while (*p == ' ' || *p == '\t')
			p++;
		n = strlen(p);
		while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t'))
			n--;
		if (n >= sizeof name)
			n = sizeof name - 1;
		memcpy(name, p, n);
		name[n] = 0;
		s_use_mac_font(name);
	}
}

static int
cnf_is_file(const char *path)
{
	struct stat st;
	return path != NULL && path[0] != 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/*  Same search as defload: beside the executable, then Application Support. */
static int
resolve_sted2_cnf(char *path, size_t pathSize)
{
	char tmp[PATHLEN];
	const char *argv0;
	const char *base;

	argv0 = (sted_saved_argc > 0 && sted_saved_argv != NULL && sted_saved_argv[0] != NULL)
		? sted_saved_argv[0] : "sted2";
	strmfe(tmp, argv0, "cnf");
	if (cnf_is_file(tmp)) {
		strlcpy(path, tmp, pathSize);
		return 1;
	}
	if (s_sted_default_path[0] != 0) {
		base = strrchr(tmp, '/');
		snprintf(path, pathSize, "%s/%s", s_sted_default_path, base != NULL ? base + 1 : tmp);
		if (cnf_is_file(path))
			return 1;
	}
	path[0] = 0;
	return 0;
}

/*  keys[0] is the sound bank. allowComment accepts a leading '*'. */
static int
cnf_line_key(const char *s, const char *const *keys, int nkeys, int allowComment)
{
	int k;

	if (s == NULL)
		return -1;
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '*') {
		if (!allowComment)
			return -1;
		s++;
		while (*s == ' ' || *s == '\t')
			s++;
	}
	for (k = 0; k < nkeys; k++) {
		if (strncasecmp(s, keys[k], strlen(keys[k])) == 0)
			return k;
	}
	return -1;
}

int
XSTed_save_dls_settings(void)
{
	char path[PATHLEN];
	NSString *filePath;
	NSError *err = nil;
	NSStringEncoding enc = 0;
	NSString *text, *out;
	NSMutableArray *lines, *kept;
	DLSEffectSettings fx;
	DLSInternalReverb bus;
	const char *font;
	NSString *values[11];
	static const char *keys[11] = {
		"#dls_synth_font=",
		"#dls_synth_use_reverb=",
		"#dls_synth_reverbtype=",
		"#dls_total_effect=",
		"#dls_eq_global_gain=",
		"#dls_bass_gain=",
		"#dls_treble_gain=",
		"#dls_reverb_type=",
		"#dls_reverb_mix=",
		"#dls_limiter_boost=",
		"#dls_volume=",
	};
	NSInteger commentAnchor = -1;
	NSInteger activeAnchor = -1;
	NSInteger anchor = -1;
	NSUInteger i, n, insertAt, anchorOut;
	int k;
	int anchorIsComment;

	if (!resolve_sted2_cnf(path, sizeof path)) {
		NSLog(@"sted2.cnf was not found; DLS settings were not saved");
		return -1;
	}
	filePath = [NSString stringWithUTF8String:path];
	text = [NSString stringWithContentsOfFile:filePath usedEncoding:&enc error:&err];
	if (text == nil) {
		enc = NSUTF8StringEncoding;
		err = nil;
		text = [NSString stringWithContentsOfFile:filePath encoding:NSUTF8StringEncoding error:&err];
	}
	if (text == nil) {
		NSLog(@"could not read %@: %@", filePath, err);
		return -1;
	}
	CoreMIDIGetDLSEffects(&fx);
	CoreMIDIGetDLSInternalReverb(&bus);
	font = CoreMIDIDLSSynthFontFilename();
	if (font == NULL)
		font = "";
	values[0] = [NSString stringWithFormat:@"#dls_synth_font=%s", font];
	values[1] = [NSString stringWithFormat:@"#dls_synth_use_reverb=%d", bus.useReverb ? 1 : 0];
	values[2] = [NSString stringWithFormat:@"#dls_synth_reverbtype=%d", bus.roomType];
	values[3] = [NSString stringWithFormat:@"#dls_total_effect=%d", fx.totalEffect ? 1 : 0];
	values[4] = [NSString stringWithFormat:@"#dls_eq_global_gain=%@", DotFloat(fx.globalGain, 1)];
	values[5] = [NSString stringWithFormat:@"#dls_bass_gain=%@", DotFloat(fx.bassGain, 1)];
	values[6] = [NSString stringWithFormat:@"#dls_treble_gain=%@", DotFloat(fx.trebleGain, 1)];
	values[7] = [NSString stringWithFormat:@"#dls_reverb_type=%d", fx.reverbPreset];
	values[8] = [NSString stringWithFormat:@"#dls_reverb_mix=%@", DotFloat(fx.reverbMix, 1)];
	values[9] = [NSString stringWithFormat:@"#dls_limiter_boost=%@", DotFloat(fx.limiterBoost, 1)];
	values[10] = [NSString stringWithFormat:@"#dls_volume=%@", DotFloat(fx.volume, 3)];
	text = [text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
	text = [text stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
	lines = [NSMutableArray arrayWithArray:[text componentsSeparatedByString:@"\n"]];
	n = [lines count];
	for (i = 0; i < n; i++) {
		const char *s = [[lines objectAtIndex:i] UTF8String];
		if (cnf_line_key(s, keys, 11, 1) != 0)
			continue;
		if (cnf_line_key(s, keys, 11, 0) == 0)
			activeAnchor = (NSInteger)i;
		else
			commentAnchor = (NSInteger)i;
	}
	/*  Commented examples mark the sound-bank section. An active line is the fallback. */
	anchor = commentAnchor >= 0 ? commentAnchor : activeAnchor;
	anchorIsComment = commentAnchor >= 0;
	kept = [NSMutableArray array];
	anchorOut = NSNotFound;
	for (i = 0; i < n; i++) {
		NSString *line = [lines objectAtIndex:i];
		const char *s = [line UTF8String];
		int active = cnf_line_key(s, keys, 11, 0);
		if (anchor >= 0 && i == (NSUInteger)anchor) {
			anchorOut = [kept count];
			[kept addObject:line];
			continue;
		}
		if (active >= 0)
			continue;
		if ([line isEqualToString:@"* Apple DLS Synth effect settings (DLS Synth dialog)"])
			continue;
		[kept addObject:line];
	}
	if (anchorOut == NSNotFound) {
		insertAt = [kept count];
		if (insertAt > 0 && [[kept lastObject] length] == 0)
			insertAt--;
	} else if (anchorIsComment) {
		insertAt = anchorOut + 1;
	} else {
		[kept replaceObjectAtIndex:anchorOut withObject:values[0]];
		insertAt = anchorOut + 1;
	}
	if (anchorIsComment || anchorOut == NSNotFound)
		[kept insertObject:values[0] atIndex:insertAt++];
	for (k = 1; k < 11; k++)
		[kept insertObject:values[k] atIndex:insertAt++];
	out = [kept componentsJoinedByString:@"\n"];
	if (![out hasSuffix:@"\n"])
		out = [out stringByAppendingString:@"\n"];
	if (enc == 0)
		enc = NSUTF8StringEncoding;
	err = nil;
	if (![out writeToFile:filePath atomically:YES encoding:enc error:&err]) {
		NSLog(@"could not write %@: %@", filePath, err);
		return -1;
	}
	return 0;
}

#pragma mark "=== Graphic functions ==="
			
static int
tpage(int plane)
{
	if (plane == 0)
		return 1;
	else if (plane == 1)
		return 2;
	else return 3;
}

static void
MakeRect(Rect *rect, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	if (x0 < x1) {
		rect->left = x0;
		rect->right = x1;
	} else {
		rect->left = x1;
		rect->right = x0;
	}
	if (y0 < y1) {
		rect->top = y0;
		rect->bottom = y1;
	} else {
		rect->top = y1;
		rect->bottom = y0;
	}
}

static void
InvalTextInRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	NSRect rect = NSMakeRect(x0, y0, x1 - x0, y1 - y0);
	if (sTextInvalRect.size.width == 0.0 || sTextInvalRect.size.height == 0.0)
		sTextInvalRect = rect;
	else {
		sTextInvalRect = NSUnionRect(sTextInvalRect, rect);
	}
}

static void
InvalGraphInRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	//  The rect must be in the screen coordinate
	NSRect rect = NSMakeRect(x0 - sGraphOrigin.x, y0 - sGraphOrigin.y, x1 - x0, y1 - y0);
	if (sGraphInvalRect.size.width == 0.0 || sGraphInvalRect.size.height == 0.0)
		sGraphInvalRect = rect;
	else {
		sGraphInvalRect = NSUnionRect(sGraphInvalRect, rect);
	}
}

void
XSTed_txxline(uint16_t p, int16_t x0, int16_t y0, int16_t x1, uint16_t ls)
{
	int16_t i, x1e;
	uint8_t col;

	/* x1 is length in pixels (IOCS TXXLINE / xwin_graph.c), not end x. */
	if (x1 <= 0 || y0 < 0 || y0 >= sTextHeight)
		return;
	x1e = x0 + x1 - 1;
	if (x0 < 0)
		x0 = 0;
	if (x1e >= sTextWidth)
		x1e = sTextWidth - 1;
	if (x0 > x1e)
		return;
	col = (ls == 0 ? 0 : tpage(p));
	[sLock lock];
	for (i = x0; i <= x1e; i++)
		sTextVram[y0 * sTextWidth + i] = col;
	InvalTextInRect(x0, y0, x1e + 1, y0 + 1);
	[sLock unlock];
}

void
XSTed_txyline(uint16_t p, int16_t x0, int16_t y0, int16_t y1, uint16_t ls)
{
	int16_t i, y1e;
	uint8_t col;

	/* y1 is length in pixels (IOCS TXYLINE / xwin_graph.c), not end y. */
	if (y1 <= 0 || x0 < 0 || x0 >= sTextWidth)
		return;
	y1e = y0 + y1 - 1;
	if (y0 < 0)
		y0 = 0;
	if (y1e >= sTextHeight)
		y1e = sTextHeight - 1;
	if (y0 > y1e)
		return;
	col = (ls == 0 ? 0 : tpage(p));
	[sLock lock];
	for (i = y0; i <= y1e; i++)
		sTextVram[i * sTextWidth + x0] = col;
	InvalTextInRect(x0, y0, x0 + 1, y1e + 1);
	[sLock unlock];
}

void
XSTed_txbox(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t p)
{
	int16_t lw, lh;

	/* Corners (x0,y0)-(x1,y1), same as X11 XSTed_txbox. */
	lw = x1 - x0;
	lh = y1 - y0;
	if (lw < 0) {
		x0 = x1;
		lw = -lw;
	}
	if (lh < 0) {
		y0 = y1;
		lh = -lh;
	}
	XSTed_txxline(p, x0, y0, lw, 1);
	XSTed_txxline(p, x0, y0 + lh, lw, 1);
	XSTed_txyline(p, x0, y0, lh, 1);
	XSTed_txyline(p, x0 + lw, y0, lh, 1);
}

void
XSTed_trev(int xch, int ych, int lch, int col)
{
	int16_t x0, x1, y0, i, j;
	uint32_t *ip;
	x0 = xch * 8;
	y0 = ych * 16;
	if (y0 < 0 || y0 >= sTextHeight)
		return;
	x1 = (xch + lch) * 8;
	if (x0 < 0)
		x0 = 0;
	if (x1 >= sTextWidth)
		x1 = sTextWidth - 1;
	[sLock lock];
	for (i = x0; i < x1; i++) {
		for (j = 0; j < 16; j++) {
			sTextVram[(j + y0) * sTextWidth + i] ^= 3;
		}
	}
	InvalTextInRect(x0, y0, x1 + 1, y0 + 16);
	[sLock unlock];
}

void
XSTed_rev_area(int r_ad, int r_ln, int edit_scr)
{
//	printf("XSTed_rev_area %d %d %d\n", r_ad, r_ln, edit_scr);
	/*  Not implemented yet  */
}

void
XSTed_tfill(uint16_t p, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t ls)
{
	uint8_t col;
	int16_t i, j, x1e, y1e;

	/* x1/y1 are width/height (IOCS TXFILL / xwin_graph.c), not end point. */
	if (x1 <= 0 || y1 <= 0)
		return;
	x1e = x0 + x1 - 1;
	y1e = y0 + y1 - 1;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1e >= sTextWidth)
		x1e = sTextWidth - 1;
	if (y1e >= sTextHeight)
		y1e = sTextHeight - 1;
	if (x0 > x1e || y0 > y1e)
		return;
	col = (ls == 0 ? 0 : tpage(p));
	[sLock lock];
	for (i = x0; i <= x1e; i++) {
		for (j = y0; j <= y1e; j++) {
			sTextVram[j * sTextWidth + i] = col;
		}
	}
	InvalTextInRect(x0, y0, x1e + 1, y1e + 1);
	[sLock unlock];
//	printf("XSTed_tfill %d %d %d %d %d %d\n", p, x0, y0, x1, y1, ls);
}

void
XSTed_trascpy(int dst, int src, int line, int mode)
{
	int i;
	line /= 4;
	if (mode >= 0x8000) {
		/*  Move downward, dst > src  */
		src = (src - 3) / 4 - line + 1;
		dst = (dst - 3) / 4 - line + 1;
		if (dst + line >= sTextHeight / 16)
			line = sTextHeight / 16 - dst;
		if (line <= 0)
			return;
		[sLock lock];
		memmove(sTextVram + dst * 16 * sTextWidth, sTextVram + src * 16 * sTextWidth, line * 16 * sTextWidth);
		/* FIX: Commented out memset to prevent erasing original line when pressing Return (line insertion). */
		/* Original code: memset(sTextVram + src * 16 * sTextWidth, 0, (dst - src) * 16 * sTextWidth); */
		// memset(sTextVram + src * 16 * sTextWidth, 0, (dst - src) * 16 * sTextWidth);
		InvalTextInRect(0, src * 16, sTextWidth, (dst + line) * 16);
		[sLock unlock];
	} else {
		/*  Move upward, dst < src  */
		src = src / 4;
		dst = dst / 4;
		if (src == 63) {
			/*  Clear  */
			if (dst + line >= sTextHeight / 16)
				line = sTextHeight / 16 - dst;
			if (line <= 0)
				return;
			[sLock lock];
			/* FIX: Enabled memset to clear VRAM when explicit clear (src == 63) is requested. */
			memset(sTextVram + dst * 16 * sTextWidth, 0, line * 16 * sTextWidth);
			InvalTextInRect(0, dst * 16, sTextWidth, (dst + line) * 16);
			[sLock unlock];
		} else {
			if (src + line >= sTextHeight / 16)
				line = sTextHeight / 16 - src;
			if (line <= 0)
				return;
			[sLock lock];
			memmove(sTextVram + dst * 16 * sTextWidth, sTextVram + src * 16 * sTextWidth, line * 16 * sTextWidth);
		//	memset(sTextVram + (dst + line) * 16 * sTextWidth, 0, (src - dst) * 16 * sTextWidth);
			InvalTextInRect(0, dst * 16, sTextWidth, (src + line) * 16);
			[sLock unlock];
		}
	}
}

void
XSTed_t_scrw(int x1, int y1, int xs, int ys, int x2, int y2)
{
	int xxs, yys, xc, xcs, y;
	x1 *= 8;
	y1 *= 8;
	x2 *= 8;
	y2 *= 8;
	if (x1 < 0) {
		xs += x1;
		x1 = 0;
	}
	if (y1 < 0) {
		ys += y1;
		y1 = 0;
	}
	if (xs <= 0 || ys <= 0)
		return;
	if (x2 + xs < sTextWidth)
		xxs = xs;
	else xxs = sTextWidth - x2;
	if (y2 + ys < sTextHeight)
		yys = ys;
	else yys = sTextHeight - y2;
	if (y1 != y2) {
		xc = x1;
		xcs = xs;
	} else {
		if (x1 < x2) {
			xc = x1;
			xcs = x2 - x1;
		} else {
			xc = x2 + xs;
			xcs = x1 - x2;
		}
	}
	[sLock lock];
	if (y1 < y2) {
		for (y = yys - 1; y >= 0; y--) {
			memmove(sTextVram + (y1 + y) * sTextWidth + x1, sTextVram + (y2 + y) * sTextWidth + x2, xxs);
			memset(sTextVram + (y1 + y) * sTextWidth + xc, 0, xcs);
		}
	} else {
		for (y = 0; y < yys; y++) {
			memmove(sTextVram + (y1 + y) * sTextWidth + x1, sTextVram + (y2 + y) * sTextWidth + x2, xxs);
			memset(sTextVram + (y1 + y) * sTextWidth + xc, 0, xcs);
		}
	}
	InvalTextInRect(x1, y1, x1 + xs, y1 + ys);
	InvalTextInRect(x2, y2, x2 + xxs, y2 + ys);
	[sLock unlock];
}

void
XSTed_gbox(int x1, int y1, int x2, int y2, unsigned int col, unsigned int ls)
{
	int x, y;
	col = col % 16;
	[sLock lock];
	if (y1 >= 0 && y1 < sGraphHeight) {
		for (x = x1; x <= x2; x++) {
			if (x >= 0 && x < sGraphWidth)
				sGraphVram[y1 * sGraphWidth + x] = col;
		}
	}
	if (y2 >= 0 && y2 < sGraphHeight) {
		for (x = x1; x <= x2; x++) {
			if (x >= 0 && x < sGraphWidth)
				sGraphVram[y2 * sGraphWidth + x] = col;
		}
	}
	if (x1 >= 0 && x1 < sGraphWidth) {
		for (y = y1; y <= y2; y++) {
			if (y >= 0 && y < sGraphHeight)
				sGraphVram[y * sGraphWidth + x1] = col;
		}
	}
	if (x2 >= 0 && x2 < sGraphWidth) {
		for (y = y1; y <= y2; y++) {
			if (y >= 0 && y < sGraphHeight)
				sGraphVram[y * sGraphWidth + x2] = col;
		}
	}
	InvalGraphInRect(x1, y1, x2 + 1, y2 + 1);
	[sLock unlock];
//	printf("XSTed_gbox %d %d %d %d %d %d\n", x1, y1, x2, y2, col, ls);
}

void
XSTed_gfill(int x1, int y1, int x2, int y2, int col)
{
	int x, y;
	col = col % 16;
	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 >= sGraphWidth)
		x2 = sGraphWidth - 1;
	if (y2 >= sGraphHeight)
		y2 = sGraphHeight - 1;
	[sLock lock];
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			sGraphVram[y * sGraphWidth + x] = col;
		}
	}
	InvalGraphInRect(x1, y1, x2 + 1, y2 + 1);
	[sLock unlock];
//	printf("XSTed_gfill %d %d %d %d %d\n", x1, y1, x2, y2, col);
}

int
XSTed_gpoint(int x, int y)
{
//	printf("XSTed_gpoint %d %d\n", x, y);
	return 0;
}

void
XSTed_gline(int x1, int y1, int x2, int y2, int col, int ls)
{
	int dx, wx, dy, wy, x, y, i;
	float fx, fy, d;
	col = col % 16;
	wx = x2 - x1;
	if (wx < 0) {
		wx = -wx;
		dx = -1;
	} else dx = 1;
	wy = y2 - y1;
	if (wy < 0) {
		wy = -wy;
		dy = -1;
	} else dy = 1;
	[sLock lock];
	if (wx == wy) {
		if (x1 >= 0 && x1 < sGraphWidth && y1 >= 0 && y1 < sGraphHeight)
			sGraphVram[y1 * sGraphWidth + x1] = col;
	} else if (wx > wy) {
		//  Proceed along the x axis
		d = dy * (float)wy / wx;
		for (x = x1, fy = y1, i = 0; i <= wx; x += dx, fy += d, i++) {
			y = floor(fy + 0.5);
			if (x >= 0 && x < sGraphWidth && y >= 0 && y < sGraphHeight)
				sGraphVram[y * sGraphWidth + x] = col;
		}
	} else {
		//  Proceed along the y axis
		d = dx * (float)wx / wy;
		for (fx = x1, y = y1, i = 0; i <= wy; fx += d, y += dy, i++) {
			x = floor(fx + 0.5);
			if (x >= 0 && x < sGraphWidth && y >= 0 && y < sGraphHeight)
				sGraphVram[y * sGraphWidth + x] = col;
		}
	}
	x = (dx > 0 ? x1 : x2);
	y = (dy > 0 ? y1 : y2);
	InvalGraphInRect(x, y, x + wx + 1, y + wy + 1);
	[sLock unlock];
//	printf("XSTed_gline %d %d %d %d %d %d\n", x1, y1, x2, y2, col, ls);
}

void
XSTed_tg_copy(int edit_scr)
{
	//  Copy text screen to the 'sub' graphic screen (i.e. y >= 512)
	//  edit_scr 0: (2,4)-(19,31), 1: (58,4)-(75,31)?
	//              (2,6)-(37,29)     (58,6)-(93,29)?
	int x1, x2, y1, y2, x, y;
	if (edit_scr == 0) {
		x1 = 2;
		x2 = 37;
		y1 = 6;
		y2 = 29;
	} else {
		x1 = 58;
		x2 = 93;
		y1 = 6;
		y2 = 29;
	}
	[sLock lock];
	for (y = y1 * 16; y < (y2 + 1) * 16; y++) {
		for (x = x1 * 8; x < (x2 + 1) * 8; x++) {
			sGraphVram[(y + 512) * sGraphWidth + x] = sTextVram[y * sTextWidth + x];
		}
	}
	InvalGraphInRect(x1 * 8, y1 * 16, (x2 + 1) * 16, (y2 + 1) * 16);
	[sLock unlock];
//	printf("XSTed_tg_copy %d\n", edit_scr);
}

void
XSTed_tg_copy2(int edit_scr)
{
	//  Copy text screen to the 'sub' graphic screen (i.e. y >= 512)
	/* (0,6)-(27,29)         (38,6)-(65,29) */
	//  edit_scr 0: (0,6)-(55,29), 1: (38,6)-(93,29)?
	//              (0,6)-(27,29)     (38,6)-(65,29)?
	int x1, x2, y1, y2, x, y;
	if (edit_scr == 0) {
		x1 = 0;
		x2 = 27;
		y1 = 6;
		y2 = 29;
	} else {
		x1 = 38;
		x2 = 93;
		y1 = 6;
		y2 = 29;
	}
	[sLock lock];
	for (y = y1 * 16; y < (y2 + 1) * 16; y++) {
		for (x = x1 * 8; x < (x2 + 1) * 8; x++) {
			sGraphVram[(y + 512) * sGraphWidth + x] = sTextVram[y * sTextWidth + x];
		}
	}
	InvalGraphInRect(x1 * 8, y1 * 16, (x2 + 1) * 16, (y2 + 1) * 16);
	[sLock unlock];
//	 printf("XSTed_tg_copy2 %d\n", edit_scr);
}

void
XSTed_ghome(int home_flag)
{
	if (home_flag)
		sGraphOrigin.y = 512;
	else
		sGraphOrigin.y = 0;
	InvalGraphInRect(0, 0, sGraphWidth, sGraphHeight);
//	printf("XSTed_ghome %d\n", home_flag);
}

void
XSTed_gclr(void)
{
	[sLock lock];
	memset(sGraphVram, 0, sizeof(sGraphVram[0]) * sGraphWidth * sGraphHeight);
	InvalGraphInRect(0, 0, sGraphWidth, sGraphHeight);
	[sLock unlock];
//	printf("XSTed_gclr\n");
}

/*
void
XSTed_tclr(void)
{
	[sLock lock];
	memset(sTextVram, 0, sizeof(sTextVram[0]) * sTextWidth * sTextHeight);
	InvalTextInRect(0, 0, sTextWidth, sTextHeight);
	[sLock unlock];
	printf("XSTed_tclr\n");
}
*/

//  B_CLR_AL: clear all text screen
//  (Equivalent to IOCS call B_CLR_ST(mode) with mode = 2)
void
XSTed_cls_al(void)
{
	[sLock lock];
	memset(sTextVram, 0, sizeof(sTextVram[0]) * sTextWidth * sTextHeight);
	InvalTextInRect(0, 0, sTextWidth, sTextHeight);
	[sLock unlock];
//	printf("XSTed_cls_al\n");
}

void
XSTed_overlap(void)
{
//	printf("XSTed_overlap\n");
}

#pragma mark "=== Color/palette functions ==="

static int
s_XSTed_setpalet(uint32_t *pp, int col)
{
	uint32_t c = col, c2;
	int32_t rgb[3];
	int i;
	rgb[0] = ((c >> 6) & 0x1f);
	rgb[1] = ((c >> 11) & 0x1f);
	rgb[2] = ((c >> 1) & 0x1f);
	for (i = 0; i < 3; i++)
		rgb[i] = (rgb[i] ? (rgb[i] + 1) * 8 - 1 : 0);
	c = (rgb[0] << 24) + (rgb[1] << 16) + (rgb[2] << 8) + 0x00ff;
	c2 = *pp;
	*pp = NSSwapHostIntToBig(c);
	c2 = NSSwapBigIntToHost(c2);
	rgb[0] = ((c2 >> 24) & 0xff) >> 3;
	rgb[1] = ((c2 >> 16) & 0xff) >> 3;
	rgb[2] = ((c2 >> 8) & 0xff) >> 3;
	return (rgb[0] << 6) + (rgb[1] << 11) + (rgb[2] << 1);
}

int
XSTed_gpalet( int pal, int col )
{
	int c = s_XSTed_setpalet(&sGraphPalette[pal % 16], col);
//	printf("XSTed_gpalet %d %04x %08x\n", pal, col, sGraphPalette[pal % 16]);
	InvalGraphInRect(0, 0, sGraphWidth, sGraphHeight);
	return c;
}

int
XSTed_tpalet( int pal, int col )
{
	int c = s_XSTed_setpalet(&sTextPalette[pal % 4], col);
//	printf("XSTed_tpalet %d %04x %08x\n", pal, col, sGraphPalette[pal % 16]);
	InvalTextInRect(0, 0, sTextWidth, sTextHeight);
	return c;
}

int
XSTed_tcolor( int col )
{
	int c = sTextColor;
	sTextColor = col;
	return c;
}

int
XSTed_gcolor( int col )
{
	int c = sGraphColor;
	sGraphColor = col;
	return c;
}

int
XSTed_SetTCol( int col )
{
//	printf("XSTed_SetTCol %d\n", col);
	return 0;
}

int
XSTed_SetGCol( int col )
{
//	printf("XSTed_SetGCol %d\n", col);
	return 0;
}

#pragma mark "=== Text functions ==="

void
XSTed_curon(void)
{
//	printf("XSTed_curon\n");
}

void
XSTed_curoff(void)
{
//	printf("XSTed_curoff\n");
}

void
XSTed_tlocate(int x, int y)
{
	sCursorX = x;
	sCursorY = y;
}

static void
s_XSTed_tputs(const char *message, int *xbase, int *ybase, int col, int isText)
{
	uint8_t ch, ch2, *p, dp;
	uint8_t b, rev;
	int i, x, y, xmin, ymin, xmax, ymax, dx, em;

	/* Kanji ROM is indexed by raw X68k/SJIS bytes, including STed gaiji
	   (0xEBxx) and g_print2's 0xF0 prefix. Do not run sted_ensure_sjis
	   here: a Unicode round-trip remaps those codes to ordinary kanji. */

	xmin = xmax = *xbase;
	ymin = ymax = *ybase;
	[sLock lock];
	rev = 0;
	em = 0;
	if (isText) {
		if (col & 4)
			em = 1;
		if (col & 8)
			rev = 1;
		col = col % 4;
	}
	for (i = 0; (ch = message[i]) != 0; i++) {
		ch2 = 0xff;
		if (ch >= 0x20 && ch < 0x7f)
			ch -= 0x20;
		else if (ch >= 0xa0 && ch <= 0xdf)
			ch -= (0xa0 - 96);
		else {
			/*  Shift jis  */
			if (ch >= 0x81 && ch <= 0x9f)
				ch -= 0x81;
			else if (ch >= 0xe0 && ch <= 0xef)
				ch -= (0xe0 - 31);
			else ch = 32;  /*  No corresponding character  */
			i++;
			ch2 = message[i];
			if (ch2 == 0) {
				ch = 0;  /*  hankaku space  */
				ch2 = 0xff;
				i--;
			} else {
				if (ch2 >= 0x40 && ch2 <= 0x7e)
					ch2 -= 0x40;
				else if (ch2 >= 0x80 && ch2 <= 0xfc)
					ch2 -= (0x80 - 63);
				else {
					ch = ch2 = 0; /*  zenkaku space  */
				}
			}
		}
		if (ch2 == 0xff) {
			p = sFontData + ch * 16;
			dp = 1;
			dx = 8;
		} else {
			p = sKanjiData + (ch * 188 + ch2) * 32;
			dp = 2;
			if ((isText ? sTextWidth : sGraphWidth) - *xbase >= 16)
				dx = 16;
			else dx = 8;
		}
		for (y = 0; y < 16; y++, p += dp) {
			int ofs = (*ybase + y) * (isText ? sTextWidth : sGraphWidth) + *xbase;
			uint8_t bb;
			b = *p;
			for (x = 0; x < 8; x++) {
				if (isText) {
					bb = col * ((b & 1) ^ rev);
					sTextVram[ofs + x] = bb;
					if (em && (dx == 16 || x < 8))
						sTextVram[ofs + x + 1] = bb;
				} else {
					if (b & 1)
						sGraphVram[ofs + x] = col;
				}
				b >>= 1;
			}
			if (dx == 16) {
				b = p[1];
				for (x = 0; x < 8; x++) {
					if (isText) {
						bb = col * ((b & 1) ^ rev);
						sTextVram[ofs + 8 + x] = bb;
						if (em && x < 8)
							sTextVram[ofs + 9 + x] = bb;
					} else {
						if (b & 1)
							sGraphVram[ofs + 8 + x] = col;
					}
					b >>= 1;
				}
			}
		}
		*xbase += dx;
		if (*xbase > xmax)
			xmax = *xbase;
		if (isText) {
			if (*xbase > sTextWidth - 8) {
				*xbase = 0;
				*ybase += 16;
				if (*ybase > sTextHeight - 16)
					*ybase -= 16;
			}
		} else {
			if (*xbase > sGraphWidth - 8) {
				*xbase = 0;
				*ybase += 16;
				if (*ybase > sGraphHeight - 16)
					*ybase -= 16;
			}
		}
		if (*xbase < xmin)
			xmin = *xbase;
		if (*ybase > ymax)
			ymax = *ybase;
	}
	if (isText)
		InvalTextInRect(xmin, ymin, xmax, ymax + 16);
	else
		InvalGraphInRect(xmin, ymin, xmax, ymax + 16);

	[sLock unlock];
}

/* Scroll one text row inside the C_WINDOW range. Lines outside it stay put. */
static void
s_scroll_text_window(int top, int bottom)
{
	int rowBytes;
	int lines;
	int last;

	last = sTextHeight / 16 - 1;
	if (top < 0)
		top = 0;
	if (bottom > last)
		bottom = last;
	if (bottom < top)
		return;
	rowBytes = 16 * sTextWidth;
	lines = bottom - top;
	[sLock lock];
	if (lines > 0) {
		memmove(sTextVram + top * rowBytes,
			sTextVram + (top + 1) * rowBytes,
			(size_t)lines * rowBytes);
	}
	memset(sTextVram + bottom * rowBytes, 0, (size_t)rowBytes);
	InvalTextInRect(0, top * 16, sTextWidth, (bottom + 1) * 16);
	[sLock unlock];
}

void
XSTed_tputs(const char *message)
{
	int x, y;

	/* A leading CR LF moves to column 0 of the next line, inside C_WINDOW.
	   At the bottom row, that range scrolls up one line and the footer stays. */
	if (message[0] == '\r') {
		extern unsigned int iocs_console_ytop, iocs_current_console_height;
		int top, bottom, last;

		if (message[1] != 0)
			message += 2;
		else
			message += 1;
		top = (int)iocs_console_ytop;
		bottom = top + (int)iocs_current_console_height - 1;
		last = sTextHeight / 16 - 1;
		if (bottom > last)
			bottom = last;
		if (bottom < 0)
			bottom = 0;
		if (top > bottom)
			top = bottom;
		sCursorX = 0;
		if (sCursorY < bottom)
			sCursorY++;
		else {
			s_scroll_text_window(top, bottom);
			sCursorY = bottom;
		}
	}
	x = sCursorX * 8;
	y = sCursorY * 16;
	s_XSTed_tputs(message, &x, &y, sTextColor, 1);
	sCursorX = x / 8;
	sCursorY = y / 16;
}

void
XSTed_gputs(int x0, int y0, const char *message)
{
	s_XSTed_tputs(message, &x0, &y0, sGraphColor % 16, 0);
//	printf("XSTed_gputs %d %d %d %s\n", x0, y0, sGraphColor % 16, message);
}

void
XSTed_cls_eol(void)
{
	int16_t x0 = sCursorX * 8;
	int16_t y0 = sCursorY * 16;

	XSTed_tfill(3, x0, y0, sTextWidth - x0, 16, 0);
}

void
XSTed_cls_ed(void)
{
	int16_t x0 = sCursorX * 8;
	int16_t y0 = sCursorY * 16;

	XSTed_tfill(3, x0, y0, sTextWidth - x0, 16, 0);
	if (y0 + 16 < sTextHeight)
		XSTed_tfill(3, 0, y0 + 16, sTextWidth, sTextHeight - (y0 + 16), 0);
}

#pragma mark "=== Keyboard functions ==="

char font_name[1024];

int
XSTed_key_init(void)
{
	return 0;
}

/*  X680x0 INPOUT(ch)  */
/*  ch == 0xfe: check for key input and return immediately (if no key input, returns 0).  */
/*  ch == 0xff: key input */
/*  other ch: output one character (not used in STed2)  */
int
XSTed_keyin(int code)
{
	uint16_t ch = 0, ret;
	extern char fnc_key[12][6];
	int options;
	if (code == 0xfe)
		options = kGetKeyNoWaitOption | kGetKeyKeepInBufferOption;
	else if (code == 0xff)
		options = 0;
	else return 0;
	ch = [X68kView getKeyWithOptions:options];
	if (ch == 0)
		return 0;
	/*  One of the function keys is pressed  */
	switch ((ch >> 8) & 0xff) {
		case x68k_up:       ret = fnc_key[4][0]; break;
		case x68k_left:     ret = fnc_key[5][0]; break;
		case x68k_right:    ret = fnc_key[6][0]; break;
		case x68k_down:     ret = fnc_key[7][0]; break;
		case x68k_home:     ret = fnc_key[10][0]; break;
		case x68k_undo:     ret = fnc_key[11][0]; break;
		case x68k_del:      ret = fnc_key[3][0]; break;
		case x68k_ins:      ret = fnc_key[2][0]; break;
		case x68k_rolldown: ret = fnc_key[0][0]; break;
		case x68k_rollup:   ret = fnc_key[1][0]; break;
		case x68k_f1:       ret = fnc_func[0][0]; break;
		case x68k_f2:       ret = fnc_func[1][0]; break;
		case x68k_f3:       ret = fnc_func[2][0]; break;
		case x68k_f4:       ret = fnc_func[3][0]; break;
		case x68k_f5:       ret = fnc_func[4][0]; break;
		case x68k_f6:       ret = fnc_func[5][0]; break;
		case x68k_f7:       ret = fnc_func[6][0]; break;
		case x68k_f8:       ret = fnc_func[7][0]; break;
		case x68k_f9:       ret = fnc_func[8][0]; break;
		case x68k_f10:      ret = fnc_func[9][0]; break;
		default: ret = (ch & 0xff); break;
	}
	if (ch != 0 && ret == 0) {
		//  Key was pressed but no charater input
		//  If it is still in the buffer, it is removed
		if (options & kGetKeyKeepInBufferOption)
			ch = [X68kView getKeyWithOptions:0];
	}
	return ret;
}

/*  X680x0 B_KEYINP()  */
/*  Wait for key input.  */
/*  Return value: high byte = scan code, low byte = ASCII code  */
/*  For SHIFT, CTRL, OPT.1 and OPT.2 keys, key-up is also detected, with the
    scan code +0x80.  */
int
XSTed_keyinp(void)
{
	uint16_t ch;
	ch = [X68kView getKeyWithOptions:kGetKeySenseModifiersOption];
	//	printf("XSTed_keyinp\n");
	return ch;
}

/*  X680x0 B_SFTSNS()  */
/*  Examine the present state of modifier keys.  */
/*  bit 0-7: shift, control, opt1, opt2, kana, romaji, code-input, caps  */
int
XSTed_sftsns(void)
{
	unsigned int modifiers, suppress;
	uint16_t ch;
	[sLock lock];
	modifiers = sModifierFlags;
	suppress = sKeySenseSuppress;
	sKeySenseSuppress = 0;
	[sLock unlock];
	ch = 0;
	if (modifiers & NSEventModifierFlagShift)
		ch |= 1;
	if (modifiers & NSEventModifierFlagControl)
		ch |= 2;
	if (modifiers & NSEventModifierFlagOption)
		ch |= 4;
	if (modifiers & NSEventModifierFlagCapsLock)
		ch |= 8;
	ch &= (uint16_t)~suppress;
//	printf("XSTed_sftsns\n");
	return ch;
}

/*  X680x0 BITSNS(group)  */
/*  Examine which keys in the specified key group are pressed  */
/*  Complete implementation is very difficult, so it is partially implemented
    as follows; for shift, control, opt1 (option) and opt2 (caps lock) are
    implemented by use of the modifier flags. ROLL DOWN, ROLL UP, UNDO, UP and
    DOWN follow keyUp. Other keys are recognized as 'pressed' when the last
    return value of getKeyWithOptions matches that key.  */
int
XSTed_bitsns(int group)
{
	int ret = 0;
	unsigned int held;
	if (group == 14) {
		/*  shift, ctrl, opt1, opt2  */
		unsigned int mflags = sModifierFlags;
		if (mflags & NSEventModifierFlagShift)
			ret |= 1;
		if (mflags & NSEventModifierFlagControl)
			ret |= 2;
		if (mflags & NSEventModifierFlagOption)
			ret |= 4;
		if (mflags & NSEventModifierFlagCapsLock)
			ret |= 16;
		return ret;
	}
	[sLock lock];
	held = sHeldGroup7;
	[sLock unlock];
	ret = (sLastKeyDown >> 8) & 0xff;  //  Scan code
	if (ret / 8 == group)
		ret = (1 << (ret % 8));
	else ret = 0;
	if (group == 7) {
		/* Drop the sticky copy of the five live scans, then apply keyUp state. */
		ret &= ~(1 | 2 | 4 | 16 | 64);
		ret |= held;
	}
//	printf("XSTed_bitsns %d\n", group);
	return ret;
}

/*  X680x0 B_KEYSNS  */
/*  Peek only. Must not go through XSTed_keyin(0xfe): that path drops
    scan-only keys (KIGOU/TOROKU/XF, ascii 0) as "no character".
    Include modifier make/break (0x70-0x73 / 0xf0-0xf3); B_KEYINP already
    returns them, and EDIT uses 0x70/0xf0 to switch the function-key guide.  */
int
XSTed_keysns(void)
{
	uint16_t ch = [X68kView getKeyWithOptions:kGetKeyNoWaitOption | kGetKeyKeepInBufferOption | kGetKeySenseModifiersOption];
	return ch != 0;
}

void
XSTed_key_wait(void)
{
	while (XSTed_keyin(0xfe) == 0) {
		usleep(50000);
	}
}

void
XSTed_midi_wait(void)
{
	while (1) {
		if (XSTed_keysns())
			return;
		if (is_midi_in())
			return;
		usleep(10000);
	}
}

void
XSTed_ledmod(int code, int onoff)
{
//  Not implemented
//	printf("XSTed_ledmod %d %d\n", code, onoff);
}

#pragma mark "=== Mouse functions ==="

void
XSTed_ms_curof(void)
{
}

void
XSTed_ms_curon(void)
{
}

int
XSTed_ms_getdt(void)
{
	int buttons;

	if (sLock == nil)
		return 0;
	[sLock lock];
	buttons = sMsButtons;
	[sLock unlock];
	return buttons;
}

void
XSTed_ms_init(void)
{
	if (sLock == nil)
		return;
	[sLock lock];
	sMsButtons = 0;
	sMsMinX = 0;
	sMsMinY = 0;
	sMsMaxX = sTextWidth - 1;
	sMsMaxY = sTextHeight - 1;
	[sLock unlock];
}

int
XSTed_ms_limit( int xs, int ys, int xe, int ye )
{
	if (sLock == nil)
		return 0;
	[sLock lock];
	sMsMinX = xs;
	sMsMinY = ys;
	sMsMaxX = xe;
	sMsMaxY = ye;
	[sLock unlock];
	return 0;
}

int
XSTed_ms_pos(int *x, int *y)
{
	if (sLock == nil)
		return 0;
	[sLock lock];
	if (x != NULL)
		*x = sMsX;
	if (y != NULL)
		*y = sMsY;
	[sLock unlock];
	return 0;
}

static int
jis_to_sjis(int code)
{
	uint8_t c1, c2;
	c1 = (code >> 8) & 0xff;
	c2 = code & 0xff;
	if (c1 % 2) {
		c1 = ((c1 + 1) / 2) + 0x70;
		c2 = c2 + 0x1f;
	} else {
		c1 = (c1 / 2) + 0x70;
		c2 = c2 + 0x7d;
	}
	if (c1 >= 0xa0)
		c1 = c1 + 0x40;
	if (c2 >= 0x7f)
		c2 = c2 + 1;
	return ((int)c1 << 8) + c2;
	
}

int
XSTed_defchr(int type, int code, const void *buff)
{
	uint8_t c1, c2, n1, n2;
	int scode = code;
	if ((code >= 0x7621 && code <= 0x767e) || (code >= 0x7721 && code <= 0x777e)) {
		/*  Shift-jis 0xeb9f-0xebfc, 0xec40-0xec7e  */
		scode = jis_to_sjis(code);
	}
	c1 = (scode >> 8) & 0xff;
	c2 = scode & 0xff;
	if ((scode >= 0xeb9f && scode <= 0xebfc) || (scode >= 0xec40 && scode <= 0xec9e)) {
		/*  test  */
		int i, j, b, bb;
		uint8_t *p = (uint8_t *)buff;
		if (sKanjiData == NULL)
			return -1;
		n1 = c1 - (0xe0 - 31);
		if (c2 >= 0x40 && c2 <= 0x7e)
			n2 = c2 - 0x40;
		else n2 = c2 - (0x80 - 63);
	//	printf("defchr %04x\n", scode);
		for (i = 0; i < 32; i++) {
		//	if (i % 2 == 0)
		//		printf("%02d:", i);
			b = p[i];
			bb = 0;
			for (j = 0; j < 8; j++) {
				if (b & (0x80 >> j))
					bb |= (1 << j);
		//		printf("%c", (bb & (1 << j) ? '#' : '.'));
			}
		//	if (i % 2 == 1)
		//		printf("\n");
			sKanjiData[(n1 * 188 + n2) * 32 + i] = bb;
		}
	} else {
		printf("Non-definable character code %04X [%04X]\n", scode, code);
		return -1;
	}
	return 0;
}

/*  The following functions are called from the X68000 thread, which cannot
    access directly to the Application Kit. So it 'throws' message to the main
    thread, which is handled in -[X68kView timerHandler:].  */

static void
s_MacSelectFile(void **args)
{
	char *fna, *path;
	int is_save;
	const char *mes, *ext;
	id panel;
	int i;
	char *p, *pp;
	NSMutableArray *filetypes;
	NSString *title;
	
	fna = (char *)args[0];
	path = (char *)args[1];
	is_save = (int)(intptr_t)args[2];
	mes = (const char *)args[3];
	ext = (const char *)args[4];
	
	if (is_save)
		panel = [NSSavePanel savePanel];
	else
		panel = [NSOpenPanel openPanel];
	if (ext != NULL) {
		pp = p = strdup(ext);
		filetypes = [NSMutableArray array];
		for (i = 0; p[i] != 0; i++)
			p[i] = tolower(p[i]);
		while (p != NULL && *p != 0) {
			char *tok = strsep(&p, ":");
			if (*tok != 0)
				[filetypes addObject:[NSString stringWithUTF8String:tok]];
		}
		free(pp);
	} else filetypes = nil;
	[panel setAllowedFileTypes:filetypes];
	if (mes != NULL) {
		title = [NSString stringWithFormat:@"%s %s File", (is_save ? "Save" : "Load"), mes];
		[panel setTitle:title];
	}
	if ([panel runModal] == NSModalResponseOK) {
		NSString *pathstr = [[panel URL] path];
		NSString *lastComp = [pathstr lastPathComponent];
		NSString *dirPath = [pathstr stringByDeletingLastPathComponent];
		strncpy(fna, [lastComp fileSystemRepresentation], PATHLEN - 1);
		fna[PATHLEN - 1] = 0;
		strncpy(path, [dirPath fileSystemRepresentation], PATHLEN - 2);
		path[PATHLEN - 2] = 0;
		strcat(path, "/");
	} else {
		fna[0] = 0;
	}
	sRequestAppKitCall = 0;
}

static void
s_MacErrorBox(void **args)
{
	const char *mes = (const char *)args[0];
	int icon = (int)(intptr_t)args[1];
	NSAlert *alert = [NSAlert alertWithMessageText:nil defaultButton:nil alternateButton:nil otherButton:nil informativeTextWithFormat:@"%s", mes];
	[alert setAlertStyle:icon]; /* 0: warning, 1: information, 2: critical */
	[alert runModal];
	sRequestAppKitCall = 0;
}

static void
s_line_input(void **args)
{
	char *buf = (char *)args[0];
	int len = (int)(intptr_t)args[1];
	int flag = (int)(intptr_t)args[2];  /*  1 on first call, 0 otherwise  */
	int x, y, width;
	NSRect frame;
	NSTextField *tf;
	if (flag == 0)
		return;
	x = sCursorX * 8 - 2;
	y = sTextHeight - 18 - sCursorY * 16;
	width = (len & 0xff) * 8 + 4;
	frame = NSMakeRect(x, y, width, 24);
	tf = [[NSTextField alloc] initWithFrame:frame];
	[tf setBackgroundColor:[NSColor colorWithCalibratedRed:0.8 green:0.8 blue:0.8 alpha:1.0]];
	[tf setDrawsBackground:YES];
	[tf setDelegate:sMainView];
	[tf setFont:[NSFont userFixedPitchFontOfSize:16]];
	[tf setStringValue:[NSString stringWithCString:buf encoding:NSShiftJISStringEncoding]];
	[sMainView addSubview:tf];
	[[sMainView window] makeFirstResponder:tf];
	[tf selectText:nil];
	args[2] = (void *)0;  /*  Avoid duplicate call  */
}

void
MacSelectFile(char *fna, char *path, int is_save, const char *mes, const char *ext)
{
	void *args[] = {(void *)s_MacSelectFile, (void *)fna, (void *)path, (void *)(intptr_t)is_save, (void *)mes, (void *)ext};
	sRequestAppKitArgs = args;
	sRequestAppKitCall = 1;
	while (sRequestAppKitCall == 1) {
		usleep(100000);
	}
}

void
MacErrorBox(const char *mes, int icon)
{
	void *args[] = {(void *)s_MacErrorBox, (void *)mes, (void *)(intptr_t)icon};
	sRequestAppKitArgs = args;
	sRequestAppKitCall = 2;
	while (sRequestAppKitCall == 2) {
		usleep(100000);
	}
}

void
line_input(char *buf, int len)
{
	void *args[] = {(void *)s_line_input, (void *)buf, (void *)(intptr_t)len, (void *)(intptr_t)1};
	sRequestAppKitArgs = args;
	sRequestAppKitCall = 3;
	while (sRequestAppKitCall == 3) {
		usleep(100000);
	}
}

@end
