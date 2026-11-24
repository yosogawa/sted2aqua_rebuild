/* AppController.m */
/* The application controller object for sted2_aqua. */
/* Jun.01.2003 Toshi Nagata */
/* Copyright 2003 Toshi Nagata. All rights reserved. */

#import "AppController.h"
#import "X68kView.h"
// #import "X68kWindow.h"
#import "CoreMIDIRCPPlayer.h"
#import "MIDISettingsController.h"
#import "AboutPanelController.h"
#import "DLSSynthPanel.h"
#import "colorpanel.h"
#import "Sf2DefGenerator.h"
#import "KeyboardViewer.h"
#import "CommandReference.h"

#include <stdlib.h>
#include <CoreServices/CoreServices.h>
#include "rcp_transport.h"
#include "sted.h"

// prototype added
void  XSTed_init_window(void);
void  XSTed_start_main(int (*)(int, char **), int, char **);
extern int sted_saved_argc;
extern char **sted_saved_argv;
extern void sted_redraw_midi_in(void);
extern void sted_redraw_sync_mode(void);
extern void sted_redraw_thru_mode(void);
extern char thrumode;
extern int XSTed_save_thrumode(int on);
extern int XSTed_save_mac_font(const char *psName);
extern const char *XSTed_mac_font_name(void);
extern int ecount_sw; /* record.c: mirrors RcpClock source for SYNC display */

static NSString * const kMidiSyncExtKey = @"MidiSyncExt";

/* Non-zero when another STed2 is already running. open -n still creates this
   process; Launch Services does not. The file, if any, is forwarded before
   this process exits. */
static pid_t sYieldToPid;

static pid_t
sted_other_instance_pid(void)
{
	NSString *ident;
	pid_t selfPid;
	NSRunningApplication *app;

	ident = [[NSBundle mainBundle] bundleIdentifier];
	if (ident == nil)
		return 0;
	selfPid = [[NSRunningApplication currentApplication] processIdentifier];
	for (app in [NSRunningApplication runningApplicationsWithBundleIdentifier:ident]) {
		if ([app processIdentifier] != selfPid && ![app isTerminated])
			return [app processIdentifier];
	}
	return 0;
}

static void
sted_note_existing_instance(void)
{
	if (sYieldToPid == 0)
		sYieldToPid = sted_other_instance_pid();
}

static void
sted_forward_open_url(pid_t pid, NSURL *url)
{
	NSAppleEventDescriptor *target, *event, *list, *file;
	NSError *error = nil;

	if (pid == 0 || url == nil || ![url isFileURL])
		return;
	target = [NSAppleEventDescriptor descriptorWithProcessIdentifier:pid];
	event = [NSAppleEventDescriptor appleEventWithEventClass:kCoreEventClass
							 eventID:kAEOpenDocuments
						targetDescriptor:target
							returnID:kAutoGenerateReturnID
						   transactionID:kAnyTransactionID];
	list = [NSAppleEventDescriptor listDescriptor];
	file = [NSAppleEventDescriptor descriptorWithFileURL:url];
	if (target == nil || event == nil || list == nil || file == nil)
		return;
	[list insertDescriptor:file atIndex:1];
	[event setParamDescriptor:list forKeyword:keyDirectObject];
	[event sendEventWithOptions:NSAppleEventSendNoReply
			    timeout:kAEDefaultTimeout
			      error:&error];
}

static void
ApplyMidiSyncMode(int ext)
{
	rcp_clock_set_source(ext ? 1 : 0);
	ecount_sw = ext ? 1 : 0;
}

static void
ApplySyncModeMenuTitle(NSMenuItem *item)
{
	NSMenu *menu = [item menu];
	NSFont *font = [NSFont menuFontOfSize:0];
	NSDictionary *measure = @{ NSFontAttributeName: font };
	NSString *label = @"SYNC MODE";
	NSString *mode = rcp_clock_get_source() ? @"EXT" : @"INT";
	CGFloat tabX = [label sizeWithAttributes:measure].width + 28.0
		+ [mode sizeWithAttributes:measure].width;
	NSInteger i, n = (menu == nil) ? 0 : [menu numberOfItems];
	NSTextTab *tab;
	NSMutableParagraphStyle *style;
	NSAttributedString *title;

	for (i = 0; i < n; i++) {
		NSMenuItem *other = [menu itemAtIndex:i];
		NSString *text;
		CGFloat w;

		if (other == item || [other isSeparatorItem])
			continue;
		text = [other title];
		if ([text length] == 0)
			continue;
		w = [text sizeWithAttributes:measure].width;
		if (w > tabX)
			tabX = w;
	}

	/* Right tab: "SYNC MODE" stays put, INT/EXT sits on the title column's edge. */
	tab = [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight
					      location:tabX
					       options:@{}];
	style = [[NSMutableParagraphStyle alloc] init];
	[style setTabStops:[NSArray arrayWithObject:tab]];
	[style setLineBreakMode:NSLineBreakByClipping];
	[tab release];
	title = [[NSAttributedString alloc] initWithString:
		[NSString stringWithFormat:@"%@\t%@", label, mode]
		attributes:@{
			NSFontAttributeName: font,
			NSParagraphStyleAttributeName: style
		}];
	[style release];
	[item setTitle:[NSString stringWithFormat:@"%@ %@", label, mode]];
	[item setAttributedTitle:title];
	[title release];
}

