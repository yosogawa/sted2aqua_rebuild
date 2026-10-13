#import "DLSSynthPanel.h"
#import "CoreMIDIRCPPlayer.h"

#include "sted.h"

enum {
	kPanelW = 468,
	kPanelH = 306,
	kMargin = 20,
	kLabelW = 112,
	kValueW = 78,
	kRowH = 26,
	kRowStep = 36,
	kSepSpace = 16
};

static CGFloat
CenteredLabelY(CGFloat rowY, NSFont *font)
{
	CGFloat lineH = [font ascender] - [font descender];

	return rowY - (kRowH - lineH) * 0.5;
}

static NSString *
SignedDB(double v)
{
	int neg = v < 0.0;
	double a = neg ? -v : v;
	int whole, frac;

	a = floor(a * 10.0 + 0.5);
	whole = (int)(a / 10.0);
	frac = (int)a - whole * 10;
	if (v > 0.0)
		return [NSString stringWithFormat:@"+%d.%d dB", whole, frac];
	if (neg)
		return [NSString stringWithFormat:@"-%d.%d dB", whole, frac];
	return [NSString stringWithFormat:@"%d.%d dB", whole, frac];
}

static NSString *
PlainNumber(double v, int decimals, NSString *suffix)
{
	int scale = 1;
	int i, whole, frac;
	double a = v;

	if (a < 0.0)
		a = 0.0;
	for (i = 0; i < decimals; i++)
		scale *= 10;
	a = floor(a * (double)scale + 0.5);
	whole = (int)(a / (double)scale);
	frac = (int)a - whole * scale;
	if (decimals >= 2)
		return [NSString stringWithFormat:@"%d.%02d%@", whole, frac, suffix];
	return [NSString stringWithFormat:@"%d.%d%@", whole, frac, suffix];
}

static NSString *
ReverbMixLabel(double v)
{
	int tenths = (int)floor(v * 10.0 + 0.5);

	if (tenths <= 0)
		return @"0(Dry)%";
	if (tenths >= 1000)
		return @"100(Wet)%";
	return PlainNumber(v, 1, @" %");
}

@implementation DLSSynthPanel
{
	NSWindow *window;
	NSPopUpButton *bankPopup;
	NSPopUpButton *reverbPopup;
	NSSlider *bassSlider;
	NSSlider *trebleSlider;
	NSSlider *mixSlider;
	NSSlider *limitSlider;
	NSSlider *volumeSlider;
	NSTextField *bassValue;
	NSTextField *trebleValue;
	NSTextField *mixValue;
	NSTextField *limitValue;
	NSTextField *volumeValue;
}

static DLSSynthPanel *sPanel;

static const char *kReverbNames[13] = {
	"Small Room",
	"Medium Room",
	"Large Room",
	"Medium Hall",
	"Large Hall",
	"Plate",
	"Medium Chamber",
	"Large Chamber",
	"Cathedral",
	"Large Room 2",
	"Medium Hall 2",
	"Medium Hall 3",
	"Large Hall 2"
};

+ (void)run
{
	if (sPanel == nil)
		sPanel = [[DLSSynthPanel alloc] init];
	[sPanel show];
}

- (void)dealloc
{
	[window setDelegate:nil];
	[bankPopup release];
	[reverbPopup release];
	[bassSlider release];
	[trebleSlider release];
	[mixSlider release];
	[limitSlider release];
	[volumeSlider release];
	[bassValue release];
	[trebleValue release];
	[mixValue release];
	[limitValue release];
	[volumeValue release];
	[window release];
	[super dealloc];
}

- (NSTextField *)addLabel:(NSString *)title y:(CGFloat)y
{
	NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(kMargin, y, kLabelW, kRowH)];
	[field setStringValue:title];
	[field setBezeled:NO];
	[field setBordered:NO];
	[field setDrawsBackground:NO];
	[field setEditable:NO];
	[field setSelectable:NO];
	[field setAlignment:NSTextAlignmentRight];
	[field setFrameOrigin:NSMakePoint(kMargin, CenteredLabelY(y, [field font]))];
	[[window contentView] addSubview:field];
	[field release];
	return field;
}

- (NSTextField *)addValueAtY:(CGFloat)y
{
	CGFloat x = kPanelW - kMargin - kValueW;
	NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, kValueW, kRowH)];
	[field setFrameOrigin:NSMakePoint(x, CenteredLabelY(y, [field font]))];
	[field setBezeled:NO];
	[field setBordered:NO];
	[field setDrawsBackground:NO];
	[field setEditable:NO];
	[field setSelectable:NO];
	[field setAlignment:NSTextAlignmentRight];
	[[window contentView] addSubview:field];
	return field;
}

