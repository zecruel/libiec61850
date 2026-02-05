
#include <unistd.h>
#include <signal.h>

#include <stdbool.h> // For `true` (`1`) and `false` (`0`) macros in C
#include <stdint.h>  // For `uint8_t`, `int8_t`, etc.
#include <stdio.h>   // For `printf()`
#include <string.h>  // `strerror()`

// Local includes
#include "timinglib.h"
#include "sv_publisher.h"
#include "hal_ethernet.h"
#include "hal_socket.h"

// Linux includes
#include <pthread.h>

#include <math.h>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

static int running = 0;

struct sv_param {
  char *interface;
  char *asdu_name;
};

void handle_signal(int signal) {
  printf("Signal received: %d\n", signal);
  running = 0;
}
void * pthread_ptp(void * argument) {
  struct sv_param *param = argument;
  if (!argument) return (void*)"Error in param";

  EthernetSocket sock  = Ethernet_createSocket(param->interface, NULL);
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
       for (int i = 0; i < packet_size; i++){
	      printf("%02X ", buffer[i]);
       } printf("\n"); 
      }
    }
  }


  Ethernet_destroySocket(sock);
}

void * pthread_task(void * argument) {
  struct sv_param *param = argument;
  if (!argument) return (void*)"Error in param";

  SVPublisher svPublisher = SVPublisher_create(NULL, param->interface);
  if (!svPublisher) return (void*)"error in publisher";
  
  SVPublisher_ASDU asdu = SVPublisher_addASDU(svPublisher, param->asdu_name, NULL, 1);
  //SVPublisher_ASDU_setSmpSynch(asdu, 2);

  int amp1 = SVPublisher_ASDU_addINT32(asdu);
  int amp1q = SVPublisher_ASDU_addQuality(asdu);
  int amp2 = SVPublisher_ASDU_addINT32(asdu);
  int amp2q = SVPublisher_ASDU_addQuality(asdu);
  int amp3 = SVPublisher_ASDU_addINT32(asdu);
  int amp3q = SVPublisher_ASDU_addQuality(asdu);
  int amp4 = SVPublisher_ASDU_addINT32(asdu);
  int amp4q = SVPublisher_ASDU_addQuality(asdu);
  
  int vol1 = SVPublisher_ASDU_addINT32(asdu);
  int vol1q = SVPublisher_ASDU_addQuality(asdu);
  int vol2 = SVPublisher_ASDU_addINT32(asdu);
  int vol2q = SVPublisher_ASDU_addQuality(asdu);
  int vol3 = SVPublisher_ASDU_addINT32(asdu);
  int vol3q = SVPublisher_ASDU_addQuality(asdu);
  int vol4 = SVPublisher_ASDU_addINT32(asdu);
  int vol4q = SVPublisher_ASDU_addQuality(asdu);

  SVPublisher_ASDU_setSmpCntWrap(asdu, 4800);
  //SVPublisher_ASDU_setRefrTm(asdu, 2);
  

  SVPublisher_setupComplete(svPublisher);
  
  
  //const char* thread_name = (const char*)argument;

  // =============================================================================================
  // SET LOOP PERIOD (FREQUENCY) HERE!
  //    10 us ( 0.01 ms) --> 100 KHz
  //   100 us ( 0.10 ms) -->  10 KHz
  //  1000 us ( 1.00 ms) -->   1 KHz
  // 10000 us (10.00 ms) --> 100 Hz
  const uint64_t PERIOD_US = 208;
  // =============================================================================================
  // Seed the last wake time with the current time.
  uint64_t last_wake_time_us = micros();

  //printf("thread_name = %s\n", thread_name);
  printf("loop period = %lu ns (%lu us); freq = %.1f Hz\n",
      US_TO_NS(PERIOD_US), PERIOD_US, 1.0/US_TO_SEC((double)PERIOD_US));

  
  Quality q = QUALITY_VALIDITY_GOOD;

  int vol = (int) (80000.f * sqrt(2));
  int amp = 750;
  float phaseAngle = 0.15f;

  int voltageA;
  int voltageB;
  int voltageC;
  int voltageN;
  int currentA;
  int currentB;
  int currentC;
  int currentN;

  int sampleCount = 0;
  
  while (running) {
    // Wait for the next cycle.
    sleep_until_us(&last_wake_time_us, PERIOD_US);
    
    /* update measurement values */
    int samplePoint = sampleCount % 80;

    double angleA = (2 * M_PI / 80) * samplePoint;
    double angleB = (2 * M_PI / 80) * samplePoint - ( 2 * M_PI / 3);
    double angleC = (2 * M_PI / 80) * samplePoint - ( 4 * M_PI / 3);

    voltageA = (vol * sin(angleA)) * 100;
    voltageB = (vol * sin(angleB)) * 100;
    voltageC = (vol * sin(angleC)) * 100;
    voltageN = voltageA + voltageB + voltageC;

    currentA = (amp * sin(angleA - phaseAngle)) * 1000;
    currentB = (amp * sin(angleB - phaseAngle)) * 1000;
    currentC = (amp * sin(angleC - phaseAngle)) * 1000;
    currentN = currentA + currentB + currentC;

    // -----------------------------------------------------------------------------------------
    // Perform whatever action you want here at this fixed interval.
    // -----------------------------------------------------------------------------------------


    SVPublisher_ASDU_setINT32(asdu, amp1, currentA);
    SVPublisher_ASDU_setQuality(asdu, amp1q, q);
    SVPublisher_ASDU_setINT32(asdu, amp2, currentB);
    SVPublisher_ASDU_setQuality(asdu, amp2q, q);
    SVPublisher_ASDU_setINT32(asdu, amp3, currentC);
    SVPublisher_ASDU_setQuality(asdu, amp3q, q);
    SVPublisher_ASDU_setINT32(asdu, amp4, currentN);
    SVPublisher_ASDU_setQuality(asdu, amp4q, q);

    SVPublisher_ASDU_setINT32(asdu, vol1, voltageA);
    SVPublisher_ASDU_setQuality(asdu, vol1q, q);
    SVPublisher_ASDU_setINT32(asdu, vol2, voltageB);
    SVPublisher_ASDU_setQuality(asdu, vol2q, q);
    SVPublisher_ASDU_setINT32(asdu, vol3, voltageC);
    SVPublisher_ASDU_setQuality(asdu, vol3q, q);
    SVPublisher_ASDU_setINT32(asdu, vol4, voltageN);
    SVPublisher_ASDU_setQuality(asdu, vol4q, q);

    //SVPublisher_ASDU_setRefrTmNs(asdu, Hal_getTimeInNs());

    SVPublisher_ASDU_setSmpCnt(asdu, (uint16_t) sampleCount);

    SVPublisher_publish(svPublisher);

    sampleCount = ((sampleCount + 1) % 4800);

  }
  SVPublisher_destroy(svPublisher);
  return (void*)"Done!";
}

