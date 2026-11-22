/* Keyboard viewer.
 * A non-activating panel that cannot become the key window. Option and
 * Control follow the app's modifier state, and the sequencer keeps the keys.
 * The drawn shape is ANSI, ISO, or JIS from KBGetLayoutType, with a
 * navigation cluster and numeric pad when the connected keyboard is not
 * a laptop or a known compact board.
 */
#import "KeyboardViewer.h"
#import "X68kView.h"

#import <Carbon/Carbon.h>
#import <IOKit/hid/IOHIDKeys.h>
#import <IOKit/hid/IOHIDUsageTables.h>
#import <IOKit/IOKitLib.h>

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

enum {
	kShapeANSI = 0,
	kShapeISO = 1,
	kShapeJIS = 2,
	kMaxCaps = 140
};

typedef struct {
	int vk;
	float x, y, w, h;
	unsigned mac;
	char legend[48];
} Cap;

@interface KeyboardMapView : NSView
{
	Cap caps[kMaxCaps];
	int ncaps;
	int shape;
	int full;
	int modMask;
	int commandDown;
	NSString *product;
	float unitW, unitH;
}
- (void)noteModifiers:(NSEventModifierFlags)flags;
- (void)refreshHardware;
- (NSSize)unitSize;
@end

static id sMonitor = nil;
static KeyboardMapView *sMap = nil;
static NSPanel *sPanel = nil;

static int
add_cap(Cap *c, int n, int vk, float x, float y, float w, float h)
{
	if (n >= kMaxCaps)
		return n;
	c[n].vk = vk;
	c[n].x = x;
	c[n].y = y;
	c[n].w = w;
	c[n].h = h;
	return n + 1;
}

static float
place_keys(Cap *c, int *n, const int *vk, int count, float x, float y,
	   float w, float h, float pitch)
{
	int i;
	for (i = 0; i < count; i++) {
		*n = add_cap(c, *n, vk[i], x, y, w, h);
		x += pitch;
	}
	return x;
}

static const int kFn[] = {
	0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F
};
static const int kDigits[] = {
	0x32, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D, 0x1B, 0x18
};
static const int kDigitsJIS[] = {
	0x32, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D, 0x1B, 0x18, 0x5D
};
static const int kLettersQ[] = {
	0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x10, 0x20, 0x22, 0x1F, 0x23
};
static const int kLettersA[] = {
	0x00, 0x01, 0x02, 0x03, 0x05, 0x04, 0x26, 0x28, 0x25
};
static const int kLettersZ[] = {
	0x06, 0x07, 0x08, 0x09, 0x0B, 0x2D, 0x2E, 0x2B, 0x2F, 0x2C
};

static void
measure(const Cap *c, int n, float *w, float *h)
{
	int i;
	float mw = 0, mh = 0;
	for (i = 0; i < n; i++) {
		if (c[i].x + c[i].w > mw)
			mw = c[i].x + c[i].w;
		if (c[i].y + c[i].h > mh)
			mh = c[i].y + c[i].h;
	}
	*w = mw;
	*h = mh;
}

