/*
 * Copyright (C) 2011-2014 Swift Navigation Inc.
 * Contact: Fergus Noble <fergus@swift-nav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <string.h>

#include <libswiftnav/sbp_messages.h>

#include <ch.h>

#include "board/leds.h"
#include "board/max2769.h"
#include "board/nap/nap_conf.h"
#include "board/nap/acq_channel.h"
#include "board/max2769.h"
#include "sbp.h"
#include "init.h"
#include "manage.h"
#include "track.h"
#include "timing.h"
#include "solution.h"
#include "position.h"
#include "system_monitor.h"
#include "simulator.h"
#include "settings.h"

#if !defined(SYSTEM_CLOCK)
#define SYSTEM_CLOCK 130944000
#endif

/* TODO: Think about thread safety when updating ephemerides. */
ephemeris_t es[32] _CCM;
ephemeris_t es_old[32] _CCM;

/* Required by exit() which is called from BLAS/LAPACK. */
void _fini(void)
{
  return;
}

static WORKING_AREA_CCM(wa_nav_msg_thread, 3000);
static msg_t nav_msg_thread(void *arg)
{
  (void)arg;
  chRegSetThreadName("nav msg");
  while (TRUE) {
    chThdSleepMilliseconds(1000);

    /* Check if there is a new nav msg subframe to process.
     * TODO: move this into a function */

    /* TODO: This should be trigged by a semaphore from the tracking loop, not
     * just ran periodically. */

    memcpy(es_old, es, sizeof(es));

    for (u8 i=0; i<nap_track_n_channels; i++) {
      if (tracking_channel[i].state == TRACKING_RUNNING &&
          tracking_channel[i].nav_msg.subframe_start_index) {

        __asm__("CPSID i;");
        s8 ret = process_subframe(&tracking_channel[i].nav_msg,
                                  &es[tracking_channel[i].prn]);
        __asm__("CPSIE i;");

        if (ret < 0)
          printf("PRN %02d ret %d\n", tracking_channel[i].prn+1, ret);

        if (ret == 1 && !es[tracking_channel[i].prn].healthy)
          printf("PRN %02d unhealthy\n", tracking_channel[i].prn+1);

        if (memcmp(&es[tracking_channel[i].prn],
                   &es_old[tracking_channel[i].prn], sizeof(ephemeris_t))) {

          printf("New ephemeris for PRN %02d\n", tracking_channel[i].prn+1);

          /* TODO: This is a janky way to set the time... */
          gps_time_t t;
          t.wn = es[tracking_channel[i].prn].toe.wn;
          t.tow = tracking_channel[i].TOW_ms / 1000.0;
          if (gpsdifftime(t, es[tracking_channel[i].prn].toe) > 2*24*3600)
            t.wn--;
          else if (gpsdifftime(t, es[tracking_channel[i].prn].toe) < 2*24*3600)
            t.wn++;
          /*set_time(TIME_COARSE, t);*/

        }
      }
    }
  }

  return 0;
}

int main(void)
{
  /* Initialise SysTick timer that will be used as the ChibiOS kernel tick
   * timer. */
  STBase->RVR = SYSTEM_CLOCK / CH_FREQUENCY - 1;
  STBase->CVR = 0;
  STBase->CSR = CLKSOURCE_CORE_BITS | ENABLE_ON_BITS | TICKINT_ENABLED_BITS;

  /* Kernel initialization, the main() function becomes a thread and the RTOS
   * is active. */
  chSysInit();

  /* Piksi hardware initialization. */
  init();
  settings_setup();
  usarts_setup();

  /* Check NAP authentication status. */
  u8 nhs = nap_hash_status();
  if (nhs != NAP_HASH_MATCH) {
    led_on(LED_GREEN);
    led_off(LED_RED);
    while (1)
      DO_EVERY(10000000,
        printf("NAP Verification Failed\n");
        led_toggle(LED_GREEN);
        led_toggle(LED_RED);
      );
  }

  static char nap_version_string[64] = {0};
  nap_conf_rd_version_string(nap_version_string);

  static s32 serial_number;
  serial_number = nap_conf_rd_serial_number();

  max2769_setup();
  timing_setup();
  position_setup();

  manage_acq_setup();
  manage_track_setup();
  system_monitor_setup();
  solution_setup();

  simulator_setup();

  if (serial_number < 0) {
    READ_ONLY_PARAMETER("system_info", "serial_number", "(unknown)", TYPE_STRING);
  } else {
    READ_ONLY_PARAMETER("system_info", "serial_number", serial_number, TYPE_INT);
  }
  READ_ONLY_PARAMETER("system_info", "firmware_version", GIT_VERSION,
                      TYPE_STRING);
  READ_ONLY_PARAMETER("system_info", "firmware_built", __DATE__ " " __TIME__,
                      TYPE_STRING);

  static struct setting hw_rev = {
    "system_info", "hw_revision", NULL, 0,
    settings_read_only_notify, NULL,
    NULL, false
  };
  hw_rev.addr = (char *)nap_conf_rd_hw_rev_string();
  hw_rev.len = strlen(hw_rev.addr);
  settings_register(&hw_rev, TYPE_STRING);

  READ_ONLY_PARAMETER("system_info", "nap_version", nap_version_string,
                      TYPE_STRING);
  READ_ONLY_PARAMETER("system_info", "nap_channels", nap_track_n_channels,
                      TYPE_INT);
  READ_ONLY_PARAMETER("system_info", "nap_taps", nap_acq_n_taps, TYPE_INT);

  chThdCreateStatic(wa_nav_msg_thread, sizeof(wa_nav_msg_thread),
                    NORMALPRIO-1, nav_msg_thread, NULL);

  /* Send message to inform host we are up and running. */
  u32 startup_flags = 0;
  sbp_send_msg(SBP_STARTUP, sizeof(startup_flags), (u8 *)&startup_flags);

  while (1) {
    chThdSleepSeconds(60);
  }
}

