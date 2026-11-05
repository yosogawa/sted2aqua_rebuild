/* MIDISettingsController.m
 * The panel menus are filled from CoreMIDI. sted2.cnf is the saved record.
 */

#import "MIDISettingsController.h"
#import "CoreMIDIRCPPlayer.h"

#include <string.h>

extern int XSTed_save_midi_port_settings(const char *inName, const char *out1, const char *out2, int clock1, int clock2);
extern void XSTed_apply_midi_port_settings(void);

extern void sted_redraw_midi_in(void);

NSString * const MIDIPortSettingsChangedNotification = @"MIDIPortSettingsChanged";

static MIDISettingsController *sSharedPanel;
static int sSuspend;

static char sInName[256];
static char sOutName[2][256];
static int sOutClock[2];

const char *
MIDISettingsSavedInputName(void)
{
	return sInName;
}

static int
MidiOutIsInternalSynth(NSString *name)
{
	if (name == nil || [name length] == 0)
		return 1;
	if ([name isEqualToString:@"Apple DLS Synth"])
		return 1;
	if ([name isEqualToString:@"Apple DLS Synth 2"])
		return 1;
	if ([name isEqualToString:@"AMY Synthesizer"])
		return 1;
	return 0;
}

@implementation MIDISettingsController

+ (MIDISettingsController *)sharedController
{
	if (sSharedPanel == nil)
		sSharedPanel = [[MIDISettingsController alloc] initWithWindowNibName:@"MIDISettings"];
	return sSharedPanel;
}

+ (void)showPanel
{
	MIDISettingsController *panel = [MIDISettingsController sharedController];
	[panel showWindow:nil];
	[panel refreshDeviceLists];
	[[panel window] makeKeyAndOrderFront:nil];
}

+ (void)noteInputDeviceName:(const char *)name
{
	if (name == NULL)
		name = "";
	strlcpy(sInName, name, sizeof sInName);
	if (sInName[0] != 0)
		CoreMIDISelectInputDeviceByName(sInName);
}

+ (void)noteOutputDeviceName:(const char *)name port:(int)port
{
	const char *canon;

	if (port < 0 || port > 1)
		return;
	if (name == NULL)
		name = "";
	canon = CoreMIDICanonicalOutputName(name);
	if (canon == NULL)
		canon = name;
	strlcpy(sOutName[port], canon, sizeof sOutName[port]);
	if (sOutName[port][0] == 0)
		return;
	if (CoreMIDISelectDeviceByNameForPort(sOutName[port], port) < 0) {
		const char *fallback = (port == 0) ? "Apple DLS Synth" : "Apple DLS Synth 2";
		CoreMIDISelectDeviceByNameForPort(fallback, port);
	}
}

+ (void)noteOutputClock:(int)on port:(int)port
{
	if (port < 0 || port > 1)
		return;
	sOutClock[port] = on ? 1 : 0;
}

+ (NSString *)outputNameForPort:(int)port
{
	if (port < 0 || port > 1 || sOutName[port][0] == 0)
		return nil;
	return [NSString stringWithUTF8String:sOutName[port]];
}

- (NSArray *)deviceList { return deviceList; }
- (void)setDeviceList:(NSArray *)value
{
	if (deviceList == value)
		return;
	[deviceList release];
	deviceList = [value retain];
}

- (NSArray *)inDeviceList { return inDeviceList; }
- (void)setInDeviceList:(NSArray *)value
{
	if (inDeviceList == value)
		return;
	[inDeviceList release];
	inDeviceList = [value retain];
}

- (NSString *)midiIn { return midiIn; }
- (NSString *)midiOut1 { return midiOut1; }
- (NSString *)midiOut2 { return midiOut2; }
- (BOOL)midiOut1Clock { return midiOut1Clock; }
- (BOOL)midiOut2Clock { return midiOut2Clock; }

- (void)rememberInput
{
	const char *s = midiIn != nil ? [midiIn UTF8String] : "";
	if (s == NULL)
		s = "";
	strlcpy(sInName, s, sizeof sInName);
}

- (void)rememberOutput:(int)port
{
	NSString *name = (port == 0) ? midiOut1 : midiOut2;
	const char *s = name != nil ? [name UTF8String] : "";
	if (s == NULL)
		s = "";
	strlcpy(sOutName[port], s, sizeof sOutName[port]);
}

