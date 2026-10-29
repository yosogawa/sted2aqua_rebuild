/* AppController.m */
/* The application controller object for sted2_aqua. */
/* Jun.01.2003 Toshi Nagata */
/* Copyright 2003 Toshi Nagata. All rights reserved. */

#import "AppController.h"
#import "X68kView.h"
// #import "X68kWindow.h"
#import "CoreMIDIRCPPlayer.h"
#import "MyDialogController.h"
#import "AboutPanelController.h"
#import "DLSSynthPanel.h"

#include <stdlib.h>
#include "rcp_transport.h"

// prototype added
void  XSTed_init_window(void);
void  XSTed_start_main(int (*)(int, char **), int, char **);
extern int isconsole;
extern int sted_saved_argc;
extern char **sted_saved_argv;
extern void sted_redraw_midi_in(void);
extern void sted_redraw_sync_mode(void);
extern void sted_redraw_thru_mode(void);
extern char thrumode;
extern int XSTed_save_thrumode(int on);
extern int ecount_sw; /* record.c: mirrors RcpClock source for SYNC display */

static NSString * const kMidiSyncExtKey = @"MidiSyncExt";

static void
ApplyMidiSyncMode(int ext)
{
	rcp_clock_set_source(ext ? 1 : 0);
	ecount_sw = ext ? 1 : 0;
}

@interface AppController (PrivateMenus)
- (void)installViewMenu;
- (void)installDLSSynthMenu;
- (void)installThruModeMenu;
- (void)installSyncModeMenu;
@end

@implementation AppController

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
	(void)notification;
	/* Nib load can still order the window front. Keep it hidden until
	   the main screen is drawn. */
	if (!isconsole && myWindow != nil) {
		[myWindow setAlphaValue:0];
		[myWindow orderOut:nil];
	}
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	extern int sted2_main(int, char**);
	static char *fallback[] = {"sted2", NULL};
	int argc = sted_saved_argc;
	char **argv = sted_saved_argv;
	X68kView *xview;

	if (isconsole) {
		[myWindow orderOut:nil];
		[myWindow close];
		return;
	}

	xview = (X68kView *)[myWindow contentView];

	[X68kView initializeX68k];
	[xview setNeedsDisplay:YES];
	XSTed_init_window();
	[self installViewMenu];
	[self installDLSSynthMenu];
	[self installThruModeMenu];
	[self installSyncModeMenu];
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
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	NSMutableArray *devices = [NSMutableArray array];
	NSMutableArray *inDevices = [NSMutableArray array];
	int i;
	NSString *devName, *outdevice1, *outdevice2, *indevice;

	for (i = 0; (devName = CoreMIDIDeviceNameAtIndex(i)) != nil; i++) {
		[devices addObject:devName];
	}
	[def setValue:devices forKey:@"devicelist"];

	for (i = 0; (devName = CoreMIDIInDeviceNameAtIndex(i)) != nil; i++) {
		[inDevices addObject:devName];
	}
	[def setValue:inDevices forKey:@"indevicelist"];

	outdevice1 = [def valueForKey:@"midiout1"];
	outdevice2 = [def valueForKey:@"midiout2"];
	indevice   = [def valueForKey:@"midiin"];
	if (outdevice1 != nil)
		[def setValue:[NSString stringWithUTF8String:CoreMIDICanonicalOutputName([outdevice1 UTF8String])] forKey:@"midiout1"];
	if (outdevice2 != nil)
		[def setValue:[NSString stringWithUTF8String:CoreMIDICanonicalOutputName([outdevice2 UTF8String])] forKey:@"midiout2"];

	if ([MyDialogController runDialogWithNibName:@"MIDISettings"]) {
		NSString *chosen;
		if (CoreMIDISelectDeviceByNameForPort([[def valueForKey:@"midiout1"] UTF8String], 0) < 0)
			CoreMIDISelectDeviceByNameForPort("Apple DLS Synth", 0);
		if (CoreMIDISelectDeviceByNameForPort([[def valueForKey:@"midiout2"] UTF8String], 1) < 0)
			CoreMIDISelectDeviceByNameForPort("Apple DLS Synth 2", 1);
		chosen = CoreMIDISelectedOutputName(0);
		if (chosen != nil)
			[def setValue:chosen forKey:@"midiout1"];
		chosen = CoreMIDISelectedOutputName(1);
		if (chosen != nil)
			[def setValue:chosen forKey:@"midiout2"];
		if ([def valueForKey:@"midiin"] != nil) {
			CoreMIDISelectInputDeviceByName([[def valueForKey:@"midiin"] UTF8String]);
		}
		sted_redraw_midi_in();
	} else {
		[def setValue:outdevice1 forKey:@"midiout1"];
		[def setValue:outdevice2 forKey:@"midiout2"];
		[def setValue:indevice forKey:@"midiin"];
	}
}

