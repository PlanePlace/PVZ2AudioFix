// SPDX-License-Identifier: MIT
// PVZ2 ARM64, version-unrestricted experimental audio compatibility patch.
// Same fallback as the device-confirmed v2 probe, without its decoder hooks.
#include <AudioToolbox/AudioToolbox.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_prot.h>
#include <dlfcn.h>
#include <os/log.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(AudioChannelLayout) == 32, "Reviewed layout size changed");
static intptr_t imageSlide;
static __typeof__(&AudioFileGetProperty) nativeGet;
static __typeof__(&AudioFileGetPropertyInfo) nativeInfo;
static atomic_uint notices;
static os_log_t logger;

static bool knownMonoAAC(AudioFileID file) {
    AudioStreamBasicDescription format = {0};
    UInt32 size = sizeof(format);
    return nativeGet(file, kAudioFilePropertyDataFormat, &size, &format) == noErr &&
           size == sizeof(format) && format.mFormatID == kAudioFormatMPEG4AAC &&
           format.mChannelsPerFrame == 1 &&
           (format.mSampleRate == 8000 || format.mSampleRate == 11025);
}

static void notice(bool info) {
    // At most four notices per launch. No timers, files, PCM reads or queues.
    if (atomic_fetch_add_explicit(&notices, 1, memory_order_relaxed) < 4 && logger)
        os_log_with_type(logger, OS_LOG_TYPE_DEFAULT,
                         "PVZ2AudioFix 1.1.0: mono layout fallback (%{public}s)",
                         info ? "info" : "get");
}

static OSStatus finishGet(AudioFileID file, AudioFilePropertyID property,
                          UInt32 capacity, UInt32 *size, void *output,
                          OSStatus result) {
    if (property == kAudioFilePropertyChannelLayout &&
        result == kAudioFileBadPropertySizeError && size && output &&
        capacity >= sizeof(AudioChannelLayout) &&
        knownMonoAAC(file)) {
        AudioChannelLayout layout = {0};
        layout.mChannelLayoutTag = kAudioChannelLayoutTag_Mono;
        memcpy(output, &layout, sizeof(layout));
        *size = sizeof(layout);
        notice(false);
        return noErr;
    }
    return result;
}

static OSStatus finishInfo(AudioFileID file, AudioFilePropertyID property,
                           UInt32 *size, UInt32 *writable,
                           OSStatus result) {
    if (property == kAudioFilePropertyChannelLayout &&
        result == kAudioFileBadPropertySizeError && size &&
        knownMonoAAC(file)) {
        *size = sizeof(AudioChannelLayout);
        if (writable) *writable = 0;
        notice(true);
        return noErr;
    }
    return result;
}

static OSStatus fixedGet(AudioFileID file, AudioFilePropertyID property,
                         UInt32 *size, void *output) {
    UInt32 capacity = size ? *size : 0;
    OSStatus result = nativeGet(file, property, size, output);
    return finishGet(file, property, capacity, size, output, result);
}

static OSStatus fixedInfo(AudioFileID file, AudioFilePropertyID property,
                          UInt32 *size, UInt32 *writable) {
    OSStatus result = nativeInfo(file, property, size, writable);
    return finishInfo(file, property, size, writable, result);
}

typedef struct { void **slot; void *replacement; unsigned kind; } Patch;

static bool anotherAudioModule(void) {
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *path = _dyld_get_image_name(i);
        if (!path) continue;
        const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
        if (!strcmp(name, "PVZAudioProbe.dylib")) return true;
    }
    return false;
}

