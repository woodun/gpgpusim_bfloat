// Copyright (c) 2009-2011, Tor M. Aamodt, Ali Bakhoda, George L. Yuan,
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dram_sched.h"
#include "gpu-misc.h"
#include "gpu-sim.h"
#include "../abstract_hardware_model.h"
#include "mem_latency_stat.h"

///////////////myedit bfloat
unsigned dram_initialized = 0;
float target_coverage = 0;

unsigned approximated_req_count_temp_all = 0;
unsigned total_access_count_temp_all = 0;
unsigned approximated_req_count_all = 0;
unsigned total_access_count_all = 0;
int threshold_length_static_all = 0; ///////////all and partial are same for static
int threshold_length_dynamic_all = 0;
//double threshold_bw_static_all = 0;
//double threshold_bw_dynamic_all = 0;
unsigned total_float_count_all = 0;
unsigned total_int_count_all = 0;


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

double temp_bwutil = 0;
double temp_bwutil_global_read = 0;
double temp_bwutil_global_write = 0;

unsigned print_profile;
unsigned redo_in_l1;
unsigned always_fill;
unsigned bypassl2d;

unsigned current_truncate_ratio;
///////////////myedit bfloat


frfcfs_scheduler::frfcfs_scheduler(const memory_config *config, dram_t *dm,
		memory_stats_t *stats) {
	m_config = config;
	m_stats = stats;
	m_num_pending = 0;
	m_dram = dm;
	m_queue = new std::list<dram_req_t*>[m_config->nbk];
	m_bins =
			new std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >[m_config->nbk];
	m_last_row =
			new std::list<std::list<dram_req_t*>::iterator>*[m_config->nbk];
	curr_row_service_time = new unsigned[m_config->nbk];
	row_service_timestamp = new unsigned[m_config->nbk];

	//////////////myeditAMC
	if (dram_initialized == 0) {

		//////////coverage control
		target_coverage = (float) (m_config->coverage) / 100;
		//////////coverage control

		///////////////myedit bfloat: check if all these variables are used correctly
		print_profile = m_config->print_profile;
		redo_in_l1 = m_config->redo_in_l1;
		always_fill = m_config->always_fill;
		bypassl2d = m_config->bypassl2d;

		threshold_length_static_all = m_config->threshold_length; ///////////used for both all and partial
		threshold_length_dynamic_all = m_config->threshold_length; ////////////dynamic threshold is also initialized to static value
		m_dram->threshold_length_dynamic_partial = m_config->threshold_length;
//		threshold_bw_static_all = m_config->threshold_bw;
//		threshold_bw_dynamic_all = m_config->threshold_bw; ////////////dynamic threshold is also initialized to static value
//		m_dram->threshold_bw_dynamic_partial = m_config->threshold_bw;

		current_truncate_ratio = m_config->default_truncate_ratio;
		///////////////myedit bfloat

		dram_initialized = 1;
	}
	//////////////myeditAMC

	for (unsigned i = 0; i < m_config->nbk; i++) {
		m_queue[i].clear();
		m_bins[i].clear();
		m_last_row[i] = NULL;
		curr_row_service_time[i] = 0;
		row_service_timestamp[i] = 0;
	}
}

void frfcfs_scheduler::add_req(dram_req_t *req) {
	m_num_pending++;

	m_queue[req->bk].push_front(req);
	std::list<dram_req_t*>::iterator ptr = m_queue[req->bk].begin();
	m_bins[req->bk][req->row].push_front(ptr); //newest reqs to the front
}

void frfcfs_scheduler::data_collection(unsigned int bank) { //////////////however the last row activation's information is not counted.
	if (gpu_sim_cycle > row_service_timestamp[bank]) {
		curr_row_service_time[bank] = gpu_sim_cycle
				- row_service_timestamp[bank];
		if (curr_row_service_time[bank]
				> m_stats->max_servicetime2samerow[m_dram->id][bank])
			m_stats->max_servicetime2samerow[m_dram->id][bank] =
					curr_row_service_time[bank];
	}
	curr_row_service_time[bank] = 0;
	row_service_timestamp[bank] = gpu_sim_cycle;
	if (m_stats->concurrent_row_access[m_dram->id][bank]
			> m_stats->max_conc_access2samerow[m_dram->id][bank]) { /////the last row activation's information is not counted. There's a slight inaccuracy. There's no way to fix it.
		m_stats->max_conc_access2samerow[m_dram->id][bank] =
				m_stats->concurrent_row_access[m_dram->id][bank];
	}

	m_stats->concurrent_row_access[m_dram->id][bank] = 0; ///////////it is recounted here, whereas row_access is not.
	m_stats->num_activates[m_dram->id][bank]++;
}