- (NSSlider *)addSliderAtY:(CGFloat)y min:(double)min max:(double)max value:(double)value
{
	CGFloat x = kMargin + kLabelW + 8;
	CGFloat valueX = kPanelW - kMargin - kValueW;
	NSSlider *slider = [[NSSlider alloc] initWithFrame:NSMakeRect(x, y, valueX - 8 - x, kRowH)];
	[slider setMinValue:min];
	[slider setMaxValue:max];
	[slider setDoubleValue:value];
	[slider setContinuous:YES];
	[slider setTarget:self];
	[slider setAction:@selector(sliderChanged:)];
	[[window contentView] addSubview:slider];
	return slider;
}

- (NSPopUpButton *)addPopupAtY:(CGFloat)y
{
	CGFloat x = kMargin + kLabelW + 8;
	CGFloat w = kPanelW - kMargin - x;
	NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(x, y - 2, w, kRowH) pullsDown:NO];
	[[window contentView] addSubview:popup];
	return popup;
}

- (void)addSeparatorBeforeRowAtY:(CGFloat)rowY
{
	CGFloat gap = kRowStep + kSepSpace - kRowH;
	CGFloat h = 5;
	CGFloat center = rowY + kRowH + gap * 0.5;
	NSBox *line = [[NSBox alloc] initWithFrame:NSMakeRect(kMargin, center - h * 0.5, kPanelW - kMargin * 2, h)];
	[line setBoxType:NSBoxSeparator];
	[line setTitlePosition:NSNoTitle];
	[[window contentView] addSubview:line];
	[line release];
}

- (void)fillBankPopup
{
	NSString *dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Audio/Sounds/Banks"];
	NSArray *found = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil];
	NSMutableArray *names = [NSMutableArray array];
	NSString *current = [NSString stringWithUTF8String:CoreMIDIDLSSynthFontFilename()];
	NSUInteger i;
	BOOL selected = NO;

	[bankPopup removeAllItems];
	[bankPopup addItemWithTitle:@"Built-in (GM)"];
	[[bankPopup lastItem] setRepresentedObject:@""];
	for (i = 0; i < [found count]; i++) {
		NSString *name = [found objectAtIndex:i];
		NSString *ext = [[name pathExtension] lowercaseString];
		if ([ext isEqualToString:@"sf2"] || [ext isEqualToString:@"dls"])
			[names addObject:name];
	}
	[names sortUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
	for (i = 0; i < [names count]; i++) {
		NSString *name = [names objectAtIndex:i];
		[bankPopup addItemWithTitle:name];
		[[bankPopup lastItem] setRepresentedObject:name];
	}
	if (current == nil)
		current = @"";
	for (i = 0; i < (NSUInteger)[bankPopup numberOfItems]; i++) {
		NSMenuItem *item = [bankPopup itemAtIndex:(NSInteger)i];
		NSString *rep = [item representedObject];
		if (rep != nil && [rep isEqualToString:current]) {
			[bankPopup selectItemAtIndex:(NSInteger)i];
			selected = YES;
			break;
		}
	}
	if (!selected && [current length] > 0) {
		[bankPopup addItemWithTitle:current];
		[[bankPopup lastItem] setRepresentedObject:current];
		[bankPopup selectItem:[bankPopup lastItem]];
	}
}

- (void)fillReverbPopup:(int)preset
{
	int i;
	[reverbPopup removeAllItems];
	for (i = 0; i < 13; i++) {
		[reverbPopup addItemWithTitle:[NSString stringWithUTF8String:kReverbNames[i]]];
		[[reverbPopup lastItem] setTag:i];
	}
	if (preset < 0 || preset > 12)
		preset = 3;
	[reverbPopup selectItemWithTag:preset];
}

- (void)refreshValueLabels
{
	[bassValue setStringValue:SignedDB([bassSlider doubleValue])];
	[trebleValue setStringValue:SignedDB([trebleSlider doubleValue])];
	[mixValue setStringValue:ReverbMixLabel([mixSlider doubleValue])];
	[limitValue setStringValue:PlainNumber([limitSlider doubleValue], 1, @" dB")];
	[volumeValue setStringValue:PlainNumber([volumeSlider doubleValue], 2, @"")];
}

