/*
 * sv_subscriber_example.c
 *
 * Example program for Sampled Values (SV) subscriber
 *
 */

#include <stdint.h>  // For `uint8_t`, `int8_t`, etc.
#include "hal_thread.h"
#include <signal.h>
#include <stdio.h>
#include "sv_subscriber.h"
#include <pthread.h>

#define SMP_S 4800
#define MAX_BUF 2*SMP_S

struct Buffer{
  int32_t data[MAX_BUF];
  int pos;
};

static struct Buffer buf_vm;
static struct Buffer buf_az;
static struct Buffer buf_br;
static struct Buffer buf_r;


static int sync = 0;
static int smp_cnt = 0;


static bool running = true;

void sigint_handler(int signalId)
{
    running = 0;
}

void * pthread_ptp(void * argument) {
  char *interface = argument;
  if (!argument) return (void*)"Error in param";

  EthernetSocket sock  = Ethernet_createSocket(interface, NULL);
  if (!sock) return (void*)"error in socket";

  Ethernet_setProtocolFilter(sock, 0x88f7);

  EthernetHandleSet hs = EthernetHandleSet_new();
  EthernetHandleSet_addSocket(hs, sock);
  uint8_t buffer[1518];

  while (running){
    switch (EthernetHandleSet_waitReady(hs, 100)){
      case -1: printf("hs fail"); break;
      case 0: break;
      default:{
       int packet_size = Ethernet_receivePacket(sock, buffer, 1518);
       //printf("%d\n", packet_size);
       if (packet_size > 57) {
	       if (buffer[14] == 0x00 || buffer[14] == 0x08){
            uint64_t corr_ns = 0;
		       corr_ns = buffer[27] | buffer[26]<<8 | buffer[25]<<16 |
              buffer[24] << 24 | buffer[23]<<32 | buffer[22] << 48;
           uint64_t sec = 0;
		       sec = buffer[53] | buffer[52]<<8 | buffer[51]<<16 |
              buffer[50] << 24 | buffer[49]<<32 | buffer[48] << 48;
		       uint32_t ns = buffer[57] | buffer[56]<<8 | buffer[55]<<16 | buffer[54] << 24;
          if (sec > 0){
              ns += corr_ns;
              sync = 4800 * (float) ns/1000000000;
		       //printf("%fs ",(float) ns/1000000000);
		      //smp_cnt = 4800 * (float) ns/1000000000;
		      //printf("smpCnt = %d\n", smp_cnt);
              printf("corr_ns=%llu\tsec=%llu\tns=%llu\tsmp=%d\n", corr_ns, sec, ns, sync);
		      //sync = 1;
      }}}}
    }
  }


  Ethernet_destroySocket(sock);
}


/* Callback handler for received SV messages */
static void
svUpdateListener (SVSubscriber subscriber, void* parameter, SVSubscriber_ASDU asdu)
{
    //printf("svUpdateListener called\n");
    struct Buffer *buf = parameter; 
    const char* svID = SVSubscriber_ASDU_getSvId(asdu);

    //if (svID != NULL)
      //  printf("  svID=(%s)\n", svID);

    smp_cnt = SVSubscriber_ASDU_getSmpCnt(asdu);
    buf->data[buf->pos] = 0;
      
    if (buf->pos % SMP_S != smp_cnt){
      printf ("pos=%d, cnt=%d\n", buf->pos, smp_cnt);
      buf->pos = smp_cnt;
    }

    //printf("  confRev: %u\n", SVSubscriber_ASDU_getConfRev(asdu));

    /*
     * Access to the data requires a priori knowledge of the data set.
     * For this example we assume a data set consisting of FLOAT32 values.
     * A FLOAT32 value is encoded as 4 bytes. You can find the first FLOAT32
     * value at byte position 0, the second value at byte position 4, the third
     * value at byte position 8, and so on.
     *
     * To prevent damages due configuration, please check the length of the
     * data block of the SV message before accessing the data.
     */
    if (SVSubscriber_ASDU_getDataSize(asdu) >= 96) {
        buf->data[buf->pos] = SVSubscriber_ASDU_getINT32(asdu, 0);
        if (smp_cnt > 3400 && smp_cnt < 3500) printf("%s\t%d:\t%d\n", svID, buf->pos, buf->data[buf->pos]);
        //printf("   DATA[1]: %f\n", SVSubscriber_ASDU_getINT32(asdu, 8));
    }
    buf->pos++;
    if (buf->pos >= MAX_BUF) buf->pos = 0;
}

int
main(int argc, char** argv)
{
    SVReceiver receiver = SVReceiver_create();
    memset(buf_vm.data, 0, MAX_BUF*sizeof(int32_t));
    memset(buf_az.data, 0, MAX_BUF*sizeof(int32_t));
    memset(buf_br.data, 0, MAX_BUF*sizeof(int32_t));
    memset(buf_r.data, 0, MAX_BUF*sizeof(int32_t));

 

    char *interface;

    if (argc > 1) {
      SVReceiver_setInterfaceId(receiver, argv[1]);
		  printf("Set interface id: %s\n", argv[1]);
      interface = argv[1];
    }
    else {
      printf("Using interface eth0\n");
      SVReceiver_setInterfaceId(receiver, "eth0");
      interface = "eth0";
    }
    pthread_t ptp_r;
    int retcode = pthread_create(&ptp_r, NULL, pthread_ptp, (void*)interface);
    if (retcode != 0){
      printf("Failed to create ptp. retcode = %i: %s\n", retcode, strerror(retcode));
    }
 

    /* Create a subscriber listening to SV messages with APPID 4000h */
    SVSubscriber subs_vm = SVSubscriber_create(NULL, 0x5409);
    SVSubscriber subs_az = SVSubscriber_create(NULL, 0x540b);
    SVSubscriber subs_br = SVSubscriber_create(NULL, 0x540d);
    SVSubscriber subs_r = SVSubscriber_create(NULL, 0x540f);

    /* Install a callback handler for the subscriber */
    SVSubscriber_setListener(subs_vm, svUpdateListener, &buf_vm);
    SVSubscriber_setListener(subs_az, svUpdateListener, &buf_az);
    SVSubscriber_setListener(subs_br, svUpdateListener, &buf_br);
    SVSubscriber_setListener(subs_r, svUpdateListener, &buf_r);

    /* Connect the subscriber to the receiver */
    SVReceiver_addSubscriber(receiver, subs_vm);
    SVReceiver_addSubscriber(receiver, subs_az);
    SVReceiver_addSubscriber(receiver, subs_br);
    SVReceiver_addSubscriber(receiver, subs_r);

    /* Start listening to SV messages - starts a new receiver background thread */
    SVReceiver_start(receiver);

    if (SVReceiver_isRunning(receiver)) {
        signal(SIGINT, sigint_handler);

        while (running){ /* infinite loop */}
            //Thread_sleep(1);

        /* Stop listening to SV messages */
        SVReceiver_stop(receiver);
    }
    else {
        printf("Failed to start SV subscriber. Reason can be that the Ethernet interface doesn't exist or root permission are required.\n");
    }

    /* Cleanup and free resources */
    SVReceiver_destroy(receiver);
    return 0;
}
