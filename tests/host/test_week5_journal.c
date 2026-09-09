#include "week5_journal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t mem[24576],wel,selected;
static uint32_t calls,erases,writes;
static int corrupt;
static void cs(void *c,uint8_t v){(void)c;assert(v!=selected);selected=v;}
static int bus(void *c,const uint8_t *t,uint8_t *r,uint16_t n){
    uint32_t a;unsigned i;(void)c;assert(selected && n<=68);calls++;memset(r,0,n);
    a=n>=4?((uint32_t)t[1]<<16)|((uint32_t)t[2]<<8)|t[3]:0;
    switch(t[0]){
    case 0x9f:r[1]=0xef;r[2]=0x40;r[3]=0x18;break;
    case 5:r[1]=wel?2:0;break;
    case 6:wel=1;break;
    case 3:assert(a+n-4<=sizeof(mem));memcpy(r+4,mem+a,n-4);if(corrupt)r[4]^=1;break;
    case 0x20:assert(wel && a>=4096 && a+4096<=sizeof(mem));memset(mem+a,255,4096);wel=0;erases++;break;
    case 2:assert(wel && a>=4096 && a+n-4<=20480);for(i=4;i<n;i++)mem[a+i-4]&=t[i];wel=0;writes++;break;
    default:assert(0);
    }return 0;
}
static void drain(W5Journal *j){unsigned n;for(n=0;n<5000 && j->state!=W5J_READY && j->state!=W5J_ERROR;n++){uint32_t before=calls;W5Journal_Service(j,j->flash->deadline_start+n);assert(calls-before<=1);}assert(n<5000);}
int main(void){
    SpiFlash f;W5Journal j;uint8_t p[48]={0};unsigned i;
    memset(mem,0x5a,sizeof(mem));assert(!SpiFlash_Init(&f,bus,cs,0));W5Journal_Init(&j,&f);
    assert(W5Journal_Append(&j,p,0)<0);assert(W5Journal_Prepare(&j,1,20480,0)<0);
    assert(!erases);assert(!W5Journal_Prepare(&j,4096,20480,0));drain(&j);
    assert(j.state==W5J_READY && erases==5);
    for(i=0;i<180;i++){assert(!W5Journal_Append(&j,p,(i+1)*10000U));drain(&j);}
    assert(j.records==180 && writes==180 && erases==5);
    for(i=0;i<4096;i++)assert(mem[i]==0x5a);
    assert(!W5Journal_BackgroundErase(&j,0));drain(&j);assert(erases==6 && j.records==180);
    assert(!W5Journal_Append(&j,p,1810000));corrupt=1;drain(&j);
    assert(j.state==W5J_ERROR && j.records==180 && j.errors==1);
    assert(W5Journal_Append(&j,p,1820000)<0);
    puts("week5 journal: guarded prepare, bounded service, 180 readbacks, isolation, corruption PASS");return 0;
}