- (void)saveSettings
{
	if (XSTed_save_dls_settings() == 0)
		return;
	NSAlert *alert = [[NSAlert alloc] init];
	[alert setMessageText:@"Could not save sted2.cnf"];
	[alert setInformativeText:@"The DLS settings were applied, but the configuration file could not be written."];
	[alert runModal];
	[alert release];
}

- (void)buildWindow
{
	DLSEffectSettings fx;
	NSPanel *panel;
	CGFloat y = kPanelH - 16 - kRowH;

	CoreMIDIGetDLSEffects(&fx);
	panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, kPanelW, kPanelH)
					   styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskUtilityWindow)
					     backing:NSBackingStoreBuffered
					       defer:NO];
	window = panel;
	[panel setTitle:@"DLS Synth"];
	[panel setFloatingPanel:YES];
	[panel setBecomesKeyOnlyIfNeeded:YES];
	[panel setHidesOnDeactivate:NO];
	[panel setReleasedWhenClosed:NO];
	[panel setDelegate:(id)self];
	[panel center];

	[self addLabel:@"Sound Bank" y:y];
	bankPopup = [self addPopupAtY:y];
	[bankPopup setTarget:self];
	[bankPopup setAction:@selector(bankChanged:)];
	[self fillBankPopup];
	y -= kRowStep + kSepSpace;
	[self addSeparatorBeforeRowAtY:y];

	[self addLabel:@"Bass Gain" y:y];
	bassSlider = [self addSliderAtY:y min:-6.0 max:12.0 value:fx.bassGain];
	bassValue = [self addValueAtY:y];
	y -= kRowStep;

	[self addLabel:@"Treble Gain" y:y];
	trebleSlider = [self addSliderAtY:y min:-6.0 max:12.0 value:fx.trebleGain];
	trebleValue = [self addValueAtY:y];
	y -= kRowStep;

	[self addLabel:@"Reverb Type" y:y];
	reverbPopup = [self addPopupAtY:y];
	[reverbPopup setTarget:self];
	[reverbPopup setAction:@selector(reverbChanged:)];
	[self fillReverbPopup:fx.reverbPreset];
	y -= kRowStep;

	[self addLabel:@"Reverb Mix" y:y];
	mixSlider = [self addSliderAtY:y min:0.0 max:100.0 value:fx.reverbMix];
	mixValue = [self addValueAtY:y];
	y -= kRowStep;

	[self addLabel:@"Limiter Boost" y:y];
	limitSlider = [self addSliderAtY:y min:0.0 max:6.0 value:fx.limiterBoost];
	limitValue = [self addValueAtY:y];
	y -= kRowStep + kSepSpace;
	[self addSeparatorBeforeRowAtY:y];

	[self addLabel:@"Total Volume" y:y];
	volumeSlider = [self addSliderAtY:y min:0.0 max:1.0 value:fx.volume];
	volumeValue = [self addValueAtY:y];
	[self refreshValueLabels];
}

- (DLSEffectSettings)settingsFromControls
{
	DLSEffectSettings s;
	s.bassGain = (float)[bassSlider doubleValue];
	s.trebleGain = (float)[trebleSlider doubleValue];
	s.reverbPreset = (int)[reverbPopup selectedTag];
	s.reverbMix = (float)[mixSlider doubleValue];
	s.limiterBoost = (float)[limitSlider doubleValue];
	s.volume = (float)[volumeSlider doubleValue];
	return s;
}

- (void)sliderChanged:(id)sender
{
	NSEvent *event = [NSApp currentEvent];
	DLSEffectSettings s = [self settingsFromControls];
	(void)sender;
	[self refreshValueLabels];
	CoreMIDISetDLSEffects(&s);
	if (event == nil || [event type] != NSEventTypeLeftMouseDragged)
		[self saveSettings];
}

- (void)reverbChanged:(id)sender
{
	DLSEffectSettings s = [self settingsFromControls];
	(void)sender;
	CoreMIDISetDLSEffects(&s);
	[self saveSettings];
}

- (void)bankChanged:(id)sender
{
	NSString *name = [[bankPopup selectedItem] representedObject];
	const char *utf;
	(void)sender;
	if (name == nil)
		name = @"";
	utf = [name UTF8String];
	CoreMIDISetDLSSynthFont(utf != NULL ? utf : "");
	[self saveSettings];
}

- (void)show
{
	if (window == nil)
		[self buildWindow];
	[window orderFront:nil];
}

- (void)windowWillClose:(NSNotification *)notification
{
	(void)notification;
	[self saveSettings];
	if (sPanel == self) {
		sPanel = nil;
		[self autorelease];
	}
}

@end
