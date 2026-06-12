/*
 * sv_subscriber_example.c
 *
 * Example program for Sampled Values (SV) subscriber
 *
 * to execute without sudo permission, change program's capabilities by:
 * sudo setcap "cap_sys_nice=ep cap_ipc_lock=ep cap_net_raw+eip" ./test_sv
 */

#include <stdint.h>  // For `uint8_t`, `int8_t`, etc.
#include "hal_thread.h"
#include <signal.h>
#include <stdio.h>
#include "sv_subscriber.h"
#include <pthread.h>
#include "goose_receiver.h"
#include "goose_subscriber.h"
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
/* shared memory */
#include <sys/mman.h>





void set_nonblocking(int non) {
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if(non)
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
  else
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

#define SMP_S 4800
#define MAX_BUF 2*SMP_S

struct Buffer{
  int AppId;
  int ds_size;
  int *ds_idx;
  int payload_size;
  int smp_max;
  int buf_max;
  int32_t *data;
  int32_t *pos;
  int lock;
};



static int sync = 0;
static int smp_cnt = 0;
static int trg_smp_cnt = 0;
static int trigged = 0;
static int last_buf_pos = 0;
static int start_buf_pos = 0;
static int end_buf_pos = 0;
static int osc_complete = 0;


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
              //printf("corr_ns=%llu\tsec=%llu\tns=%llu\tsmp=%d\n", corr_ns, sec, ns, sync);
              //sync = 1;
            }
          }
        }
      }
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
  //const char* svID = SVSubscriber_ASDU_getSvId(asdu);
  int app_id = buf->AppId;

  //if (svID != NULL)
  //  printf("  svID=(%s)\n", svID);

  smp_cnt = SVSubscriber_ASDU_getSmpCnt(asdu);
  //buf->data[*(buf->pos)] = 0;

  if (*(buf->pos) % buf->smp_max != smp_cnt){
    printf ("pos=%d, cnt=%d\n", *(buf->pos), smp_cnt);
    *(buf->pos) = smp_cnt;
  }

  last_buf_pos = *(buf->pos);

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
  //printf("payload = %d \n", SVSubscriber_ASDU_getDataSize(asdu));
  if (SVSubscriber_ASDU_getDataSize(asdu) >= buf->payload_size) {
    if(!trigged || !buf->lock){
      int i;
      for (i = 0; i < buf->ds_size; i++){
        int idx = *(buf->pos) * buf->ds_size + i;
        buf->data[idx] = SVSubscriber_ASDU_getINT32(asdu, buf->ds_idx[i]);
        printf("%d,", buf->data[idx]);
      }
      printf("\n");
      //if (app_id == 0x540d) printf("%s\t%d:\t%d\n", svID, *(buf->pos), buf->data[*(buf->pos)]);
      //printf("   DATA[1]: %f\n", SVSubscriber_ASDU_getINT32(asdu, 8));
    }
  }
  if(trigged && *(buf->pos) == end_buf_pos){
    osc_complete++;
    buf->lock = 1;
  }
  *(buf->pos) = *(buf->pos) + 1;
  if (*(buf->pos) >= buf->buf_max) *(buf->pos) = 0;
}