@interface AppController (PrivateMenus)
- (void)localizeMenuItemWithAction:(SEL)action title:(NSString *)title;
- (void)localizeAboutMenuItem;
- (void)installViewMenu;
- (void)installPlaybackMenu;
- (IBAction)playbackCommand:(id)sender;
- (void)installKeyboardViewerMenu;
- (void)installCommandReferenceMenu;
- (void)installAudioMenu;
- (void)installGmSystemOnMenu;
- (void)installThruModeMenu;
- (void)installSyncModeMenu;
- (void)installMidiInputMonitorMenu;
- (IBAction)midiInputMonitor:(id)sender;
@end

@implementation AppController

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
	(void)notification;
	/* Nib load can still order the window front. Keep it hidden until
	   the main screen is drawn. */
	if (displayMode == DISPLAY_GUI && myWindow != nil) {
		[myWindow setAlphaValue:0];
		[myWindow orderOut:nil];
	}
	sted_note_existing_instance();
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
	(void)notification;
	/* EXIT calls this and then exit(), so it never reaches this method. */
	sted_release_notes();
}

static char sFinderArg[PATHLEN];
static char *sFinderArgv[48];

/* Copy of the stripped argv plus, when present, one launch path.
   sted_saved_argv itself is left unchanged. */
static char **
sted_argv_with_launch_file(int *argcIO, char **argv)
{
	int argc, i, positional;

	if (argcIO == NULL || argv == NULL)
		return argv;
	argc = *argcIO;
	if (sFinderArg[0] == '\0' || argc < 1 || argc + 1 >= 48)
		return argv;
	positional = 0;
	for (i = 1; i < argc; i++) {
		if (argv[i] != NULL && argv[i][0] != '\0' && argv[i][0] != '-' &&
		    strcmp(argv[i], "YES") != 0 && strcmp(argv[i], "NO") != 0 &&
		    strcmp(argv[i], "TRUE") != 0 && strcmp(argv[i], "FALSE") != 0)
			positional = 1;
	}
	if (positional)
		return argv;
	for (i = 0; i < argc; i++)
		sFinderArgv[i] = argv[i];
	sFinderArgv[argc] = sFinderArg;
	sFinderArgv[argc + 1] = NULL;
	*argcIO = argc + 1;
	return sFinderArgv;
}

- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{
	NSURL *u;
	const char *fs;

	(void)application;
	sted_note_existing_instance();
	if ([urls count] != 1)
		return;
	u = [urls objectAtIndex:0];
	if (u == nil || ![u isFileURL])
		return;
	if (sYieldToPid != 0) {
		sted_forward_open_url(sYieldToPid, u);
		return;
	}
	fs = [u fileSystemRepresentation];
	sted_finder_receive(fs);
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	extern int sted2_main(int, char**);
	static char *fallback[] = {"sted2", NULL};
	int argc = sted_saved_argc;
	char **argv = sted_saved_argv;
	X68kView *xview;

	if (sYieldToPid != 0) {
		NSRunningApplication *peer;

		peer = [NSRunningApplication runningApplicationWithProcessIdentifier:sYieldToPid];
		[peer activateWithOptions:NSApplicationActivateIgnoringOtherApps];
		dispatch_async(dispatch_get_main_queue(), ^{
			[NSApp terminate:nil];
		});
		return;
	}

	if (displayMode == DISPLAY_CONSOLE) {
		[myWindow orderOut:nil];
		[myWindow close];
		return;
	}

	sFinderArg[0] = '\0';
	sted_finder_take_launch_path(sFinderArg, sizeof sFinderArg);
	sted_finder_go_live();

	xview = (X68kView *)[myWindow contentView];

	[X68kView initializeX68k];
	[xview setNeedsDisplay:YES];
	XSTed_init_window();
	[self localizeAboutMenuItem];
	[self installViewMenu];
	[self installPlaybackMenu];
	[self installKeyboardViewerMenu];
	[self installCommandReferenceMenu];
	[self installGmSystemOnMenu];
	[self installThruModeMenu];
	[self installSyncModeMenu];
	[self installMidiInputMonitorMenu];
	[self installAudioMenu];
	{
		NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
		NSInteger percent = [def integerForKey:@"DisplayScalePercent"];
		[X68kView applyDisplayScalePercent:(int)percent toView:xview];
		ApplyMidiSyncMode([def boolForKey:kMidiSyncExtKey] ? 1 : 0);
	}

	if (argc < 1 || argv == NULL) {
		argc = 1;
		argv = fallback;
	}
	argv = sted_argv_with_launch_file(&argc, argv);
	XSTed_start_main(sted2_main, argc, argv);
}

- (void)startX68kThread: (id)argument
{
	int (*mainPtr)(int, char**);
	int argc;
	char **argv;
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	mainPtr = (int (*)(int, char **))[[argument objectAtIndex: 0] pointerValue];
	argc = [[argument objectAtIndex: 1] intValue];
	argv = (char **)[[argument objectAtIndex: 2] pointerValue];
	(*mainPtr)(argc, argv);
	[pool release];
}