int main(int argc, char** argv){
  struct sv_param param;

  if (argc > 1)
      param.interface = argv[1];
  else
      param.interface = "eth0";
  
  param.asdu_name = "svtst";
  
  
  signal(SIGINT, handle_signal); // Register signal handler for SIGINT
  
  printf("Activating realtime scheduler.\n");
  use_realtime_scheduler();

  printf("Starting pthread at fixed interval using `sleep_until_us()`.\n\n");

  pthread_t thread, ptp_r;
  //const char thread_name[] = "some thread name"; // this can really be ANY argument
  
  int retcode = pthread_create(&thread, NULL, pthread_task, (void*)&param);
  running = 1;
  if (retcode != 0){
      printf("Failed to create pthread. retcode = %i: %s\n", retcode, strerror(retcode));
  }
  
  retcode = pthread_create(&ptp_r, NULL, pthread_ptp, (void*)&param);
  if (retcode != 0){
      printf("Failed to create ptp. retcode = %i: %s\n", retcode, strerror(retcode));
  }
  

  printf("Waiting for signal...\n");
  pause(); // Suspend program execution until a signal arrives
  printf("Program resumed after signal.\n");
  
  const char * return_message;
  retcode = pthread_join(thread, (void**)&return_message);
  if (retcode != 0)
  {
      printf("Failed to join (terminate) pthread. retcode = %i: %s\n",
          retcode, strerror(retcode));
  }

  printf("\nFinal message from thread = %s\n", return_message);

  return 0;
}
