// Check production fallback gates and buffer boundaries without call-site restrictions.
#define PVZ2_AUDIO_FIX_UNIT_TEST 1
#include "../src/PVZ2AudioFix.c"
#include <assert.h>
#include <stdio.h>

static AudioStreamBasicDescription format;
static OSStatus formatStatus;
static UInt32 formatSize;
static unsigned reads, checks;
static OSStatus mockGet(AudioFileID file, AudioFilePropertyID property, UInt32 *size, void *out) {
    (void)file;
    assert(property == kAudioFilePropertyDataFormat);
    ++reads;
    if (formatStatus) return formatStatus;
    memcpy(out, &format, sizeof(format)); *size = formatSize;
    return noErr;
}
static void defaults(void) {
    format = (AudioStreamBasicDescription){ .mFormatID = kAudioFormatMPEG4AAC,
        .mChannelsPerFrame = 1, .mSampleRate = 11025 };
    formatStatus = noErr; formatSize = sizeof(format); reads = 0;
}
static void getCase(bool accept, OSStatus error, AudioFilePropertyID property,
                    unsigned capacity, bool nullSize, bool nullOutput) {
    unsigned char buffer[64], before[64]; memset(buffer, 0xa5, sizeof(buffer));
    memcpy(before, buffer, sizeof(buffer)); UInt32 size = capacity;
    OSStatus result = finishGet((AudioFileID)(uintptr_t)1, property, capacity,
        nullSize ? NULL : &size, nullOutput ? NULL : buffer + 16, error);
    assert(result == (accept ? noErr : error));
    if (accept) {
        AudioChannelLayout expected = { .mChannelLayoutTag = kAudioChannelLayoutTag_Mono };
        assert(size == sizeof(expected));
        assert(!memcmp(buffer + 16, &expected, sizeof(expected)));
        assert(!memcmp(buffer, before, 16)); assert(!memcmp(buffer + 48, before + 48, 16));
    } else { assert(!memcmp(buffer, before, sizeof(buffer))); assert(size == capacity); }
    ++checks;
}
int main(void) {
    nativeGet = mockGet;
    for (unsigned rate = 0; rate < 2; ++rate) {
        defaults(); format.mSampleRate = rate ? 11025 : 8000;
        getCase(true, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
        UInt32 size = 0, writable = 7;
        assert(finishInfo((AudioFileID)(uintptr_t)1, kAudioFilePropertyChannelLayout, &size,
            &writable, kAudioFileBadPropertySizeError) == noErr);
        assert(size == 32 && writable == 0); ++checks;
    }
    defaults(); getCase(true, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 48, false, false);
    assert(reads == 1);
    getCase(false, noErr, kAudioFilePropertyChannelLayout, 32, false, false);
    getCase(false, -1234, kAudioFilePropertyChannelLayout, 32, false, false);
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyMagicCookieData, 32, false, false);
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 31, false, false);
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, true, false);
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, true);
    defaults(); format.mChannelsPerFrame = 2;
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
    defaults(); format.mSampleRate = 44100;
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
    defaults(); format.mFormatID = kAudioFormatLinearPCM;
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
    defaults(); formatStatus = -123;
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
    defaults(); formatSize = 0;
    getCase(false, kAudioFileBadPropertySizeError, kAudioFilePropertyChannelLayout, 32, false, false);
    defaults();
    for (unsigned which = 1; which < 5; ++which) {
        UInt32 size = 9, writable = 7;
        OSStatus error = which == 1 ? noErr : which == 2 ? -123 : kAudioFileBadPropertySizeError;
        OSStatus result = finishInfo((AudioFileID)(uintptr_t)1,
            which == 3 ? kAudioFilePropertyMagicCookieData : kAudioFilePropertyChannelLayout,
            which == 4 ? NULL : &size, &writable, error);
        assert(result == error && size == 9 && writable == 7); ++checks;
    }
    UInt32 size = 0;
    assert(finishInfo((AudioFileID)(uintptr_t)1, kAudioFilePropertyChannelLayout, &size,
        NULL, kAudioFileBadPropertySizeError) == noErr && size == 32); ++checks;
    for (unsigned which = 0; which < 5; ++which) {
        defaults();
        if (which == 0) format.mChannelsPerFrame = 2;
        if (which == 1) format.mSampleRate = 44100;
        if (which == 2) format.mFormatID = kAudioFormatLinearPCM;
        if (which == 3) formatStatus = -123;
        if (which == 4) formatSize = 0;
        UInt32 infoSize = 9, writable = 7;
        assert(finishInfo((AudioFileID)(uintptr_t)1, kAudioFilePropertyChannelLayout,
            &infoSize, &writable, kAudioFileBadPropertySizeError) == kAudioFileBadPropertySizeError);
        assert(infoSize == 9 && writable == 7); ++checks;
    }
    printf("PASS: %u fallback cases; formats, errors and buffer canaries; no caller whitelist\n", checks);
    return 0;
}
