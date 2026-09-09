#include "frame_parser.h"
#include "frame_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t accepted,expected_seq;
static void receive(const ParsedFrame *f,void *ctx){(void)ctx;assert(f->sequence==expected_seq);assert(f->command==1);accepted++;}
int main(void){
    FrameParser p;uint8_t payload[32],wire[64];uint32_t seed=20260909,expected=0,tick=0,i,k;uint16_t n;
    FrameParser_Init(&p);
    for(i=0;i<10000;i++){
        seed=1664525U*seed+1013904223U;memset(payload,0x31,sizeof(payload));payload[5]=0xaa;payload[6]=0x55;
        expected_seq=i+1;n=Frame_Encode(expected_seq,1,payload,sizeof(payload),wire,sizeof(wire));assert(n);
        if(seed%4==0){wire[n-1]^=1;}
        else if(seed%4==1){wire[2]=0xff;wire[3]=0xff;}
        else expected++;
        for(k=0;k<n;k++){FrameParser_PushByte(&p,wire[k],tick,receive,0);if((k%7)==0)tick++;}
        /* End a malformed/truncated candidate before the next independent case. */
        tick+=101;FrameParser_Service(&p,tick);
    }
    assert(accepted==expected && p.stats.crc_errors && p.stats.length_errors);
    expected_seq=10001;n=Frame_Encode(expected_seq,1,payload,32,wire,sizeof(wire));
    for(k=0;k<n;k++)FrameParser_PushByte(&p,wire[k],tick,receive,0);
    assert(accepted==expected+1);
    printf("week5 fixed-seed 10000-case C parser matrix: %lu valid, zero invalid execution, recovery PASS\n",(unsigned long)expected);
    return 0;
}