- (IBAction)showAboutPanel:(id)sender
{
	(void)sender;
	[AboutPanelController showPanel];
}

- (IBAction)midiSettingsDialog:(id)sender
{
	(void)sender;
	[MIDISettingsController showPanel];
}

- (IBAction)dlsSynthDialog:(id)sender
{
	[DLSSynthPanel run];
}

- (IBAction)generateDefFromSf2:(id)sender
{
	(void)sender;
	[Sf2DefGenerator run];
}

- (IBAction)allSoundOff:(id)sender
{
	CoreMIDIAllSoundOff();
}

- (IBAction)sendResetSysEx:(id)sender
{
	(void)sender;
	if (rcp_transport_is_active())
		return;
	sted_reset_sysex_post();
}

- (void)installGmSystemOnMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *midiMenu = nil;
	NSMenuItem *item;
	NSInteger i, n, at;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	for (i = 0; i < n; i++) {
		NSMenuItem *top = [mainMenu itemAtIndex:i];
		if ([[top title] isEqualToString:@"MIDI"]) {
			midiMenu = [top submenu];
			break;
		}
	}
	if (midiMenu == nil)
		return;
	item = [[NSMenuItem alloc] initWithTitle:@"Send Reset SysEX"
					  action:@selector(sendResetSysEx:)
				   keyEquivalent:@"z"];
	/* Plain Z is a label. The emulator still receives the key. */
	[item setKeyEquivalentModifierMask:0];
	[item setTarget:self];
	[midiMenu setDelegate:self];
	at = [midiMenu indexOfItemWithTitle:@"All Sound Off"];
	if (at != NSNotFound)
		[midiMenu insertItem:item atIndex:at + 1];
	else
		[midiMenu addItem:item];
	[item release];
}

- (void)installAudioMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *audioMenu;
	NSMenuItem *top, *item;
	NSInteger i, n, insertAt;
	NSString *title;

	if (mainMenu == nil)
		return;
	title = NSLocalizedString(@"Audio", @"Audio menu");
	n = [mainMenu numberOfItems];
	insertAt = n;
	for (i = 0; i < n; i++) {
		if ([[[mainMenu itemAtIndex:i] title] isEqualToString:@"MIDI"]) {
			insertAt = i + 1;
			break;
		}
	}
	audioMenu = [[NSMenu alloc] initWithTitle:title];
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(
					@"SoundFont, FX, Mixer Setting", @"Audio menu")
					  action:@selector(dlsSynthDialog:)
				   keyEquivalent:@""];
	[item setTarget:self];
	[audioMenu addItem:item];
	[item release];
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(
					@"Generate DEF file from SF2", @"Audio menu")
					  action:@selector(generateDefFromSf2:)
				   keyEquivalent:@""];
	[item setTarget:self];
	[audioMenu addItem:item];
	[item release];
	top = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
	[top setSubmenu:audioMenu];
	[mainMenu insertItem:top atIndex:insertAt];
	[audioMenu release];
	[top release];
}

- (void)installThruModeMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *midiMenu = nil;
	NSMenuItem *item;
	NSInteger i, n;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	for (i = 0; i < n; i++) {
		NSMenuItem *top = [mainMenu itemAtIndex:i];
		if ([[top title] isEqualToString:@"MIDI"]) {
			midiMenu = [top submenu];
			break;
		}
	}
	if (midiMenu == nil)
		return;
	item = [[NSMenuItem alloc] initWithTitle:@"THRU MODE"
					  action:@selector(toggleThruMode:)
				   keyEquivalent:@""];
	[item setTarget:self];
	if ([midiMenu numberOfItems] > 0)
		[midiMenu insertItem:item atIndex:1];
	else
		[midiMenu addItem:item];
	[item release];
}

- (IBAction)toggleThruMode:(id)sender
{
	(void)sender;
	XSTed_save_thrumode(thrumode ? 0 : 1);
	sted_redraw_thru_mode();
}

- (void)installSyncModeMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *midiMenu = nil;
	NSMenuItem *item;
	NSInteger i, n, at;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	for (i = 0; i < n; i++) {
		NSMenuItem *top = [mainMenu itemAtIndex:i];
		if ([[top title] isEqualToString:@"MIDI"]) {
			midiMenu = [top submenu];
			break;
		}
	}
	if (midiMenu == nil)
		return;

	item = [[NSMenuItem alloc] initWithTitle:@"SYNC MODE"
					  action:@selector(setSyncMode:)
				   keyEquivalent:@""];
	[item setTarget:self];
	at = [midiMenu indexOfItemWithTitle:@"THRU MODE"];
	if (at != NSNotFound)
		[midiMenu insertItem:item atIndex:at + 1];
	else
		[midiMenu addItem:item];
	ApplySyncModeMenuTitle(item);
	[item release];
}

- (IBAction)setSyncMode:(id)sender
{
	int ext = rcp_clock_get_source() ? 0 : 1;
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];

	[def setBool:(ext != 0) forKey:kMidiSyncExtKey];
	ApplyMidiSyncMode(ext);
	sted_redraw_sync_mode();
	if ([sender isKindOfClass:[NSMenuItem class]])
		ApplySyncModeMenuTitle((NSMenuItem *)sender);
}

