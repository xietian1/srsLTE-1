/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include "liblte/rrc/rrc.h"
#include "liblte/phy/phy.h"
#include "liblte/cuhd/cuhd.h"
#include "cell_search_utils.h"

#define B210_DEFAULT_GAIN         40.0
#define B210_DEFAULT_GAIN_CORREC  80.0 // Gain of the Rx chain when the gain is set to 40

float gain_offset = B210_DEFAULT_GAIN_CORREC;

cell_detect_cfg_t cell_detect_config = {
  500, // nof_frames_total 
  50,  // nof_frames_detected
  0.4 // threshold
};

/**********************************************************************
 *  Program arguments processing
 ***********************************************************************/
typedef struct {
  int nof_subframes;
  bool disable_plots;
  int force_N_id_2;
  char *uhd_args; 
  float uhd_freq; 
  float uhd_gain;
}prog_args_t;

void args_default(prog_args_t *args) {
  args->nof_subframes = -1; 
  args->force_N_id_2 = -1; // Pick the best
  args->uhd_args = "";
  args->uhd_freq = -1.0;
  args->uhd_gain = B210_DEFAULT_GAIN; 
}

void usage(prog_args_t *args, char *prog) {
  printf("Usage: %s [aglnv] -f rx_frequency (in Hz)\n", prog);
  printf("\t-a UHD args [Default %s]\n", args->uhd_args);
  printf("\t-g UHD RX gain [Default %.2f dB]\n", args->uhd_gain);
  printf("\t-l Force N_id_2 [Default best]\n");
  printf("\t-n nof_subframes [Default %d]\n", args->nof_subframes);
  printf("\t-v [set verbose to debug, default none]\n");
}

void parse_args(prog_args_t *args, int argc, char **argv) {
  int opt;
  args_default(args);
  while ((opt = getopt(argc, argv, "aglnvf")) != -1) {
    switch (opt) {
    case 'a':
      args->uhd_args = argv[optind];
      break;
    case 'g':
      args->uhd_gain = atof(argv[optind]);
      break;
    case 'f':
      args->uhd_freq = atof(argv[optind]);
      break;
    case 'n':
      args->nof_subframes = atoi(argv[optind]);
      break;
    case 'l':
      args->force_N_id_2 = atoi(argv[optind]);
      break;
    case 'v':
      verbose++;
      break;
    default:
      usage(args, argv[0]);
      exit(-1);
    }
  }
  if (args->uhd_freq < 0) {
    usage(args, argv[0]);
    exit(-1);
  }
}
/**********************************************************************/

/* TODO: Do something with the output data */
uint8_t data[10000], data_unpacked[1000];

int cuhd_recv_wrapper(void *h, void *data, uint32_t nsamples) {
  DEBUG(" ----  Receive %d samples  ---- \n", nsamples);
  return cuhd_recv(h, data, nsamples, 1);
}

extern float mean_exec_time;

enum receiver_state { DECODE_MIB, DECODE_SIB, MEASURE} state; 

