#include "approximate_memory_controller.h"

////////////externs
double bwutil;
double bwutil_global_read;
double bwutil_global_write;
unsigned int n_cmd;
unsigned int n_activity;
unsigned int n_nop;
unsigned int n_act;
unsigned int n_pre;
unsigned int n_rd;
unsigned int n_wr;
unsigned int n_req;

double temp_bwutil;
double temp_bwutil_global_read;
double temp_bwutil_global_write;

double act_bwutil;
double act_bwutil_gread;
double act_bwutil_gwrite;

double req_bwutil;
double req_bwutil_gread;
double req_bwutil_gwrite;

unsigned int act_cmd; ///////cycles in window
unsigned int req_cmd; ///////cycles in window
////////////externs

std::FILE * profiling_output;

unsigned amc_initialized = 0;
unsigned dram_initialized = 0;

unsigned readonly_activation_num[6]; //////readonly
unsigned readonly_activation_num_all; //////readonly

unsigned same_row_access_all[130]; ////////////////readonly 128+2
unsigned same_row_access[6][130]; ////////////////readonly 128+2

unsigned global_access_num[6]; ///////readonly
unsigned global_access_num_all; ///////readonly

unsigned concurrent_row_access_all[6][16]; //////read and write
unsigned same_row_access_overall_all[10002]; //////read and write 1024+2
//unsigned same_row_access_overall[6][10002]; //////read and write 1024+2
unsigned overall_row_hit_0[6]; //////read and write
unsigned overall_row_hit_0_all; //////read and write

unsigned row_access_all[6][16][4096]; ////////readonly ideal, 4k row size
unsigned overall_row_access_all[6][16][4096]; ////////read and write ideal, 4k row size
unsigned concurrent_row_reads[6][16]; //////readonly, just used to count the maximum number of continuous same row accesses(row hits + 1) for this bank.
unsigned concurrent_row_reads_neg[6][16]; //////////readonly, other type of accesses appeared in this row open of the bank.
unsigned row_reads[6][16]; ////////////////row access is counted per request(not per bank operation).

unsigned come_after[6];
unsigned come_after_all;

unsigned row_hit_0[6]; /////////column 1
unsigned row_hit_0_all; /////////column 1

unsigned candidate_0[6]; ///////////////removed
unsigned candidate_0_all; ///////////////removed

dram_req_temp temp_store[6][16];
unsigned dynamic_on;
unsigned l2_warmup_count;
unsigned approx_enabled;
unsigned delay_threshold;

///////////profiling
unsigned profiling_done_flag;
unsigned profiling_done_cycle;
unsigned print_profile;
unsigned auto_delay;
unsigned profiling_cycles_es;
unsigned warmup_cycles;
unsigned profiling_done;
unsigned min_bw;
unsigned moving_direction; /////////////0 is left, 1 is right, 2 is middle.
float baseline_bw;
std::array<int, 9> delay_steps;
unsigned activation_window;
unsigned request_window;
unsigned reprofiling_cycles;
unsigned cycles_after_profiling_done;
unsigned previous_delay_threshold;
unsigned reprofiling_count;
unsigned profiling_cycles_bw;

unsigned previous_total_access_count[6]; //////////contains the removed requests
unsigned previous_total_access_count_all; //////////contains the removed requests
unsigned previous_total_activation_num[6]; //////////contains the removed requests
unsigned previous_total_activation_num_all; //////////contains the removed requests
///////////profiling

unsigned actual_redo;

unsigned overall_access_count[6]; /////////read and write/////////not containing the removed requests
unsigned overall_access_count_all; /////////read and write/////////not containing the removed requests
unsigned activation_num[6]; //////read and write/////////not containing the removed requests
unsigned activation_num_all; //////read and write/////////not containing the removed requests

unsigned temp_access_count[6]; ////////profile/////////not containing the removed requests
unsigned temp_access_count_all; ////////profile/////////not containing the removed requests
unsigned temp_activation_num[6]; ////////profile/////////not containing the removed requests
unsigned temp_activation_num_all; ////////profile/////////not containing the removed requests

unsigned readonly_access_count[6]; ///////////readonly
unsigned readonly_access_count_all; ///////////readonly

std::map<unsigned, unsigned> all_access_count_per_pc; ////read and write access count of each pc
std::map<unsigned, unsigned> required_access_count_per_pc; ////read only hit0 access count of each pc
std::map<unsigned, unsigned> all_required_access_count_per_pc; ////read only hit0 access count of each pc
std::map<unsigned, unsigned> removed_access_count_per_pc; ////removed access count of each pc
std::map<unsigned, unsigned> all_rw_hit0_count_per_pc; ////read and write hit0 access count of each pc

unsigned delay_queue_full_all;
unsigned delay_queue_full[6];

unsigned hit_in_delay_queue;
unsigned added_in_delay_queue;
unsigned echo_in_delay_queue;

unsigned redo_in_l1;
unsigned always_fill;
unsigned searching_radius;

//////////coverage control
//float coverage[6];
float target_coverage;

unsigned total_access_count[6]; //////////contains the removed requests
unsigned total_access_count_all; //////////contains the removed requests
unsigned total_activation_num[6];  //////////contains the removed requests
unsigned total_activation_num_all;  //////////contains the removed requests

unsigned approximated_req_count[6];  /////////removed
unsigned approximated_req_count_all;  /////////removed

