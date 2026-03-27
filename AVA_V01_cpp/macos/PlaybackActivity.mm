#import <Foundation/Foundation.h>

#include "PlaybackActivity.h"

static id avaPlaybackActivityToken = nil;

void avaBeginPlaybackUserActivity(void) {
    if (avaPlaybackActivityToken != nil) {
        return;
    }
    NSProcessInfo* info = [NSProcessInfo processInfo];
    if (info == nil) {
        return;
    }
    avaPlaybackActivityToken =
        [info beginActivityWithOptions:NSActivityUserInitiated reason:@"AVA video playback"];
}

void avaEndPlaybackUserActivity(void) {
    if (avaPlaybackActivityToken == nil) {
        return;
    }
    NSProcessInfo* info = [NSProcessInfo processInfo];
    if (info != nil) {
        [info endActivity:avaPlaybackActivityToken];
    }
    avaPlaybackActivityToken = nil;
}