/* X68000 scan for R, from sKeyTable. inkey3 keeps the ASCII and reads shift
   from B_SFTSNS, which the synthetic shift bit satisfies. */
enum { kScanR = 0x14 };

- (void)installMidiInputMonitorMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *midiMenu = nil;
	NSMenuItem *item;
	NSInteger i, n, at;
	NSString *title;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	for (i = 0; i < n; i++) {
		NSMenuItem *top = [mainMenu itemAtIndex:i];
		if ([[top title] isEqualToString:@"MIDI"]) {
			midiMenu = [top submenu];
			break;
		}
	}
	if (midiMenu == nil)
		return;
	/* Shift+R is a label in the key column, same as ⌘M and Z. The menu
	   delegate returns NO from menuHasKeyEquivalent:, so the emulator
	   still receives the key. */
	title = NSLocalizedString(@"MIDI Input Monitor", @"MIDI menu");
	item = [[NSMenuItem alloc] initWithTitle:title
					  action:@selector(midiInputMonitor:)
				   keyEquivalent:@"r"];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagShift];
	[item setTarget:self];
	at = NSNotFound;
	n = [midiMenu numberOfItems];
	for (i = 0; i < n; i++) {
		if ([[midiMenu itemAtIndex:i] action] == @selector(setSyncMode:)) {
			at = i;
			break;
		}
	}
	if (at != NSNotFound)
		[midiMenu insertItem:item atIndex:at + 1];
	else
		[midiMenu addItem:item];
	[item release];
}

- (IBAction)midiInputMonitor:(id)sender
{
	(void)sender;
	/* Same path as SHIFT+R: com_sel reverses the RECORD row, then inpmon. */
	XSTed_post_command_key((kScanR << 8) | 'R', 1);
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
	NSInteger i, n = [menu numberOfItems];

	for (i = 0; i < n; i++) {
		NSMenuItem *item = [menu itemAtIndex:i];
		if ([item action] == @selector(setSyncMode:))
			ApplySyncModeMenuTitle(item);
	}
}

- (void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item
{
	NSInteger i, n = [menu numberOfItems];

	for (i = 0; i < n; i++) {
		NSMenuItem *other = [menu itemAtIndex:i];
		int on;

		if ([other action] != @selector(playbackCommand:))
			continue;
		on = [other isEnabled];
		ApplyPlaybackItemTitle(other, on, other == item && on);
	}
}

- (void)localizeMenuItemWithAction:(SEL)action title:(NSString *)title
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSInteger i, n, j, m;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	for (i = 0; i < n; i++) {
		NSMenu *sub = [[mainMenu itemAtIndex:i] submenu];
		if (sub == nil)
			continue;
		m = [sub numberOfItems];
		for (j = 0; j < m; j++) {
			NSMenuItem *item = [sub itemAtIndex:j];
			if ([item action] == action) {
				[item setTitle:title];
				return;
			}
		}
	}
}

- (void)localizeAboutMenuItem
{
	[self localizeMenuItemWithAction:@selector(showAboutPanel:)
		title:NSLocalizedString(@"About STed2", @"Application menu")];
	[self localizeMenuItemWithAction:@selector(midiSettingsDialog:)
		title:NSLocalizedString(@"MIDI Port Settings...", @"MIDI menu")];
	[self localizeMenuItemWithAction:@selector(showHelp:)
		title:NSLocalizedString(@"STed2 Help", @"Help menu")];
}

- (void)installViewMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *viewMenu;
	NSMenuItem *viewTop, *item;
	NSInteger i, n, insertAt;
	struct {
		const char *title;
		int tag;
	} scales[] = {
		{ "100%", 100 },
		{ "150%", 150 },
		{ "200%", 200 },
		{ "Max Height", 0 }, /* 0 = fit to visible-frame height */
	};

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	insertAt = n;
	for (i = 0; i < n; i++) {
		NSString *title = [[mainMenu itemAtIndex:i] title];
		if ([title isEqualToString:@"MIDI"] || [title isEqualToString:@"Help"]) {
			insertAt = i;
			break;
		}
	}
	viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
	for (i = 0; i < (int)(sizeof scales / sizeof scales[0]); i++) {
		item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:scales[i].title]
						  action:@selector(setDisplayScale:)
					   keyEquivalent:@""];
		[item setTag:scales[i].tag];
		[item setTarget:self];
		[viewMenu addItem:item];
		[item release];
	}
	[viewMenu addItem:[NSMenuItem separatorItem]];
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Fonts…", @"View menu")
					  action:@selector(showFontPanel:)
				   keyEquivalent:@"t"];
	[item setTarget:self];
	[viewMenu addItem:item];
	[item release];
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Colors…", @"View menu")
					  action:@selector(showColorPanel:)
				   keyEquivalent:@""];
	[item setTarget:self];
	[viewMenu addItem:item];
	[item release];
	viewTop = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
	[viewTop setSubmenu:viewMenu];
	[mainMenu insertItem:viewTop atIndex:insertAt];
	[viewMenu release];
	[viewTop release];
}

enum {
	kPbPlay = 1,
	kPbPause,
	kPbResume,
	kPbStop,
	kPbMeasure,
	kPbPointOne,
	kPbPointAll
};

