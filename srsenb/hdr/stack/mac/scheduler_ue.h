/*
 * Copyright 2013-2019 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SRSENB_SCHEDULER_UE_H
#define SRSENB_SCHEDULER_UE_H

#include "srslte/common/log.h"
#include "srslte/interfaces/sched_interface.h"
#include <map>

#include "scheduler_harq.h"
#include "srslte/asn1/rrc_asn1.h"
#include <mutex>

namespace srsenb {

class sched_params_t;

/** This class is designed to be thread-safe because it is called from workers through scheduler thread and from
 * higher layers and mac threads.
 *
 * 1 mutex is created for every user and only access to same user variables are mutexed
 */
class sched_ue
{

public:
  // used by sched_metric to store the pdsch/pusch allocations
  bool has_pucch = false;

  typedef struct {
    uint32_t cce_start[4][6];
    uint32_t nof_loc[4];
  } sched_dci_cce_t;

  /*************************************************************
   *
   * FAPI-like Interface
   *
   ************************************************************/
  sched_ue();
  void reset();
  void phy_config_enabled(uint32_t tti, bool enabled);
  void set_cfg(uint16_t rnti, const sched_params_t& sched_params_, sched_interface::ue_cfg_t* cfg);

  void set_bearer_cfg(uint32_t lc_id, srsenb::sched_interface::ue_bearer_cfg_t* cfg);
  void rem_bearer(uint32_t lc_id);

  void dl_buffer_state(uint8_t lc_id, uint32_t tx_queue, uint32_t retx_queue);
  void ul_buffer_state(uint8_t lc_id, uint32_t bsr, bool set_value = true);
  void ul_phr(int phr);
  void mac_buffer_state(uint32_t ce_code);
  void ul_recv_len(uint32_t lcid, uint32_t len);
  void set_dl_ant_info(asn1::rrc::phys_cfg_ded_s::ant_info_c_* dedicated);
  void set_ul_cqi(uint32_t tti, uint32_t cqi, uint32_t ul_ch_code);
  void set_dl_ri(uint32_t tti, uint32_t ri);
  void set_dl_pmi(uint32_t tti, uint32_t ri);
  void set_dl_cqi(uint32_t tti, uint32_t cqi);
  int  set_ack_info(uint32_t tti, uint32_t tb_idx, bool ack);
  void set_ul_crc(uint32_t tti, bool crc_res);

  /*******************************************************
   * Custom functions
   *******************************************************/

  void tpc_inc();
  void tpc_dec();

  void set_max_mcs(int mcs_ul, int mcs_dl, int max_aggr_level = -1);
  void set_fixed_mcs(int mcs_ul, int mcs_dl);

  dl_harq_proc* find_dl_harq(uint32_t tti);
  dl_harq_proc* get_dl_harq(uint32_t idx);
  uint16_t      get_rnti() const { return rnti; }

  /*******************************************************
   * Functions used by scheduler metric objects
   *******************************************************/

  uint32_t get_required_prb_dl(uint32_t req_bytes, uint32_t nof_ctrl_symbols);
  uint32_t get_required_prb_ul(uint32_t req_bytes);
  uint32_t prb_to_rbg(uint32_t nof_prb);
  uint32_t rgb_to_prb(uint32_t nof_rbg);

  uint32_t get_pending_dl_new_data(uint32_t tti);
  uint32_t get_pending_ul_new_data(uint32_t tti);
  uint32_t get_pending_ul_old_data();
  uint32_t get_pending_dl_new_data_total(uint32_t tti);

  void          reset_pending_pids(uint32_t tti_rx);
  dl_harq_proc* get_pending_dl_harq(uint32_t tti);
  dl_harq_proc* get_empty_dl_harq();
  ul_harq_proc* get_ul_harq(uint32_t tti);

  /*******************************************************
   * Functions used by the scheduler object
   *******************************************************/

  void set_sr();
  void unset_sr();

  void set_needs_ta_cmd(uint32_t nof_ta_cmd);

  int generate_format1(dl_harq_proc*                     h,
                       sched_interface::dl_sched_data_t* data,
                       uint32_t                          tti,
                       uint32_t                          cfi,
                       const rbgmask_t&                  user_mask);
  int generate_format2a(dl_harq_proc*                     h,
                        sched_interface::dl_sched_data_t* data,
                        uint32_t                          tti,
                        uint32_t                          cfi,
                        const rbgmask_t&                  user_mask);
  int generate_format2(dl_harq_proc*                     h,
                       sched_interface::dl_sched_data_t* data,
                       uint32_t                          tti,
                       uint32_t                          cfi,
                       const rbgmask_t&                  user_mask);
  int generate_format0(sched_interface::ul_sched_data_t* data,
                       uint32_t                          tti,
                       ul_harq_proc::ul_alloc_t          alloc,
                       bool                              needs_pdcch,
                       srslte_dci_location_t             cce_range,
                       int                               explicit_mcs = -1);