static int
build_caps(Cap *c, int shape, int full)
{
	const float p = 1.14f;
	const float h = 1.0f;
	const float y0 = 0.0f;
	const float y1 = 1.22f;
	const float y2 = 2.44f;
	const float y3 = 3.66f;
	const float y4 = 4.88f;
	const float y5 = 6.10f;
	int n = 0;
	float x, edge, nav;

	x = 0;
	n = add_cap(c, n, 0x35, x, y0, 1.25f, 0.85f);
	x = 1.9f;
	x = place_keys(c, &n, kFn, 4, x, y0, 1.0f, 0.85f, p);
	x += 0.38f;
	x = place_keys(c, &n, kFn + 4, 4, x, y0, 1.0f, 0.85f, p);
	x += 0.38f;
	x = place_keys(c, &n, kFn + 8, 4, x, y0, 1.0f, 0.85f, p);

	if (shape == kShapeJIS)
		x = place_keys(c, &n, kDigitsJIS, 14, 0, y1, 1.0f, h, p);
	else
		x = place_keys(c, &n, kDigits, 13, 0, y1, 1.0f, h, p);
	n = add_cap(c, n, 0x33, x, y1, 1.55f, h);
	edge = x + 1.55f;

	x = 0;
	n = add_cap(c, n, 0x30, x, y2, 1.5f, h);
	x = 1.5f + (p - 1.0f);
	x = place_keys(c, &n, kLettersQ, 10, x, y2, 1.0f, h, p);
	n = add_cap(c, n, 0x21, x, y2, 1.0f, h);
	x += p;
	if (shape == kShapeJIS) {
		n = add_cap(c, n, 0x1E, x, y2, 1.0f, h);
	} else if (shape == kShapeANSI) {
		n = add_cap(c, n, 0x1E, x, y2, 1.0f, h);
		x += p;
		n = add_cap(c, n, 0x2A, x, y2, 1.45f, h);
	} else {
		n = add_cap(c, n, 0x1E, x, y2, 1.45f, h);
	}

	x = 0;
	n = add_cap(c, n, 0x39, x, y3, 1.75f, h);
	x = 1.75f + (p - 1.0f);
	x = place_keys(c, &n, kLettersA, 9, x, y3, 1.0f, h, p);
	n = add_cap(c, n, 0x29, x, y3, 1.0f, h);
	x += p;
	n = add_cap(c, n, 0x27, x, y3, 1.0f, h);
	x += p;
	if (shape == kShapeISO) {
		n = add_cap(c, n, 0x2A, x, y3, 1.0f, h);
		x += p;
	}
	n = add_cap(c, n, 0x24, x, y3, edge - x, h);

	x = 0;
	if (shape == kShapeISO) {
		n = add_cap(c, n, 0x38, x, y4, 1.25f, h);
		x = 1.25f + (p - 1.0f);
		n = add_cap(c, n, 0x0A, x, y4, 1.0f, h);
		x += p;
	} else if (shape == kShapeJIS) {
		n = add_cap(c, n, 0x38, x, y4, 2.05f, h);
		x = 2.05f + (p - 1.0f);
	} else {
		n = add_cap(c, n, 0x38, x, y4, 2.35f, h);
		x = 2.35f + (p - 1.0f);
	}
	x = place_keys(c, &n, kLettersZ, 10, x, y4, 1.0f, h, p);
	if (shape == kShapeJIS) {
		n = add_cap(c, n, 0x5E, x, y4, 1.0f, h);
		x += p;
	}
	if (full && edge - x > 0.4f) {
		n = add_cap(c, n, 0x3C, x, y4, edge - x, h);
	} else {
		float upx = edge - 1.0f;
		if (x < upx - 0.2f)
			n = add_cap(c, n, 0x3C, x, y4, upx - 0.12f - x, h);
		n = add_cap(c, n, 0x7E, upx, y4, 1.0f, h);
	}

	/* Modifiers stop at the main block. Arrows sit to the right of that. */
	{
		float limit = full ? edge : edge - 3.4f;
		float rightw = 1.25f + (p - 1.0f) + 1.35f;
		float spacew, rx;
		if (shape == kShapeJIS)
			rightw += (p - 1.0f) + 1.15f;
		x = 0;
		n = add_cap(c, n, 0x3F, x, y5, 1.15f, h);
		x += 1.15f + (p - 1.0f);
		n = add_cap(c, n, 0x3B, x, y5, 1.25f, h);
		x += 1.25f + (p - 1.0f);
		n = add_cap(c, n, 0x3A, x, y5, 1.25f, h);
		x += 1.25f + (p - 1.0f);
		n = add_cap(c, n, 0x37, x, y5, 1.35f, h);
		x += 1.35f + (p - 1.0f);
		if (shape == kShapeJIS) {
			n = add_cap(c, n, 0x66, x, y5, 1.15f, h);
			x += 1.15f + (p - 1.0f);
		}
		rx = limit - rightw;
		spacew = rx - (p - 1.0f) - x;
		if (spacew < 1.6f)
			spacew = 1.6f;
		n = add_cap(c, n, 0x31, x, y5, spacew, h);
		x = rx;
		n = add_cap(c, n, 0x36, x, y5, 1.35f, h);
		x += 1.35f + (p - 1.0f);
		if (shape == kShapeJIS) {
			n = add_cap(c, n, 0x68, x, y5, 1.15f, h);
			x += 1.15f + (p - 1.0f);
		}
		n = add_cap(c, n, 0x3D, x, y5, 1.25f, h);
	}
	if (!full) {
		float ax = edge - 3.28f;
		n = add_cap(c, n, 0x7B, ax, y5, 1.0f, h);
		n = add_cap(c, n, 0x7D, ax + p, y5, 1.0f, h);
		n = add_cap(c, n, 0x7C, ax + 2 * p, y5, 1.0f, h);
	} else {
		float tall = (y5 - y4) + h;
		nav = edge + 0.55f;
		n = add_cap(c, n, 0x72, nav, y1, 1.0f, h);
		n = add_cap(c, n, 0x73, nav + p, y1, 1.0f, h);
		n = add_cap(c, n, 0x74, nav + 2 * p, y1, 1.0f, h);
		n = add_cap(c, n, 0x75, nav, y2, 1.0f, h);
		n = add_cap(c, n, 0x77, nav + p, y2, 1.0f, h);
		n = add_cap(c, n, 0x79, nav + 2 * p, y2, 1.0f, h);
		n = add_cap(c, n, 0x7E, nav + p, y4, 1.0f, h);
		n = add_cap(c, n, 0x7B, nav, y5, 1.0f, h);
		n = add_cap(c, n, 0x7D, nav + p, y5, 1.0f, h);
		n = add_cap(c, n, 0x7C, nav + 2 * p, y5, 1.0f, h);

		/* + is one row. Enter spans the 1-row and the 0-row only. */
		x = nav + 3 * p + 0.55f;
		n = add_cap(c, n, 0x47, x, y1, 1.0f, h);
		n = add_cap(c, n, 0x51, x + p, y1, 1.0f, h);
		n = add_cap(c, n, 0x4B, x + 2 * p, y1, 1.0f, h);
		n = add_cap(c, n, 0x43, x + 3 * p, y1, 1.0f, h);
		n = add_cap(c, n, 0x59, x, y2, 1.0f, h);
		n = add_cap(c, n, 0x5B, x + p, y2, 1.0f, h);
		n = add_cap(c, n, 0x5C, x + 2 * p, y2, 1.0f, h);
		n = add_cap(c, n, 0x4E, x + 3 * p, y2, 1.0f, h);
		n = add_cap(c, n, 0x56, x, y3, 1.0f, h);
		n = add_cap(c, n, 0x57, x + p, y3, 1.0f, h);
		n = add_cap(c, n, 0x58, x + 2 * p, y3, 1.0f, h);
		n = add_cap(c, n, 0x45, x + 3 * p, y3, 1.0f, h);
		n = add_cap(c, n, 0x53, x, y4, 1.0f, h);
		n = add_cap(c, n, 0x54, x + p, y4, 1.0f, h);
		n = add_cap(c, n, 0x55, x + 2 * p, y4, 1.0f, h);
		n = add_cap(c, n, 0x4C, x + 3 * p, y4, 1.0f, tall);
		n = add_cap(c, n, 0x52, x, y5, p + 1.0f, h);
		n = add_cap(c, n, 0x41, x + 2 * p, y5, 1.0f, h);
	}
	return n;
}