/* X68000 scans. P is sKeyTable['P']. XF keys have no ASCII byte. */
enum {
	kScanP = 0x1a,
	kScanXF1 = 0x55,
	kScanXF2 = 0x56,
	kScanXF5 = 0x59
};

/* Right edge of every shortcut. XF keys are not AppKit key equivalents, so
   the column is a tab in the title. A real key equivalent would swallow P. */
static CGFloat sPlaybackKeyTab;

static NSColor *
PlaybackTitleColor(int enabled, int highlighted)
{
	if (highlighted)
		return [NSColor selectedMenuItemTextColor];
	if (!enabled)
		return [NSColor disabledControlTextColor];
	return [NSColor labelColor];
}

static void
ApplyPlaybackItemTitle(NSMenuItem *item, int enabled, int highlighted)
{
	NSArray *parts = [item representedObject];
	NSString *label, *keyName;
	NSFont *font;
	NSTextTab *tab;
	NSMutableParagraphStyle *style;
	NSAttributedString *title;

	if (![parts isKindOfClass:[NSArray class]] || [parts count] < 2)
		return;
	label = [parts objectAtIndex:0];
	keyName = [parts objectAtIndex:1];
	font = [NSFont menuFontOfSize:0];
	tab = [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight
					      location:sPlaybackKeyTab
					       options:@{}];
	style = [[NSMutableParagraphStyle alloc] init];
	[style setTabStops:[NSArray arrayWithObject:tab]];
	[style setLineBreakMode:NSLineBreakByClipping];
	[tab release];
	title = [[NSAttributedString alloc] initWithString:
		[NSString stringWithFormat:@"%@\t%@", label, keyName]
		attributes:@{
			NSFontAttributeName: font,
			NSForegroundColorAttributeName: PlaybackTitleColor(enabled, highlighted),
			NSParagraphStyleAttributeName: style
		}];
	[style release];
	[item setTitle:[NSString stringWithFormat:@"%@ %@", label, keyName]];
	[item setAttributedTitle:title];
	[title release];
}

static void
LayoutPlaybackMenu(NSMenu *menu)
{
	NSFont *font = [NSFont menuFontOfSize:0];
	NSDictionary *measure = @{ NSFontAttributeName: font };
	NSInteger i, n = [menu numberOfItems];
	CGFloat maxLabel = 0, maxKey = 0;

	for (i = 0; i < n; i++) {
		NSArray *parts = [[menu itemAtIndex:i] representedObject];
		CGFloat w;

		if (![parts isKindOfClass:[NSArray class]] || [parts count] < 2)
			continue;
		w = [[parts objectAtIndex:0] sizeWithAttributes:measure].width;
		if (w > maxLabel)
			maxLabel = w;
		w = [[parts objectAtIndex:1] sizeWithAttributes:measure].width;
		if (w > maxKey)
			maxKey = w;
	}
	sPlaybackKeyTab = maxLabel + 28.0 + maxKey;
	for (i = 0; i < n; i++) {
		NSMenuItem *item = [menu itemAtIndex:i];
		if ([item action] == @selector(playbackCommand:))
			ApplyPlaybackItemTitle(item, 1, 0);
	}
}

static NSMenuItem *
PlaybackItem(NSString *label, NSString *keyName, NSInteger tag, id target)
{
	NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label
						      action:@selector(playbackCommand:)
					       keyEquivalent:@""];
	[item setTag:tag];
	[item setTarget:target];
	[item setRepresentedObject:[NSArray arrayWithObjects:label, keyName, nil]];
	return item;
}

static int
playbackItemEnabled(NSInteger tag)
{
	int screen;

	if (!sted_cmdkey_waiting())
		return 0;
	screen = STedScreenCurrent();
	/* P on the edit screens is point play, the same key the editor reads. */
	if (tag == kPbPlay)
		return screen == STED_SCR_COMMAND
			|| screen == STED_SCR_TRACK_EDIT
			|| screen == STED_SCR_RHYTHM_EDIT;
	return screen == STED_SCR_COMMAND
		|| screen == STED_SCR_TRACK_EDIT
		|| screen == STED_SCR_RHYTHM_EDIT
		|| screen == STED_SCR_TRACK_SET;
}

- (void)installPlaybackMenu
{
	NSMenu *mainMenu = [NSApp mainMenu];
	NSMenu *menu;
	NSMenuItem *top, *item;
	NSInteger i, n, insertAt;

	if (mainMenu == nil)
		return;
	n = [mainMenu numberOfItems];
	insertAt = n;
	for (i = 0; i < n; i++) {
		if ([[[mainMenu itemAtIndex:i] title] isEqualToString:@"View"]) {
			insertAt = i;
			break;
		}
	}
	menu = [[NSMenu alloc] initWithTitle:NSLocalizedString(@"Playback", @"Playback menu")];
	item = PlaybackItem(NSLocalizedString(@"Play", @"Playback menu"), @"P", kPbPlay, self);
	[menu addItem:item];
	[item release];
	item = PlaybackItem(NSLocalizedString(@"Pause", @"Playback menu"), @"XF4", kPbPause, self);
	[menu addItem:item];
	[item release];
	item = PlaybackItem(NSLocalizedString(@"Resume", @"Playback menu"), @"XF3", kPbResume, self);
	[menu addItem:item];
	[item release];
	item = PlaybackItem(NSLocalizedString(@"Stop", @"Playback menu"), @"XF5", kPbStop, self);
	[menu addItem:item];
	[item release];
	[menu addItem:[NSMenuItem separatorItem]];
	item = PlaybackItem(NSLocalizedString(@"Measure Play", @"Playback menu"), @"XF1/M", kPbMeasure, self);
	[menu addItem:item];
	[item release];
	item = PlaybackItem(NSLocalizedString(@"Point Play (This Track)", @"Playback menu"),
			    @"SHIFT+XF1", kPbPointOne, self);
	[menu addItem:item];
	[item release];
	item = PlaybackItem(NSLocalizedString(@"Point Play (All Tracks)", @"Playback menu"),
			    @"XF2/P", kPbPointAll, self);
	[menu addItem:item];
	[item release];
	LayoutPlaybackMenu(menu);
	[menu setDelegate:self];
	top = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Playback", @"Playback menu")
					action:nil keyEquivalent:@""];
	[top setSubmenu:menu];
	[mainMenu insertItem:top atIndex:insertAt];
	[menu release];
	[top release];
}

