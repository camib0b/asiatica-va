#include <AppKit/AppKit.h>

namespace MotionPlatform {

bool operatingSystemReducedMotion() {
  return [NSWorkspace sharedWorkspace].accessibilityDisplayShouldReduceMotion == YES;
}

}  // namespace MotionPlatform