- (void)publishChange
{
	if (XSTed_save_midi_port_settings(sInName, sOutName[0], sOutName[1],
					  sOutClock[0], sOutClock[1]) != 0)
		NSLog(@"Could not update sted2.cnf MIDI port settings");
	[[NSNotificationCenter defaultCenter]
		postNotificationName:MIDIPortSettingsChangedNotification
			      object:self];
}

- (void)applyOutputPort:(int)port
{
	NSString *name;
	NSString *chosen;
	const char *fallback;
	int ok;

	name = (port == 0) ? midiOut1 : midiOut2;
	if (name == nil || [name length] == 0)
		return;
	ok = CoreMIDISelectDeviceByNameForPort([name UTF8String], port);
	if (ok < 0) {
		fallback = (port == 0) ? "Apple DLS Synth" : "Apple DLS Synth 2";
		ok = CoreMIDISelectDeviceByNameForPort(fallback, port);
	}
	if (ok <= 0)
		return;
	chosen = CoreMIDISelectedOutputName(port);
	if (chosen == nil || [chosen isEqualToString:name])
		return;
	sSuspend++;
	if (port == 0)
		[self setMidiOut1:chosen];
	else
		[self setMidiOut2:chosen];
	sSuspend--;
}

- (void)clearClockForPort:(int)port
{
	NSString *name = (port == 0) ? midiOut1 : midiOut2;
	if (!MidiOutIsInternalSynth(name))
		return;
	sSuspend++;
	if (port == 0)
		[self setMidiOut1Clock:NO];
	else
		[self setMidiOut2Clock:NO];
	sSuspend--;
}

- (void)setMidiIn:(NSString *)value
{
	if (value != nil && [value length] == 0)
		value = nil;
	if (midiIn == value || (value != nil && [midiIn isEqualToString:value]))
		return;
	/* A popup with no matching item writes nil. That is not a user clear. */
	if (!sSuspend && value == nil)
		return;
	[midiIn release];
	midiIn = [value copy];
	if (sSuspend)
		return;
	CoreMIDISelectInputDeviceByName(midiIn != nil ? [midiIn UTF8String] : "");
	sted_redraw_midi_in();
	[self rememberInput];
	[self publishChange];
}

- (void)setMidiOut1:(NSString *)value
{
	if (value != nil && [value length] == 0)
		value = nil;
	if (midiOut1 == value || (value != nil && [midiOut1 isEqualToString:value]))
		return;
	if (!sSuspend && value == nil)
		return;
	[midiOut1 release];
	midiOut1 = [value copy];
	if (sSuspend)
		return;
	[self applyOutputPort:0];
	[self rememberOutput:0];
	[self clearClockForPort:0];
	[self publishChange];
}

- (void)setMidiOut2:(NSString *)value
{
	if (value != nil && [value length] == 0)
		value = nil;
	if (midiOut2 == value || (value != nil && [midiOut2 isEqualToString:value]))
		return;
	if (!sSuspend && value == nil)
		return;
	[midiOut2 release];
	midiOut2 = [value copy];
	if (sSuspend)
		return;
	[self applyOutputPort:1];
	[self rememberOutput:1];
	[self clearClockForPort:1];
	[self publishChange];
}

- (void)setMidiOut1Clock:(BOOL)on
{
	on = on ? YES : NO;
	if (midiOut1Clock == on)
		return;
	midiOut1Clock = on;
	sOutClock[0] = on ? 1 : 0;
	if (sSuspend)
		return;
	if (on && MidiOutIsInternalSynth(midiOut1)) {
		sSuspend++;
		[self setMidiOut1Clock:NO];
		sSuspend--;
		return;
	}
	[self publishChange];
}

- (void)setMidiOut2Clock:(BOOL)on
{
	on = on ? YES : NO;
	if (midiOut2Clock == on)
		return;
	midiOut2Clock = on;
	sOutClock[1] = on ? 1 : 0;
	if (sSuspend)
		return;
	if (on && MidiOutIsInternalSynth(midiOut2)) {
		sSuspend++;
		[self setMidiOut2Clock:NO];
		sSuspend--;
		return;
	}
	[self publishChange];
}

- (NSString *)listedName:(NSString *)name in:(NSArray *)list
{
	NSEnumerator *items;
	NSString *item;

	if (name == nil || list == nil)
		return nil;
	items = [list objectEnumerator];
	while ((item = [items nextObject]) != nil) {
		if ([item caseInsensitiveCompare:name] == NSOrderedSame)
			return item;
	}
	return name;
}