- (IBAction)playbackCommand:(id)sender
{
	/* No key equivalent: the editor must still receive P / XF. The item
	   posts that same key, so the button reverse runs in the key handler.
	   XF3/XF4 are not queued; keyDown calls the transport directly. */
	switch ([sender tag]) {
	case kPbPlay:
		XSTed_post_command_key((kScanP << 8) | 'P', 0);
		break;
	case kPbPause:
		if (rcd != NULL)
			rcp_transport_stop();
		break;
	case kPbResume:
		if (rcd != NULL)
			rcp_transport_continue();
		break;
	case kPbStop:
		XSTed_post_command_key(kScanXF5 << 8, 0);
		break;
	case kPbMeasure:
		XSTed_post_command_key(kScanXF1 << 8, 0);
		break;
	case kPbPointOne:
		XSTed_post_command_key(kScanXF1 << 8, 1);
		break;
	case kPbPointAll:
		XSTed_post_command_key(kScanXF2 << 8, 0);
		break;
	default:
		break;
	}
}

- (void)installKeyboardViewerMenu
{
	NSMenu *help = [NSApp helpMenu];
	NSMenuItem *item;

	if (help == nil)
		return;
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Keyboard Viewer", @"Help menu")
					   action:@selector(showKeyboardViewer:)
				    keyEquivalent:@""];
	[item setTarget:self];
	[help addItem:item];
	[item release];
}

- (IBAction)showKeyboardViewer:(id)sender
{
	(void)sender;
	KeyboardViewerShow();
}

- (void)installCommandReferenceMenu
{
	NSMenu *help = [NSApp helpMenu];
	NSMenuItem *item;

	if (help == nil)
		return;
	item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Command Cheat Sheet", @"Help menu")
					   action:@selector(showCommandReference:)
				    keyEquivalent:@""];
	[item setTarget:self];
	[help addItem:item];
	[item release];
}

- (IBAction)showCommandReference:(id)sender
{
	(void)sender;
	CommandReferenceShow();
}

- (IBAction)showColorPanel:(id)sender
{
	(void)sender;
	ColorPanelShow();
}

- (IBAction)setDisplayScale:(id)sender
{
	[X68kView applyDisplayScalePercent:(int)[sender tag] toView:[myWindow contentView]];
}

static int
FontIsMonospace(NSFont *font)
{
	NSFontTraitMask traits;
	UniChar chars[2];
	CGGlyph glyphs[2];
	CGSize advances[2];
	CTFontRef ct;

	if (font == nil)
		return 0;
	if ([font isFixedPitch])
		return 1;
	traits = [[NSFontManager sharedFontManager] traitsOfFont:font];
	if ((traits & NSFixedPitchFontMask) != 0)
		return 1;
	/* Some bitmap/CJK fonts omit the fixed-pitch flag; compare advances. */
	ct = (CTFontRef)font;
	chars[0] = (UniChar)'i';
	chars[1] = (UniChar)'M';
	if (!CTFontGetGlyphsForCharacters(ct, chars, glyphs, 2))
		return 0;
	if (glyphs[0] == 0 || glyphs[1] == 0)
		return 0;
	CTFontGetAdvancesForGlyphs(ct, kCTFontOrientationHorizontal, glyphs, advances, 2);
	if (advances[0].width <= 0.0 || advances[1].width <= 0.0)
		return 0;
	return fabs(advances[0].width - advances[1].width) < 0.5;
}