unsigned total_access_count_temp_all; //////////contains the removed requests
unsigned approximated_req_count_temp_all; /////////removed

unsigned last_removed_row[6][16];

unsigned e_number;
//////////coverage control

//////////////dynamic e
unsigned accu_es[10];  //////////contains the removed requests
unsigned accu_es_partial[6][10];  //////////contains the removed requests

unsigned req_window_es[10];  //////////contains the removed requests
unsigned req_window_es_partial[6][10];  //////////contains the removed requests
unsigned req_window_total_act;  //////////contains the removed requests
unsigned req_window_total_act_partial[6]; //////////contains the removed requests

unsigned act_window_es[10];  //////////contains the removed requests
unsigned act_window_es_partial[6][10];  //////////contains the removed requests
unsigned act_window_total_req;  //////////contains the removed requests
unsigned act_window_total_req_partial[6]; //////////contains the removed requests

unsigned pf_window_es[10];
unsigned pf_window_es_partial[6][10];

unsigned can_remove; ////////////////controls which state allows remove
unsigned previous_can_remove;
unsigned chosen_e;
unsigned coverage_sufficiency_decided;

unsigned LT_schedule_count;
//////////////dynamic e

unsigned approximate(dram_req_t *req) {

	unsigned is_approximated = 0;

	return is_approximated;
}