static int
model_is_laptop(void)
{
	char model[128];
	size_t len = sizeof model;

	if (sysctlbyname("hw.model", model, &len, NULL, 0) != 0)
		return 0;
	model[sizeof model - 1] = 0;
	return strncmp(model, "MacBook", 7) == 0;
}

static NSString *
cf_string(CFTypeRef v)
{
	if (v == NULL || CFGetTypeID(v) != CFStringGetTypeID())
		return nil;
	return [(NSString *)v copy];
}

static int
name_is_internal(NSString *name, NSString *transport)
{
	if (transport != nil &&
	    [transport caseInsensitiveCompare:@"SPI"] == NSOrderedSame)
		return 1;
	if (name == nil)
		return 0;
	if ([name rangeOfString:@"Internal" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"Trackpad" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"Embedded" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	return 0;
}

static int
name_is_compact(NSString *name)
{
	if (name == nil)
		return 0;
	if ([name rangeOfString:@"HHKB" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"Happy Hacking" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"Magic Keyboard" options:NSCaseInsensitiveSearch].location != NSNotFound &&
	    [name rangeOfString:@"Numeric" options:NSCaseInsensitiveSearch].location == NSNotFound)
		return 1;
	return 0;
}

static int
name_has_pad(NSString *name)
{
	if (name == nil)
		return 0;
	if ([name rangeOfString:@"Numeric" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"NumPad" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	if ([name rangeOfString:@"Keypad" options:NSCaseInsensitiveSearch].location != NSNotFound)
		return 1;
	return 0;
}

/* Prefer an external board. A MacBook with nothing else attached stays compact. */
static void
detect_keyboard(int *shapeOut, int *fullOut, NSString **productOut)
{
	PhysicalKeyboardLayoutType kind;
	CFMutableDictionaryRef match;
	io_iterator_t it = 0;
	io_object_t dev;
	int page = kHIDPage_GenericDesktop;
	int usage = kHIDUsage_GD_Keyboard;
	int sawInternal = 0, sawExternal = 0, sawCompact = 0, sawPad = 0;
	NSString *shown = nil;
	NSString *internalName = nil;

	kind = KBGetLayoutType(LMGetKbdType());
	if (kind == kKeyboardJIS)
		*shapeOut = kShapeJIS;
	else if (kind == kKeyboardISO)
		*shapeOut = kShapeISO;
	else
		*shapeOut = kShapeANSI;

	match = IOServiceMatching(kIOHIDDeviceKey);
	if (match != NULL) {
		CFNumberRef np = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
		CFNumberRef nu = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
		CFDictionarySetValue(match, CFSTR(kIOHIDPrimaryUsagePageKey), np);
		CFDictionarySetValue(match, CFSTR(kIOHIDPrimaryUsageKey), nu);
		CFRelease(np);
		CFRelease(nu);
		if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) == KERN_SUCCESS) {
			while ((dev = IOIteratorNext(it)) != 0) {
				CFTypeRef pn = IORegistryEntryCreateCFProperty(dev, CFSTR(kIOHIDProductKey), kCFAllocatorDefault, 0);
				CFTypeRef tr = IORegistryEntryCreateCFProperty(dev, CFSTR(kIOHIDTransportKey), kCFAllocatorDefault, 0);
				NSString *name = cf_string(pn);
				NSString *transport = cf_string(tr);
				int internal = name_is_internal(name, transport);
				if (internal) {
					sawInternal = 1;
					if (internalName == nil && name != nil)
						internalName = [name retain];
				} else {
					sawExternal = 1;
					if (name_is_compact(name))
						sawCompact = 1;
					if (name_has_pad(name))
						sawPad = 1;
					if (shown == nil && name != nil)
						shown = [name retain];
				}
				if (name != nil)
					[name release];
				if (transport != nil)
					[transport release];
				if (pn != NULL)
					CFRelease(pn);
				if (tr != NULL)
					CFRelease(tr);
				IOObjectRelease(dev);
			}
			IOObjectRelease(it);
		}
	}
	if (shown == nil && internalName != nil)
		shown = [internalName retain];
	if (internalName != nil)
		[internalName release];

	if (sawExternal) {
		if (sawPad)
			*fullOut = 1;
		else if (sawCompact && !sawPad)
			*fullOut = 0;
		else
			*fullOut = 1;
	} else if (sawInternal && model_is_laptop()) {
		*fullOut = 0;
	} else if (model_is_laptop()) {
		*fullOut = 0;
	} else {
		*fullOut = 1;
	}
	if (*productOut != nil)
		[*productOut release];
	*productOut = shown;
}

static unsigned
mac_char_for_vk(int vk)
{
	TISInputSourceRef src;
	CFDataRef data;
	const UCKeyboardLayout *layout;
	UInt32 dead = 0;
	UniChar chars[8];
	UniCharCount len = 0;
	OSStatus st;
	unsigned ch;

	switch (vk) {
	case 0x7A: return NSF1FunctionKey;
	case 0x78: return NSF2FunctionKey;
	case 0x63: return NSF3FunctionKey;
	case 0x76: return NSF4FunctionKey;
	case 0x60: return NSF5FunctionKey;
	case 0x61: return NSF6FunctionKey;
	case 0x62: return NSF7FunctionKey;
	case 0x64: return NSF8FunctionKey;
	case 0x65: return NSF9FunctionKey;
	case 0x6D: return NSF10FunctionKey;
	case 0x67: return NSF11FunctionKey;
	case 0x6F: return NSF12FunctionKey;
	case 0x12: return '1';
	case 0x13: return '2';
	case 0x14: return '3';
	case 0x15: return '4';
	case 0x17: return '5';
	case 0x16: return '6';
	case 0x1A: return '7';
	case 0x1C: return '8';
	case 0x19: return '9';
	case 0x1D: return '0';
	case 0x7E: return NSUpArrowFunctionKey;
	case 0x7D: return NSDownArrowFunctionKey;
	case 0x7B: return NSLeftArrowFunctionKey;
	case 0x7C: return NSRightArrowFunctionKey;
	case 0x73: return NSHomeFunctionKey;
	case 0x77: return NSEndFunctionKey;
	case 0x74: return NSPageUpFunctionKey;
	case 0x79: return NSPageDownFunctionKey;
	case 0x75: return NSDeleteFunctionKey;
	case 0x47: return NSClearLineFunctionKey;
	case 0x66: return kMacEisuToken;
	case 0x68: return kMacKanaToken;
	default: break;
	}

	src = TISCopyCurrentKeyboardLayoutInputSource();
	if (src == NULL)
		src = TISCopyCurrentKeyboardInputSource();
	if (src == NULL)
		return 0;
	data = (CFDataRef)TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData);
	if (data == NULL) {
		CFRelease(src);
		return 0;
	}
	layout = (const UCKeyboardLayout *)CFDataGetBytePtr(data);
	st = UCKeyTranslate(layout, (UInt16)vk, kUCKeyActionDisplay, 0,
			    LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
			    &dead, 8, &len, chars);
	CFRelease(src);
	if (st != noErr || len == 0)
		return 0;
	ch = chars[0];
	if (ch >= 'A' && ch <= 'Z')
		ch = ch - 'A' + 'a';
	return ch;
}

static NSString *
fallback_legend(int vk)
{
	switch (vk) {
	case 0x7A: return @"F1";
	case 0x78: return @"F2";
	case 0x63: return @"F3";
	case 0x76: return @"F4";
	case 0x60: return @"F5";
	case 0x61: return @"F6";
	case 0x62: return @"F7";
	case 0x64: return @"F8";
	case 0x65: return @"F9";
	case 0x6D: return @"F10";
	case 0x67: return @"F11";
	case 0x6F: return @"F12";
	case 0x35: return @"esc";
	case 0x30: return @"tab";
	case 0x24: return @"return";
	case 0x33: return @"⌫";
	case 0x39: return @"caps";
	case 0x38:
	case 0x3C: return @"shift";
	case 0x3B:
	case 0x3E: return @"control";
	case 0x3A:
	case 0x3D: return @"option";
	case 0x37:
	case 0x36: return @"command";
	case 0x3F: return @"fn";
	case 0x66: return @"英数";
	case 0x68: return @"かな";
	case 0x7E: return @"↑";
	case 0x7D: return @"↓";
	case 0x7B: return @"←";
	case 0x7C: return @"→";
	case 0x72: return @"help";
	case 0x73: return @"home";
	case 0x77: return @"end";
	case 0x74: return @"page up";
	case 0x79: return @"page down";
	case 0x75: return @"del";
	default: return @"";
	}
}

static void
store_legend(Cap *cap)
{
	TISInputSourceRef src;
	CFDataRef data;
	const UCKeyboardLayout *layout;
	UInt32 dead = 0;
	UniChar chars[8];
	UniCharCount len = 0;
	NSString *s;

	cap->legend[0] = 0;
	s = fallback_legend(cap->vk);
	if (s != nil && [s length] > 0) {
		snprintf(cap->legend, sizeof cap->legend, "%s", [s UTF8String]);
		return;
	}
	src = TISCopyCurrentKeyboardLayoutInputSource();
	if (src == NULL)
		src = TISCopyCurrentKeyboardInputSource();
	if (src != NULL) {
		data = (CFDataRef)TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData);
		if (data != NULL) {
			layout = (const UCKeyboardLayout *)CFDataGetBytePtr(data);
			if (UCKeyTranslate(layout, (UInt16)cap->vk, kUCKeyActionDisplay, 0,
					   LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
					   &dead, 8, &len, chars) == noErr && len > 0) {
				s = [NSString stringWithCharacters:chars length:(NSUInteger)len];
				if (s != nil && [s length] > 0 && [s length] < 12 &&
				    [s characterAtIndex:0] >= 33 &&
				    [s characterAtIndex:0] < 0xF700 &&
				    [s UTF8String] != NULL)
					snprintf(cap->legend, sizeof cap->legend, "%s", [s UTF8String]);
			}
		}
		CFRelease(src);
	}
}

