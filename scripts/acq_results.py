#!/usr/bin/env python
# Copyright (C) 2011-2014 Swift Navigation Inc.
# Contact: Colin Beighley <colin@swift-nav.com>
#
# This source is subject to the license found in the file 'LICENSE' which must
# be be distributed together with this source. All other rights reserved.
#
# THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
# EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.


import time

from sbp.client.main import *
from sbp.client.handler import *
from sbp.client.drivers.pyserial_driver import *
from sbp.client.drivers.pyftdi_driver import *

from numpy           import mean
from sbp.acquisition import SBP_MSG_ACQ_RESULT
from sbp.piksi       import SBP_MSG_PRINT

N_RECORD = 0 # Number of results to keep in memory, 0 = no limit.
N_PRINT = 32

SNR_THRESHOLD = 25

class AcqResults():

  def __init__(self):
    self.acqs = []
    self.max_corr = 0

  def __str__(self):
    tmp = "Last %d acquisitions:\n" % len(self.acqs[-N_PRINT:])
    for a in self.acqs[-N_PRINT:]:
      tmp += "PRN %2d, SNR: %3.2f\n" % (a.prn, a.snr)
    tmp += "Max SNR         : %3.2f\n" % (self.max_snr())
    tmp += "Mean of max SNRs: %3.2f\n" % (self.mean_max_snrs(SNR_THRESHOLD))
    return tmp

  # Return the maximum SNR received.
  def max_snr(self):
    try:
      return max([a.snr for a in self.acqs])
    except ValueError, KeyError:
      return 0

  # Return the mean of the max SNR (above snr_threshold) of each PRN.
  def mean_max_snrs(self, snr_threshold):
    snrs = []
    # Get the max SNR for each PRN.
    for prn in set([a.prn for a in self.acqs]):
      acqs_prn = filter(lambda x: x.prn == prn, self.acqs)
      acqs_prn_max_snr = max([a.snr for a in acqs_prn])
      if acqs_prn_max_snr >= snr_threshold:
        snrs += [max([a.snr for a in acqs_prn])]
    if snrs:
      return mean(snrs)
    else:
      return 0

  def receive_acq_result(self, sbp_msg):
    while N_RECORD > 0 and len(self.acqs) >= N_RECORD:
      self.acqs.pop(0)
    self.acqs.append(MsgAcqResult(sbp_msg))

def get_args():
  """
  Get and parse arguments.
  """
  import argparse
  parser = argparse.ArgumentParser(description='Acquisition Monitor')
  parser.add_argument("-f", "--ftdi",
                      help="use pylibftdi instead of pyserial.",
                      action="store_true")
  parser.add_argument("-p", "--port",
                      default=[SERIAL_PORT], nargs=1,
                      help="specify the serial port to use.")
  parser.add_argument("-b", "--baud",
                      default=[SERIAL_BAUD], nargs=1,
                      help="specify the baud rate to use.")
  parser.add_argument("-i", "--input-filename",
                      default=[None], nargs=1,
                      help="use input file to read SBP messages from.")
  return parser.parse_args()

def main():
  """
  Get configuration, get driver, and build handler and start it.
  """
  args = get_args()
  port = args.port[0]
  baud = args.baud[0]
  use_ftdi = args.ftdi
  input_filename = args.input_filename[0]
  with get_driver(use_ftdi, input_filename, port, baud) as driver:
    with Handler(driver.read, driver.write, False) as handler:
      acq_results = AcqResults()
      handler.add_callback(print_callback, SBP_MSG_PRINT)
      handler.add_callback(acq_results.receive_acq_result, SBP_MSG_ACQ_RESULT)
      try:
        while True:
          print acq_results
          time.sleep(0.1)
      except KeyboardInterrupt:
        pass

if __name__ == "__main__":
  main()