/* System NSFontPanel cannot filter to monospace (willIncludeFont: dead since 10.3). */
- (NSArray *)buildMonospaceFontEntries
{
	NSFontManager *fm = [NSFontManager sharedFontManager];
	NSMutableDictionary *byName;
	NSMutableArray *entries;
	NSArray *names;
	NSUInteger i, n;
	NSString *ps;
	NSFont *font;
	NSString *title;

	byName = [NSMutableDictionary dictionary];
	names = [fm availableFontNamesWithTraits:NSFixedPitchFontMask];
	n = [names count];
	for (i = 0; i < n; i++) {
		ps = [names objectAtIndex:i];
		if ([byName objectForKey:ps] != nil)
			continue;
		font = [NSFont fontWithName:ps size:16.0];
		if (font == nil || !FontIsMonospace(font))
			continue;
		title = [font displayName];
		if (title == nil || [title length] == 0)
			title = ps;
		[byName setObject:[NSDictionary dictionaryWithObjectsAndKeys:
					   ps, @"psName",
					   title, @"title",
					   nil]
				  forKey:ps];
	}
	/* Also catch mono faces that omit NSFixedPitchFontMask (e.g. some KH Dot). */
	names = [fm availableFonts];
	n = [names count];
	for (i = 0; i < n; i++) {
		ps = [names objectAtIndex:i];
		if ([byName objectForKey:ps] != nil)
			continue;
		font = [NSFont fontWithName:ps size:16.0];
		if (font == nil || !FontIsMonospace(font))
			continue;
		title = [font displayName];
		if (title == nil || [title length] == 0)
			title = ps;
		[byName setObject:[NSDictionary dictionaryWithObjectsAndKeys:
					   ps, @"psName",
					   title, @"title",
					   nil]
				  forKey:ps];
	}
	entries = [NSMutableArray arrayWithArray:[byName allValues]];
	[entries sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
		return [[a objectForKey:@"title"] localizedCompare:[b objectForKey:@"title"]];
	}];
	return entries;
}

- (void)updateFontPickerPreview
{
	NSFont *font = fontPanelPendingFont;

	if (fontPickerPreview == nil)
		return;
	if (font == nil)
		font = [NSFont userFixedPitchFontOfSize:18.0];
	else
		font = [NSFont fontWithName:[font fontName] size:18.0];
	if (font == nil)
		font = [NSFont userFixedPitchFontOfSize:18.0];
	[fontPickerPreview setFont:font];
	[fontPickerPreview setStringValue:NSLocalizedString(@"Aaあア漢 0123",
							    @"Font picker preview sample")];
}

- (void)ensureFontPickerPanel
{
	NSView *content;
	NSScrollView *scroll;
	NSTableColumn *col;
	NSTextField *hint;
	NSButton *apply;

	if (fontPickerPanel != nil)
		return;

	fontPickerPanel = [[NSPanel alloc]
		initWithContentRect:NSMakeRect(0, 0, 360, 460)
			  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
				     | NSWindowStyleMaskResizable | NSWindowStyleMaskUtilityWindow)
			    backing:NSBackingStoreBuffered
			      defer:YES];
	[fontPickerPanel setTitle:NSLocalizedString(@"Fonts…", @"Monospace font picker title")];
	[fontPickerPanel setMinSize:NSMakeSize(280, 280)];
	[fontPickerPanel setReleasedWhenClosed:NO];
	[fontPickerPanel setHidesOnDeactivate:NO];
	[fontPickerPanel center];

	content = [fontPickerPanel contentView];
	scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 100, 336, 346)];
	[scroll setHasVerticalScroller:YES];
	[scroll setAutohidesScrollers:YES];
	[scroll setBorderType:NSBezelBorder];
	[scroll setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

	fontPickerTable = [[NSTableView alloc] initWithFrame:[scroll bounds]];
	col = [[NSTableColumn alloc] initWithIdentifier:@"title"];
	[[col headerCell] setStringValue:NSLocalizedString(@"Monospace fonts",
							   @"Font picker column header")];
	[col setWidth:300];
	[fontPickerTable addTableColumn:col];
	[col release];
	[fontPickerTable setHeaderView:nil];
	[fontPickerTable setRowHeight:22.0];
	[fontPickerTable setAllowsEmptySelection:NO];
	[fontPickerTable setAllowsMultipleSelection:NO];
	[fontPickerTable setDataSource:self];
	[fontPickerTable setDelegate:self];
	[fontPickerTable setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
	[scroll setDocumentView:fontPickerTable];
	[content addSubview:scroll];
	[scroll release];

	/* Preview sample drawn in the selected typeface. */
	fontPickerPreview = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 56, 336, 36)];
	[fontPickerPreview setEditable:NO];
	[fontPickerPreview setSelectable:NO];
	[fontPickerPreview setBezeled:YES];
	[fontPickerPreview setBezelStyle:NSTextFieldRoundedBezel];
	[fontPickerPreview setDrawsBackground:YES];
	[fontPickerPreview setAlignment:NSTextAlignmentCenter];
	[fontPickerPreview setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];
	[content addSubview:fontPickerPreview];

	hint = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 28, 240, 18)];
	[hint setStringValue:NSLocalizedString(@"Takes effect after restart",
					       @"Font panel accessory hint")];
	[hint setBezeled:NO];
	[hint setDrawsBackground:NO];
	[hint setEditable:NO];
	[hint setSelectable:NO];
	[hint setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
	[hint setTextColor:[NSColor secondaryLabelColor]];
	[hint setAutoresizingMask:(NSViewMaxYMargin | NSViewWidthSizable)];
	[content addSubview:hint];
	[hint release];

	apply = [[NSButton alloc] initWithFrame:NSMakeRect(260, 8, 88, 24)];
	[apply setTitle:NSLocalizedString(@"Apply", @"Font panel apply button")];
	[apply setBezelStyle:NSBezelStyleRounded];
	[apply setTarget:self];
	[apply setAction:@selector(applyFontPanelSelection:)];
	[apply setKeyEquivalent:@"\r"];
	[apply setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
	[content addSubview:apply];
	[apply release];
}

- (void)selectCurrentFontInPicker
{
	NSString *current;
	NSUInteger i, n;

	current = [NSString stringWithUTF8String:XSTed_mac_font_name()];
	n = [fontPickerEntries count];
	for (i = 0; i < n; i++) {
		NSDictionary *e = [fontPickerEntries objectAtIndex:i];
		if ([[e objectForKey:@"psName"] isEqualToString:current]) {
			[fontPickerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:i]
				     byExtendingSelection:NO];
			[fontPickerTable scrollRowToVisible:i];
			[fontPanelPendingFont release];
			fontPanelPendingFont = [[NSFont fontWithName:current size:16.0] retain];
			[self updateFontPickerPreview];
			return;
		}
	}
	if (n > 0) {
		[fontPickerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
				 byExtendingSelection:NO];
		[self tableViewSelectionDidChange:nil];
	}
}