int main(int argc, char **argv) {
  int ret; 
  cf_t *sf_buffer; 
  prog_args_t prog_args; 
  lte_cell_t cell;  
  int64_t sf_cnt;
  ue_sync_t ue_sync; 
  ue_mib_t ue_mib; 
  void *uhd; 
  ue_dl_t ue_dl; 
  lte_fft_t fft; 
  chest_dl_t chest; 
  uint32_t nframes=0;
  uint32_t nof_trials = 0; 
  uint32_t sfn = 0; // system frame number
  int n; 
  uint8_t bch_payload[BCH_PAYLOAD_LEN], bch_payload_unpacked[BCH_PAYLOAD_LEN];
  uint32_t sfn_offset; 
  float rssi_utra=0,rssi=0, rsrp=0, rsrq=0, snr=0;
  cf_t *nullce[MAX_PORTS]; 
  
  for (int i=0;i<MAX_PORTS;i++) {
    nullce[i] = NULL;
  }

  parse_args(&prog_args, argc, argv);

  printf("Opening UHD device...\n");
  if (cuhd_open(prog_args.uhd_args, &uhd)) {
    fprintf(stderr, "Error opening uhd\n");
    exit(-1);
  }
  /* Set receiver gain */
  cuhd_set_rx_gain(uhd, prog_args.uhd_gain);

  /* set receiver frequency */
  cuhd_set_rx_freq(uhd, (double) prog_args.uhd_freq);
  cuhd_rx_wait_lo_locked(uhd);
  printf("Tunning receiver to %.3f MHz\n", (double ) prog_args.uhd_freq/1000000);

  if (detect_and_decode_cell(&cell_detect_config, uhd, prog_args.force_N_id_2, &cell)) {
    fprintf(stderr, "Cell not found\n");
    exit(-1); 
  }
  
  cuhd_start_rx_stream(uhd);
  
  if (ue_sync_init(&ue_sync, cell, cuhd_recv_wrapper, uhd)) {
    fprintf(stderr, "Error initiating ue_sync\n");
    exit(-1); 
  }
  if (ue_dl_init(&ue_dl, cell, 1234)) { 
    fprintf(stderr, "Error initiating UE downlink processing module\n");
    exit(-1);
  }
  if (ue_mib_init(&ue_mib, cell, false)) {
    fprintf(stderr, "Error initaiting UE MIB decoder\n");
    exit(-1);
  }
  pdsch_set_rnti(&ue_dl.pdsch, SIRNTI);

  /* Initialize subframe counter */
  sf_cnt = 0;
    
  if (lte_fft_init(&fft, cell.cp, cell.nof_prb)) {
    fprintf(stderr, "Error initiating FFT\n");
    return -1;
  }
  if (chest_dl_init(&chest, cell)) {
    fprintf(stderr, "Error initiating channel estimator\n");
    return -1;
  }
  
  int sf_re = SF_LEN_RE(cell.nof_prb, cell.cp);

  cf_t *sf_symbols = vec_malloc(sf_re * sizeof(cf_t));

  /* Main loop */
  while (sf_cnt < prog_args.nof_subframes || prog_args.nof_subframes == -1) {
    
    ret = ue_sync_get_buffer(&ue_sync, &sf_buffer);
    if (ret < 0) {
      fprintf(stderr, "Error calling ue_sync_work()\n");
    }

        
    /* ue_sync_get_buffer returns 1 if successfully read 1 aligned subframe */
    if (ret == 1) {
      switch (state) {
        case DECODE_MIB:
          if (ue_sync_get_sfidx(&ue_sync) == 0) {
            pbch_decode_reset(&ue_mib.pbch);
            n = ue_mib_decode_aligned_frame(&ue_mib,
                                            sf_buffer, bch_payload_unpacked, 
                                            NULL, &sfn_offset);
            if (n < 0) {
              fprintf(stderr, "Error decoding UE MIB\n");
              exit(-1);
            } else if (n == MIB_FOUND) {             
              bit_unpack_vector(bch_payload_unpacked, bch_payload, BCH_PAYLOAD_LEN);
              bcch_bch_unpack(bch_payload, BCH_PAYLOAD_LEN, &cell, &sfn);
              printf("MIB found SFN: %d, offset: %d\n", sfn, sfn_offset);
              sfn = (sfn<<2) + sfn_offset; 
              state = DECODE_SIB; 
            }
          }
          break;
        case DECODE_SIB:
          sfn=0; // FIXME: Use correct SFN!! 
          /* We are looking for SI Blocks, search only in appropiate places */
          if ((ue_sync_get_sfidx(&ue_sync) == 5 && (sfn%2)==0)) {
            n = ue_dl_decode(&ue_dl, sf_buffer, data, ue_sync_get_sfidx(&ue_sync), sfn, SIRNTI);
            if (n < 0) {
              fprintf(stderr, "Error decoding UE DL\n");fflush(stdout);
              exit(-1);
            } else if (n == 0) {
              printf("CFO: %+6.4f KHz, SFO: %+6.4f Khz, ExecTime: %5.1f us, NOI: %.2f, PDCCH-Det: %.3f\r",
                      ue_sync_get_cfo(&ue_sync)/1000, ue_sync_get_sfo(&ue_sync)/1000, 
                      mean_exec_time, pdsch_average_noi(&ue_dl.pdsch),
                      (float) ue_dl.nof_pdcch_detected/nof_trials);                
              nof_trials++; 
            } else {
              printf("\n\nDecoded SIB1 Message Len %d: ",n);
              bit_unpack_vector(data, data_unpacked, n);
              void *dlsch_msg = bcch_dlsch_unpack(data_unpacked, n);
              if (dlsch_msg) {
                printf("\n");fflush(stdout);
                cell_access_info_t cell_info; 
                bcch_dlsch_sib1_get_cell_access_info(dlsch_msg, &cell_info);
                printf("Cell ID: 0x%x\n", cell_info.cell_id);
              }
              state = MEASURE; 
            }
          }
        break;
      case MEASURE:
        /* Run FFT for all subframe data */
        lte_fft_run_sf(&fft, sf_buffer, sf_symbols);
        
        chest_dl_estimate(&chest, sf_symbols, nullce, ue_sync_get_sfidx(&ue_sync));
        
        
        rssi = VEC_CMA(vec_avg_power_cf(sf_buffer,SF_LEN(lte_symbol_sz(cell.nof_prb))),rssi,nframes);
        rssi_utra = VEC_CMA(chest_dl_get_rssi(&chest),rssi_utra,nframes);
        rsrq = VEC_EMA(chest_dl_get_rsrq(&chest),rsrq,0.001);
        rsrp = VEC_CMA(chest_dl_get_rsrp(&chest),rsrp,nframes);      
        snr = VEC_CMA(chest_dl_get_snr(&chest),snr,nframes);      
        nframes++;
        
        // Plot and Printf
        if ((nframes%10) == 0) {
          printf("CFO: %+8.4f KHz, SFO: %+8.4f Khz, RSSI: %5.1f dBm, RSSI/ref-symbol: %+5.1f dBm, "
                 "RSRP: %+5.1f dBm, RSRQ: %5.1f dB, SNR: %5.1f dB\r",
                ue_sync_get_cfo(&ue_sync)/1000, ue_sync_get_sfo(&ue_sync)/1000, 
                10*log10(rssi*1000)-gain_offset, 
                10*log10(rssi_utra*1000)-gain_offset, 
                10*log10(rsrp*1000)-gain_offset, 
              10*log10(rsrq), 10*log10(snr));                
        }
        break;
      }
      if (ue_sync_get_sfidx(&ue_sync) == 0) {
        sfn++; 
        if (sfn == 1024) {
          sfn = 0; 
        }
      }
    } else if (ret == 0) {
      printf("Finding PSS... Peak: %8.1f, FrameCnt: %d, State: %d\r", 
        sync_get_peak_value(&ue_sync.sfind), 
        ue_sync.frame_total_cnt, ue_sync.state);      
    }
   
        
    sf_cnt++;                  
  } // Main loop

  ue_sync_free(&ue_sync);
  cuhd_close(uhd);
  printf("\nBye\n");
  exit(0);
}