static BOOL
key_has_char(const Cap *c, int n, unsigned macChar)
{
	int i;
	for (i = 0; i < n; i++)
		if (c[i].mac == macChar)
			return YES;
	return NO;
}

static NSString *
missing_line(const Cap *c, int n, int modMask, int commandDown)
{
	NSMutableArray *names;
	NSMutableSet *seen;
	int i;

	names = [NSMutableArray array];
	seen = [NSMutableSet set];
	for (i = 0; ; i++) {
		unsigned mac = 0, need = 0;
		char raw[32], shown[32];
		NSString *label;
		if (!XSTed_binding_at(i, &mac, &need, raw, sizeof raw))
			break;
		if ((modMask & (int)need) != (int)need)
			continue;
		if (!XSTed_viewer_label(0, mac, modMask, commandDown, shown, sizeof shown))
			continue;
		if (strcmp(shown, raw) != 0)
			continue;
		if (key_has_char(c, n, mac))
			continue;
		label = [NSString stringWithUTF8String:shown];
		if (label == nil || [seen containsObject:label])
			continue;
		[seen addObject:label];
		[names addObject:label];
	}
	if ([names count] == 0)
		return nil;
	return [NSString stringWithFormat:NSLocalizedString(@"Not on this keyboard: %@",
							    @"Keyboard viewer"),
		[names componentsJoinedByString:@"  "]];
}