- (IBAction)dlsSynthDialog:(id)sender
{
	[DLSSynthPanel run];
}

- (IBAction)allSoundOff:(id)sender
{
	CoreMIDIAllSoundOff();
}

- (void)installDLSSynthMenu
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
	item = [[NSMenuItem alloc] initWithTitle:@"DLS Synth..."
					  action:@selector(dlsSynthDialog:)
				   keyEquivalent:@""];
	[item setTarget:self];
	if ([midiMenu numberOfItems] > 0)
		[midiMenu insertItem:item atIndex:1];
	else
		[midiMenu addItem:item];
	[item release];
}

- (void)installThruModeMenu
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
	item = [[NSMenuItem alloc] initWithTitle:@"THRU MODE"
					  action:@selector(toggleThruMode:)
				   keyEquivalent:@""];
	[item setTarget:self];
	at = [midiMenu indexOfItemWithTitle:@"DLS Synth..."];
	if (at != NSNotFound)
		[midiMenu insertItem:item atIndex:at + 1];
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
	NSMenu *syncMenu;
	NSMenuItem *syncTop, *item;
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

	syncMenu = [[NSMenu alloc] initWithTitle:@"Sync Mode"];
	item = [[NSMenuItem alloc] initWithTitle:@"INT"
					  action:@selector(setSyncMode:)
				   keyEquivalent:@""];
	[item setTag:0];
	[item setTarget:self];
	[syncMenu addItem:item];
	[item release];
	item = [[NSMenuItem alloc] initWithTitle:@"EXT"
					  action:@selector(setSyncMode:)
				   keyEquivalent:@""];
	[item setTag:1];
	[item setTarget:self];
	[syncMenu addItem:item];
	[item release];

	syncTop = [[NSMenuItem alloc] initWithTitle:@"Sync Mode"
					     action:nil
				      keyEquivalent:@""];
	[syncTop setSubmenu:syncMenu];
	at = [midiMenu indexOfItemWithTitle:@"THRU MODE"];
	if (at != NSNotFound)
		[midiMenu insertItem:syncTop atIndex:at + 1];
	else
		[midiMenu addItem:syncTop];
	[syncMenu release];
	[syncTop release];
}

- (IBAction)setSyncMode:(id)sender
{
	int ext = ((NSMenuItem *)sender).tag ? 1 : 0;
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];

	[def setBool:(ext != 0) forKey:kMidiSyncExtKey];
	ApplyMidiSyncMode(ext);
	sted_redraw_sync_mode();
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
	viewTop = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
	[viewTop setSubmenu:viewMenu];
	[mainMenu insertItem:viewTop atIndex:insertAt];
	[viewMenu release];
	[viewTop release];
}

- (IBAction)setDisplayScale:(id)sender
{
	[X68kView applyDisplayScalePercent:(int)[sender tag] toView:[myWindow contentView]];
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
		int ext = rcp_clock_get_source();
		[menuItem setState:(([menuItem tag] != 0) == (ext != 0))
				    ? NSOnState : NSOffState];
		return YES;
	}
	return YES;
}

@end