void
gooseListener(GooseSubscriber subscriber, void* parameter)
{


  int appId = GooseSubscriber_getAppId(subscriber);

  if (appId == 0x1807){
    /*
    printf("GOOSE event:\n");
    printf("  vlanTag: %s\n", GooseSubscriber_isVlanSet(subscriber) ? "found" : "NOT found");
    if (GooseSubscriber_isVlanSet(subscriber))
    {
        printf("    vlanId: %u\n", GooseSubscriber_getVlanId(subscriber));
        printf("    vlanPrio: %u\n", GooseSubscriber_getVlanPrio(subscriber));
    }
    printf("  appId: %d\n", GooseSubscriber_getAppId(subscriber));
    uint8_t macBuf[6];
    GooseSubscriber_getSrcMac(subscriber,macBuf);
    printf("  srcMac: %02X:%02X:%02X:%02X:%02X:%02X\n", macBuf[0],macBuf[1],macBuf[2],macBuf[3],macBuf[4],macBuf[5]);
    GooseSubscriber_getDstMac(subscriber,macBuf);
    printf("  dstMac: %02X:%02X:%02X:%02X:%02X:%02X\n", macBuf[0],macBuf[1],macBuf[2],macBuf[3],macBuf[4],macBuf[5]);
    printf("  goId: %s\n", GooseSubscriber_getGoId(subscriber));
    printf("  goCbRef: %s\n", GooseSubscriber_getGoCbRef(subscriber));
    printf("  dataSet: %s\n", GooseSubscriber_getDataSet(subscriber));
    printf("  confRev: %u\n", GooseSubscriber_getConfRev(subscriber));
    printf("  ndsCom: %s\n", GooseSubscriber_needsCommission(subscriber) ? "true" : "false");
    printf("  simul: %s\n", GooseSubscriber_isTest(subscriber) ? "true" : "false");
    printf("  stNum: %u sqNum: %u\n", GooseSubscriber_getStNum(subscriber),
            GooseSubscriber_getSqNum(subscriber));
    printf("  timeToLive: %u\n", GooseSubscriber_getTimeAllowedToLive(subscriber));

    uint64_t timestamp = GooseSubscriber_getTimestamp(subscriber);

    printf("  timestamp: %u.%u\n", (uint32_t) (timestamp / 1000), (uint32_t) (timestamp % 1000));
    printf("  message is %s\n", GooseSubscriber_isValid(subscriber) ? "valid" : "INVALID");

*/
    MmsValue* values = GooseSubscriber_getDataSetValues(subscriber);
    MmsValue* trig_v = MmsValue_getElement(values, 8);
    int trigger = MmsValue_getBoolean(trig_v);
    if (trigger) {
      printf ("FALTA++++++++++++++++++++++++++++++++++++++++++\n");
      if (!trigged){
        trigged = 1;
        trg_smp_cnt = last_buf_pos;
        start_buf_pos = last_buf_pos - 1440;
        if(start_buf_pos < 0) start_buf_pos = MAX_BUF + start_buf_pos;
        end_buf_pos = last_buf_pos - 1441;
        if(end_buf_pos < 0) end_buf_pos = MAX_BUF + end_buf_pos;
        printf("start = %d, end = %d\n", start_buf_pos, end_buf_pos);
      }
    }

    /*
    char buffer[1024];

    MmsValue_printToBuffer(values, buffer, 1024);

    printf("  AllData: %s\n", buffer);
    MmsValue_printToBuffer(trig_v, buffer, 1024);

    printf("  trig: %s\n", buffer); */
  }
}

