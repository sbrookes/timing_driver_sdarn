#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/types.h>
#include "../../include/do_csr.h"
#include "../../include/8254_timer.h"

#define ATT (0x1 << 15) /* yellow */
#define TR  (0x1 << 14) /* blue */
#define TX  (0x1 << 13) /* pink */ 
#define SS  (0x1 << 12) /* green */

#define PULSE_NUM 8
#define SIXTEEN_K 16*1024
#define CLOCK_PERIOD_US 10

int main(void) {

  __u32 cmd;
  __u32 word;
  __u32 *fifo;

  FILE *outfile;

  unsigned char clock;
  unsigned char timer;

  int i, j;
  int DO_CSR, DO_FIFO, TIMER_CTRL, TIMER_1, PLX_9080;

  int ptab[PULSE_NUM] = { 0, 14, 22, 24, 27, 31, 42, 43 };

  /* values in microseconds */
  int tau = 1500;
  int tr_buffer = 150;
  int att_buffer = 100;
  int tx_duration = 300;

  /* convert to clock periods */
  tau         /= CLOCK_PERIOD_US;
  tr_buffer   /= CLOCK_PERIOD_US;
  att_buffer  /= CLOCK_PERIOD_US;
  tx_duration /= CLOCK_PERIOD_US;

  /* allocate space for array */
  fifo = (__u32*) malloc(SIXTEEN_K * sizeof(__u32));

  /* zero the array */
  word = 0x00;
  for ( i = 0; i < SIXTEEN_K; i++ )
    fifo[i] = word;

  /* push 1 tau for buffer in the front */
  for ( i = 0; i < PULSE_NUM; i++ )
    ptab[i]++;

  /* for each word of the buffer */
  for ( j = 0, i = 0; i < SIXTEEN_K; i++ ) {
 
    /* start with zero */
    word = 0x00;

    /* 0 after all of the pulses */
    if ( j >= PULSE_NUM ) {
      fifo[i] = word;
      continue;
    }

    /* generate scope sync */
    if ( i == 1 )
      word |= SS;

    /* check for attenuator bit */
    if ( (i > ((ptab[j] * tau) - tr_buffer - att_buffer)) &&
	 (i <= ((ptab[j] * tau) + tr_buffer + att_buffer + tx_duration)) )
      word |= ATT;

    /* check for TR bit */
    if ( (i > ((ptab[j] * tau) - tr_buffer)) && 
	 (i <= ((ptab[j] * tau) + tr_buffer + tx_duration)) )
      word |= TR;

    /* check for TX bit */
    if ( (i > (ptab[j] * tau)) && (i <= (ptab[j] * tau) + tx_duration) )
      word |= TX;

    /* when we should move over to the next pulse... */
    if ( i == ((ptab[j] * tau) + tr_buffer + att_buffer + tx_duration) )
      j++;

    /* save the collection */
    fifo[i] = word;
  }

  /* output the test file */
  outfile = fopen("test_result.txt", "w");

  /* scope sync bit */
  for ( j = 0; j < SIXTEEN_K; j++) {   
    if ( fifo[j] & SS ) 
      fprintf(outfile, "|");
    else
      fprintf(outfile, "_");
  }
  fprintf(outfile, "\n");

  /* attenuator bit */
  for ( j = 0; j < SIXTEEN_K; j++) {   
    if ( fifo[j] & ATT ) 
      fprintf(outfile, "-");
    else
      fprintf(outfile, "_");
  }
  fprintf(outfile, "\n");

  /* tx bit */
  for ( j = 0; j < SIXTEEN_K; j++) {   
    if ( fifo[j] & TX ) 
      fprintf(outfile, "-");
    else
      fprintf(outfile, "_");
  }
  fprintf(outfile, "\n");

  /* tr bit */
  for ( j = 0; j < SIXTEEN_K; j++) {   
    if ( fifo[j] & TR ) 
      fprintf(outfile, "-");
    else
      fprintf(outfile, "_");
  }
  fprintf(outfile, "\n");
  
  fclose(outfile);

  /* open devices */
  DO_FIFO    = open("/dev/timing5" , O_WRONLY);
  DO_CSR     = open("/dev/timing1" , O_WRONLY);
  PLX_9080   = open("/dev/timing12", O_WRONLY);
  TIMER_CTRL = open("/dev/timing11", O_WRONLY);
  TIMER_1    = open("/dev/timing9" , O_WRONLY);

  /* check for correct opening */
  if ( DO_CSR < 1 || DO_FIFO < 1 || PLX_9080 < 1 ||
       TIMER_CTRL < 1 || TIMER_1 < 1 ) {
    printf("couldn't open devices \n");
    exit(3);
  }

  /* reset DO_CSR */
  RESET_OCSR(cmd);
  WIDTH_32_OCSR(cmd);
  CLOCK_TIMER_OCSR(cmd);
  DISABLE_OCSR(cmd);
  CLEAR_FIFO_OCSR(cmd);

  write(DO_CSR, &cmd, sizeof(__u32));

  /* set up the onboard timer */
  timer = 0x00;

  MODE_2_8254(timer);
  BINARY_8254(timer);
  LSB_ONLY_8254(timer);
  COUNTER_1_8254(timer);

  write(TIMER_CTRL, &timer, sizeof(char));

  /* set up the clock for 10 us output period */
  clock = 100;

  write(TIMER_1, &clock, sizeof(char));

  /* set up the digital output */
  RESET_OCSR(cmd);
  WIDTH_32_OCSR(cmd);
  CLOCK_TIMER_OCSR(cmd);
  NO_PAT_GEN_OCSR(cmd);
  DISABLE_OCSR(cmd);
  TERM_OFF_OCSR(cmd);
  CLEAR_FIFO_OCSR(cmd);
  NO_WAIT_NAE_OCSR(cmd);
  NO_TRIG_OCSR(cmd);
  NO_TRIG_END_OCSR(cmd);
  CLEAR_UNDER_OCSR(cmd);
  NO_SHAKE_OCSR(cmd);
 
  write(DO_CSR, &cmd, sizeof(__u32));

  /* write the fifo */
  write(DO_FIFO, fifo, SIXTEEN_K * sizeof(__u32));

  /* pause */
  sleep(1);

  /* begin output */
  SAVE_FIFO_OCSR(cmd);
  ENABLE_OCSR(cmd);

  write(DO_CSR, &cmd, sizeof(__u32));

  /* close devices */
  close(DO_CSR);
  close(DO_FIFO);
  close(TIMER_1);
  close(PLX_9080);
  close(TIMER_CTRL);

  /* free memory resources */
  free(fifo);

  return 0;
}
