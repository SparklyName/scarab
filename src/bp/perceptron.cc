/* Copyright 2020 HPS/SAFARI Research Groups
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "perceptron.h"

#include <vector>

extern "C" {
#include "bp/bp.param.h"
#include "core.param.h"
#include "globals/assert.h"
#include "statistics.h"
#include <stdio.h>
}

#define PHT_INIT_VALUE (0x1 << (PHT_CTR_BITS - 1)) /* weakly taken */
#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_BP_DIR, ##args)
#define HARDWARE_BUDGET 524288
#define PERCEPTRON_TABLE_WIDTH (HARDWARE_BUDGET / (HIST_LENGTH * 8)) 
#define PTW PERCEPTRON_TABLE_WIDTH

namespace {

struct Perceptron_State {
  std::vector<std::vector<uns8>> weights;
  std::vector<uns8> outcomes;
};

std::vector<Perceptron_State> perceptron_state_all_cores;

/*uns32 get_pht_index(const Addr addr, const uns32 hist) {
  const uns32 cooked_hist = hist >> (32 - HIST_LENGTH);
  const uns32 cooked_addr = (addr >> 2) & N_BIT_MASK(HIST_LENGTH);
  return cooked_hist ^ cooked_addr;
}*/
}  // namespace

// The only speculative state of gshare is the global history which is managed
// by bp.c. Thus, no internal timestamping and recovery mechanism is needed.
void bp_perceptron_timestamp(Op* op) {}
void bp_perceptron_recover(Recovery_Info* info) {}
void bp_perceptron_spec_update(Op* op) {}
void bp_perceptron_retire(Op* op) {}


void bp_perceptron_init() {
  perceptron_state_all_cores.resize(NUM_CORES);
  for(auto& perceptron_state : perceptron_state_all_cores) {
    perceptron_state.weights.resize(PERCEPTRON_TABLE_WIDTH);
    perceptron_state.outcomes.resize(PERCEPTRON_TABLE_WIDTH);
    for (auto& perceptron_weights : perceptron_state.weights) {
      perceptron_weights.resize(HIST_LENGTH + 1, 0);
    }
  }
}

uns8 bp_perceptron_pred(Op* op) {
  const uns   proc_id      = op->proc_id;
  auto& perceptron_state = perceptron_state_all_cores.at(proc_id);

  const Addr  addr      = op->oracle_info.pred_addr;
  const uns32 hist      = op->oracle_info.pred_global_hist;
  const uns32 tron_index = addr % PTW; 
  std::vector<uns8> &weights = perceptron_state.weights[tron_index];
  int32_t y = weights[0]; 
  for (uns32 i = 0; i < HIST_LENGTH; i++) {
    y += ((hist & (1 << i)) > 0) * weights[i + 1];
  }

  /*
  const uns32 pht_index = get_pht_index(addr, hist);
  const uns8  pht_entry = gshare_state.pht[pht_index];
  const uns8  pred      = pht_entry >> (PHT_CTR_BITS - 1) & 0x1;

  DEBUG(proc_id, "Predicting with gshare for  op_num:%s  index:%d\n",
        unsstr64(op->op_num), pht_index);
  DEBUG(proc_id, "Predicting  addr:%s  pht:%u  pred:%d  dir:%d\n",
        hexstr64s(addr), pht_index, pred, op->oracle_info.dir);

  std::cout << pred << std::endl;
  */
  perceptron_state.outcomes[tron_index] = y;
  return y > 0;
}

void bp_perceptron_update(Op* op) {
  if(op->table_info->cf_type != CF_CBR) {
    // If op is not a conditional branch, we do not interact with gshare.
    return;
  }
  
  const uns   proc_id      = op->proc_id;
  auto& perceptron_state = perceptron_state_all_cores.at(proc_id);

  const Addr  addr      = op->oracle_info.pred_addr;
  const uns32 hist      = op->oracle_info.pred_global_hist;
  const uns32 tron_index = addr % PTW; 
  std::vector<uns8> &weights = perceptron_state.weights[tron_index];
  const int outcome = perceptron_state.outcomes[tron_index] ? 1 : -1;
  for (uns32 i = 0; i < HIST_LENGTH; i++) {
    weights[i] += ((hist & (1 << i)) > 0) * outcome;  
  }
  
  /*

  const uns   proc_id      = op->proc_id;
  auto&       gshare_state = gshare_state_all_cores.at(proc_id);
  const Addr  addr         = op->oracle_info.pred_addr;
  const uns32 hist         = op->oracle_info.pred_global_hist;
  const uns32 pht_index    = get_pht_index(addr, hist);
  const uns8  pht_entry    = gshare_state.pht[pht_index];

  DEBUG(proc_id, "Writing gshare PHT for  op_num:%s  index:%d  dir:%d\n",
        unsstr64(op->op_num), pht_index, op->oracle_info.dir);

  if(op->oracle_info.dir) {
    gshare_state.pht[pht_index] = SAT_INC(pht_entry, N_BIT_MASK(PHT_CTR_BITS));
  } else {
    gshare_state.pht[pht_index] = SAT_DEC(pht_entry, 0);
  }

  DEBUG(proc_id, "Updating addr:%s  pht:%u  ent:%u  dir:%d\n", hexstr64s(addr),
        pht_index, gshare_state.pht[pht_index], op->oracle_info.dir);

  return;
  */
}