@implementation KeyboardMapView

- (void)refreshHardware
{
	int sh = kShapeANSI, fl = 0;
	NSString *name = nil;

	detect_keyboard(&sh, &fl, &name);
	if (sh != shape || fl != full || ncaps == 0) {
		shape = sh;
		full = fl;
		ncaps = build_caps(caps, shape, full);
		measure(caps, ncaps, &unitW, &unitH);
	}
	for (int i = 0; i < ncaps; i++) {
		caps[i].mac = mac_char_for_vk(caps[i].vk);
		store_legend(&caps[i]);
	}
	if (product != nil)
		[product release];
	product = name;
	[self setNeedsDisplay:YES];
}

- (NSSize)unitSize
{
	return NSMakeSize(unitW, unitH);
}

- (void)noteModifiers:(NSEventModifierFlags)flags
{
	int mask = 0;
	int cmd;

	if (flags & NSEventModifierFlagControl)
		mask |= 2;
	if (flags & NSEventModifierFlagOption)
		mask |= 4;
	cmd = (flags & NSEventModifierFlagCommand) != 0;
	if (mask == modMask && cmd == commandDown)
		return;
	modMask = mask;
	commandDown = cmd;
	[self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	BOOL dark = NO;
	NSString *layer;
	NSString *head;
	NSString *hint = nil;
	NSString *miss;
	NSDictionary *headAttr, *missAttr, *hintAttr;
	CGFloat headerH = 48, footerH, pad = 12;
	BOOL idle;
	CGFloat availW, availH, scale, ox, top;
	int i;
	NSColor *keyFill, *mappedFill, *stroke, *textColor, *subColor, *held, *promptStroke;

	(void)dirty;
	if (@available(macOS 10.14, *)) {
		NSAppearanceName match = [self.effectiveAppearance
			bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
		dark = [match isEqualToString:NSAppearanceNameDarkAqua];
	}
	if (dark) {
		[[NSColor colorWithCalibratedWhite:0.12 alpha:1] setFill];
		keyFill = [NSColor colorWithCalibratedWhite:0.24 alpha:1];
		mappedFill = [NSColor colorWithCalibratedRed:0.22 green:0.40 blue:0.62 alpha:1];
		stroke = [NSColor colorWithCalibratedWhite:0.45 alpha:1];
		textColor = [NSColor whiteColor];
		subColor = [NSColor colorWithCalibratedWhite:0.75 alpha:1];
		held = [NSColor colorWithCalibratedRed:0.35 green:0.55 blue:0.85 alpha:1];
		promptStroke = [NSColor colorWithCalibratedRed:0.45 green:0.78 blue:1 alpha:1];
	} else {
		[[NSColor colorWithCalibratedWhite:0.90 alpha:1] setFill];
		keyFill = [NSColor colorWithCalibratedWhite:0.97 alpha:1];
		mappedFill = [NSColor colorWithCalibratedRed:0.73 green:0.86 blue:0.98 alpha:1];
		stroke = [NSColor colorWithCalibratedWhite:0.55 alpha:1];
		textColor = [NSColor blackColor];
		subColor = [NSColor colorWithCalibratedWhite:0.35 alpha:1];
		held = [NSColor colorWithCalibratedRed:0.55 green:0.74 blue:0.95 alpha:1];
		promptStroke = [NSColor colorWithCalibratedRed:0.10 green:0.42 blue:0.82 alpha:1];
	}
	NSRectFill(b);

	idle = (modMask & 6) == 0;
	if ((modMask & 6) == 6)
		layer = NSLocalizedString(@"Option+Control", @"Keyboard viewer modifiers");
	else if (modMask & 4)
		layer = NSLocalizedString(@"Option", @"Keyboard viewer modifiers");
	else if (modMask & 2)
		layer = NSLocalizedString(@"Control", @"Keyboard viewer modifiers");
	else
		layer = nil;
	if (idle) {
		hint = NSLocalizedString(@"Hold Control, Option, or both. The view changes only while STed2 is in front.", @"Keyboard viewer hint");
		head = (product != nil && [product length] > 0) ? product : @"";
	} else if (product != nil && [product length] > 0)
		head = [NSString stringWithFormat:@"%@    %@", layer, product];
	else
		head = layer;
	headAttr = [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont systemFontOfSize:13], NSFontAttributeName,
		textColor, NSForegroundColorAttributeName, nil];
	[head drawAtPoint:NSMakePoint(pad, b.size.height - 20)
		withAttributes:headAttr];
	if (hint != nil) {
		NSRect hr = NSMakeRect(pad, b.size.height - headerH, b.size.width - pad * 2, 22);
		hintAttr = [NSDictionary dictionaryWithObjectsAndKeys:
			[NSFont systemFontOfSize:12], NSFontAttributeName,
			textColor, NSForegroundColorAttributeName, nil];
		[hint drawWithRect:hr
			   options:NSStringDrawingTruncatesLastVisibleLine | NSStringDrawingUsesLineFragmentOrigin
			attributes:hintAttr];
	}

	miss = missing_line(caps, ncaps, modMask, commandDown);
	footerH = (miss != nil) ? 34 : 8;
	missAttr = [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont systemFontOfSize:12], NSFontAttributeName,
		textColor, NSForegroundColorAttributeName, nil];
	if (miss != nil) {
		NSRect mr = NSMakeRect(pad, 6, b.size.width - pad * 2, 24);
		[miss drawWithRect:mr options:NSStringDrawingTruncatesLastVisibleLine | NSStringDrawingUsesLineFragmentOrigin
			attributes:missAttr];
	}

	availW = b.size.width - pad * 2;
	availH = b.size.height - headerH - footerH - 8;
	if (unitW < 1 || unitH < 1 || availW < 1 || availH < 1)
		return;
	scale = availW / unitW;
	if (availH / unitH < scale)
		scale = availH / unitH;
	ox = pad + (availW - unitW * scale) / 2;
	top = footerH + (availH - unitH * scale) / 2 + unitH * scale;

	for (i = 0; i < ncaps; i++) {
		NSRect r;
		NSString *native, *xname = nil;
		char xbuf[32];
		int heldKey = 0;
		int promptKey = 0;
		NSBezierPath *path;
		NSFont *mainFont, *subFont;

		r.origin.x = ox + caps[i].x * scale;
		r.size.width = caps[i].w * scale - 3;
		r.size.height = caps[i].h * scale - 3;
		r.origin.y = top - (caps[i].y + caps[i].h) * scale;
		if (r.size.width < 4 || r.size.height < 4)
			continue;
		if (XSTed_viewer_label((unsigned)caps[i].vk, caps[i].mac, modMask, commandDown,
				       xbuf, sizeof xbuf))
			xname = [NSString stringWithUTF8String:xbuf];
		if (caps[i].vk == 0x3B || caps[i].vk == 0x3E)
			heldKey = (modMask & 2) != 0;
		if (caps[i].vk == 0x3A || caps[i].vk == 0x3D)
			heldKey = (modMask & 4) != 0;
		if (idle && (caps[i].vk == 0x3B || caps[i].vk == 0x3E ||
			     caps[i].vk == 0x3A || caps[i].vk == 0x3D))
			promptKey = 1;

		if (heldKey)
			[held setFill];
		else if (xname != nil)
			[mappedFill setFill];
		else
			[keyFill setFill];
		path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, promptKey ? 1.5 : 0.5, promptKey ? 1.5 : 0.5)
							xRadius:4 yRadius:4];
		[path fill];
		if (promptKey) {
			[promptStroke setStroke];
			[path setLineWidth:2.5];
		} else {
			[stroke setStroke];
			[path setLineWidth:1];
		}
		[path stroke];

		native = [NSString stringWithUTF8String:caps[i].legend];
		if (xname != nil && native != nil &&
		    [xname caseInsensitiveCompare:native] == NSOrderedSame)
			native = nil;
		subFont = [NSFont systemFontOfSize:MAX(8.0, scale * 0.16)];
		mainFont = [NSFont boldSystemFontOfSize:MAX(9.0, scale * 0.22)];
		if (xname != nil)
			[self drawLabel:xname inRect:r font:mainFont color:textColor
				top:(native != nil && [native length] > 0)];
		if (native != nil && [native length] > 0) {
			if (xname != nil)
				[self drawLabel:native inRect:r font:subFont color:subColor top:NO above:YES];
			else
				[self drawLabel:native inRect:r font:mainFont color:textColor top:NO];
		}
	}
}

