#ifndef LINE_AMC_INCLUDED//include guard.
#define LINE_AMC_INCLUDED

#include "gpgpu-sim/dram_sched.h"
#include "gpgpu-sim/dram.h"
#include "gpgpu-sim/shader.h"
#include "gpgpu-sim/gpu-sim.h"
#include <array>

//////////////myedit AMC
class dram_req_temp {
public:
	void store_temp(class dram_req_t *req) {

		row = req->row;
		col = req->col;
		bk = req->bk;
		nbytes = req->nbytes;
		txbytes = req->txbytes;
		dqbytes = req->dqbytes;
		age = req->age;
		timestamp = req->timestamp;
		rw = req->rw; //is the request a read or a write?
		addr = req->addr;
		insertion_time = req->insertion_time;
		sid = req->data->get_sid();
		pc = req->data->get_pc();
		wid = req->data->get_wid();
	}

	unsigned int row;
	unsigned int col;
	unsigned int bk;
	unsigned int nbytes;
	unsigned int txbytes;
	unsigned int dqbytes;
	unsigned int age;
	unsigned int timestamp;
	unsigned char rw; //is the request a read or a write?
	unsigned long long int addr;
	unsigned int insertion_time;
	unsigned int sid;
	unsigned int pc;
	unsigned int wid;
};
//////////////myedit AMC

extern unsigned amc_initialized;
extern unsigned dram_initialized;

extern unsigned activation_num[6];///////////read and write
extern unsigned activation_num_all;///////////read and write

extern unsigned temp_access_count[6]; ////////profile/////////not containing the removed requests
extern unsigned temp_access_count_all; ////////profile/////////not containing the removed requests
extern unsigned temp_activation_num[6]; ////////profile/////////not containing the removed requests
extern unsigned temp_activation_num_all; ////////profile/////////not containing the removed requests

extern unsigned readonly_activation_num[6];//////readonly
extern unsigned readonly_activation_num_all;//////readonly

extern unsigned same_row_access_all[130];
extern unsigned same_row_access[6][130];

extern unsigned global_access_num[6];
extern unsigned global_access_num_all;

extern unsigned same_row_access_overall_all[10002]; //////read and write
//extern unsigned same_row_access_overall[6][10002]; //////read and write
extern unsigned concurrent_row_access_all[6][16]; //////read and write
extern unsigned overall_row_hit_0[6]; //////read and write
extern unsigned overall_row_hit_0_all; //////read and write

extern unsigned row_access_all[6][16][4096]; ////////4k row size
extern unsigned overall_row_access_all[6][16][4096]; ////////4k row size, read and write
extern unsigned concurrent_row_reads[6][16]; //////just used to count the maximum number of continuous same row accesses(row hits + 1) for this bank.
extern unsigned concurrent_row_reads_neg[6][16]; //////////other type of accesses appeared in this row open of the bank.
extern unsigned row_reads[6][16]; ////////////////row access is counted per request(not per bank operation).

extern unsigned come_after[6];
extern unsigned come_after_all;

extern unsigned row_hit_0[6];
extern unsigned row_hit_0_all;

extern unsigned candidate_0[6];
extern unsigned candidate_0_all;

extern dram_req_temp temp_store[6][16];
extern unsigned print_profile;
extern unsigned dynamic_on;
extern unsigned auto_delay;
extern unsigned profiling_cycles_es;
extern unsigned warmup_cycles;
extern unsigned l2_warmup_count;
extern unsigned profiling_done;
extern unsigned delay_threshold;

extern unsigned min_bw;
extern unsigned moving_direction;
extern unsigned profiling_done_flag;
extern unsigned profiling_done_cycle;
extern std::array<int, 9> delay_steps;
extern float baseline_bw;
extern unsigned activation_window;
extern unsigned request_window;
extern unsigned reprofiling_cycles;
extern unsigned cycles_after_profiling_done;
extern unsigned previous_delay_threshold;
extern unsigned reprofiling_count;
extern unsigned profiling_cycles_bw;

extern unsigned previous_total_access_count[6]; //////////contains the removed requests
extern unsigned previous_total_access_count_all; //////////contains the removed requests
extern unsigned previous_total_activation_num[6];  //////////contains the removed requests
extern unsigned previous_total_activation_num_all;  //////////contains the removed requests

extern std::FILE * profiling_output;

extern unsigned actual_redo;
extern unsigned approx_enabled;

extern unsigned overall_access_count[6];
extern unsigned overall_access_count_all;

extern unsigned readonly_access_count[6];
extern unsigned readonly_access_count_all;

extern std::map<unsigned, unsigned> all_access_count_per_pc; ////access count of each pc
extern std::map<unsigned, unsigned> required_access_count_per_pc; ////access count of each pc
extern std::map<unsigned, unsigned> all_required_access_count_per_pc; ////access count of each pc
extern std::map<unsigned, unsigned> removed_access_count_per_pc; ////access count of each pc

extern unsigned delay_queue_full_all;
extern unsigned delay_queue_full[6];

extern unsigned hit_in_delay_queue;
extern unsigned added_in_delay_queue;
extern unsigned echo_in_delay_queue;

extern unsigned redo_in_l1;
extern unsigned always_fill;
extern unsigned searching_radius;

//////////coverage control
//extern float coverage[6];
extern float target_coverage;

extern unsigned total_access_count[6];
extern unsigned total_access_count_all;
extern unsigned total_activation_num[6];  //////////contains the removed requests
extern unsigned total_activation_num_all;  //////////contains the removed requests

extern unsigned approximated_req_count[6];
extern unsigned approximated_req_count_all;

extern unsigned total_access_count_temp_all; //////////contains the removed requests
extern unsigned approximated_req_count_temp_all; /////////removed

extern unsigned last_removed_row[6][16];

extern unsigned e_number;
//////////coverage control

//////////////dynamic e
extern unsigned accu_es[10];  //////////contains the removed requests
extern unsigned accu_es_partial[6][10];  //////////contains the removed requests

extern unsigned req_window_es[10];  //////////contains the removed requests
extern unsigned req_window_es_partial[6][10];  //////////contains the removed requests
extern unsigned req_window_total_act;  //////////contains the removed requests
extern unsigned req_window_total_act_partial[6]; //////////contains the removed requests

extern unsigned act_window_es[10];  //////////contains the removed requests
extern unsigned act_window_es_partial[6][10];  //////////contains the removed requests
extern unsigned act_window_total_req;  //////////contains the removed requests
extern unsigned act_window_total_req_partial[6]; //////////contains the removed requests

extern unsigned pf_window_es[10];
extern unsigned pf_window_es_partial[6][10];

extern unsigned can_remove;
extern unsigned previous_can_remove;
extern unsigned chosen_e;
extern unsigned coverage_sufficiency_decided;

extern unsigned LT_schedule_count;
//////////////dynamic e

void print_amc();

unsigned approximate(dram_req_t *req);

void count_profile(dram_req_t *req, unsigned m_dramid, unsigned bank);

#endif//end of #ifndef LINE_AMC_INCLUDED