void count_profile(dram_req_t *req, unsigned m_dramid, unsigned bank) {

	if (approx_enabled) {

		if (amc_initialized == 0) { /////////////initialize

			actual_redo = 0;

			for (int i = 0; i < 130; i++) {

				same_row_access_all[i] = 0;

				for (int j = 0; j < 6; j++) {

					same_row_access[j][i] = 0;
				}
			}

			for (int i = 0; i < 10002; i++) {

				same_row_access_overall_all[i] = 0; /////////read and write

				//for (int j = 0; j < 6; j++) {

				//same_row_access_overall[j][i] = 0; ///////////read and write
				//}
			}

			activation_num_all = 0; //////////read and write//////not containing removed
			total_activation_num_all = 0; //////////read and write//////////containing the removed
			req_window_total_act = 0; ///////dynamic e
			readonly_activation_num_all = 0; ////////////readonly
			global_access_num_all = 0;
			come_after_all = 0;
			row_hit_0_all = 0;
			overall_row_hit_0_all = 0; //////read and write
			candidate_0_all = 0;
			overall_access_count_all = 0;
			readonly_access_count_all = 0;
			delay_queue_full_all = 0;
			hit_in_delay_queue = 0;
			added_in_delay_queue = 0;
			echo_in_delay_queue = 0;
			temp_access_count_all = 0; ////////profile/////////not containing the removed requests
			temp_activation_num_all = 0; ////////profile/////////not containing the removed requests
			profiling_done = 0;
			baseline_bw = 0;
			moving_direction = 2; /////////////0 is left, 1 is right, 2 is middle.
			profiling_done_flag = 0;
			cycles_after_profiling_done = 0;
			reprofiling_count = 0;
			profiling_done_cycle = 0;
			previous_total_access_count_all = 0; //////////contains the removed requests
			previous_total_activation_num_all = 0; //////////contains the removed requests

			for (int j = 0; j < 6; j++) {

				activation_num[j] = 0; //////////read and write//////not containing removed
				total_activation_num[j] = 0; //////////read and write//////////containing the removed
				req_window_total_act_partial[j] = 0; ///////dynamic e
				readonly_activation_num[j] = 0; ////////////readonly
				global_access_num[j] = 0;
				come_after[j] = 0;
				row_hit_0[j] = 0;
				overall_row_hit_0[j] = 0; //////read and write
				candidate_0[j] = 0;
				overall_access_count[j] = 0;
				readonly_access_count[j] = 0;
				delay_queue_full[j] = 0;
				temp_access_count[j] = 0; ////////profile/////////not containing the removed requests
				temp_activation_num[j] = 0; ////////profile/////////not containing the removed requests
				previous_total_activation_num[j] = 0; //////////contains the removed requests
				previous_total_access_count[j] = 0; //////////contains the removed requests
			}

			for (int j = 0; j < 6; j++) {
				for (int i = 0; i < 16; i++) {

					concurrent_row_reads[j][i] = 0;
					concurrent_row_access_all[j][i] = 0; ////////////////read and write
					row_reads[j][i] = 0;
					for (int k = 0; k < 4096; k++) {
						row_access_all[j][i][k] = 0;
						overall_row_access_all[j][i][k] = 0;
					}
				}
			}

			//////////coverage control
			total_access_count_all = 0;
			approximated_req_count_all = 0;
			total_access_count_temp_all = 0;
			approximated_req_count_temp_all = 0;

			for (int j = 0; j < 6; j++) {

				total_access_count[j] = 0;
				approximated_req_count[j] = 0;
			}
			//////////coverage control

			//////////dynamic e
			act_window_total_req = 0;

			for (int j = 0; j < 6; j++) {

				act_window_total_req_partial[j] = 0;
			}

			for (int i = 0; i < 10; i++) {
				accu_es[i] = 0;  //////////contains the removed requests
				req_window_es[i] = 0;  //////////contains the removed requests
				act_window_es[i] = 0;  //////////contains the removed requests
				pf_window_es[i] = 0;

				for (int j = 0; j < 6; j++) {
					accu_es_partial[j][i] = 0; //////////contains the removed requests
					req_window_es_partial[j][i] = 0; //////////contains the removed requests
					act_window_es_partial[j][i] = 0; //////////contains the removed requests
					pf_window_es_partial[j][i] = 0;
				}
			}
			//////////dynamic e

			amc_initialized = 1;
		} /////////////initialize

		////////////////////count with no limitation
		all_access_count_per_pc[req->data->get_pc()]++; ////////////////count all pcs read and write
		overall_access_count[m_dramid]++;
		overall_access_count_all++; //////////////////just total number of acceses, not satisfying the prediction requirement.
		temp_access_count[m_dramid]++; /////////profile
		temp_access_count_all++; /////////profile

		total_access_count[m_dramid]++; ///////////coverage control
		total_access_count_all++; ///////////coverage control
		total_access_count_temp_all++;

		//////////dynamic e
		act_window_total_req_partial[m_dramid]++;
		act_window_total_req++;
		//////////dynamic e

		overall_row_access_all[m_dramid][bank][req->row]++; //////////////read and write
		////////////////////count with no limitation

		if (req->data->get_access_type() == GLOBAL_ACC_R
				&& !req->data->is_access_atomic()) { /////////////////this counts no-atomic reads only

			global_access_num[m_dramid]++;
			global_access_num_all++; //////////////not accurate since cannot guarantee for later requests in this activation

			row_access_all[m_dramid][bank][req->row]++; ////used to calculate ideal case global hit distribution////not accurate for readonly activations, but does not matter since we will not use it.
			assert(m_dramid < 6);
			assert(bank < 16);
			assert(req->row < 4096);
		}

		/////////////////read and write
		concurrent_row_access_all[m_dramid][bank]++; ///////increment

		if (concurrent_row_access_all[m_dramid][bank] < 10002) { //////////count occurrence

			same_row_access_overall_all[(concurrent_row_access_all[m_dramid][bank])]++; ////////////////0 is wasted
			//same_row_access_overall[m_dramid][(concurrent_row_access_all[m_dramid][bank])]++; //////////////0 is wasted
		}
		/////////////////read and write

		if (concurrent_row_reads_neg[m_dramid][bank] == 0) { ////////nothing other than read happened before

			if (req->data->get_access_type() == GLOBAL_ACC_R
					&& !req->data->is_access_atomic()) { //////////this counts row activation

				temp_store[m_dramid][bank].store_temp(req); ///////////store the last request of this bank.
				row_reads[m_dramid][bank]++;

				concurrent_row_reads[m_dramid][bank]++; //////////accumulated before the first write

				readonly_access_count[m_dramid]++; ////////////////readonly
				readonly_access_count_all++; ////////////////readonly

				if (concurrent_row_reads[m_dramid][bank] < 130) {

					same_row_access_all[(concurrent_row_reads[m_dramid][bank])]++; ////////////////0 is wasted////////////////readonly
					same_row_access[m_dramid][(concurrent_row_reads[m_dramid][bank])]++; //////////////0 is wasted////////////////readonly
				}
			} else { ////////////once other operation happens, clear all previous added reads, and prevent later reads as well.

				concurrent_row_reads_neg[m_dramid][bank] = 1;

				///////////////////clear activations
				readonly_activation_num[m_dramid]--; /////////////////readonly
				readonly_activation_num_all--; /////////////////readonly

				///////////clear accesses
				readonly_access_count[m_dramid] =
						readonly_access_count[m_dramid]
								- concurrent_row_reads[m_dramid][bank]; ////////////////readonly, reduce what was falsely added in this new activation
				readonly_access_count_all = readonly_access_count_all
						- concurrent_row_reads[m_dramid][bank]; ////////////////readonly, reduce what was falsely added in this new activation

				//////////////clear cdf
				if (concurrent_row_reads[m_dramid][bank] < 130) {
					for (int i = concurrent_row_reads[m_dramid][bank]; i >= 1;
							i--) { ///////////////start removing from the highest hit number.

						same_row_access_all[i]--; ////////////////0 is wasted
						same_row_access[m_dramid][i]--; ////////////////0 is wasted
					}
				} else {
					for (int i = 129; i >= 1; i--) { ///////////////start removing from the highest hit number.

						same_row_access_all[i]--; ////////////////0 is wasted
						same_row_access[m_dramid][i]--; ////////////////0 is wasted
					}
				}
			}
		} /////////end of: if (concurrent_row_reads_neg[m_dramid][bank] == 0)

	} ///////end of: if (approx_enabled)
}