- (void)drawLabel:(NSString *)text inRect:(NSRect)r font:(NSFont *)font color:(NSColor *)color
	      top:(BOOL)low above:(BOOL)above
{
	CGFloat size = [font pointSize];
	NSFont *f = font;
	NSDictionary *attr;
	NSSize sz;
	CGFloat y;
	BOOL bold = [[font fontName] rangeOfString:@"Bold"].location != NSNotFound ||
		(font.fontDescriptor.symbolicTraits & NSFontBoldTrait) != 0;

	[NSGraphicsContext saveGraphicsState];
	NSRectClip(NSInsetRect(r, 1, 1));
	while (size > 6.5 && [text sizeWithAttributes:
		[NSDictionary dictionaryWithObject:f forKey:NSFontAttributeName]].width > r.size.width - 4) {
		size -= 0.5;
		f = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
	}
	attr = [NSDictionary dictionaryWithObjectsAndKeys:
		f, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
	sz = [text sizeWithAttributes:attr];
	if (above)
		y = r.origin.y + r.size.height - sz.height - 2;
	else if (low)
		y = r.origin.y + 2;
	else
		y = r.origin.y + (r.size.height - sz.height) / 2;
	[text drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) / 2, y)
		withAttributes:attr];
	[NSGraphicsContext restoreGraphicsState];
}