dram_req_t *frfcfs_scheduler::schedule(unsigned bank, unsigned curr_row) { ////////the curr_row is actually the previous row.

	if (m_last_row[bank] == NULL) {
		if (m_queue[bank].empty())
			return NULL;

		std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator bin_ptr = m_bins[bank].find(curr_row); /////Nothing is sorted in m_queue and m_bins. They follow the order they came.
		if (bin_ptr == m_bins[bank].end()) {
			dram_req_t *req = m_queue[bank].back();
			bin_ptr = m_bins[bank].find(req->row);
			assert(bin_ptr != m_bins[bank].end()); // where did the request go???
			m_last_row[bank] = &(bin_ptr->second); //////////last row is the oldest row if same row is not found.
			data_collection(bank);
		} else {
			m_last_row[bank] = &(bin_ptr->second); //////////last row is still the current row if same row is found. (this means it is found only after new requests are added this cycle.)
		}
	}
	std::list<dram_req_t*>::iterator next = m_last_row[bank]->back(); /////////The oldest request.
	dram_req_t *req = (*next);

	m_stats->concurrent_row_access[m_dram->id][bank]++; //////just used to count the maximum number of continuous same row accesses(row hits + 1) for this bank. (only the non-removed ones.)
	m_stats->row_access[m_dram->id][bank]++; ////////////////row access is counted per request(not per bank operation). (only the non-removed ones.)
	m_last_row[bank]->pop_back(); //////bin_ptr

	m_queue[bank].erase(next);
	if (m_last_row[bank]->empty()) {
		m_bins[bank].erase(req->row);
		m_last_row[bank] = NULL;
	}
#ifdef DEBUG_FAST_IDEAL_SCHED
		if ( req )
		printf("%08u : DRAM(%u) scheduling memory request to bank=%u, row=%u\n",
				(unsigned)gpu_sim_cycle, m_dram->id, req->bk, req->row );
#endif
	assert(req != NULL && m_num_pending != 0);
	m_num_pending--;

	return req;
}

void frfcfs_scheduler::print(FILE *fp) {
	for (unsigned b = 0; b < m_config->nbk; b++) {
		printf(" %u: queue length = %u\n", b, (unsigned) m_queue[b].size());
	}
}

