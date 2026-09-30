// Native AAC -> metadata gate -> AudioQueue decode, separate macOS test only.
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

static OSStatus readFile(void *user, SInt64 position, UInt32 request, void *out, UInt32 *actual) {
    FILE *f=user;
    if(fseeko(f,position,SEEK_SET)) return -1;
    *actual=(UInt32)fread(out,1,request,f); return ferror(f)?-1:0;
}
static SInt64 fileSize(void *user) {
    FILE *f=user; off_t old=ftello(f); fseeko(f,0,SEEK_END); off_t size=ftello(f); fseeko(f,old,SEEK_SET); return size;
}
static void consumed(void *user, AudioQueueRef q, AudioQueueBufferRef b) { (void)user; (void)q; (void)b; }
#define CHECK(call) do { OSStatus status=(call); printf("%s: %d\n",#call,status); if(status) {result=1; goto done;} } while(0)
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    AudioFileID file=NULL; AudioQueueRef q=NULL; void *cookie=NULL;
    int result=0; AudioStreamBasicDescription fmt={0}; UInt32 size=sizeof(fmt);
    CHECK(AudioFileOpenWithCallbacks(f,readFile,NULL,fileSize,NULL,kAudioFileM4AType,&file));
    CHECK(AudioFileGetProperty(file,kAudioFilePropertyDataFormat,&size,&fmt));
    // Reproduce the Wwise channel-layout metadata gate before creating a queue.
    UInt32 channelSize=0;
    CHECK(AudioFileGetPropertyInfo(file,kAudioFilePropertyChannelLayout,&channelSize,NULL));
    if(channelSize!=sizeof(AudioChannelLayout)) {result=1;goto done;}
    AudioChannelLayout sourceLayout={0};
    CHECK(AudioFileGetProperty(file,kAudioFilePropertyChannelLayout,&channelSize,&sourceLayout));
    if(sourceLayout.mChannelLayoutTag!=kAudioChannelLayoutTag_Mono) {result=1;goto done;}
    CHECK(AudioQueueNewOutput(&fmt,consumed,NULL,NULL,NULL,0,&q));
    CHECK(AudioFileGetPropertyInfo(file,kAudioFilePropertyMagicCookieData,&size,NULL));
    cookie=malloc(size);
    CHECK(AudioFileGetProperty(file,kAudioFilePropertyMagicCookieData,&size,cookie));
    CHECK(AudioQueueSetProperty(q,kAudioQueueProperty_MagicCookie,cookie,size));
    AudioChannelLayout layout={.mChannelLayoutTag=kAudioChannelLayoutTag_Mono};
    CHECK(AudioQueueSetProperty(q,kAudioQueueProperty_ChannelLayout,&layout,sizeof(layout)));
    AudioStreamBasicDescription pcm={.mSampleRate=fmt.mSampleRate,.mFormatID=kAudioFormatLinearPCM,
        .mFormatFlags=kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,.mBytesPerPacket=2,
        .mFramesPerPacket=1,.mBytesPerFrame=2,.mChannelsPerFrame=1,.mBitsPerChannel=16};
    CHECK(AudioQueueSetOfflineRenderFormat(q,&pcm,&layout));
    // An intentionally invalid property verifies capture and unchanged error propagation.
    OSStatus invalid=AudioQueueSetProperty(q,0x54455354,&size,sizeof(size));
    printf("invalid property status: %d\n",invalid); if(!invalid) {result=1; goto done;}
    UInt32 upper=0;size=sizeof(upper);
    CHECK(AudioFileGetProperty(file,kAudioFilePropertyPacketSizeUpperBound,&size,&upper));
    AudioQueueBufferRef input=NULL,output=NULL;
    CHECK(AudioQueueAllocateBufferWithPacketDescriptions(q,upper*8,8,&input));
    UInt32 bytes=upper*8,packets=8;
    CHECK(AudioFileReadPackets(file,false,&bytes,input->mPacketDescriptions,0,&packets,input->mAudioData));
    input->mAudioDataByteSize=bytes;
    CHECK(AudioQueueEnqueueBufferWithParameters(q,input,packets,input->mPacketDescriptions,0,0,0,NULL,NULL,NULL));
    CHECK(AudioQueueAllocateBuffer(q,4096,&output));
    CHECK(AudioQueueStart(q,NULL));
    AudioTimeStamp ts={.mFlags=kAudioTimeStampSampleTimeValid};
    double peak=0;
    for(unsigned pass=0;pass<8;pass++) {
        ts.mSampleTime=pass*1024.0;
        CHECK(AudioQueueOfflineRender(q,&ts,output,1024));
        for(unsigned i=0;i<output->mAudioDataByteSize/2;i++) {double v=fabs(((int16_t*)output->mAudioData)[i]/32768.0); if(v>peak) peak=v;}
        if(peak>0) break;
    }
    printf("render bytes=%u peak=%g\n",output->mAudioDataByteSize,peak);
    if(!output->mAudioDataByteSize || peak==0) result=1;
done:
    if(q) AudioQueueDispose(q,true);
    if(file) AudioFileClose(file);
    free(cookie); fclose(f);
    return result;
}