static void installFix(void) {
    // Injection loaders need not enumerate the executable at image index zero.
    const struct mach_header_64 *header = NULL;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const struct mach_header *candidate = _dyld_get_image_header(i);
        if (candidate && candidate->magic == MH_MAGIC_64 && candidate->filetype == MH_EXECUTE) {
            header = (const void *)candidate;
            imageSlide = _dyld_get_image_vmaddr_slide(i);
            break;
        }
    }
    if (!header) return;
    const struct symtab_command *sym = NULL;
    const struct dysymtab_command *dysym = NULL;
    const struct segment_command_64 *linkedit = NULL;
    const char *end = (const char *)(header + 1) + header->sizeofcmds;
    const struct load_command *command = (const void *)(header + 1);
    for (unsigned i = 0; i < header->ncmds; ++i) {
        if ((const char *)command + sizeof(*command) > end ||
            command->cmdsize < sizeof(*command) ||
            command->cmdsize > (uintptr_t)(end - (const char *)command)) return;
        if (command->cmd == LC_SYMTAB && command->cmdsize >= sizeof(*sym)) sym = (const void *)command;
        if (command->cmd == LC_DYSYMTAB && command->cmdsize >= sizeof(*dysym)) dysym = (const void *)command;
        if (command->cmd == LC_SEGMENT_64 && command->cmdsize >= sizeof(*linkedit) &&
            !strncmp(((const struct segment_command_64 *)command)->segname, "__LINKEDIT", 16))
            linkedit = (const void *)command;
        command = (const void *)((const char *)command + command->cmdsize);
    }
    // Cross-version test: no UUID/version whitelist or fixed call-site addresses.
    // Hook only named imports in writable sections of the main ARM64 executable.
    if (!sym || !dysym || !linkedit) {
        os_log_with_type(logger, OS_LOG_TYPE_DEFAULT, "PVZ2AudioFix 1.1.0: disabled (missing Mach-O metadata)");
        return;
    }
    if (anotherAudioModule()) {
        os_log_with_type(logger, OS_LOG_TYPE_DEFAULT, "PVZ2AudioFix 1.1.0: disabled (PVZAudioProbe already present)");
        return;
    }
    nativeGet = dlsym(RTLD_DEFAULT, "AudioFileGetProperty");
    nativeInfo = dlsym(RTLD_DEFAULT, "AudioFileGetPropertyInfo");
    if (!nativeGet || !nativeInfo) return;
    uintptr_t base = imageSlide + linkedit->vmaddr - linkedit->fileoff;
    const struct nlist_64 *symbols = (const void *)(base + sym->symoff);
    const char *strings = (const void *)(base + sym->stroff);
    const uint32_t *indirect = (const void *)(base + dysym->indirectsymoff);
    Patch patches[8]; unsigned count = 0, found[2] = {0};
    command = (const void *)(header + 1);
    for (unsigned i = 0; i < header->ncmds; ++i, command = (const void *)((const char *)command + command->cmdsize)) {
        if (command->cmd != LC_SEGMENT_64) continue;
        const struct segment_command_64 *segment = (const void *)command;
        if (command->cmdsize < sizeof(*segment) ||
            segment->nsects > (command->cmdsize - sizeof(*segment)) / sizeof(struct section_64)) return;
        if (!(segment->initprot & VM_PROT_WRITE) || strncmp(segment->segname, "__DATA", 16)) continue;
        const struct section_64 *section = (const void *)(segment + 1);
        for (unsigned j = 0; j < segment->nsects; ++j, ++section) {
            unsigned type = section->flags & SECTION_TYPE;
            if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS) continue;
            void **slots = (void **)(imageSlide + section->addr);
            for (uint64_t p = 0; p < section->size / sizeof(void *); ++p) {
                uint64_t index = section->reserved1 + p;
                if (index >= dysym->nindirectsyms) continue;
                uint32_t si = indirect[index]; if (si >= sym->nsyms) continue;
                uint32_t str = symbols[si].n_un.n_strx; if (str >= sym->strsize) continue;
                const char *name = strings + str;
                if (!memchr(name, 0, sym->strsize - str)) return;
                unsigned kind;
                if (!strcmp(name, "_AudioFileGetProperty")) kind = 0;
                else if (!strcmp(name, "_AudioFileGetPropertyInfo")) kind = 1;
                else continue;
                Dl_info owner;
                if (dladdr(slots[p], &owner) && owner.dli_fname && strstr(owner.dli_fname, "PVZ2AudioFix")) {
                    os_log_with_type(logger, OS_LOG_TYPE_DEFAULT, "PVZ2AudioFix 1.1.0: disabled (fix already installed)");
                    return;
                }
                if (count >= 8) return;
                patches[count++] = (Patch){ slots + p, kind ? (void *)fixedInfo : (void *)fixedGet, kind };
                ++found[kind];
            }
        }
    }
    // Validate the complete plan before touching either pointer.
    if (!found[0] || !found[1]) {
        os_log_with_type(logger, OS_LOG_TYPE_DEFAULT, "PVZ2AudioFix 1.1.0: disabled (unexpected import slots)");
        return;
    }
    for (unsigned i = 0; i < count; ++i)
        __atomic_store_n(patches[i].slot, patches[i].replacement, __ATOMIC_RELEASE);
    os_log_with_type(logger, OS_LOG_TYPE_DEFAULT, "PVZ2AudioFix 1.1.0: enabled (%{public}u import slots; version/UUID unrestricted)", count);
}

#ifndef PVZ2_AUDIO_FIX_UNIT_TEST
__attribute__((constructor)) static void startFix(void) {
    logger = os_log_create("local.pvz2.audiofix", "compatibility");
    installFix();
}
#endif