void print_amc() {

	if (approx_enabled) {
//////////////myedit AMC

		unsigned delta_same_row_access_all[129]; ///////readonly
		unsigned delta_same_row_access[6][129]; ///////readonly

		unsigned delta_same_row_access_overall_all[10001]; ///////////read and write
		//unsigned delta_same_row_access_overall[6][10001]; ///////////read and write

		unsigned ideal_same_row_access_all[129]; ///////readonly, pdf
		unsigned ideal_same_row_access[6][129]; ///////readonly, pdf

		unsigned ideal_same_row_access_all_128more; ///////readonly
		unsigned ideal_same_row_access_128more[6]; ///////readonly
		unsigned ideal_same_row_access_all_32more; ///////readonly
		unsigned ideal_same_row_access_32more[6]; ///////readonly

		unsigned overall_ideal_same_row_access_all[129]; ///////////read and write
		unsigned overall_ideal_same_row_access[6][129]; ///////////read and write

		unsigned overall_ideal_same_row_access_all_128more; ///////////read and write
		unsigned overall_ideal_same_row_access_128more[6]; ///////////read and write
		unsigned overall_ideal_same_row_access_all_32more; ///////////read and write
		unsigned overall_ideal_same_row_access_32more[6]; ///////////read and write

		/////////////initialize
		ideal_same_row_access_all_128more = 0;
		ideal_same_row_access_all_32more = 0;

		overall_ideal_same_row_access_all_128more = 0; ///////////read and write
		overall_ideal_same_row_access_all_32more = 0; ///////////read and write

		for (int j = 0; j < 6; j++) {

			ideal_same_row_access_128more[j] = 0;
			ideal_same_row_access_32more[j] = 0;

			overall_ideal_same_row_access_128more[j] = 0; ///////////read and write
			overall_ideal_same_row_access_32more[j] = 0; ///////////read and write
		}

		for (int i = 0; i < 129; i++) {

			ideal_same_row_access_all[i] = 0;
			overall_ideal_same_row_access_all[i] = 0; //////////read and write

			for (int j = 0; j < 6; j++) {

				ideal_same_row_access[j][i] = 0;
				overall_ideal_same_row_access[j][i] = 0; //////////read and write
			}
		}

		for (int i = 0; i < 129; i++) {

			delta_same_row_access_all[i] = 0;

			for (int j = 0; j < 6; j++) {

				delta_same_row_access[j][i] = 0;
			}
		}

		for (int i = 0; i < 10001; i++) {

			delta_same_row_access_overall_all[i] = 0; //////////read and write

			//for (int j = 0; j < 6; j++) {

			//delta_same_row_access_overall[j][i] = 0; //////////read and write
			//}
		}

		///////////////////////process overall activations 128 pdf
		printf("overall 128 and more all:");
		for (int i = 1; i < 10001; i++) { ////////////0 is wasted

			delta_same_row_access_overall_all[i] =
					same_row_access_overall_all[i]
							- same_row_access_overall_all[i + 1];
			printf("%u ", delta_same_row_access_overall_all[i]);
			//fflush(stdout);
		}
		printf("%u", same_row_access_overall_all[10001]); /////////////129 and more
		printf("\n\n");
		//fflush(stdout);

		/*
		 for (int j = 0; j < 6; j++) {

		 printf("overall 128 and more dram%d:", j);
		 for (int i = 1; i < 10001; i++) { /////////////0 is wasted

		 delta_same_row_access_overall[j][i] =
		 same_row_access_overall[j][i]
		 - same_row_access_overall[j][i + 1];
		 printf("%u ", delta_same_row_access_overall[j][i]); /////////////////pdf
		 fflush(stdout);
		 }
		 printf("%u", same_row_access_overall[j][10001]); /////////////129 and more
		 fflush(stdout);
		 printf("\n\n");
		 }
		 */

		///////////////////////print overall 32 pdf
		printf("overall 32 and more all:");
		for (int i = 1; i < 33; i++) { //////////////0 is wasted

			printf("%u ", delta_same_row_access_overall_all[i]); ///////////////there is nothing beyond 32, so it's ok to not sum 33 to 128 up.
			//fflush(stdout);
		}
		printf("%u", same_row_access_overall_all[33]); /////////////33 and more
		printf("\n\n");
		//fflush(stdout);

		/*
		 for (int j = 0; j < 6; j++) {

		 printf("overall 32 and more dram%d:", j);
		 for (int i = 1; i < 33; i++) { //////////////0 is wasted

		 printf("%u ", delta_same_row_access_overall[j][i]);
		 fflush(stdout);
		 }
		 printf("%u", same_row_access_overall[j][33]); /////////////33 and more
		 fflush(stdout);
		 printf("\n\n");
		 }
		 */

		/////////////////count overall
		unsigned count_overall_accumulate = 0;
		unsigned count_overall_temp = 0;

		/*
		 ///////////////per dram
		 for (int j = 0; j < 6; j++) {

		 count_overall_accumulate = 0;
		 count_overall_temp = 0;
		 printf("overall count 128 and more dram%d:", j);
		 for (int i = 1; i < 10001; i++) { /////////////0 is wasted

		 count_overall_temp = i * delta_same_row_access_overall[j][i];
		 count_overall_accumulate += count_overall_temp;
		 printf("%u ", count_overall_temp); /////////////////pdf
		 fflush(stdout);
		 }
		 printf("%u", overall_access_count[j] - count_overall_accumulate); /////////////129 and more
		 fflush(stdout);
		 printf("\n\n");
		 }
		 */

		count_overall_accumulate = 0;
		count_overall_temp = 0;
		///////////////////////print overall accesses 128 pdf
		printf("overall count 128 and more all:");
		for (int i = 1; i < 10001; i++) { ////////////0 is wasted

			count_overall_temp = i * delta_same_row_access_overall_all[i];
			count_overall_accumulate += count_overall_temp;
			printf("%u ", count_overall_temp);
			//fflush(stdout);
		}
		printf("%u", overall_access_count_all - count_overall_accumulate); /////////////129 and more
		printf("\n\n");
		//fflush(stdout);

		/*
		 ///////////////per dram
		 for (int j = 0; j < 6; j++) {

		 count_overall_accumulate = 0;
		 count_overall_temp = 0;
		 printf("overall count 32 and more dram%d:", j);
		 for (int i = 1; i < 33; i++) { //////////////0 is wasted

		 count_overall_temp = i * delta_same_row_access_overall[j][i];
		 count_overall_accumulate += count_overall_temp;
		 printf("%u ", count_overall_temp); /////////////////pdf
		 fflush(stdout);
		 }
		 printf("%u", overall_access_count[j] - count_overall_accumulate); /////////////33 and more
		 fflush(stdout);
		 printf("\n\n");
		 }
		 */

		count_overall_accumulate = 0;
		count_overall_temp = 0;
		///////////////////////print overall 32 pdf
		printf("overall count 32 and more all:");
		for (int i = 1; i < 33; i++) { //////////////0 is wasted

			count_overall_temp = i * delta_same_row_access_overall_all[i];
			count_overall_accumulate += count_overall_temp;
			printf("%u ", count_overall_temp);
			//fflush(stdout);
		}
		printf("%u", overall_access_count_all - count_overall_accumulate); /////////////33 and more
		printf("\n\n");
		//fflush(stdout);
		/////////////////count overall

		///////////////////////process readonly 128 pdf

		printf("readonly 128 and more all:");
		for (int i = 1; i < 129; i++) {

			delta_same_row_access_all[i] = same_row_access_all[i]
					- same_row_access_all[i + 1];
			printf("%u ", delta_same_row_access_all[i]);
			//fflush(stdout);
		}
		printf("%u", same_row_access_all[129]); /////////////129 and more
		printf("\n\n");
		//fflush(stdout);

		for (int j = 0; j < 6; j++) {

			printf("readonly 128 and more dram%d:", j);
			for (int i = 1; i < 129; i++) {

				delta_same_row_access[j][i] = same_row_access[j][i]
						- same_row_access[j][i + 1];
				printf("%u ", delta_same_row_access[j][i]); /////////////////pdf
				//fflush(stdout);
			}
			printf("%u", same_row_access[j][129]); /////////////129 and more
			printf("\n\n");
			//fflush(stdout);
		}

		///////////////////////print real case 32 pdf
		printf("readonly 32 and more all:");
		for (int i = 1; i < 33; i++) {

			printf("%u ", delta_same_row_access_all[i]); ///////////////there is nothing beyond 32, so it's ok to not sum 33 to 128 up.
			//fflush(stdout);
		}
		printf("%u", same_row_access_all[33]); /////////////33 and more
		printf("\n\n");
		//fflush(stdout);

		for (int j = 0; j < 6; j++) {

			printf("readonly 32 and more dram%d:", j);
			for (int i = 1; i < 33; i++) {

				printf("%u ", delta_same_row_access[j][i]);
				//fflush(stdout);
			}
			printf("%u", same_row_access[j][33]); /////////////33 and more
			printf("\n\n");
			//fflush(stdout);
		}

		/////////////////count readonly
		unsigned count_readonly_accumulate = 0;
		unsigned count_readonly_temp = 0;

		///////////////per dram
		for (int j = 0; j < 6; j++) {

			count_readonly_accumulate = 0;
			count_readonly_temp = 0;
			printf("readonly count 128 and more dram%d:", j);
			for (int i = 1; i < 129; i++) { /////////////0 is wasted

				count_readonly_temp = i * delta_same_row_access[j][i];
				count_readonly_accumulate += count_readonly_temp;
				printf("%u ", count_readonly_temp); /////////////////pdf
				//fflush(stdout);
			}
			printf("%u", readonly_access_count[j] - count_readonly_accumulate); /////////////129 and more
			printf("\n\n");
			//fflush(stdout);
		}

		count_readonly_accumulate = 0;
		count_readonly_temp = 0;
		///////////////////////print readonly 128 pdf
		printf("readonly count 128 and more all:");
		for (int i = 1; i < 129; i++) { ////////////0 is wasted

			count_readonly_temp = i * delta_same_row_access_all[i];
			count_readonly_accumulate += count_readonly_temp;
			printf("%u ", count_readonly_temp);
			//fflush(stdout);
		}
		printf("%u", readonly_access_count_all - count_readonly_accumulate); /////////////129 and more
		printf("\n\n");
		//fflush(stdout);

		///////////////per dram
		for (int j = 0; j < 6; j++) {

			count_readonly_accumulate = 0;
			count_readonly_temp = 0;
			printf("readonly count 32 and more dram%d:", j);
			for (int i = 1; i < 33; i++) { //////////////0 is wasted

				count_readonly_temp = i * delta_same_row_access[j][i];
				count_readonly_accumulate += count_readonly_temp;
				printf("%u ", count_readonly_temp); /////////////////pdf
				//fflush(stdout);
			}
			printf("%u", readonly_access_count[j] - count_readonly_accumulate); /////////////33 and more
			printf("\n\n");
			//fflush(stdout);
		}

		count_readonly_accumulate = 0;
		count_readonly_temp = 0;
		///////////////////////print readonly 32 pdf
		printf("readonly count 32 and more all:");
		for (int i = 1; i < 33; i++) { //////////////0 is wasted

			count_readonly_temp = i * delta_same_row_access_all[i];
			count_readonly_accumulate += count_readonly_temp;
			printf("%u ", count_readonly_temp);
			//fflush(stdout);
		}
		printf("%u", readonly_access_count_all - count_readonly_accumulate); /////////////33 and more
		printf("\n\n");
		//fflush(stdout);
		/////////////////count readonly

		///////////////////////count ideal
		unsigned used_row_read = 0;

		unsigned ideal_more_than_32_accesses = 0;
		unsigned ideal_more_than_128_accesses = 0;

		for (int i = 0; i < 6; i++) {

			for (int j = 0; j < 16; j++) {

				for (int k = 0; k < 4096; k++) {

					if (0 < row_access_all[i][j][k]
							&& row_access_all[i][j][k] < 129) {

						used_row_read++;

						ideal_same_row_access_all[(row_access_all[i][j][k])]++; ////////0 is wasted
						ideal_same_row_access[i][(row_access_all[i][j][k])]++; ////////0 is wasted
					}

					if (row_access_all[i][j][k] > 128) {

						used_row_read++;

						ideal_same_row_access_all_128more++;
						ideal_same_row_access_128more[i]++;

						ideal_more_than_128_accesses += row_access_all[i][j][k];
					}
					if (row_access_all[i][j][k] > 32) {

						ideal_same_row_access_all_32more++;
						ideal_same_row_access_32more[i]++;

						ideal_more_than_32_accesses += row_access_all[i][j][k];
					}
				}
			}
		}

		///////////////////////print ideal 128
		printf("used row read:%u\n", used_row_read);

		printf("ideal case 128 and more all:");
		for (int i = 1; i < 129; i++) {

			printf("%u ", ideal_same_row_access_all[i]);
			//fflush(stdout);
		}
		printf("%u", ideal_same_row_access_all_128more);
		printf("\n\n");

		///////////////////////count ideal 128
		printf("ideal count 128 and more all:");
		for (int i = 1; i < 129; i++) {

			printf("%u ", ideal_same_row_access_all[i] * i);
			//fflush(stdout);
		}
		printf("%u", ideal_more_than_128_accesses);
		printf("\n\n");

		for (int j = 0; j < 6; j++) {

			printf("ideal case 128 and more dram%d:", j);
			for (int i = 1; i < 129; i++) {

				printf("%u ", ideal_same_row_access[j][i]);
				//fflush(stdout);
			}
			printf("%u", ideal_same_row_access_128more[j]);
			printf("\n\n");
		}

		///////////////////////print ideal 32
		printf("ideal case 32 and more all:");
		for (int i = 1; i < 33; i++) {

			printf("%u ", ideal_same_row_access_all[i]);
			//fflush(stdout);
		}
		printf("%u", ideal_same_row_access_all_32more);
		printf("\n\n");

		///////////////////////count ideal 32
		printf("ideal count 32 and more all:");
		for (int i = 1; i < 33; i++) {

			printf("%u ", ideal_same_row_access_all[i] * i);
			//fflush(stdout);
		}
		printf("%u", ideal_more_than_32_accesses);
		printf("\n\n");

		for (int j = 0; j < 6; j++) {

			printf("ideal case 32 and more dram%d:", j);
			for (int i = 1; i < 33; i++) {

				printf("%u ", ideal_same_row_access[j][i]);
				//fflush(stdout);
			}
			printf("%u ", ideal_same_row_access_32more[j]);
			printf("\n\n");
		}
		///////////////////////count ideal

		///////////////////////count perfect, read and write
		unsigned used_row_sum = 0;

		unsigned perfect_more_than_32_accesses = 0;
		unsigned perfect_more_than_128_accesses = 0;

		for (int i = 0; i < 6; i++) {

			for (int j = 0; j < 16; j++) {

				for (int k = 0; k < 4096; k++) {

					unsigned row_access_num = overall_row_access_all[i][j][k];

					if (0 < row_access_num && row_access_num < 129) {

						used_row_sum++;

						overall_ideal_same_row_access_all[row_access_num]++; ////////0 is wasted
						overall_ideal_same_row_access[i][row_access_num]++; ////////0 is wasted
					}

					if (row_access_num > 128) {

						used_row_sum++;

						overall_ideal_same_row_access_all_128more++;
						overall_ideal_same_row_access_128more[i]++;

						perfect_more_than_128_accesses += row_access_num;
					}
					if (row_access_num > 32) {

						overall_ideal_same_row_access_all_32more++;
						overall_ideal_same_row_access_32more[i]++;

						perfect_more_than_32_accesses += row_access_num;
					}
				}
			}
		}

		///////////////////////print perfect 128
		printf("used row all:%u\n", used_row_sum);

		printf("perfect 128 and more all:");
		for (int i = 1; i < 129; i++) {

			printf("%u ", overall_ideal_same_row_access_all[i]);
			//fflush(stdout);
		}
		printf("%u", overall_ideal_same_row_access_all_128more);
		printf("\n\n");

		///////////////////////count perfect 128
		printf("perfect count 128 and more all:");
		for (int i = 1; i < 129; i++) {

			printf("%u ", overall_ideal_same_row_access_all[i] * i);
			//fflush(stdout);
		}
		printf("%u", perfect_more_than_128_accesses);
		printf("\n\n");

		for (int j = 0; j < 6; j++) {

			printf("perfect 128 and more dram%d:", j);
			for (int i = 1; i < 129; i++) {

				printf("%u ", overall_ideal_same_row_access[j][i]);
				//fflush(stdout);
			}
			printf("%u ", overall_ideal_same_row_access_128more[j]);
			printf("\n\n");
		}

		///////////////////////print perfect 32
		printf("perfect 32 and more all:");
		for (int i = 1; i < 33; i++) {

			printf("%u ", overall_ideal_same_row_access_all[i]);
			//fflush(stdout);
		}
		printf("%u", overall_ideal_same_row_access_all_32more);
		printf("\n\n");

		///////////////////////count perfect 32
		printf("perfect count 32 and more all:");
		for (int i = 1; i < 33; i++) {

			printf("%u ", overall_ideal_same_row_access_all[i] * i);
			//fflush(stdout);
		}
		printf("%u", perfect_more_than_32_accesses);
		printf("\n\n");

		for (int j = 0; j < 6; j++) {

			printf("perfect 32 and more dram%d:", j);
			for (int i = 1; i < 33; i++) {

				printf("%u ", overall_ideal_same_row_access[j][i]);
				//fflush(stdout);
			}
			printf("%u ", overall_ideal_same_row_access_32more[j]);
			printf("\n\n");
		}
		///////////////////////count perfect, read and write

		//////////////////print total access count
		printf("total global read access:%u\n", global_access_num_all); ///////////only the global non atomic ones

		for (int j = 0; j < 6; j++) {

			printf("dram%d global read access:%u\n", j, global_access_num[j]); ///////////only the global non atomic ones
			//fflush(stdout);
		}

		//////////////////print total access count
		printf("come after all:%u\n", come_after_all); ////////////////hit requests which only come in next cycle

		for (int j = 0; j < 6; j++) { ///////////////just for reference, not accurate for requests satisfying the requirement.

			printf("dram%d come after:%u\n", j, come_after[j]); ////////////////hit requests which only come in next cycle
			//fflush(stdout);
		}

		///////////////////post processing
		for (int i = 0; i < 6; i++) {
			for (int j = 0; j < 16; j++) {

				/////////////read activation only
				if (concurrent_row_reads_neg[i][j] == 0 ///here use a different way to count and print single hit (need post processing to count the last one).
				&& concurrent_row_reads[i][j] == 1) { ///This can also be applied to count same_row_access_all.

					row_hit_0[i]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)
					row_hit_0_all++;

					if (i == 0) {

						if (0 && print_profile) {

							std::fprintf(profiling_output,
									"address:%llu, sid:%u, pc:%u, wid:%u, age:%u, bk:%u, row:%u, col:%u\n",
									temp_store[i][j].addr, temp_store[i][j].sid,
									temp_store[i][j].pc, temp_store[i][j].wid,
									temp_store[i][j].age, temp_store[i][j].bk,
									temp_store[i][j].row, temp_store[i][j].col); /////post processing this as well
						}

						required_access_count_per_pc[temp_store[i][j].pc]++; ////////////////count all pcs for dram0
					}

					all_required_access_count_per_pc[temp_store[i][j].pc]++; ////////////////read only count all pcs for all drams
				}

				///////////////////post processing read and write
				if (concurrent_row_access_all[i][j] == 1) {

					overall_row_hit_0[i]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)
					overall_row_hit_0_all++;
					all_rw_hit0_count_per_pc[temp_store[i][j].pc]++;///////////////read and write
				}
			}
		}

		////////////////////////////print removed and original single hits
		printf("removed all:%u\n", candidate_0_all); //////make sense in removing, it's both access and activation num////////////when it's not 0, separate counters will be needed.
		printf("readonly row_hit_0 all:%u\n", row_hit_0_all); /////make sense in profile, it's both access and activation num
		printf("overall row_hit_0 all:%u\n", overall_row_hit_0_all); //////read and write

		printf("actual_redo all:%u\n", actual_redo); /////

		///////////////////////print readonly and overall case 32
		unsigned first32_overall_accesses = 0; ////////////read and write
		unsigned first32_overall_activations = 0; ////////////read and write
		unsigned first32_read_reads = 0; /////////readonly
		unsigned first32_read_activations = 0; /////////readonly

		for (int i = 1; i < 33; i++) { ////////////////merge with count above

			/////////readonly
			first32_read_activations += delta_same_row_access_all[i];
			first32_read_reads += delta_same_row_access_all[i] * i; ////get total from pdf
			////////////read and write
			first32_overall_activations += delta_same_row_access_overall_all[i];
			first32_overall_accesses += delta_same_row_access_overall_all[i]
					* i; ////get total from pdf
		}

		printf("first 32 readonly activations:%u\n", //////////////get in profile
				first32_read_activations);
		printf("first 32 readonly accesses:%u\n", ////////////get in profile
				first32_read_reads);

		printf("first 32 overall activations:%u\n", //////////////get in profile
				first32_overall_activations);
		printf("first 32 overall accesses:%u\n", ////////////get in profile
				first32_overall_accesses);

		//////////////////print activation and access count
		printf("readonly activations all:%u\n", readonly_activation_num_all); ////////////all activations////////////get in profile
		printf("readonly accesses all:%u\n", readonly_access_count_all); ////////////get in profile

		printf("overall activations all:%u\n", activation_num_all); ////////////all activations////////////get in profile
		printf("overall accesses all:%u\n", overall_access_count_all); ////////////get in profile

		printf("removed also activations all:%u\n", total_activation_num_all); ////////////all activations
		printf("removed also accesses all:%u\n", total_access_count_all); ////////////get in profile

		printf("delay queue full all:%u\n", delay_queue_full_all); ////////////get in profile
		printf("hit in delay queue all:%u\n", hit_in_delay_queue); ////////////get in profile
		printf("added in delay queue all:%u\n", added_in_delay_queue); ////////////get in profile
		printf("echo in delay queue all:%u\n", echo_in_delay_queue); ////////////get in profile

		printf("reprofiling count:%u\n", reprofiling_count); /////////get in auto delay

		float actual_coverage = 0;
		if (approximated_req_count_all != 0 && total_access_count_all != 0) {

			actual_coverage = (float) (approximated_req_count_all)
					/ (float) (total_access_count_all);	///////////coverage control
			printf("actual coverage all:%f\n", actual_coverage); ////////////get in remove
		}

		printf("actual delay:%u\n", delay_threshold); ////////////profiling
		printf("profiling done cycle:%u\n", profiling_done_cycle); ////////////profiling

		///////////////////////print for each dram
		for (int j = 0; j < 6; j++) { ///////////////////////////////////

			if (actual_coverage != 0) {
				actual_coverage = (float) (approximated_req_count[j])
						/ (float) (total_access_count[j]); ///////////coverage control
				printf("dram%d coverage:%f\n", j, actual_coverage); //////////what we found
			}

			printf("dram%d removed:%u\n", j, candidate_0[j]); //////////what we found
			printf("dram%d readonly row_hit_0:%u\n", j, row_hit_0[j]); //////////what we want
			printf("dram%d overall row_hit_0:%u\n", j, overall_row_hit_0[j]); //////read and write

			printf("dram%d readonly accesses:%u\n", j,
					readonly_access_count[j]); /////////////////////////readonly
			printf("dram%d readonly activations:%u\n", j,
					readonly_activation_num[j]); ////////////readonly activations

			printf("dram%d overall accesses:%u\n", j, overall_access_count[j]); /////////////////////////everything out there
			printf("dram%d overall activations:%u\n", j, activation_num[j]); ////////////all activations

			printf("dram%d removed also accesses:%u\n", j,
					total_access_count[j]); /////////////////////////everything out there
			printf("dram%d removed also activations:%u\n", j,
					total_activation_num[j]); ////////////all activations

			printf("dram%d delay queue full:%u\n", j, delay_queue_full[j]); //////
		}
		///////////////////////print for each dram

		printf("###################################################\n");
		int PC_count = all_access_count_per_pc.size();
		printf("number of different PCs:%u\n", PC_count); ///////////////read and write all pcs////////////get in profile
		for (std::map<unsigned, unsigned>::iterator iterator =
				all_access_count_per_pc.begin();
				iterator != all_access_count_per_pc.end(); iterator++) {

			printf("all PC:%u, all count:%u\n", iterator->first,
					all_access_count_per_pc[iterator->first]);
		}
		printf("###################################################\n");

		int PC_count2 = required_access_count_per_pc.size();
		printf("dram0 different PCs:%u\n", PC_count2); //////////////read only pcs that are in single hit in dram0////////////get in profile
		for (std::map<unsigned, unsigned>::iterator iterator =
				required_access_count_per_pc.begin();
				iterator != required_access_count_per_pc.end(); iterator++) {

			printf("dram0 required PC:%u, dram0 required count:%u\n", iterator->first,
					required_access_count_per_pc[iterator->first]);
		}
		printf("###################################################\n");

		int PC_count3 = all_required_access_count_per_pc.size();
		printf("all_required PCs:%u\n", PC_count3); //////////////read only all pcs that are in single hit////////////get in profile
		for (std::map<unsigned, unsigned>::iterator iterator =
				all_required_access_count_per_pc.begin();
				iterator != all_required_access_count_per_pc.end();
				iterator++) {

			printf("all required PC:%u, all required count:%u\n", iterator->first,
					all_required_access_count_per_pc[iterator->first]);
		}
		printf("###################################################\n");

		int PC_count5 = all_rw_hit0_count_per_pc.size();
		printf("all_rw_hit0 PCs:%u\n", PC_count5); //////////////read and write all pcs that are in single hit////////////get in profile
		for (std::map<unsigned, unsigned>::iterator iterator =
				all_rw_hit0_count_per_pc.begin();
				iterator != all_rw_hit0_count_per_pc.end();
				iterator++) {

			printf("all rw hit0 PC:%u, all rw hit0 count:%u\n", iterator->first,
					all_rw_hit0_count_per_pc[iterator->first]);
		}
		printf("###################################################\n");

		int PC_count4 = removed_access_count_per_pc.size();
		printf("removed PCs:%u\n", PC_count4); //////////////pcs that we guess are in single hit////////////get in remove
		for (std::map<unsigned, unsigned>::iterator iterator =
				removed_access_count_per_pc.begin();
				iterator != removed_access_count_per_pc.end(); iterator++) {

			printf("removed PC:%u, removed count:%u\n", iterator->first,
					removed_access_count_per_pc[iterator->first]);
		}
		printf("###################################################\n");
		fflush (stdout);
	} /////////////end of: if (approx_enabled)
//////////////myedit AMC
}
