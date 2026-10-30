/* MIDISettingsController.m */

#import "MIDISettingsController.h"

NSString * const kMidiOut1ClockKey = @"midiout1_clock";
NSString * const kMidiOut2ClockKey = @"midiout2_clock";
static NSString * const kMidiOutClockEnabledTransformerName = @"MidiOutClockEnabledTransformer";

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

+ (BOOL)runDialog
{
	MIDISettingsController *cont;
	NSModalResponse result;

	cont = [[MIDISettingsController alloc] initWithWindowNibName:@"MIDISettings"];
	result = [NSApp runModalForWindow:[cont window]];
	[cont close];
	[cont release];
	return (result == NSModalResponseStop);
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
	    [keyPath isEqualToString:@"values.midiout2"])
		[self syncClockFlags];
}

- (void)defaultsDidChange:(NSNotification *)note
{
	(void)note;
	[self syncClockFlags];
}

- (void)windowDidLoad
{
	NSUserDefaultsController *udc;

	[super windowDidLoad];

	[self bindClockEnabled:clockOut1Check outKey:@"midiout1"];
	[self bindClockEnabled:clockOut2Check outKey:@"midiout2"];

	udc = [NSUserDefaultsController sharedUserDefaultsController];
	[udc addObserver:self forKeyPath:@"values.midiout1" options:0 context:NULL];
	[udc addObserver:self forKeyPath:@"values.midiout2" options:0 context:NULL];
	[[NSNotificationCenter defaultCenter]
		addObserver:self
		   selector:@selector(defaultsDidChange:)
		       name:NSUserDefaultsDidChangeNotification
		     object:[NSUserDefaults standardUserDefaults]];
	[self syncClockFlags];
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[clockOut1Check unbind:NSEnabledBinding];
	[clockOut2Check unbind:NSEnabledBinding];
	NSUserDefaultsController *udc = [NSUserDefaultsController sharedUserDefaultsController];
	@try {
		[udc removeObserver:self forKeyPath:@"values.midiout1"];
		[udc removeObserver:self forKeyPath:@"values.midiout2"];
	} @catch (__unused NSException *ex) {
	}
	[super dealloc];
}

- (IBAction)okPressed:(id)sender
{
	[self syncClockFlags];
	[super okPressed:sender];
}

@end