  srslte_dci_format_t get_dci_format();
  uint32_t            get_aggr_level(uint32_t nof_bits);
  sched_dci_cce_t*    get_locations(uint32_t current_cfi, uint32_t sf_idx);

  bool     needs_cqi(uint32_t tti, bool will_send = false);
  uint32_t get_max_retx();

  bool get_pucch_sched(uint32_t current_tti, uint32_t prb_idx[2]);
  bool pucch_sr_collision(uint32_t current_tti, uint32_t n_cce);

private:
  typedef struct {
    sched_interface::ue_bearer_cfg_t cfg;
    int                              buf_tx;
    int                              buf_retx;
    int                              bsr;
  } ue_bearer_t;

  bool is_sr_triggered();
  int  alloc_pdu(int tbs, sched_interface::dl_sched_pdu_t* pdu);

  static uint32_t format1_count_prb(uint32_t bitmask, uint32_t cell_nof_prb);
  static int      cqi_to_tbs(uint32_t  cqi,
                             uint32_t  nof_prb,
                             uint32_t  nof_re,
                             uint32_t  max_mcs,
                             uint32_t  max_Qm,
                             bool      is_ul,
                             uint32_t* mcs);
  int             alloc_tbs_dl(uint32_t nof_prb, uint32_t nof_re, uint32_t req_bytes, int* mcs);
  int             alloc_tbs_ul(uint32_t nof_prb, uint32_t nof_re, uint32_t req_bytes, int* mcs);
  int             alloc_tbs(uint32_t nof_prb, uint32_t nof_re, uint32_t req_bytes, bool is_ul, int* mcs);

  static bool bearer_is_ul(ue_bearer_t* lch);
  static bool bearer_is_dl(ue_bearer_t* lch);

  uint32_t get_pending_dl_new_data_unlocked(uint32_t tti);
  uint32_t get_pending_ul_old_data_unlocked();
  uint32_t get_pending_ul_new_data_unlocked(uint32_t tti);
  uint32_t get_pending_dl_new_data_total_unlocked(uint32_t tti);

  bool needs_cqi_unlocked(uint32_t tti, bool will_send = false);

  int generate_format2a_unlocked(dl_harq_proc*                     h,
                                 sched_interface::dl_sched_data_t* data,
                                 uint32_t                          tti,
                                 uint32_t                          cfi,
                                 const rbgmask_t&                  user_mask);

  bool is_first_dl_tx();

  sched_interface::ue_cfg_t cfg   = {};
  srslte_cell_t             cell  = {};
  srslte::log*              log_h = nullptr;
  const sched_params_t*     sched_params;

  std::mutex mutex;

  /* Buffer states */
  bool                                             sr      = false;
  int                                              buf_mac = 0;
  int                                              buf_ul  = 0;
  std::array<ue_bearer_t, sched_interface::MAX_LC> lch     = {};

  int      power_headroom  = 0;
  uint32_t dl_ri           = 0;
  uint32_t dl_ri_tti       = 0;
  uint32_t dl_pmi          = 0;
  uint32_t dl_pmi_tti      = 0;
  uint32_t dl_cqi          = 0;
  uint32_t dl_cqi_tti      = 0;
  uint32_t cqi_request_tti = 0;
  uint32_t ul_cqi          = 0;
  uint32_t ul_cqi_tti      = 0;
  uint16_t rnti            = 0;
  uint32_t max_mcs_dl      = 0;
  uint32_t max_aggr_level  = 0;
  uint32_t max_mcs_ul      = 0;
  uint32_t max_msg3retx    = 0;
  int      fixed_mcs_ul    = 0;
  int      fixed_mcs_dl    = 0;

  uint32_t nof_ta_cmd = 0;

  int next_tpc_pusch = 0;
  int next_tpc_pucch = 0;

  // Allowed DCI locations per CFI and per subframe
  std::array<std::array<sched_dci_cce_t, 10>, 3> dci_locations = {};

  const static int                              SCHED_MAX_HARQ_PROC = SRSLTE_FDD_NOF_HARQ;
  std::array<dl_harq_proc, SCHED_MAX_HARQ_PROC> dl_harq             = {};
  std::array<ul_harq_proc, SCHED_MAX_HARQ_PROC> ul_harq             = {};

  bool                                   phy_config_dedicated_enabled = false;
  asn1::rrc::phys_cfg_ded_s::ant_info_c_ dl_ant_info;
};
} // namespace srsenb

#endif // SRSENB_SCHEDULER_UE_H