- (IBAction)showFontPanel:(id)sender
{
	(void)sender;
	/* Prefer our monospace-only list over the system Font panel. */
	if ([NSFontPanel sharedFontPanelExists])
		[[NSFontPanel sharedFontPanel] orderOut:self];
	[self ensureFontPickerPanel];
	[fontPickerEntries release];
	fontPickerEntries = [[self buildMonospaceFontEntries] retain];
	[fontPickerTable reloadData];
	[self selectCurrentFontInPicker];
	[fontPickerPanel makeKeyAndOrderFront:self];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
	(void)tableView;
	return (NSInteger)[fontPickerEntries count];
}

- (id)tableView:(NSTableView *)tableView
objectValueForTableColumn:(NSTableColumn *)tableColumn
	    row:(NSInteger)row
{
	NSDictionary *entry;
	NSString *title, *ps;
	NSFont *font;
	NSDictionary *attrs;

	(void)tableView;
	(void)tableColumn;
	if (row < 0 || row >= (NSInteger)[fontPickerEntries count])
		return @"";
	entry = [fontPickerEntries objectAtIndex:(NSUInteger)row];
	title = [entry objectForKey:@"title"];
	ps = [entry objectForKey:@"psName"];
	font = [NSFont fontWithName:ps size:14.0];
	if (font == nil)
		return title;
	/* Show each row in that typeface so the list itself is a preview. */
	attrs = [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName];
	return [[[NSAttributedString alloc] initWithString:title attributes:attrs] autorelease];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
	NSInteger row;
	NSString *ps;
	NSFont *font;

	(void)notification;
	row = [fontPickerTable selectedRow];
	if (row < 0 || row >= (NSInteger)[fontPickerEntries count])
		return;
	ps = [[fontPickerEntries objectAtIndex:(NSUInteger)row] objectForKey:@"psName"];
	font = [NSFont fontWithName:ps size:16.0];
	if (font == nil)
		return;
	[fontPanelPendingFont release];
	fontPanelPendingFont = [font retain];
	[self updateFontPickerPreview];
}

- (IBAction)applyFontPanelSelection:(id)sender
{
	NSString *psName;
	NSAlert *alert;

	(void)sender;
	[self tableViewSelectionDidChange:nil];
	if (fontPanelPendingFont == nil) {
		NSBeep();
		return;
	}
	psName = [fontPanelPendingFont fontName];
	if (psName == nil || [psName length] == 0) {
		NSBeep();
		return;
	}
	if (XSTed_save_mac_font([psName UTF8String]) == 0) {
		[fontPickerPanel orderOut:self];
		return;
	}
	alert = [[NSAlert alloc] init];
	[alert setMessageText:NSLocalizedString(@"Could not save sted2.cnf",
						@"Font panel save failure title")];
	[alert setInformativeText:NSLocalizedString(@"Could not write the font setting.",
						    @"Font panel save failure detail")];
	[alert runModal];
	[alert release];
}

- (BOOL)menuHasKeyEquivalent:(NSMenu *)menu forEvent:(NSEvent *)event target:(id *)target action:(SEL *)action
{
	(void)menu;
	(void)event;
	(void)target;
	(void)action;
	return NO;
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
	if ([menuItem action] == @selector(setDisplayScale:)) {
		[menuItem setState:(([menuItem tag] == [X68kView displayScalePercent])
				    ? NSOnState : NSOffState)];
		return YES;
	}
	if ([menuItem action] == @selector(toggleThruMode:)) {
		[menuItem setState:(thrumode ? NSOnState : NSOffState)];
		return YES;
	}
	if ([menuItem action] == @selector(setSyncMode:)) {
		ApplySyncModeMenuTitle(menuItem);
		return YES;
	}
	if ([menuItem action] == @selector(sendResetSysEx:))
		return !rcp_transport_is_active();
	if ([menuItem action] == @selector(playbackCommand:)) {
		int on = playbackItemEnabled([menuItem tag]);
		ApplyPlaybackItemTitle(menuItem, on, 0);
		return on;
	}
	if ([menuItem action] == @selector(midiInputMonitor:))
		return sted_cmdkey_waiting() && STedScreenCurrent() == STED_SCR_COMMAND;
	return YES;
}

@end