- (void)drawLabel:(NSString *)text inRect:(NSRect)r font:(NSFont *)font color:(NSColor *)color
	      top:(BOOL)low
{
	[self drawLabel:text inRect:r font:font color:color top:low above:NO];
}

- (void)dealloc
{
	[product release];
	[super dealloc];
}

@end

/* A click on the title bar must not take the key window from STed2,
   or the emulator stays in its key wait. */
@interface KeyboardViewerPanel : NSPanel
@end

@implementation KeyboardViewerPanel

- (BOOL)canBecomeKeyWindow
{
	return NO;
}

@end

static void
ensure_panel(void)
{
	KeyboardMapView *view;
	NSRect frame;

	if (sPanel != nil)
		return;
	frame = NSMakeRect(0, 0, 980, 340);
	sPanel = [[KeyboardViewerPanel alloc] initWithContentRect:frame
					    styleMask:(NSWindowStyleMaskTitled |
						       NSWindowStyleMaskClosable |
						       NSWindowStyleMaskResizable |
						       NSWindowStyleMaskUtilityWindow |
						       NSWindowStyleMaskNonactivatingPanel)
					      backing:NSBackingStoreBuffered
						defer:NO];
	[sPanel setFloatingPanel:YES];
	[sPanel setBecomesKeyOnlyIfNeeded:YES];
	[sPanel setHidesOnDeactivate:NO];
	[sPanel setLevel:NSFloatingWindowLevel];
	[sPanel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
		NSWindowCollectionBehaviorFullScreenAuxiliary];
	[sPanel setReleasedWhenClosed:NO];
	[sPanel setMinSize:NSMakeSize(520, 200)];
	view = [[KeyboardMapView alloc] initWithFrame:frame];
	[view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[sPanel setContentView:view];
	sMap = view;
	[view release];
	/* MRC: the monitor is removed when this object is released. */
	sMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
		(NSEventMaskFlagsChanged | NSEventMaskKeyDown)
		handler:^NSEvent *(NSEvent *ev) {
			KeyboardViewerNoteFlags([ev modifierFlags]);
			return ev;
		}] retain];
}

void
KeyboardViewerNoteFlags(unsigned long flags)
{
	if (sMap == nil || sPanel == nil || ![sPanel isVisible])
		return;
	[sMap noteModifiers:(NSEventModifierFlags)flags];
}

void
KeyboardViewerShow(void)
{
	float key = 48;
	NSRect vis;

	ensure_panel();
	[sPanel setTitle:NSLocalizedString(@"Keyboard Viewer", @"Help menu")];
	[sMap noteModifiers:[NSEvent modifierFlags]];
	[sMap refreshHardware];
	if (![sPanel isVisible]) {
		NSSize unit = [sMap unitSize];
		NSSize sz = NSMakeSize(unit.width * key + 28, unit.height * key + 92);
		vis = [[NSScreen mainScreen] visibleFrame];
		if (sz.width > vis.size.width * 0.92)
			sz.width = vis.size.width * 0.92;
		if (sz.height > vis.size.height * 0.55)
			sz.height = vis.size.height * 0.55;
		[sPanel setContentSize:sz];
		[sPanel center];
	}
	[sPanel orderFront:nil];
}
