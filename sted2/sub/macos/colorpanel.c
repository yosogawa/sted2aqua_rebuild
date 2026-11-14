/* Palette panel.
 * The five memories live in Preferences. The 16 colors on screen are the
 * only thing written back, as the existing #palet0= ... #palet15= lines.
 * The factory colors are a fixed table in this file. */

#import "colorpanel.h"

#import <Cocoa/Cocoa.h>

#include <sys/stat.h>

#include "sted.h"
#include "x68funcs.h"

enum {
	kSlots = 5,
	kColors = 16,
	kPanelW = 392,
	kPanelH = 508,
	kRowH = 24,
	kRowStep = 28
};

/* The palette shipped with sted2.cnf. Not stored, and not rewritten as a theme. */
static const int kRom[kColors] = {
	0x0046, 0xe3b8, 0xe71c, 0xe738,
	0x5296, 0x210a, 0x2708, 0x3018,
	0x51a2, 0x8800, 0xf800, 0xf83e,
	0x8c40, 0xff9c, 0x73a0, 0xfffe
};

static NSString *const kRole[kColors] = {
	@"Back color",
	@"Cursor / special controller",
	@"Cursor / chord",
	@"Cursor / message",
	@"Frame 2",
	@"(unused)",
	@"RECORD MODE",
	@"Input guide",
	@"Text background",
	@"Expression",
	@"Modulation",
	@"Repeat mark",
	@"Volume",
	@"Pitch bend",
	@"Frame 1",
	@"Velocity"
};

/* The screen expands a level with (v ? (v + 1) * 8 - 1 : 0) and reads it
 * back with byte >> 3. The same cut keeps 248..255 on the top level.
 * A value of 32 does not fit in five bits and spills into the next channel. */
static int
FiveFromByte(int byte)
{
	int v;

	if (byte <= 0)
		return 0;
	if (byte > 255)
		byte = 255;
	v = byte >> 3;
	if (v > 31)
		v = 31;
	return v;
}

static int
ByteFromComponent(CGFloat f)
{
	if (f <= 0.0)
		return 0;
	if (f >= 1.0)
		return 255;
	return (int)(f * 255.0 + 0.5);
}

static int
X68kFromColor(NSColor *color)
{
	NSColor *c;
	int r, g, b;

	c = [color colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]];
	if (c == nil)
		return 0;
	r = FiveFromByte(ByteFromComponent([c redComponent]));
	g = FiveFromByte(ByteFromComponent([c greenComponent]));
	b = FiveFromByte(ByteFromComponent([c blueComponent]));
	return (g << 11) | (r << 6) | (b << 1);
}

static NSColor *
ColorFromX68k(int col)
{
	int r, g, b;
	int R, G, B;

	r = (col >> 6) & 31;
	g = (col >> 11) & 31;
	b = (col >> 1) & 31;
	R = r ? (r + 1) * 8 - 1 : 0;
	G = g ? (g + 1) * 8 - 1 : 0;
	B = b ? (b + 1) * 8 - 1 : 0;
	return [NSColor colorWithCalibratedRed:R / 255.0
					  green:G / 255.0
					   blue:B / 255.0
					  alpha:1.0];
}

static NSString *
SlotKey(int slot)
{
	return [NSString stringWithFormat:@"ColorPanelSlot%d", slot + 1];
}

static int
SlotOccupied(int slot)
{
	NSArray *a;

	a = [[NSUserDefaults standardUserDefaults] arrayForKey:SlotKey(slot)];
	return a != nil && [a count] == kColors;
}

static int
SlotLoad(int slot, int out[kColors])
{
	NSArray *a;
	int i;

	a = [[NSUserDefaults standardUserDefaults] arrayForKey:SlotKey(slot)];
	if (a == nil || [a count] != kColors)
		return 0;
	for (i = 0; i < kColors; i++)
		out[i] = [[a objectAtIndex:(NSUInteger)i] intValue] & 0xffff;
	return 1;
}

static void
SlotSave(int slot)
{
	NSMutableArray *a;
	int i;

	a = [NSMutableArray arrayWithCapacity:kColors];
	for (i = 0; i < kColors; i++)
		[a addObject:[NSNumber numberWithInt:palet_dat[i] & 0xffff]];
	[[NSUserDefaults standardUserDefaults] setObject:a forKey:SlotKey(slot)];
}