void dram_t::scheduler_frfcfs() {
	unsigned mrq_latency;
	frfcfs_scheduler *sched = m_frfcfs_scheduler;

	while (!mrqq->empty()
			&& (!m_config->gpgpu_frfcfs_dram_sched_queue_size
					|| sched->num_pending()
							< m_config->gpgpu_frfcfs_dram_sched_queue_size)) { ////add from mrqq to m_queue and m_bins until it is full
		dram_req_t *req = mrqq->pop();

		// Power stats
		//if(req->data->get_type() != READ_REPLY && req->data->get_type() != WRITE_ACK)
		m_stats->total_n_access++;

		if (req->data->get_type() == WRITE_REQUEST) {
			m_stats->total_n_writes++;
		} else if (req->data->get_type() == READ_REQUEST) {
			m_stats->total_n_reads++;
		}

		req->data->set_status(IN_PARTITION_MC_INPUT_QUEUE,
				gpu_sim_cycle + gpu_tot_sim_cycle);
		sched->add_req(req);
	}

////////////////////////////////////////////////////////////myeditAMC
///////////////////////////////////////////////////////////////////////bw profiling
	if(m_config->truncate_enabled){
		if (m_config->profiling_cycles_bw != 0
				&& ( n_cmd_partial % m_config->profiling_cycles_bw == 0) && n_cmd_partial != 0) { ///////////////////////profiling window size reached

			///////////////////////////////////////////distributed or overall scheduling?
			if ( m_config->distributed_scheduling ) { ////////distributed scheduling

				//double temp_normalized_bwutil_partial = temp_bwutil_partial / m_config->profiling_cycles_bw;

				/////////////////////////////////partial coverage
				float current_coverage_partial = 0;
				if (total_access_count_partial != 0) {
					current_coverage_partial = (float) (approximated_req_count_partial)
							/ (float) (total_access_count_partial);	///////////coverage control
				}

				/////////////////////////////////temp partial coverage
				float temp_coverage_partial = 0;
				if (total_access_count_temp_partial != 0) {
					temp_coverage_partial = (float) (approximated_req_count_temp_partial)
							/ (float) (total_access_count_temp_partial);///////////coverage control
				}

				if (m_config->dynamic_on) { ////////dynamic bfloat scheme

					if (temp_coverage_partial < 0.98 * target_coverage) {
						if(threshold_length_dynamic_partial >16){
							threshold_length_dynamic_partial--;
						}

					} else if ( temp_coverage_partial >= 0.98 * target_coverage
							&& temp_coverage_partial <= 1.01 * target_coverage ) {
						////////threshold_length kept unchanged

					} else { //////////////current_coverage > 1.01 * target_coverage
						if(threshold_length_dynamic_partial < 32){
							threshold_length_dynamic_partial++;
						}
					}////////////////////end of: if (current_coverage < 0.99 * target_coverage) {

				} else { ////////static bfloat scheme

				} ///////////////////////////////////////////////////////////end of: if (m_config->dynamic_on) { ////////dynamic bfloat scheme

			} else if(id == 5){ ////////overall scheduling

				//double temp_normalized_bwutil_all = temp_bwutil / (m_config->profiling_cycles_bw * 6);

				/////////////////////////////////overall coverage
				float current_coverage = 0;
				if (total_access_count_all != 0) {
					current_coverage = (float) (approximated_req_count_all)
							/ (float) (total_access_count_all);	///////////coverage control
				}

				/////////////////////////////////temp overall coverage
				float temp_coverage = 0;
				if (total_access_count_temp_all != 0) {
					temp_coverage = (float) (approximated_req_count_temp_all)
							/ (float) (total_access_count_temp_all);///////////coverage control
				}

				if (m_config->dynamic_on) { ////////dynamic bfloat scheme

					if (temp_coverage < 0.98 * target_coverage) {
						if(threshold_length_dynamic_all >16){
							threshold_length_dynamic_all--;
						}

					} else if ( temp_coverage >= 0.98 * target_coverage
							&& temp_coverage <= 1.01 * target_coverage ) {
						////////threshold_length kept unchanged

					} else { //////////////current_coverage > 1.01 * target_coverage
						if(threshold_length_dynamic_all < 32){
							threshold_length_dynamic_all++;
						}
					}////////////////////end of: if (current_coverage < 0.99 * target_coverage) {

	//				if( temp_normalized_bwutil_all > threshold_bw_dynamic_all ){ ///////////using bw in previous window as estimation for the next window, to decide can_truncate for the next window, not good
	//					can_truncate = 1;
	//				}

				} else { ////////static bfloat scheme

				} ///////////////////////////////////////////////////////////end of: if (m_config->dynamic_on) { ////////dynamic bfloat scheme
			}//////////end of: if (id == 5) { ////////overall scheduling



			////////////clear overall status
			if(id == 5){
				temp_gpu_sim_insn = 0; //ipc (ALL DRAMs)
				temp_gpu_sim_cycle = 0; //ipc (ALL DRAMs)

				temp_bwutil = 0; //bw (ALL DRAMs)
				temp_bwutil_global_read = 0; //bw gread (ALL DRAMs)
				temp_bwutil_global_write = 0; //bw gwrite (ALL DRAMs)

				approximated_req_count_temp_all = 0;
				total_access_count_temp_all = 0;
			}

			////////////clear partial status
			temp_bwutil_partial = 0; //bw (per DRAM)
			temp_bwutil_partial_gread = 0; //per DRAM
			temp_bwutil_partial_gwrite = 0; //per DRAM

			approximated_req_count_temp_partial = 0; //per DRAM
			total_access_count_temp_partial = 0; //per DRAM

		} ///////////////end of: if (m_config->profiling_cycles_bw != 0 && ( n_cmd_partial % m_config->profiling_cycles_bw == 0) ) {
	} /////////////end of: if(m_config->truncate_enabled){
///////////////////////////////////////////////////////////////////////bw profiling
////////////////////////////////////////////////////////////myeditAMC

	dram_req_t *req;
	unsigned i;
	for (i = 0; i < m_config->nbk; i++) {
		unsigned b = (i + prio) % m_config->nbk;
		if (!bk[b]->mrq) { //////////////////////only for banks that are not serving a request. Looking for the first empty bank (with null).

			req = sched->schedule(b, bk[b]->curr_row); ///////////the previous row activated.

			if (req) { ///////////////because of assert( req != NULL && m_num_pending != 0 ); this must be true

				////////////myedit bfloat
				unsigned can_truncate = 0;

				if(m_config->truncate_enabled){
					if(req->data->get_inst().oprnd_type == 1 && req->data->get_access_type() == GLOBAL_ACC_R ){ //////must be float and global read, does not have to be non-atomic ( !mf->is_access_atomic() ?)
						if ( m_config->distributed_scheduling ) { ////////distributed scheduling

							float current_coverage_partial = 0;
							if (total_access_count_partial != 0) {
								current_coverage_partial = (float) (approximated_req_count_partial)
										/ (float) (total_access_count_partial);	///////////coverage control
							}

							if(sched->num_pending() >= threshold_length_dynamic_partial && m_config->dynamic_on){
								if(current_coverage_partial < target_coverage){
									can_truncate = 1;
								}
							}else if(sched->num_pending() >= threshold_length_static_all && !m_config->dynamic_on){
								if(current_coverage_partial < target_coverage || target_coverage == 1){
									can_truncate = 1;
								}
							}

						}else{ ////////overall scheduling

							float current_coverage = 0;
							if (total_access_count_all != 0) {
								current_coverage = (float) (approximated_req_count_all)
										/ (float) (total_access_count_all);	///////////coverage control
							}

							if(sched->num_pending() >= threshold_length_dynamic_all && m_config->dynamic_on){
								if(current_coverage < target_coverage){
									can_truncate = 1;
								}
							}else if(sched->num_pending() >= threshold_length_static_all && !m_config->dynamic_on){
								if(current_coverage < target_coverage || target_coverage == 1){
									can_truncate = 1;
								}
							}

						} ////////end of: overall scheduling
					} ////////////////end of: if(req->data->get_inst().oprnd_type == 1){
				} ///////////////end of: if(m_config->truncate_enabled){

				if (m_config->remove_all) { ///////////////test scope
					if(req->data->get_inst().oprnd_type == 1 && req->data->get_access_type() == GLOBAL_ACC_R){ //////must be float and global read, does not have to be non-atomic ( !mf->is_access_atomic() ?)

						req->nbytes = req->nbytes / 2;

						if(m_config->approx_enabled){ ///////redo ld with approximate data or not

							req->data->set_approx(); //////mark this mf as approximated data
							req->data->set_truncate_ratio( current_truncate_ratio ); //////set truncate ratio
						}

						approximated_req_count_all++;
						approximated_req_count_temp_all++;
						approximated_req_count_partial++;
						approximated_req_count_temp_partial++;
					}

				}else{

					if(can_truncate == 1){

						req->nbytes = req->nbytes / 2;

						if(m_config->approx_enabled){ ///////redo ld with approximate data or not
							req->data->set_approx(); //////mark this mf as approximated data
							req->data->set_truncate_ratio( current_truncate_ratio ); //////set truncate ratio
						}

						approximated_req_count_all++;
						approximated_req_count_temp_all++;
						approximated_req_count_partial++;
						approximated_req_count_temp_partial++;
					}
				}

				if(req->data->get_inst().oprnd_type == 1){
					total_float_count_all++;
				}else if(req->data->get_inst().oprnd_type == 0){
					total_int_count_all++;
				}

				total_access_count_all++;
				total_access_count_temp_all++;
				total_access_count_partial++;
				total_access_count_temp_partial++;
				////////////myedit bfloat

				req->data->set_status(IN_PARTITION_MC_BANK_ARB_QUEUE,
						gpu_sim_cycle + gpu_tot_sim_cycle);
				prio = (prio + 1) % m_config->nbk;
				bk[b]->mrq = req; //////////////////assign for this bank and with this request.

				if (m_config->gpgpu_memlatency_stat) {
					mrq_latency = gpu_sim_cycle + gpu_tot_sim_cycle
							- bk[b]->mrq->timestamp;
					bk[b]->mrq->timestamp = gpu_tot_sim_cycle + gpu_sim_cycle;
					m_stats->mrq_lat_table[LOGB2(mrq_latency)]++;
					if (mrq_latency > m_stats->max_mrq_latency) {
						m_stats->max_mrq_latency = mrq_latency;
					}
				}

				break; ////////////only one bank can be assigned per cycle.
			}
		}
	}
}