- (void)loadFromSaved
{
	NSString *inName = sInName[0] != 0 ? [NSString stringWithUTF8String:sInName] : nil;
	NSString *out1 = sOutName[0][0] != 0 ? [NSString stringWithUTF8String:sOutName[0]] : nil;
	NSString *out2 = sOutName[1][0] != 0 ? [NSString stringWithUTF8String:sOutName[1]] : nil;

	inName = [self listedName:inName in:inDeviceList];
	out1 = [self listedName:out1 in:deviceList];
	out2 = [self listedName:out2 in:deviceList];
	sSuspend++;
	[self setMidiIn:inName];
	[self setMidiOut1:out1];
	[self setMidiOut2:out2];
	[self setMidiOut1Clock:sOutClock[0] ? YES : NO];
	[self setMidiOut2Clock:sOutClock[1] ? YES : NO];
	sSuspend--;
}

- (void)refreshDeviceLists
{
	NSMutableArray *devices = [NSMutableArray array];
	NSMutableArray *inDevices = [NSMutableArray array];
	NSString *devName;
	int i;

	XSTed_apply_midi_port_settings();
	for (i = 0; (devName = CoreMIDIDeviceNameAtIndex(i)) != nil; i++)
		[devices addObject:devName];
	[self setDeviceList:devices];
	for (i = 0; (devName = CoreMIDIInDeviceNameAtIndex(i)) != nil; i++)
		[inDevices addObject:devName];
	[self setInDeviceList:inDevices];
	[self loadFromSaved];
	[self syncControls];
}

- (void)fillPopup:(NSPopUpButton *)popup titles:(NSArray *)titles selected:(NSString *)selected
{
	NSEnumerator *items;
	NSString *title;

	if (popup == nil)
		return;
	[popup removeAllItems];
	items = [titles objectEnumerator];
	while ((title = [items nextObject]) != nil)
		[popup addItemWithTitle:title];
	if (selected != nil && [popup indexOfItemWithTitle:selected] >= 0)
		[popup selectItemWithTitle:selected];
	else
		[popup selectItem:nil];
}

- (void)syncControls
{
	[self fillPopup:midiInPopup titles:inDeviceList selected:midiIn];
	[self fillPopup:midiOut1Popup titles:deviceList selected:midiOut1];
	[self fillPopup:midiOut2Popup titles:deviceList selected:midiOut2];
	[clockOut1Check setState:midiOut1Clock ? NSControlStateValueOn : NSControlStateValueOff];
	[clockOut2Check setState:midiOut2Clock ? NSControlStateValueOn : NSControlStateValueOff];
	[clockOut1Check setEnabled:!MidiOutIsInternalSynth(midiOut1)];
	[clockOut2Check setEnabled:!MidiOutIsInternalSynth(midiOut2)];
}

- (IBAction)midiInChanged:(id)sender
{
	(void)sender;
	[self setMidiIn:[midiInPopup titleOfSelectedItem]];
}

- (IBAction)midiOut1Changed:(id)sender
{
	(void)sender;
	[self setMidiOut1:[midiOut1Popup titleOfSelectedItem]];
	[self syncControls];
}

- (IBAction)midiOut2Changed:(id)sender
{
	(void)sender;
	[self setMidiOut2:[midiOut2Popup titleOfSelectedItem]];
	[self syncControls];
}

- (IBAction)clockOut1Changed:(id)sender
{
	(void)sender;
	[self setMidiOut1Clock:[clockOut1Check state] == NSControlStateValueOn];
	[self syncControls];
}

- (IBAction)clockOut2Changed:(id)sender
{
	(void)sender;
	[self setMidiOut2Clock:[clockOut2Check state] == NSControlStateValueOn];
	[self syncControls];
}

- (void)windowDidLoad
{
	NSWindow *win;

	[super windowDidLoad];

	win = [self window];
	[win setStyleMask:(NSWindowStyleMaskTitled |
			   NSWindowStyleMaskClosable |
			   NSWindowStyleMaskUtilityWindow)];
	[win setReleasedWhenClosed:NO];
	if ([win respondsToSelector:@selector(setFloatingPanel:)])
		[(NSPanel *)win setFloatingPanel:YES];
	[win setHidesOnDeactivate:NO];
}

- (void)dealloc
{
	[deviceList release];
	[inDeviceList release];
	[midiIn release];
	[midiOut1 release];
	[midiOut2 release];
	[super dealloc];
}

@end
