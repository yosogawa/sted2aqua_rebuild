/* MIDISettingsController.m */

#import "MIDISettingsController.h"
#import "CoreMIDIRCPPlayer.h"

extern void sted_redraw_midi_in(void);

NSString * const kMidiOut1ClockKey = @"midiout1_clock";
NSString * const kMidiOut2ClockKey = @"midiout2_clock";
static NSString * const kMidiOutClockEnabledTransformerName = @"MidiOutClockEnabledTransformer";

static MIDISettingsController *sSharedPanel;
static int sApplyingPorts;

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

@interface MidiOutClockEnabledTransformer : NSValueTransformer
@end

@implementation MidiOutClockEnabledTransformer
+ (Class)transformedValueClass { return [NSNumber class]; }
+ (BOOL)allowsReverseTransformation { return NO; }
- (id)transformedValue:(id)value
{
	NSString *name = [value isKindOfClass:[NSString class]] ? value : nil;
	return [NSNumber numberWithBool:!MidiOutIsInternalSynth(name)];
}
@end

@implementation MIDISettingsController

+ (void)initialize
{
	if (self != [MIDISettingsController class])
		return;
	[[NSUserDefaults standardUserDefaults] registerDefaults:@{
		kMidiOut1ClockKey: @NO,
		kMidiOut2ClockKey: @NO
	}];
	[NSValueTransformer setValueTransformer:[[MidiOutClockEnabledTransformer new] autorelease]
					forName:kMidiOutClockEnabledTransformerName];
}

+ (void)showPanel
{
	if (sSharedPanel == nil)
		sSharedPanel = [[MIDISettingsController alloc] initWithWindowNibName:@"MIDISettings"];
	[sSharedPanel refreshDeviceLists];
	[sSharedPanel showWindow:nil];
	[[sSharedPanel window] makeKeyAndOrderFront:nil];
}

- (void)refreshDeviceLists
{
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	NSMutableArray *devices = [NSMutableArray array];
	NSMutableArray *inDevices = [NSMutableArray array];
	NSString *devName, *outdevice1, *outdevice2;
	int i;

	for (i = 0; (devName = CoreMIDIDeviceNameAtIndex(i)) != nil; i++)
		[devices addObject:devName];
	[def setValue:devices forKey:@"devicelist"];

	for (i = 0; (devName = CoreMIDIInDeviceNameAtIndex(i)) != nil; i++)
		[inDevices addObject:devName];
	[def setValue:inDevices forKey:@"indevicelist"];

	outdevice1 = [def valueForKey:@"midiout1"];
	outdevice2 = [def valueForKey:@"midiout2"];
	if (outdevice1 != nil)
		[def setValue:[NSString stringWithUTF8String:CoreMIDICanonicalOutputName([outdevice1 UTF8String])]
			   forKey:@"midiout1"];
	if (outdevice2 != nil)
		[def setValue:[NSString stringWithUTF8String:CoreMIDICanonicalOutputName([outdevice2 UTF8String])]
			   forKey:@"midiout2"];
}

- (void)applyPortsFromDefaults
{
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	NSString *chosen;
	NSString *out1, *out2, *indev;

	if (sApplyingPorts)
		return;
	sApplyingPorts = 1;

	out1 = [def valueForKey:@"midiout1"];
	out2 = [def valueForKey:@"midiout2"];
	indev = [def valueForKey:@"midiin"];

	if (out1 != nil) {
		if (CoreMIDISelectDeviceByNameForPort([out1 UTF8String], 0) < 0)
			CoreMIDISelectDeviceByNameForPort("Apple DLS Synth", 0);
	}
	if (out2 != nil) {
		if (CoreMIDISelectDeviceByNameForPort([out2 UTF8String], 1) < 0)
			CoreMIDISelectDeviceByNameForPort("Apple DLS Synth 2", 1);
	}
	chosen = CoreMIDISelectedOutputName(0);
	if (chosen != nil)
		[def setValue:chosen forKey:@"midiout1"];
	chosen = CoreMIDISelectedOutputName(1);
	if (chosen != nil)
		[def setValue:chosen forKey:@"midiout2"];
	if (indev != nil)
		CoreMIDISelectInputDeviceByName([indev UTF8String]);
	sted_redraw_midi_in();

	[self syncClockFlags];
	sApplyingPorts = 0;
}

- (void)clearClockIfInternalForOutKey:(NSString *)outKey clockKey:(NSString *)clockKey
{
	id values = [[NSUserDefaultsController sharedUserDefaultsController] values];
	NSString *name = [values valueForKey:outKey];
	if (!MidiOutIsInternalSynth(name))
		return;
	if ([[values valueForKey:clockKey] boolValue])
		[values setValue:@NO forKey:clockKey];
}

- (void)syncClockFlags
{
	[self clearClockIfInternalForOutKey:@"midiout1" clockKey:kMidiOut1ClockKey];
	[self clearClockIfInternalForOutKey:@"midiout2" clockKey:kMidiOut2ClockKey];
}

- (void)bindClockEnabled:(NSButton *)check outKey:(NSString *)outKey
{
	NSUserDefaultsController *udc = [NSUserDefaultsController sharedUserDefaultsController];
	[check bind:NSEnabledBinding
	   toObject:udc
	withKeyPath:[@"values." stringByAppendingString:outKey]
	    options:@{ NSValueTransformerNameBindingOption: kMidiOutClockEnabledTransformerName }];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
		      ofObject:(id)object
			change:(NSDictionary *)change
		       context:(void *)context
{
	(void)object;
	(void)change;
	(void)context;
	if ([keyPath isEqualToString:@"values.midiout1"] ||
	    [keyPath isEqualToString:@"values.midiout2"] ||
	    [keyPath isEqualToString:@"values.midiin"])
		[self applyPortsFromDefaults];
}

- (void)windowDidLoad
{
	NSWindow *win;
	NSUserDefaultsController *udc;

	[super windowDidLoad];

	win = [self window];
	[win setStyleMask:(NSWindowStyleMaskTitled |
			   NSWindowStyleMaskClosable |
			   NSWindowStyleMaskUtilityWindow)];
	[win setReleasedWhenClosed:NO];
	if ([win respondsToSelector:@selector(setFloatingPanel:)])
		[(NSPanel *)win setFloatingPanel:YES];
	[win setHidesOnDeactivate:NO];

	[self bindClockEnabled:clockOut1Check outKey:@"midiout1"];
	[self bindClockEnabled:clockOut2Check outKey:@"midiout2"];

	udc = [NSUserDefaultsController sharedUserDefaultsController];
	[udc addObserver:self forKeyPath:@"values.midiout1" options:0 context:NULL];
	[udc addObserver:self forKeyPath:@"values.midiout2" options:0 context:NULL];
	[udc addObserver:self forKeyPath:@"values.midiin" options:0 context:NULL];
	[self syncClockFlags];
}

- (void)dealloc
{
	[clockOut1Check unbind:NSEnabledBinding];
	[clockOut2Check unbind:NSEnabledBinding];
	NSUserDefaultsController *udc = [NSUserDefaultsController sharedUserDefaultsController];
	@try {
		[udc removeObserver:self forKeyPath:@"values.midiout1"];
		[udc removeObserver:self forKeyPath:@"values.midiout2"];
		[udc removeObserver:self forKeyPath:@"values.midiin"];
	} @catch (__unused NSException *ex) {
	}
	[super dealloc];
}

@end