int
main(int argc, char** argv)
{
  SVReceiver SV_receiver = SVReceiver_create();

  GooseReceiver receiver = GooseReceiver_create();

  
  char *interface;

  if (argc > 1) {
    SVReceiver_setInterfaceId(SV_receiver, argv[1]);
    GooseReceiver_setInterfaceId(receiver, argv[1]);
    printf("Set interface id: %s\n", argv[1]);
    interface = argv[1];
  }
  else {
    printf("Using interface eth0\n");
    SVReceiver_setInterfaceId(SV_receiver, "eth0");
    GooseReceiver_setInterfaceId(receiver, "eth0");
    interface = "eth0";
  }
  pthread_t ptp_r;
  int retcode = pthread_create(&ptp_r, NULL, pthread_ptp, (void*)interface);
  if (retcode != 0){
    printf("Failed to create ptp. retcode = %i: %s\n", retcode, strerror(retcode));
  }
  
  /* Alloc resources */
  
  struct Buffer buf_stream1;

  buf_stream1.AppId = 0x5402;
  buf_stream1.ds_size = 8; /* 4 currents and 4 voltages */
  buf_stream1.ds_idx = (int[]){0, 8, 16, 24, 32, 40, 48, 56}; /* index of each channel in dataSet */ 
  buf_stream1.payload_size = 64;
  buf_stream1.smp_max = 4800;
  buf_stream1.buf_max = 9600;
  //buf_stream1.data = calloc(buf_stream1.buf_max * buf_stream1.ds_size, sizeof(int32_t));
  //buf_stream1.pos = 0;
  buf_stream1.lock = 0;
  
  /* shared memory */
  int oflags = O_RDWR | O_CREAT;  //O_RDONLY
	int length = (1 + buf_stream1.buf_max * buf_stream1.ds_size) * sizeof(int32_t);
	char *name = "/sv_subscriber_stream1";
	int fd = shm_open(name, oflags, 0644 );
  if (fd < 1) return 1; /* error */
	ftruncate(fd, length);
  int32_t *ptr = (int32_t *) mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  memset(ptr, 0, length); /* init all with 0 */
  buf_stream1.pos = &ptr[0];
  buf_stream1.data = &ptr[1];
  /*
  memset(buf_vm.data, 0, MAX_BUF*sizeof(int32_t));
  memset(buf_az.data, 0, MAX_BUF*sizeof(int32_t));
  memset(buf_br.data, 0, MAX_BUF*sizeof(int32_t));
  memset(buf_r.data, 0, MAX_BUF*sizeof(int32_t));


  buf_vm.lock = 0;
  buf_az.lock = 0;
  buf_br.lock = 0;
  buf_r.lock = 0;
  */

  /* Create a subscriber listening to SV messages with APPID 4000h */
  SVSubscriber subs_stream1 = SVSubscriber_create(NULL, buf_stream1.AppId);
  /*SVSubscriber subs_az = SVSubscriber_create(NULL, 0x540b);
  SVSubscriber subs_br = SVSubscriber_create(NULL, 0x540d);
  SVSubscriber subs_r = SVSubscriber_create(NULL, 0x540f);

  buf_vm.AppId = 0x5409;
  buf_az.AppId = 0x540b;
  buf_br.AppId = 0x540d;
  buf_r.AppId = 0x540f;
  */
  /* Install a callback handler for the subscriber */
  SVSubscriber_setListener(subs_stream1, svUpdateListener, &buf_stream1);
  /*
  SVSubscriber_setListener(subs_az, svUpdateListener, &buf_az);
  SVSubscriber_setListener(subs_br, svUpdateListener, &buf_br);
  SVSubscriber_setListener(subs_r, svUpdateListener, &buf_r);
  */
  /* Connect the subscriber to the receiver */
  SVReceiver_addSubscriber(SV_receiver, subs_stream1);
  /*
  SVReceiver_addSubscriber(SV_receiver, subs_az);
  SVReceiver_addSubscriber(SV_receiver, subs_br);
  SVReceiver_addSubscriber(SV_receiver, subs_r);
  */


  GooseSubscriber subscriber = GooseSubscriber_create("", NULL);
  GooseSubscriber_setObserver(subscriber);
  GooseSubscriber_setListener(subscriber, gooseListener, NULL);

  GooseReceiver_addSubscriber(receiver, subscriber);

  GooseReceiver_start(receiver);





  /* Start listening to SV messages - starts a new receiver background thread */
  SVReceiver_start(SV_receiver);
  set_nonblocking(1);

  if (SVReceiver_isRunning(SV_receiver) && GooseReceiver_isRunning(receiver)) {
    signal(SIGINT, sigint_handler);

    while (running){ /* infinite loop */
    #if(0)
      if(osc_complete > 3){
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);

        char b_time[50];
        // Example: 2024-05-14 10:45:30
        strftime(b_time, 50, "osc_%Y-%m-%d_%H-%M-%S.csv", tm_info);
        printf("================= OSCILO COMPLETA %s ===============\n", b_time);

        // 1. Create/Open a file in "w" (write) or "a" (append) mode
        FILE *fptr = fopen(b_time, "w");
        FILE *fptr2 = fopen("data.csv", "w");

        if (fptr == NULL || fptr2 == NULL) {
          printf("Error opening file!\n");
        } else {

          // 2. Write headers (optional)
          fprintf(fptr, "Amostra;Vm;Az;Br;Res\n");
          fprintf(fptr2, "Amostra;Vm;Az;Br;Res\n");

          // 3. Write data rows
          int i = 0;
          for (i = 0; i < MAX_BUF; i++){
            int pos = start_buf_pos + i;
            if (!(pos < MAX_BUF)) pos -= MAX_BUF;

            fprintf(fptr, "%d;%d;%d;%d;%d\n", i, buf_vm.data[pos], buf_az.data[pos], buf_br.data[pos], buf_r.data[pos]);
            fprintf(fptr2, "%d;%d;%d;%d;%d\n", i, buf_vm.data[pos], buf_az.data[pos], buf_br.data[pos], buf_r.data[pos]);
          }
          // 4. Close the file to save and free memory
          fclose(fptr);
          fclose(fptr2);
        } 
        Thread_sleep(1000);

        buf_vm.lock = 0;
        buf_az.lock = 0;
        buf_br.lock = 0;
        buf_r.lock = 0;
        osc_complete = 0;
        trigged = 0;
      }

      char tecla = getchar();
      if (tecla == 'x') {
        printf ("------------- Manual trigger ---------------\n");
        if (!trigged){
          trigged = 1;
          trg_smp_cnt = last_buf_pos;
          start_buf_pos = last_buf_pos - 1440;
          if(start_buf_pos < 0) start_buf_pos = MAX_BUF + start_buf_pos;
          end_buf_pos = last_buf_pos - 1441;
          if(end_buf_pos < 0) end_buf_pos = MAX_BUF + end_buf_pos;
          printf("start = %d, end = %d\n", start_buf_pos, end_buf_pos);
        }
      }
    #endif
      Thread_sleep(50);
    }
    /* Stop listening to SV messages */
    SVReceiver_stop(SV_receiver);
  }
  else {
    printf("Failed to start SV subscriber. Reason can be that the Ethernet interface doesn't exist or root permission are required.\n");
  }
  set_nonblocking(0);
  GooseReceiver_stop(receiver);

  GooseReceiver_destroy(receiver);

  /* Cleanup and free resources */
  SVReceiver_destroy(SV_receiver);
  //free(buf_stream1.data);
  munmap(ptr, length); // needed ?
  close(fd);
  shm_unlink(name); //needed ?

  return 0;
}