static int
CnfIsFile(const char *path)
{
	struct stat st;

	return path != NULL && path[0] != 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Same search as the MIDI settings: beside the executable, else Application Support. */
static int
ResolveCnf(char *path, size_t pathSize)
{
	char tmp[PATHLEN];
	const char *argv0;
	const char *base;

	argv0 = (sted_saved_argc > 0 && sted_saved_argv != NULL && sted_saved_argv[0] != NULL)
		? sted_saved_argv[0] : "sted2";
	strmfe(tmp, sizeof tmp, argv0, "cnf");
	if (CnfIsFile(tmp)) {
		strlcpy(path, tmp, pathSize);
		return 1;
	}
	if (s_sted_default_path[0] != 0) {
		base = strrchr(tmp, '/');
		snprintf(path, pathSize, "%s/%s", s_sted_default_path,
			 base != NULL ? base + 1 : tmp);
		if (CnfIsFile(path))
			return 1;
	}
	path[0] = 0;
	return 0;
}

/* Active "#paletN=" only. A leading '*' keeps the sample line. */
static int
PaletteIndex(NSString *line)
{
	NSUInteger i, n;
	unichar c;
	int index;

	n = [line length];
	i = 0;
	while (i < n && ([line characterAtIndex:i] == ' ' || [line characterAtIndex:i] == '\t'))
		i++;
	if (i >= n || [line characterAtIndex:i] == '*')
		return -1;
	if (n - i < 7)
		return -1;
	if ([[line substringWithRange:NSMakeRange(i, 6)] caseInsensitiveCompare:@"#palet"] != NSOrderedSame)
		return -1;
	i += 6;
	c = [line characterAtIndex:i];
	if (c < '0' || c > '9')
		return -1;
	index = 0;
	while (i < n) {
		c = [line characterAtIndex:i];
		if (c < '0' || c > '9')
			break;
		index = index * 10 + (int)(c - '0');
		i++;
	}
	while (i < n && ([line characterAtIndex:i] == ' ' || [line characterAtIndex:i] == '\t'))
		i++;
	if (i >= n || [line characterAtIndex:i] != '=')
		return -1;
	if (index < 0 || index >= kColors)
		return -1;
	return index;
}

static int
LineIsEnd(NSString *line)
{
	NSUInteger i, n;

	n = [line length];
	i = 0;
	while (i < n && ([line characterAtIndex:i] == ' ' || [line characterAtIndex:i] == '\t'))
		i++;
	if (i >= n || [line characterAtIndex:i] == '*')
		return 0;
	if (n - i < 4)
		return 0;
	if ([[line substringWithRange:NSMakeRange(i, 4)] caseInsensitiveCompare:@"#end"] != NSOrderedSame)
		return 0;
	i += 4;
	while (i < n && ([line characterAtIndex:i] == ' ' || [line characterAtIndex:i] == '\t'))
		i++;
	return i == n;
}

/* Swap the value token. The comment after it stays where it is. */
static NSString *
ReplaceValue(NSString *line, int value)
{
	NSRange eq;
	NSUInteger i, n, start;
	unichar c;
	NSString *neu;

	eq = [line rangeOfString:@"="];
	if (eq.location == NSNotFound)
		return line;
	n = [line length];
	i = NSMaxRange(eq);
	while (i < n && ([line characterAtIndex:i] == ' ' || [line characterAtIndex:i] == '\t'))
		i++;
	start = i;
	while (i < n) {
		c = [line characterAtIndex:i];
		if (c == ' ' || c == '\t')
			break;
		i++;
	}
	if (i == start)
		return line;
	neu = [NSString stringWithFormat:@"$%04x", value & 0xffff];
	return [[line substringToIndex:start] stringByAppendingString:
		[neu stringByAppendingString:[line substringFromIndex:i]]];
}

/* 0 on success. The screen is left as the caller set it. */
static int
WritePalette(void)
{
	char path[PATHLEN];
	NSString *filePath;
	NSError *err = nil;
	NSStringEncoding enc = 0;
	NSString *text, *out, *nl;
	NSMutableArray *lines;
	NSUInteger i, n;
	NSInteger endAt;
	int seen;
	int index;

	if (!ResolveCnf(path, sizeof path))
		return -1;
	filePath = [NSString stringWithUTF8String:path];
	if (filePath == nil)
		return -1;
	text = [NSString stringWithContentsOfFile:filePath usedEncoding:&enc error:&err];
	if (text == nil) {
		enc = NSUTF8StringEncoding;
		err = nil;
		text = [NSString stringWithContentsOfFile:filePath encoding:NSUTF8StringEncoding error:&err];
	}
	if (text == nil)
		return -1;
	if ([text rangeOfString:@"\r\n"].location != NSNotFound)
		nl = @"\r\n";
	else if ([text rangeOfString:@"\r"].location != NSNotFound)
		nl = @"\r";
	else
		nl = @"\n";
	text = [text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
	text = [text stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
	lines = [NSMutableArray arrayWithArray:[text componentsSeparatedByString:@"\n"]];
	n = [lines count];
	seen = 0;
	endAt = -1;
	for (i = 0; i < n; i++) {
		NSString *line = [lines objectAtIndex:i];
		if (endAt < 0 && LineIsEnd(line))
			endAt = (NSInteger)i;
		index = PaletteIndex(line);
		if (index < 0)
			continue;
		if (seen & (1 << index))
			continue;
		seen |= 1 << index;
		[lines replaceObjectAtIndex:i withObject:ReplaceValue(line, palet_dat[index])];
	}
	{
		NSUInteger at = endAt >= 0 ? (NSUInteger)endAt : [lines count];

		for (index = 0; index < kColors; index++) {
			NSString *added;

			if (seen & (1 << index))
				continue;
			added = [NSString stringWithFormat:@"#palet%d = $%04x", index, palet_dat[index] & 0xffff];
			[lines insertObject:added atIndex:at];
			at++;
		}
	}
	out = [lines componentsJoinedByString:nl];
	if (![out hasSuffix:nl])
		out = [out stringByAppendingString:nl];
	if (enc == 0)
		enc = NSUTF8StringEncoding;
	err = nil;
	if (![out writeToFile:filePath atomically:YES encoding:enc error:&err])
		return -1;
	return 0;
}

static void
ReportWriteFailure(void)
{
	NSAlert *alert;

	alert = [[NSAlert alloc] init];
	[alert setMessageText:NSLocalizedString(@"Could not save sted2.cnf",
						@"Palette save failure title")];
	[alert setInformativeText:NSLocalizedString(@"Could not write the palette.",
						    @"Palette save failure detail")];
	[alert runModal];
	[alert release];
}

@interface ColorPanelController : NSObject <NSWindowDelegate> {
	NSPanel *panel;
	NSColorWell *wells[kColors];
	NSButton *slots[kSlots];
	int selectedSlot;
	int dirty;
	int applying;
}
+ (void)show;
@end

@implementation ColorPanelController

+ (void)show
{
	static ColorPanelController *one = nil;

	if (one == nil)
		one = [[ColorPanelController alloc] init];
	[one open];
}

- (id)init
{
	self = [super init];
	if (self == nil)
		return nil;
	selectedSlot = -1;
	[self build];
	return self;
}

- (void)build
{
	NSView *content;
	NSFont *font;
	NSButton *button;
	int i;

	panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, kPanelW, kPanelH)
					   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskUtilityWindow
					     backing:NSBackingStoreBuffered
					       defer:NO];
	[panel setTitle:NSLocalizedString(@"Palette", @"color panel title")];
	[panel setFloatingPanel:YES];
	[panel setReleasedWhenClosed:NO];
	[panel setDelegate:self];
	[panel center];
	content = [panel contentView];
	font = [NSFont systemFontOfSize:12.0];

	for (i = 0; i < kColors; i++) {
		NSTextField *label;
		CGFloat y;

		y = 48.0 + (CGFloat)(kColors - 1 - i) * kRowStep;
		label = [[NSTextField alloc] initWithFrame:NSMakeRect(16, y, 300, kRowH)];
		[label setStringValue:[NSString stringWithFormat:@"%d  %@", i,
				       NSLocalizedString(kRole[i], @"palette role")]];
		[label setFont:font];
		[label setBezeled:NO];
		[label setDrawsBackground:NO];
		[label setEditable:NO];
		[label setSelectable:NO];
		[content addSubview:label];
		[label release];

		wells[i] = [[NSColorWell alloc] initWithFrame:NSMakeRect(324, y, 52, kRowH)];
		[wells[i] setTag:i];
		[wells[i] setContinuous:YES];
		[wells[i] setTarget:self];
		[wells[i] setAction:@selector(wellChanged:)];
		[content addSubview:wells[i]];
	}

	button = [[NSButton alloc] initWithFrame:NSMakeRect(16, 12, 92, 24)];
	[button setTitle:NSLocalizedString(@"Palette Default", @"color panel")];
	[button setBezelStyle:NSBezelStyleRounded];
	[button setTarget:self];
	[button setAction:@selector(defaultClicked:)];
	[content addSubview:button];
	[button release];

	for (i = 0; i < kSlots; i++) {
		slots[i] = [[NSButton alloc] initWithFrame:NSMakeRect(116 + i * 36, 12, 32, 24)];
		[slots[i] setTag:i];
		[slots[i] setButtonType:NSButtonTypePushOnPushOff];
		[slots[i] setBezelStyle:NSBezelStyleRounded];
		[slots[i] setTarget:self];
		[slots[i] setAction:@selector(slotClicked:)];
		[content addSubview:slots[i]];
	}

	button = [[NSButton alloc] initWithFrame:NSMakeRect(300, 12, 76, 24)];
	[button setTitle:NSLocalizedString(@"Palette Store", @"color panel")];
	[button setBezelStyle:NSBezelStyleRounded];
	[button setTarget:self];
	[button setAction:@selector(storeClicked:)];
	[content addSubview:button];
	[button release];

	[[NSNotificationCenter defaultCenter] addObserver:self
						 selector:@selector(colorPanelChanged:)
						     name:NSColorPanelColorDidChangeNotification
						   object:nil];
}

- (void)applyWell:(NSColorWell *)well
{
	int i;

	if (applying || well == nil)
		return;
	i = (int)[well tag];
	if (i < 0 || i >= kColors)
		return;
	palet_dat[i] = X68kFromColor([well color]);
	dpl(1);
	tpl(1);
	dirty = 1;
}

- (void)refreshSlots
{
	int i;

	for (i = 0; i < kSlots; i++) {
		NSString *title = [NSString stringWithFormat:@"%d", i + 1];
		if (SlotOccupied(i))
			title = [title stringByAppendingString:@"*"];
		[slots[i] setTitle:title];
		[slots[i] setState:(i == selectedSlot ? NSControlStateValueOn : NSControlStateValueOff)];
	}
}

- (void)pullWells
{
	int i;

	applying = 1;
	for (i = 0; i < kColors; i++)
		[wells[i] setColor:ColorFromX68k(palet_dat[i])];
	applying = 0;
}

- (void)showColors:(const int *)colors write:(int)write
{
	int i;

	for (i = 0; i < kColors; i++)
		palet_dat[i] = colors[i] & 0xffff;
	dpl(1);
	tpl(1);
	[self pullWells];
	if (!write) {
		dirty = 1;
		return;
	}
	if (WritePalette() == 0)
		dirty = 0;
	else {
		dirty = 1;
		ReportWriteFailure();
	}
}

- (void)open
{
	[self pullWells];
	[self refreshSlots];
	[panel makeKeyAndOrderFront:nil];
}

- (void)wellChanged:(id)sender
{
	[self applyWell:sender];
}

/* The well action can miss the last position of the slider. */
- (void)colorPanelChanged:(NSNotification *)note
{
	int i;

	(void)note;
	for (i = 0; i < kColors; i++) {
		if ([wells[i] isActive]) {
			[self applyWell:wells[i]];
			return;
		}
	}
}

- (void)defaultClicked:(id)sender
{
	(void)sender;
	[self showColors:kRom write:1];
}

- (void)slotClicked:(id)sender
{
	int n;
	int colors[kColors];
	int i;

	n = (int)[sender tag];
	if (n < 0 || n >= kSlots)
		return;
	selectedSlot = n;
	for (i = 0; i < kSlots; i++)
		[slots[i] setState:(i == n ? NSControlStateValueOn : NSControlStateValueOff)];
	if (!SlotLoad(n, colors))
		return;
	[self showColors:colors write:1];
}

- (void)storeClicked:(id)sender
{
	(void)sender;
	if (selectedSlot < 0 || selectedSlot >= kSlots) {
		NSBeep();
		return;
	}
	SlotSave(selectedSlot);
	[self refreshSlots];
}

- (void)windowWillClose:(NSNotification *)note
{
	(void)note;
	if (!dirty)
		return;
	if (WritePalette() == 0)
		dirty = 0;
	else
		ReportWriteFailure();
}

@end

void
ColorPanelShow(void)
{
	[ColorPanelController show];
}
