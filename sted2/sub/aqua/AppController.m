/* AppController.m */
/* The application controller object for sted2_aqua. */
/* Jun.01.2003 Toshi Nagata */
/* Copyright 2003 Toshi Nagata. All rights reserved. */

#import "AppController.h"
#import "X68kView.h"
// #import "X68kWindow.h"
#import "CoreMIDIRCPPlayer.h"
#import "MyDialogController.h"

#include <stdlib.h>

// prototype added
void  XSTed_init_window(void);
void  XSTed_start_main(int (*)(int, char **), int, char **);
extern int isconsole;
extern int sted_saved_argc;
extern char **sted_saved_argv;

@implementation AppController

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
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

	if ([MyDialogController runDialogWithNibName:@"MIDISettings"]) {
		CoreMIDISelectDeviceByNameForPort([[def valueForKey:@"midiout1"] UTF8String], 0);
		CoreMIDISelectDeviceByNameForPort([[def valueForKey:@"midiout2"] UTF8String], 1);
		if ([def valueForKey:@"midiin"] != nil) {
			CoreMIDISelectInputDeviceByName([[def valueForKey:@"midiin"] UTF8String]);
		}
	} else {
		[def setValue:outdevice1 forKey:@"midiout1"];
		[def setValue:outdevice2 forKey:@"midiout2"];
		[def setValue:indevice forKey:@"midiin"];
	}
}

- (IBAction)allSoundOff:(id)sender
{
	CoreMIDIAllSoundOff();
}

@end
