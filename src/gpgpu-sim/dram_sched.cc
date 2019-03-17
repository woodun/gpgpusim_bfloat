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

//////////////myedit AMC
#include "../approximate_memory_controller.h"
//////////////myedit AMC

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

	//////////////myedit AMC
	delay_queue = new std::list<dram_req_t*>[m_config->nbk];
	delay_bins = new std::map<unsigned,
			std::list<std::list<dram_req_t*>::iterator> >[m_config->nbk];

	if (dram_initialized == 0) {

		print_profile = m_config->print_profile;
		approx_enabled = m_config->approx_enabled;
		redo_in_l1 = m_config->redo_in_l1;
		always_fill = m_config->always_fill;
		searching_radius = m_config->searching_radius;
		dynamic_on = m_config->dynamic_on;
		auto_delay = m_config->auto_delay;
		profiling_cycles_es = m_config->profiling_cycles_es;
		warmup_cycles = m_config->warmup_cycles;
		l2_warmup_count = m_config->l2_warmup_count;
		min_bw = m_config->min_bw;
		activation_window = m_config->activation_window;
		request_window = m_config->request_window;
		reprofiling_cycles = m_config->reprofiling_cycles;
		//previous_delay_threshold = m_config->delay_threshold;
		//delay_threshold = m_config->delay_threshold;
		previous_delay_threshold = 128; //try starting with 128. change back later.
		delay_threshold = 128;
		profiling_cycles_bw = m_config->profiling_cycles_bw;

		//////////coverage control
		can_remove = 1; ////////////////controls which state allows remove, 0 is cannot
		previous_can_remove = 1;
		target_coverage = (float) (m_config->coverage) / 100;
		e_number = m_config->e_number;
		chosen_e = m_config->e_number;
		LT_schedule_count = 0;
		coverage_sufficiency_decided = 0;
		//chosen_e = 1;
		//////////coverage control

		priority = 0;

		std::string x_path = "profile_output.txt";
		profiling_output = std::fopen(x_path.c_str(), "w");

		dram_initialized = 1;
	}
	//////////////myedit AMC

	for (unsigned i = 0; i < m_config->nbk; i++) {
		m_queue[i].clear();
		m_bins[i].clear();
		m_last_row[i] = NULL;
		curr_row_service_time[i] = 0;
		row_service_timestamp[i] = 0;

		//////////////myedit AMC
		delay_queue[i].clear();
		delay_bins[i].clear();
		//////////////myedit AMC
	}
}

void frfcfs_scheduler::add_req(dram_req_t *req) {
	m_num_pending++;

	if (!m_config->delay_queue_size) {
		m_queue[req->bk].push_front(req);
		std::list<dram_req_t*>::iterator ptr = m_queue[req->bk].begin();
		m_bins[req->bk][req->row].push_front(ptr); //newest reqs to the front
	}

	//////////////myedit AMC

	/////6. test and debug this, set up new configs, implement the approximation part. (is it possible to use on miss? first, compare onfill and onmiss with profile.)

	if (m_config->delay_queue_size) {
		if (approx_enabled) {

			if (auto_delay == 2) {

				req->delay_time = m_dram->n_cmd_partial + delay_threshold;
			}

			std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator delay_bin_ptr =
					delay_bins[req->bk].find(req->row); /////////search delay bins first

			if (delay_bin_ptr == delay_bins[req->bk].end()) { ////////not found in delay bins

				std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator normal_bin_ptr =
						m_bins[req->bk].find(req->row); /////////search in the normal bins as well

				if (normal_bin_ptr == m_bins[req->bk].end()) { ////////not found in normal bins as well, add in delay queue

					req->row_size = 1; ///////////adding reference info for removal
					if (req->data->get_access_type() == GLOBAL_ACC_R) {

						req->read_only = 1;
					} else {

						req->read_only = 0;
					}

					////////////////////////l2 warmup info
					if (req->subpartition_id % 2 == 0) {

						req->subpar1_exists = 1;
					} else {

						assert(req->subpartition_id % 2 == 1);
						req->subpar2_exists = 1;
					}
					////////////////////////l2 warmup info

					delay_queue[req->bk].push_front(req);
					std::list<dram_req_t*>::iterator ptr =
							delay_queue[req->bk].begin();
					delay_bins[req->bk][req->row].push_front(ptr); //newest reqs to the front
				} else { ////////found in normal bins, add in normal queue (delay queue can be considered as a representative req of a row.)
					///It could end up here if it hits in the normal queue. If it's an issue hit, 'setting readonly is not needed' (issue does care read or write after all).
					///If it's an removing hit, if removed all at once (now using), 'setting readonly is not necessary' (it cannot even enter here). If remove one per cycle,
					///and if it's a write, the removing process should end immediately, and the oldest req in normal queue of this row should be moved to the delay queue with
					///a correct delay and 'reset readonly'. If there's only one queue, moving is not needed. (however, this requires a time stamp implementation. So we remove all in one cycle.)
					///issue hit has three cases. for remove hit, it cannot enter here, so there are only two cases. Therefore, no flag associating to remove needs to be set here.

					m_queue[req->bk].push_front(req);
					std::list<dram_req_t*>::iterator ptr =
							m_queue[req->bk].begin();
					m_bins[req->bk][req->row].push_front(ptr); //newest reqs to the front
				}
			} else { ////////found in delay bins, add in normal queue

				(*((delay_bin_ptr->second).back()))->row_size++; ///////////adding reference info for removal

				///if req still exists in delay queue, set readonly of it according to the added req. (remove is still available for this row.)
				if (req->data->get_access_type() != GLOBAL_ACC_R) {

					(*((delay_bin_ptr->second).back()))->read_only = 0;
				}

				////////////////////////l2 warmup info
				if (req->subpartition_id % 2 == 0) {

					(*((delay_bin_ptr->second).back()))->subpar1_exists = 1;
				} else {

					assert(req->subpartition_id % 2 == 1);
					(*((delay_bin_ptr->second).back()))->subpar2_exists = 1;
				}
				////////////////////////l2 warmup info

				m_queue[req->bk].push_front(req);
				std::list<dram_req_t*>::iterator ptr = m_queue[req->bk].begin();
				m_bins[req->bk][req->row].push_front(ptr); //newest reqs to the front
			} /////////////////end of: if (delay_bin_ptr == delay_bins[req->bk].end()) { ////////not found in delay bins

			/*
			 std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator delay_bin_ptr =
			 delay_bins[req->bk].find(req->row); /////////search delay bins first

			 if (delay_bin_ptr == delay_bins[req->bk].end()) { ////////not found in delay bins

			 std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator normal_bin_ptr =
			 m_bins[req->bk].find(req->row); /////////search in the normal bins as well

			 if (normal_bin_ptr == m_bins[req->bk].end()) { ////////not found in normal bins
			 ///////////do nothing here

			 assert(0);	/////////not possible, since it's just added.

			 } else {

			 if ((normal_bin_ptr->second).size() > 1) {////////////this row already exists

			 ///meet_in_normal_queue_all++; ////////////echo found in normal queue when adding to normal queue
			 ///meet_in_normal_queue[m_dram->id]++;

			 req->is_echo = 1;////////////only the one just added should be set to echo = 1.
			 } else {

			 ///////////do nothing here, the first one does not set echo.
			 }
			 }

			 } else {

			 come_after_all++; ////////////echo found in delay queue when adding to normal queue
			 come_after[m_dram->id]++;

			 req->is_echo = 1; ////////////both the ones in delay queue and normal queue should set echo = 1.

			 for (std::list<std::list<dram_req_t*>::iterator>::iterator iterator =
			 (delay_bin_ptr->second).begin();
			 iterator != (delay_bin_ptr->second).end(); ++iterator) {

			 (*(*iterator))->is_echo = 1; //////////////////not matter it's a read or write hit, for read the size > 1, for write the size > 1 and it becomes a 'read & write activation'.
			 }
			 } /////////////////end of: if (delay_bin_ptr == delay_bins[req->bk].end()) { ////////not found in delay bins
			 */

		} /////////end of: if (approx_enabled) {
	} /////////end of: if (m_config->delay_queue_size) {
//////////////myedit AMC
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

	////////////myedit LMC which pc is causing the thrashing?
	//if(m_stats->concurrent_row_access[m_dram->id][bank] == 1){


	//}
	////////////myedit LMC which pc is causing the thrashing?

	m_stats->concurrent_row_access[m_dram->id][bank] = 0; ///////////it is recounted here, whereas row_access is not.
	m_stats->num_activates[m_dram->id][bank]++;
}

dram_req_t *frfcfs_scheduler::schedule(unsigned bank, unsigned curr_row) { ////////the curr_row is actually the previous row.

//////////////myedit AMC
	if (m_config->delay_queue_size) {
		unsigned new_activation = 0;
		dram_req_t *req_to_return = NULL;
		unsigned normal_queue_handled = 0;

		if (approx_enabled) {

			if (m_queue[bank].empty() && delay_queue[bank].empty())
				return NULL;

			std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator delay_bin_ptr =
					delay_bins[bank].find(curr_row); /////////search delay bins first

			if (delay_bin_ptr == delay_bins[bank].end()) { ////////not found in delay bins

				std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator bin_ptr =
						m_bins[bank].find(curr_row); ////////search in normal bins if not found in delay bins

				if (bin_ptr == m_bins[bank].end()) { ////////not found in normal bins as well (new activation involved.)

					if (!delay_queue[bank].empty()) { ///////try to find new activations in delay queue first (find the ones whose locality get improved.)

						if (m_config->delay_only) { ///////////////not removed but issue.

							for (std::list<dram_req_t*>::reverse_iterator iterator =
									delay_queue[bank].rbegin();
									iterator != delay_queue[bank].rend();
									++iterator) { ///////////starting from back

								if ((auto_delay != 2
										&& (*iterator)->delay_time
												>= delay_threshold)
										|| (auto_delay == 2
												&& (*iterator)->delay_time
														<= m_dram->n_cmd_partial)) { /////////for delayonly, && !(*iterator)->is_echo is not needed, since both echo and not echo can be returned.*****redo delay_only-echo and delay_only-noecho

									if ((*iterator)->is_echo) {
										/////////////sanity check
										echo_in_delay_queue++;
									}

									dram_req_t *req_temp = (*iterator);
									std::map<unsigned,
											std::list<
													std::list<dram_req_t*>::iterator> >::iterator delay_bin_ptr =
											delay_bins[bank].find(
													req_temp->row);
									assert(
											delay_bin_ptr
													!= delay_bins[bank].end()); // where did the request go???

									unsigned is_approximable = approximate(
											req_temp);

									if (m_config->remove_all == 0
											&& is_approximable == 0) { //////////remove all is kind of profile mode
											////do nothing

									} else {

										/////////////sanity check and overlap check.
										candidate_0[m_dram->id]++; ////////////here it means the number of requests that have reached the delay threshold and issued.
										candidate_0_all++; ///////////////////right + wrong************************************///////////issued from delay queue
										removed_access_count_per_pc[req_temp->data->get_pc()]++;

										/////////////////////remove from the delay queue
										delay_queue[bank].erase(
												(++iterator).base());
										(delay_bin_ptr->second).pop_back();	////////////should check if it's the same request.
										if ((delay_bin_ptr->second).empty()) {

											delay_bins[bank].erase(
													req_temp->row);
										} else {

											assert(0); //////////////right now this cannot happen.
										}
										m_num_pending--;
										////////////////////remove from the delay queue

										////////////////////return this request
										req_to_return = req_temp; ///////////may get it from delay bins to be consistent
										new_activation = 1;

										break; //////////at most one request can be removed per cycle.
									} /////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
								} /////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion
							} /////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back
						} //////////end of: if (m_config->delay_only) { ///////////////not removed but issue.

						if (!m_config->no_echo) { ///////////////////////removing should also have two cases here
							if (new_activation == 0) { //////////////if nothing has reached the delay threshold
								for (std::list<dram_req_t*>::reverse_iterator iterator =
										delay_queue[bank].rbegin();
										iterator != delay_queue[bank].rend();
										++iterator) { ///////////starting from back

									if ((*iterator)->is_echo
											|| (((auto_delay != 2
													&& (*iterator)->delay_time
															>= delay_threshold)
													|| (auto_delay == 2
															&& (*iterator)->delay_time
																	<= m_dram->n_cmd_partial))
													&& (*iterator)->data->get_access_type()
															!= GLOBAL_ACC_R)) {

										req_to_return = (*iterator); ///////////may get it from delay bins to be consistent
										new_activation = 1;

										/////////////sanity check
										echo_in_delay_queue++;

										/////////////////////remove it from the delay bin and queue
										delay_queue[bank].erase(
												(++iterator).base());
										delay_bins[bank][req_to_return->row].pop_back();
										if (delay_bins[bank][req_to_return->row].empty()) {

											delay_bins[bank].erase(
													req_to_return->row);
										} else {

											assert(0); /////////right now only 0 hit row can exist in the delay queue, so it must be empty after removal.
										}
										m_num_pending--;
										////////////////////remove it from the delay bin and queue

										break;///////////at most one per cycle
									} /////////////end of: if ((*iterator)->is_echo)
								} /////////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[bank].rbegin(); iterator != delay_queue[bank].rend(); ++iterator)
							}
						} ////////////////end of: if (!m_config->no_echo)
						else {
							if (new_activation == 0) { //////////////if nothing has reached the delay threshold

								if (!m_config->delay_only) { ///////////////not removed but issue.

									for (std::list<dram_req_t*>::reverse_iterator iterator =
											delay_queue[bank].rbegin();
											iterator != delay_queue[bank].rend();
											++iterator) { ///////////starting from back

										if ((auto_delay != 2
												&& (*iterator)->delay_time
														>= delay_threshold)
												|| (auto_delay == 2
														&& (*iterator)->delay_time
																<= m_dram->n_cmd_partial)) { ////////////for removing case
											/////&& ((*iterator)->is_echo || (*iterator)->data->get_access_type() != GLOBAL_ACC_R) //////allow time-up echo to be issued only, no-time-up echo cannot be issued

											/////////////sanity check
											echo_in_delay_queue++;

											dram_req_t *req_temp = (*iterator);
											std::map<unsigned,
													std::list<
															std::list<
																	dram_req_t*>::iterator> >::iterator delay_bin_ptr =
													delay_bins[bank].find(
															req_temp->row);
											assert(
													delay_bin_ptr
															!= delay_bins[bank].end()); // where did the request go???

											unsigned is_approximable =
													approximate(req_temp);

											if (m_config->remove_all == 0
													&& is_approximable == 0) { //////////remove all is kind of profile mode
													////do nothing

											} else {

												/////////////sanity check and overlap check.
												//candidate_0[m_dram->id]++; ////////////here it means the number of requests that have reached the delay threshold and issued.
												//candidate_0_all++; ///////////////////right + wrong************************************
												//removed_access_count_per_pc[req_temp->data->get_pc()]++;

												/////////////////////remove from the delay queue
												delay_queue[bank].erase(
														(++iterator).base());
												(delay_bin_ptr->second).pop_back();	////////////should check if it's the same request.
												if ((delay_bin_ptr->second).empty()) {

													delay_bins[bank].erase(
															req_temp->row);
												} else {

													assert(0); //////////////right now this cannot happen.
												}
												m_num_pending--;
												////////////////////remove from the delay queue

												////////////////////return this request
												req_to_return = req_temp; ///////////may get it from delay bins to be consistent
												new_activation = 1;

												break; //////////at most one request can be removed per cycle.
											} /////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
										} /////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion

										if (0) {
											if (((auto_delay != 2
													&& (*iterator)->delay_time
															>= delay_threshold)
													|| (auto_delay == 2
															&& (*iterator)->delay_time
																	<= m_dram->n_cmd_partial))
													&& !(*iterator)->is_echo
													&& (*iterator)->data->get_access_type()
															== GLOBAL_ACC_R) { ////////////remove "in-place".

												dram_req_t *req_temp =
														(*iterator);
												std::map<unsigned,
														std::list<
																std::list<
																		dram_req_t*>::iterator> >::iterator delay_bin_ptr =
														delay_bins[bank].find(
																req_temp->row);
												assert(
														delay_bin_ptr
																!= delay_bins[bank].end()); // where did the request go???

												unsigned is_approximable =
														approximate(req_temp);

												if (m_config->remove_all == 0
														&& is_approximable
																== 0) { //////////remove all is kind of profile mode
														////do nothing

												} else {

													/////////////sanity check and overlap check.
													candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
													candidate_0_all++; ///////////////////right + wrong************************************
													removed_access_count_per_pc[req_temp->data->get_pc()]++;

													/////////////////////
													delay_queue[bank].erase(
															(++iterator).base());
													(delay_bin_ptr->second).pop_back();	////////////should check if it's the same request.
													if ((delay_bin_ptr->second).empty()) {

														delay_bins[bank].erase(
																req_temp->row);
													} else {

														assert(0); //////////////right now this cannot happen.
													}
													m_num_pending--;
													////////////////////

													////////////////////remove
													mem_fetch *data =
															req_temp->data;
													data->set_status(
															IN_PARTITION_MC_RETURNQ,
															gpu_sim_cycle
																	+ gpu_tot_sim_cycle);

													if (m_config->redo_approx) { ///////redo with approximate data or not
														data->set_approx(); //////mark this mf as approximated data
													}

													data->set_reply(); //////change the type from request to reply.
													m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

													delete req_temp;

													break; //////////at most one request can be removed per cycle.
													////////////////////remove
												}/////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
											}/////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion
										} ////////end of: if(0){

									} /////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back
								} //////////end of: if (m_config->delay_only) { ///////////////not removed but issue.
							} //////////end of: if (new_activation == 0)
						} //////////end of: else
					} /////////////end of: if (!delay_queue[bank].empty())

					if (0 && new_activation == 0) { /////////if nothing can be returned from delay queue

						if (!m_queue[bank].empty()) { ///////////////see if the last one is eligible to be returned (if not, it should be put in the delay queue)

							dram_req_t *req_temp = m_queue[bank].back();
							bin_ptr = m_bins[bank].find(req_temp->row);
							assert(bin_ptr != m_bins[bank].end()); // where did the request go???

									/*
									 if ((bin_ptr->second).size() == 1
									 //&& req_temp->data->get_access_type()
									 //		== GLOBAL_ACC_R
									 //&& !req_temp->data->is_access_atomic()
									 && !(req_temp->is_echo) ////////////////there isn't echo & no-hit in the normal queue, if it's echo, it must hit. it will hit even if it's no-echo.
									 ) { /////////size = 1 in new activation means a 0-hit request, it will be handled later on
									 //////////do nothing here

									 } else {//////////not satisfying the requirement, return the request as usual.

									 assert(!(req_temp->is_echo));///////it's not possible in echo, and should not be allowed in noecho(since it will cause hit in the delay queue).
									 ///////////So the only exit for echo in the normal queue is to be issued at hit.

									 /*
									 if ((bin_ptr->second).size() == 1
									 && req_temp->data->get_access_type()
									 == GLOBAL_ACC_R
									 && !req_temp->data->is_access_atomic()
									 && !(req_temp->is_echo)
									 && delay_queue[bank].size()
									 >= m_config->delay_queue_size) { ///////////////how many times it cannot be added because of size?

									 ////delay_queue_full_all++;
									 delay_queue_full[m_dram->id]++;
									 }
									 *//*

									 req_to_return = req_temp;
									 new_activation = 1;
									 normal_queue_handled = 1;

									 /////////////////////remove it from the normal bin and queue
									 m_queue[bank].pop_back();
									 m_bins[bank][req_to_return->row].pop_back();
									 if (m_bins[bank][req_to_return->row].empty()) {

									 m_bins[bank].erase(req_to_return->row);
									 } else {
									 //////////do nothing here, it's possible to be not empty.

									 }
									 m_num_pending--;
									 /////////////////////remove it from the normal bin and queue
									 }
									 */

							if ((bin_ptr->second).size() > 1
							//&& req_temp->data->get_access_type()
							//		== GLOBAL_ACC_R
							//&& !req_temp->data->is_access_atomic()
									&& !(req_temp->is_echo) ////////////////there isn't echo & no-hit in the normal queue, if it's echo, it must hit. it will hit even if it's no-echo.
									) { /////////size = 1 in new activation means a 0-hit request, it will be handled later on

								req_to_return = req_temp;
								new_activation = 1;
								normal_queue_handled = 1;

								/////////////////////remove it from the normal bin and queue
								m_queue[bank].pop_back();
								m_bins[bank][req_to_return->row].pop_back();
								if (m_bins[bank][req_to_return->row].empty()) {

									m_bins[bank].erase(req_to_return->row);
								} else {
									//////////do nothing here, it's possible to be not empty.

								}
								m_num_pending--;
								/////////////////////remove it from the normal bin and queue
							} //////////////end of: if ((bin_ptr->second).size() > 1 && !(req_temp->is_echo) ) {

						} /////////////end of:if (!m_queue[bank].empty()) { ///////////////see if the last one is eligible to be returned (if not, it should be put in the delay queue)
						else { ////////////////normal queue is empty

							return NULL; ////////////if nothing is in normal queue.
						} /////////////end of:if (!m_queue[bank].empty()) { ///////////////see if the last one is eligible to be returned (if not, it should be put in the delay queue)
					} //////////////end of: if (new_activation == 0) { /////////if nothing can be returned from delay queue

				} /////////////////end of: if (bin_ptr == m_bins[bank].end()) { ////////not found in normal bins also (new activation involved.)
				else { ////////found in normal bins but not in delay bins (no new activation)

					req_to_return = (*((bin_ptr->second).back()));
					normal_queue_handled = 1;

					/////////////////////remove it from the normal bin and queue
					m_queue[bank].erase((bin_ptr->second).back());
					(bin_ptr->second).pop_back();
					if ((bin_ptr->second).empty()) {

						m_bins[bank].erase(req_to_return->row);
					} else {
						//////////do nothing here, it's possible to be not empty.

					}
					m_num_pending--;
					////////////////////remove it from the normal bin and queue
				}

			} /////////////////end of: if (delay_bin_ptr == m_bins[bank].end()) { ////////not found in delay bins
			else { /////////found in delay_bins (no new activation)

				//////it is possible to enter here if more than 1 request from the same row is added to the delay queue at the same time, when size > 1 is also allowed to be added to the delay queue.*******
				///assert(0);

				req_to_return = (*((delay_bin_ptr->second).back()));

				/////////////sanity check
				hit_in_delay_queue++;

				/////////////////////remove it from the delay bin and queue
				delay_queue[bank].erase((delay_bin_ptr->second).back());
				(delay_bin_ptr->second).pop_back();
				if ((delay_bin_ptr->second).empty()) {

					delay_bins[bank].erase(req_to_return->row);
				} else {
					//////////do nothing here, it's possible to be not empty.

				}
				m_num_pending--;
				////////////////////remove it from the delay bin and queue
			} ///////////end of: /////////found in delay_bins (no new activation)

			if (0 && normal_queue_handled == 0) { //////////////trying to handle the normal queue at least once per cycle, if possible

				if (!m_queue[bank].empty()) {

					////////////here it should be able to bypass the echos in the back***********************
					if (1) {

						for (std::list<dram_req_t*>::reverse_iterator iterator =
								m_queue[bank].rbegin();
								iterator != m_queue[bank].rend(); ++iterator) { ///////////starting from back

							if (1 //////////all size can be added
							//&& (*iterator)->data->get_access_type() == GLOBAL_ACC_R
							//&& !(*iterator)->data->is_access_atomic()
									&& !((*iterator)->is_echo) ////////////////there isn't echo & no-hit in the normal queue, if it's echo, it must hit. it will hit even if it's no-echo.
									) { /////////size = 1 means a 0-hit request, not necessary in new activations.

								/////////////sanity check
								added_in_delay_queue++;

								dram_req_t *req_temp = (*iterator);
								std::map<unsigned,
										std::list<
												std::list<dram_req_t*>::iterator> >::iterator normal_bin_ptr =
										m_bins[bank].find(req_temp->row); //////////////it must exist.

								if ((normal_bin_ptr->second).size() > 1) { ///////////////////set echo for eligible ones.

									req_temp->is_echo = 1;
								}

								//////////////////add it to the delay queue
								delay_queue[bank].push_front(req_temp);
								std::list<dram_req_t*>::iterator delay_ptr =
										delay_queue[bank].begin();
								delay_bins[bank][req_temp->row].push_front(
										delay_ptr); //newest reqs to the front
								//////////////////add it to the delay queue

								/////////////////////remove it from the normal bin and queue
								m_queue[bank].erase((++iterator).base());
								(normal_bin_ptr->second).pop_back();
								if ((normal_bin_ptr->second).empty()) {

									m_bins[bank].erase(req_temp->row);
								} else {

									///////////it's possible here.
									////assert(0); /////////right now only 0 hit row can be moved into the delay queue, so it must be empty after removal.
								}

								break; /////////////only one can be served
								////////////////////remove it from the normal bin and queue

							} else { //////////not satisfying the requirement
								//////////do nothing here

							}////////////end of: if (1 && !((*iterator)->is_echo) ) {

						} /////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back

					} //////////////////end of: if (1) {
					else {

						dram_req_t *req_temp = m_queue[bank].back();
						std::map<unsigned,
								std::list<std::list<dram_req_t*>::iterator> >::iterator bin_ptr =
								m_bins[bank].find(req_temp->row);
						assert(bin_ptr != m_bins[bank].end()); // where did the request go???

						if ((bin_ptr->second).size() == 1
						//&& req_temp->data->get_access_type() == GLOBAL_ACC_R
						//&& !req_temp->data->is_access_atomic()
								&& !(req_temp->is_echo) ////////////////there isn't echo & no-hit in the normal queue, if it's echo, it must hit. it will hit even if it's no-echo.
								) { /////////size = 1 means a 0-hit request, not necessary in new activations.

							/////////////sanity check
							added_in_delay_queue++;

							//////////////////add it to the delay queue
							delay_queue[bank].push_front(req_temp);
							std::list<dram_req_t*>::iterator delay_ptr =
									delay_queue[bank].begin();
							delay_bins[bank][req_temp->row].push_front(
									delay_ptr); //newest reqs to the front
							//////////////////add it to the delay queue

							/////////////////////remove it from the normal bin and queue
							m_queue[bank].erase((bin_ptr->second).back());
							(bin_ptr->second).pop_back();
							if ((bin_ptr->second).empty()) {

								m_bins[bank].erase(req_temp->row);
							} else {

								assert(0); /////////right now only 0 hit row can be moved into the delay queue, so it must be empty after removal.
							}
							////////////////////remove it from the normal bin and queue

						} else { //////////not satisfying the requirement
							//////////do nothing here

							////assert(!(req_temp->is_echo));

							/*
							 if ((bin_ptr->second).size() == 1
							 && req_temp->data->get_access_type()
							 == GLOBAL_ACC_R
							 && !req_temp->data->is_access_atomic()
							 && !(req_temp->is_echo)
							 && delay_queue[bank].size()
							 >= m_config->delay_queue_size) {///////////////how many times it cannot be added because of size?

							 /////delay_queue_full_all++;
							 delay_queue_full[m_dram->id]++;
							 }
							 */
						}//////////end of: if ((bin_ptr->second).size() == 1 && !((*iterator)->is_echo) ) {
					} //////////////////end of: if (1) {

				} /////////////end of:if (!m_queue[bank].empty()) {
				else { ////////////////normal queue is empty
					   ////////////do nothing here, since it can be from the delay queue.

				} /////////////end of:if (!m_queue[bank].empty()) {
			} ///////////////end of: if (normal_queue_handled == 0) { //////////////trying to handle the normal queue at least once per cycle, if possible

			if (req_to_return != NULL) {
				if (new_activation) { //////////////////do after the removing part, since it shall not count if return NULL.

					data_collection(bank);

					if (concurrent_row_access_all[m_dram->id][bank] == 1) {	//////////////read and write

						overall_row_hit_0[m_dram->id]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)
						overall_row_hit_0_all++;
						all_rw_hit0_count_per_pc[temp_store[m_dram->id][bank].pc]++;/////////////read and write
					}

					if (concurrent_row_reads_neg[m_dram->id][bank] == 0 ///here use a different way to count and print single hit (need post processing to count the last one).
					&& concurrent_row_reads[m_dram->id][bank] == 1) { ///This can also be applied to count same_row_access_all.

						row_hit_0[m_dram->id]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)************************************
						row_hit_0_all++; /////////////this do not need to be moved to bottom, since it will be collected only once.(concurrent_row_reads is reset here.)

						if (m_dram->id == 0) {

							if (0 && m_config->print_profile) {

								std::fprintf(profiling_output,
										"address:%llu, sid:%u, pc:%u, wid:%u, age:%u, bk:%u, row:%u, col:%u\n",
										temp_store[m_dram->id][bank].addr,
										temp_store[m_dram->id][bank].sid,
										temp_store[m_dram->id][bank].pc,
										temp_store[m_dram->id][bank].wid,
										temp_store[m_dram->id][bank].age,
										temp_store[m_dram->id][bank].bk,
										temp_store[m_dram->id][bank].row,
										temp_store[m_dram->id][bank].col); /////post processing this as well
							}
							required_access_count_per_pc[temp_store[m_dram->id][bank].pc]++; ////////////////count all pcs for dram0
						} ////////////end of: if (m_dram->id == 0)

						all_required_access_count_per_pc[temp_store[m_dram->id][bank].pc]++; ////////////////read only count all pcs for all drams
					} /////////////////end of: if (concurrent_row_reads_neg[m_dram->id][bank] == 0 && concurrent_row_reads[m_dram->id][bank] == 1) {

					activation_num[m_dram->id]++; //////////////////not containing removed.
					activation_num_all++; //////////////////not containing removed.
					total_activation_num[m_dram->id]++; ////////////containing the removed.
					total_activation_num_all++; ////////////containing the removed.

					req_window_total_act_partial[m_dram->id]++; //////////contains the removed requests
					req_window_total_act++; //////////contains the removed requests
					temp_activation_num[m_dram->id]++; ////////profile
					temp_activation_num_all++; ////////profile

					readonly_activation_num[m_dram->id]++; /////////////////readonly
					readonly_activation_num_all++; /////////////////readonly

					unsigned previous_locality =
							concurrent_row_access_all[m_dram->id][bank];
					if (previous_locality <= 10 && previous_locality > 0) {
						assert(previous_locality > 0);

						accu_es[previous_locality - 1]++; //////////contains the removed requests
						req_window_es[previous_locality - 1]++; //////////contains the removed requests
						act_window_es[previous_locality - 1]++; //////////contains the removed requests
						pf_window_es[previous_locality - 1]++; //////////contains the removed requests

						accu_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						req_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						act_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						pf_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
					}

					concurrent_row_reads[m_dram->id][bank] = 0; ///////readonly
					concurrent_row_reads_neg[m_dram->id][bank] = 0;	//////////readonly, clear status
					concurrent_row_access_all[m_dram->id][bank] = 0;/////////read and write
				}

				count_profile(req_to_return, m_dram->id, bank); //////////////////only the non-removed ones will be counted.

				m_stats->concurrent_row_access[m_dram->id][bank]++; //////just used to count the maximum number of continuous same row accesses(row hits + 1) for this bank. (only the non-removed ones.)
				m_stats->row_access[m_dram->id][bank]++; ////////////////row access is counted per request(not per bank operation). (only the non-removed ones.)
			} ////////////end of: if (req_to_return != NULL)

			return req_to_return;
		} ///////////end of: if (approx_enabled)
//////////////myedit AMC
	} ////////////end of: if (m_config->delay_queue_size)
	else {

		unsigned new_activation = 0;

		if (m_last_row[bank] == NULL) { /////////////delete this: re-check every time can guarantee safety.*************
			if (m_queue[bank].empty())
				return NULL;

			std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator bin_ptr =
					m_bins[bank].find(curr_row); /////Nothing is sorted in m_queue and m_bins. They follow the order they came.
			if (bin_ptr == m_bins[bank].end()) {
				dram_req_t *req = m_queue[bank].back();
				bin_ptr = m_bins[bank].find(req->row);
				assert(bin_ptr != m_bins[bank].end()); // where did the request go???
				m_last_row[bank] = &(bin_ptr->second); //////////last row is the oldest row if same row is not found.

				////////moved to bottom.
				//data_collection(bank);///only collect when the previous row hit ends. concurrent_row_access[m_dram->id][bank] becomes 0 here. Also num_activates[m_dram->id][bank]++; here.

				//////////////myedit AMC
				if (approx_enabled) {

					if (concurrent_row_access_all[m_dram->id][bank] == 1) {	//////////////read and write

						overall_row_hit_0[m_dram->id]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)
						overall_row_hit_0_all++;
						all_rw_hit0_count_per_pc[temp_store[m_dram->id][bank].pc]++;/////////////////////read and write
					}

					if (concurrent_row_reads_neg[m_dram->id][bank] == 0 ///here use a different way to count and print single hit (need post processing to count the last one).
					&& concurrent_row_reads[m_dram->id][bank] == 1) { ///This can also be applied to count same_row_access_all.

						row_hit_0[m_dram->id]++; /////////////this is the real number. (in removing case nothing could enter here so this is 0.)************************************
						row_hit_0_all++; /////////////this do not need to be moved to bottom, since it will be collected only once.(concurrent_row_reads is reset here.)

						if (m_dram->id == 0) {

							if (0 && m_config->print_profile) {

								std::fprintf(profiling_output,
										"address:%llu, sid:%u, pc:%u, wid:%u, age:%u, bk:%u, row:%u, col:%u\n",
										temp_store[m_dram->id][bank].addr,
										temp_store[m_dram->id][bank].sid,
										temp_store[m_dram->id][bank].pc,
										temp_store[m_dram->id][bank].wid,
										temp_store[m_dram->id][bank].age,
										temp_store[m_dram->id][bank].bk,
										temp_store[m_dram->id][bank].row,
										temp_store[m_dram->id][bank].col); /////post processing this as well
							}

							required_access_count_per_pc[temp_store[m_dram->id][bank].pc]++; ////////////////count all pcs for dram0
						}

						all_required_access_count_per_pc[temp_store[m_dram->id][bank].pc]++; ////////////////read only count all pcs for all drams
					}

					///activation_num[m_dram->id]++;////////moved to bottom.
					///activation_num_all++; //////////////////just total number of activations, not satisfying the prediction requirement.////////moved to bottom.

					unsigned previous_locality =
							concurrent_row_access_all[m_dram->id][bank];
					if (previous_locality <= 10 && previous_locality > 0) {
						assert(previous_locality > 0);

						accu_es[previous_locality - 1]++; //////////contains the removed requests
						req_window_es[previous_locality - 1]++; //////////contains the removed requests
						act_window_es[previous_locality - 1]++; //////////contains the removed requests
						pf_window_es[previous_locality - 1]++; //////////contains the removed requests

						accu_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						req_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						act_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
						pf_window_es_partial[m_dram->id][previous_locality - 1]++; //////////contains the removed requests
					}

					concurrent_row_reads[m_dram->id][bank] = 0;
					concurrent_row_reads_neg[m_dram->id][bank] = 0;	//////////clear status
					concurrent_row_access_all[m_dram->id][bank] = 0;/////////read and write
					new_activation = 1;
				}	//////////////end of: if (approx_enabled) {

				//////////////myedit AMC
			} else {
				m_last_row[bank] = &(bin_ptr->second); //////////last row is still the current row if same row is found. (this means it is found only after new requests are added this cycle.)

				//////////////myedit AMC
				if (approx_enabled) {
					come_after_all++; ////////////////hit requests which only come in next cycle
					come_after[m_dram->id]++;

					if (concurrent_row_reads_neg[m_dram->id][bank] == 0
							&& concurrent_row_reads[m_dram->id][bank] == 1) {

						/////////////sanity check
						echo_in_delay_queue++; ////////////////here it means there's a hit request added exactly after the request with locality 1.redo profile.***********
					}
				}
				//////////////myedit AMC
			}
		}
		std::list<dram_req_t*>::iterator next = m_last_row[bank]->back(); /////////The oldest request.
		dram_req_t *req = (*next);

//////////////myedit AMC
		if (approx_enabled) {

			if (m_last_row[bank]->size() == 1 /////////the current way of finding candidates. stop when a good locality access is found (FCFS order).
			&& req->data->get_access_type() == GLOBAL_ACC_R
					&& !req->data->is_access_atomic() && new_activation) { ////////new_activation or req->row != curr_row, it's basically the same.

				/////////////sanity check and overlap check.
				candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
				candidate_0_all++; ///////////////////right + wrong************************************//////////profile
				removed_access_count_per_pc[req->data->get_pc()]++;

				unsigned is_approximable = approximate(req); ///////////////myedit trackmf*********************************

				////////////
				/*
				 if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode

				 break; /////only the ones that are removable can proceed to the following process (for motivation it should be the original status and not reduce anything, make sure it breaks here.)
				 }
				 */
				////////////
				if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
				////do nothing

				} else {

					if (!m_dram->returnq->full()) {
						/////////////////////
						m_last_row[bank]->pop_back();
						m_queue[bank].erase(next);
						if (m_last_row[bank]->empty()) {

							m_bins[bank].erase(req->row);
							m_last_row[bank] = NULL;
						} else {

							assert(0);
						}
						m_num_pending--;
						////////////////////

						////////////////////
						mem_fetch *data = req->data;
						data->set_status(IN_PARTITION_MC_RETURNQ,
								gpu_sim_cycle + gpu_tot_sim_cycle);

						if (m_config->redo_approx) { ///////redo with approximate data or not
							data->set_approx(); //////mark this mf as approximated data
						}

						data->set_reply(); //////change the type from request to reply.
						m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

						delete req;

						return NULL; /////////////we will not check or return the next 0-hit-activation, but instead just bypass this bank for this cycle.
						////////////////////
					} else {					/////do nothing if non-removable

						return NULL; /////////////we will not check or return the next 0-hit-activation, but instead just bypass this bank for this cycle.
					}
				}

				////////////
				/*
				 if (m_last_row[bank] == NULL) {
				 if (m_queue[bank].empty()) {

				 return NULL;
				 }

				 dram_req_t *req2 = m_queue[bank].back(); ///////////also following the FCFS order.
				 std::map<unsigned, std::list<std::list<dram_req_t*>::iterator> >::iterator bin_ptr2 =
				 m_bins[bank].find(req2->row);
				 assert(bin_ptr2 != m_bins[bank].end()); // where did the request go???
				 m_last_row[bank] = &(bin_ptr2->second); //////////last row is the oldest row if same row is not found.
				 } else {

				 assert(0);
				 }
				 next = m_last_row[bank]->back(); /////////The oldest request.
				 req = (*next);
				 */
				////////////
			} ////////////////end of: while (req->data->get_access_type() == GLOBAL_ACC_R && m_last_row[bank]->size() == 1 && !req->data->is_access_atomic() && new_activation)

			if (new_activation) { //////////////////do after the removing part, since it shall not count if return NULL.

				data_collection(bank);

				activation_num[m_dram->id]++;
				activation_num_all++; ////////////not containing the removed.
				total_activation_num[m_dram->id]++; ////////////containing the removed.
				total_activation_num_all++; ////////////containing the removed.

				req_window_total_act_partial[m_dram->id]++; //////////contains the removed requests
				req_window_total_act++; //////////contains the removed requests
				temp_activation_num[m_dram->id]++; ////////profile
				temp_activation_num_all++; ////////profile

				readonly_activation_num[m_dram->id]++; /////////////////readonly
				readonly_activation_num_all++; /////////////////readonly
			}

			count_profile(req, m_dram->id, bank); //////////////////only the non-removed ones will be counted.
		} ////////////////end of: if (m_config->approx_enabled)
//////////////myedit AMC

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
	} //////end of: else{ //////if delay_queue_size is 0, delay_threshold is not even used. So no delay here, it's just profile and remove nothing or remove all e1s (when remove_all = 1).
} /////////////

void frfcfs_scheduler::print(FILE *fp) {
	for (unsigned b = 0; b < m_config->nbk; b++) {
		printf(" %u: queue length = %u\n", b, (unsigned) m_queue[b].size());
	}
}

//////////////myedit AMC
void frfcfs_scheduler::increment_and_remove(unsigned n_cmd_partial) {

	if (m_config->delay_queue_size) {
		if (approx_enabled) {

			unsigned removed = 0;

			for (int i = 0; i < (int) (m_config->nbk); i++) {

				unsigned k = (i + priority) % m_config->nbk;

				if (auto_delay != 2) { /////////////auto_delay != 2 use the timestamp, thus will not increment.
					////////////increment and check every element in every bank
					for (std::list<dram_req_t*>::reverse_iterator iterator =
							delay_queue[k].rbegin();
							iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back

						(*iterator)->delay_time++;
					}
				}

				if (0 && !m_config->delay_only) {

					if (removed == 0) {

						if (!m_dram->returnq->full()) {

							for (std::list<dram_req_t*>::reverse_iterator iterator =
									m_queue[k].rbegin();
									iterator != m_queue[k].rend(); ++iterator) { ///////////starting from back of normal queue, find the hit ones first

								if ((*iterator)->row
										== last_removed_row[m_dram->id][k]) { ///////////////removing consecutive hit rows in normal queue

									assert(
											(*iterator)->data->get_access_type()
													== GLOBAL_ACC_R); ///////a row under remove can only contain reads. stop if other types were added.

									if ((*iterator)->data->get_access_type()
											!= GLOBAL_ACC_R) { ///////a row under remove can only contain reads. stop if other types were added.

										break;
									}

									///assert((*iterator)->is_echo == 1	&& (*iterator)->data->get_access_type()
									///== GLOBAL_ACC_R); ///////a row under remove can only contain reads. noecho also cannot enter, because the hit streak breaks when the row becomes empty.

									dram_req_t *req_temp = (*iterator);
									std::map<unsigned,
											std::list<
													std::list<dram_req_t*>::iterator> >::iterator normal_bin_ptr =
											m_bins[k].find(req_temp->row);
									assert(normal_bin_ptr != m_bins[k].end()); // where did the request go???

									unsigned is_approximable = approximate(
											req_temp);

									if (m_config->remove_all == 0
											&& is_approximable == 0) { //////////remove all is kind of profile mode
											////do nothing

									} else {

										/////////////sanity check and overlap check.
										candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
										candidate_0_all++; ///////////////////right + wrong************************************
										removed_access_count_per_pc[req_temp->data->get_pc()]++;

										/////////////////////
										m_queue[k].erase((++iterator).base());
										(normal_bin_ptr->second).pop_back(); ////////////should check if it's the same request. it's ok for now.
										if ((normal_bin_ptr->second).empty()) {

											m_bins[k].erase(req_temp->row);

										} else {

											//////////assert(0); //////////////in normal queue this can happen.
										}
										m_num_pending--;
										////////////////////

										////////////////////remove
										mem_fetch *data = req_temp->data;
										data->set_status(
												IN_PARTITION_MC_RETURNQ,
												gpu_sim_cycle
														+ gpu_tot_sim_cycle);

										if (m_config->redo_approx) { ///////redo with approximate data or not
											data->set_approx(); //////mark this mf as approximated data
										}

										data->set_reply(); //////change the type from request to reply.
										m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

										delete req_temp;

										priority = (priority + 1)
												% m_config->nbk; ///////////////////increment prio

										removed = 1; //////////at most one request can be removed per cycle per all banks.

										///////////coverage control
										total_access_count[m_dram->id]++;
										total_access_count_all++;
										total_access_count_temp_all++;

										approximated_req_count[m_dram->id]++;
										approximated_req_count_all++;
										approximated_req_count_temp_all++;
										///////////coverage control

										//////////dynamic e
										act_window_total_req_partial[m_dram->id]++;
										act_window_total_req++;
										//////////dynamic e

										break;//////////at most one request can be removed per cycle per bank.
										////////////////////remove
									}/////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
								}/////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion
							}/////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back
						} ///////end of: if (!m_dram->returnq->full()) {
					} //////end of: if (removed == 0) {

					if (removed == 0) {

						last_removed_row[m_dram->id][k] = 0; //////////stop the streak if no more remove hit for this bank can be found.
					}

				} //////////end of: if (!m_config->delay_only) {
			} /////////end of: for (int k = 0; k < m_config->nbk; k++) {

			if (!m_config->delay_only) {

				if (removed == 0) {	//////////////only one remove can be done per cycle.

					if (!m_dram->returnq->full()) {

						float current_coverage = 0;

						/*
						 if (total_access_count[m_dram->id] != 0) {
						 current_coverage =
						 (float) (approximated_req_count[m_dram->id])
						 / (float) (total_access_count[m_dram->id]);	///////////coverage control (per dram)
						 }
						 */

						///////////try with overall coverage now.***************todo: compare with using partial coverage.
						if (total_access_count_all != 0) {
							current_coverage =
									(float) (approximated_req_count_all)
											/ (float) (total_access_count_all);	///////////coverage control (per dram)
						}

						if ((current_coverage < target_coverage
								&& (can_remove || !auto_delay)) || dynamic_on) {/////////////coverage control of naive remove scheme (static). coverage control of dynamic e is moved down.

							unsigned remove_available = 0;
							unsigned selected_bank = 0;
							unsigned min_row_size = 1000000;////////smallest row locality found
							unsigned searching_done = 0;

							if (e_number != 0) {///////e_number = 0 indicates no scheme.////////////todo: static with first row. the number of request removed in aw or rw may be constant between window.
								//////////////////////////////////But there's no guarantee for pw it's true, so how can we remove during profiling? just don't care? let's try with that. for static and none case,
								/////////just remove the first one, even if returnq is not available, do not search for smaller ones but just quit. dynamic can just use the previous implementation for simplicity.

								if (dynamic_on) {
									//min_row_size = 9;//////e no limit removed.
								} else {
									min_row_size = e_number + 1;/////////////limit the e that can be removed in the naive scheme. set e_number = 0 or a very large number to remove this limitation.
								}
							}

							std::list<dram_req_t*>::reverse_iterator iterator_in_bank;

							for (int i = 0; i < (int) (m_config->nbk); i++) { /////loop through all banks to find the smallest e if coverage permits in the naive scheme. When no scheme, just choose the first one.

								unsigned k = (i + priority) % m_config->nbk; ////////////priority.

								for (std::list<dram_req_t*>::reverse_iterator iterator =
										delay_queue[k].rbegin();
										iterator != delay_queue[k].rend();
										++iterator) { ///////////starting from back of delay queue, secondly, find the new valid one based on coverage and priority, rows contains write cannot be removed.

									if (((auto_delay != 2
											&& (*iterator)->delay_time
													>= delay_threshold)
											|| (auto_delay == 2
													&& (*iterator)->delay_time
															<= m_dram->n_cmd_partial))
											&& (*iterator)->data->get_access_type()
													== GLOBAL_ACC_R) {

										unsigned current_row_size =
												(*iterator)->row_size;

										if ((m_dram->subchannel1_warmed_up
												|| !(*iterator)->subpar1_exists)
												&& (m_dram->subchannel2_warmed_up
														|| !(*iterator)->subpar2_exists)) { ///////////l2 warm up check

											if ((*iterator)->read_only) { ///////row must only contain reads.

												if (current_row_size
														< min_row_size) { ////////////update with the row of min locality

													min_row_size =
															current_row_size;
													selected_bank = k;
													iterator_in_bank = iterator; //////////row with the min e is selected.

													if (m_dram->returnq->available(
															min_row_size)) { ////////////////future ones will be smaller, so they are still available.
														remove_available = 1;

														//////////for static and none scheme, if the first row that satisfies e limit has no space in returnq, just do nothing this cycle.///////////anyway we quit on the first row we found for static.
														if (m_config->delay_queue_size
																!= 14) { ////a special mode that enables searchig for static
															if (e_number == 0
																	|| !dynamic_on) { ///////e_number = 0 indicates none scheme. so just pick the first row. when returnq is jammed, it will pass this round however.
																searching_done =
																		1;
																break;
															} //////////just implement the coverage and statistics version for period and try, also compare with the static and none version.*****************************todo
														}
													} else {
														//////////for static and none scheme, if the first row that satisfies e limit has no space in returnq, just do nothing this cycle.
														if (m_config->delay_queue_size
																!= 14) { ////a special mode that enables searchig for static
															if (e_number == 0
																	|| !dynamic_on) { ///////e_number = 0 indicates none scheme.
																searching_done =
																		1;
																break;
															}
														}
													}
												} ////////end of: if (current_row_size < min_row_size) { ////////////update with the row of min locality
											} ///////end of: if ((*iterator)->read_only) {
										} ///////////l2 warm up check
									} /////////////end of: if ((*iterator)->delay_time >= delay_threshold && (*iterator)->data->get_access_type() == GLOBAL_ACC_R) {
								} ///////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) {

								if (searching_done) { //////just pick the first row and halt if no scheme.
									break;
								}
							} //////////////end of: for (int i = 0; i < m_config->nbk; i++) { /////////////////loop through all banks to find the one if coverage permits

							if (remove_available) { //////////at least one req is timed up

								if ((((m_config->delay_queue_size == 16
										&& ((min_row_size == chosen_e
												&& current_coverage
														< target_coverage)
												|| min_row_size < chosen_e))
										|| (m_config->delay_queue_size == 15
												&& (min_row_size <= chosen_e
														&& current_coverage
																< target_coverage)))
										&& (can_remove || !auto_delay))
										|| !dynamic_on) { ///////////////////////coverage control of dynamic e. chosen_e should be less than given e_number.

									////////////dynamic e
									total_activation_num[m_dram->id]++; ////////////containing the removed.
									total_activation_num_all++; ////////////containing the removed.

									req_window_total_act_partial[m_dram->id]++; //////////contains the removed requests
									req_window_total_act++; //////////contains the removed requests

									if (min_row_size <= 10
											&& min_row_size > 0) {
										assert(min_row_size > 0);

										accu_es[min_row_size - 1]++; //////////contains the removed requests
										req_window_es[min_row_size - 1]++; //////////contains the removed requests
										act_window_es[min_row_size - 1]++; //////////contains the removed requests
										pf_window_es[min_row_size - 1]++; //////////contains the removed requests

										accu_es_partial[m_dram->id][min_row_size
												- 1]++; //////////contains the removed requests
										req_window_es_partial[m_dram->id][min_row_size
												- 1]++; //////////contains the removed requests
										act_window_es_partial[m_dram->id][min_row_size
												- 1]++; //////////contains the removed requests
										pf_window_es_partial[m_dram->id][min_row_size
												- 1]++; //////////contains the removed requests
									}
									////////////dynamic e

									dram_req_t *req_temp = (*iterator_in_bank);
									std::map<unsigned,
											std::list<
													std::list<dram_req_t*>::iterator> >::iterator delay_bin_ptr =
											delay_bins[selected_bank].find(
													req_temp->row);

									unsigned is_approximable = approximate(
											req_temp);

									if (m_config->remove_all == 0
											&& is_approximable == 0) { //////////remove all is kind of profile mode
											////do nothing

									} else {

										/////////////sanity check and overlap check.
										candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
										candidate_0_all++; ///////////////////right + wrong************************************
										removed_access_count_per_pc[req_temp->data->get_pc()]++;

										/////////////////////
										delay_queue[selected_bank].erase(
												(++iterator_in_bank).base());
										(delay_bin_ptr->second).pop_back();	////////////should check if it's the same request.
										if ((delay_bin_ptr->second).empty()) {

											delay_bins[selected_bank].erase(
													req_temp->row);
										} else {

											assert(0); //////////////right now this cannot happen.
										}
										m_num_pending--;
										////////////////////

										////////////////////remove
										mem_fetch *data = req_temp->data;
										data->set_status(
												IN_PARTITION_MC_RETURNQ,
												gpu_sim_cycle
														+ gpu_tot_sim_cycle);

										if (m_config->redo_approx) { ///////redo with approximate data or not
											data->set_approx(); //////mark this mf as approximated data
										}

										data->set_reply(); //////change the type from request to reply.
										m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

										last_removed_row[m_dram->id][selected_bank] =
												req_temp->row; //////////////record last removed row.

										/////////autodelay
										////bk[j]->mrq->txbytes += m_config->dram_atom_size;
										temp_bwutil +=
												(ceil(
														(double) req_temp->nbytes
																/ (double) m_config->dram_atom_size))
														* m_config->BL
														/ m_config->data_command_freq_ratio;///overall, not counted in cumulative bw, this corresponds to the increased performance estimated by delayauto.
										/////////autodelay

										delete req_temp;

										priority = (priority + 1)
												% m_config->nbk; ///////////////////increment prio

										removed = 1; //////////at most one request can be removed per cycle per all banks.
										////(now also remove all hit reqs in the normal queue in one cycle, but is explained by multiple cycles.)

										///////////coverage control
										total_access_count[m_dram->id]++;
										total_access_count_all++;
										total_access_count_temp_all++;

										approximated_req_count[m_dram->id]++;
										approximated_req_count_all++;
										approximated_req_count_temp_all++;
										///////////coverage control

										//////////dynamic e
										act_window_total_req_partial[m_dram->id]++;
										act_window_total_req++;
										//////////dynamic e

										////////////////////remove

									}/////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode

									//////////new removing part from the normal queue with better efficiency
									std::map<unsigned,
											std::list<
													std::list<dram_req_t*>::iterator> >::iterator normal_bin_ptr =
											m_bins[selected_bank].find(
													last_removed_row[m_dram->id][selected_bank]);

									if (!(normal_bin_ptr->second).empty()) {//////////not empty

										for (std::list<dram_req_t*>::reverse_iterator iterator =
												m_queue[selected_bank].rbegin();
												iterator
														!= m_queue[selected_bank].rend();
												) { ///////////starting from back of normal queue, find the hit ones first, remove all hit reqs in the normal queue

											if ((*iterator)->row
													== last_removed_row[m_dram->id][selected_bank]) { ///////////////removing consecutive hit rows in normal queue

												assert(
														(*iterator)->data->get_access_type()
																== GLOBAL_ACC_R); ///////a row under remove can only contain reads. stop if other types were added.
												//////it can only be read here, otherwise readonly flag in delay queue wouldn't have been set, and thus it cannot even enter here.

												if ((*iterator)->data->get_access_type()
														!= GLOBAL_ACC_R) { ///////a row under remove can only contain reads. stop if other types were added.

													break;
												}

												///assert((*iterator)->is_echo == 1	&& (*iterator)->data->get_access_type()
												///== GLOBAL_ACC_R); ///////a row under remove can only contain reads. noecho also cannot enter, because the hit streak breaks when the row becomes empty.

												dram_req_t *req_temp =
														(*iterator);

												assert(
														normal_bin_ptr
																!= m_bins[selected_bank].end()); //////as we found this row in normal queue, this row cannot be empty.

												unsigned is_approximable =
														approximate(req_temp);

												if (m_config->remove_all == 0
														&& is_approximable
																== 0) { //////////remove all is kind of profile mode
														////do nothing

												} else {

													/////////////sanity check and overlap check.
													candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
													candidate_0_all++; ///////////////////right + wrong************************************
													removed_access_count_per_pc[req_temp->data->get_pc()]++;

													////////////////////remove
													mem_fetch *data =
															req_temp->data;
													data->set_status(
															IN_PARTITION_MC_RETURNQ,
															gpu_sim_cycle
																	+ gpu_tot_sim_cycle);

													if (m_config->redo_approx) { ///////redo with approximate data or not
														data->set_approx(); //////mark this mf as approximated data
													}

													data->set_reply(); //////change the type from request to reply.
													m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

													/////////autodelay
													////bk[j]->mrq->txbytes += m_config->dram_atom_size;
													temp_bwutil +=
															(ceil(
																	(double) req_temp->nbytes
																			/ (double) m_config->dram_atom_size))
																	* m_config->BL
																	/ m_config->data_command_freq_ratio;///overall, not counted in cumulative bw, this corresponds to the increased performance estimated by delayauto.
													/////////autodelay

													delete req_temp;

													//priority = (priority + 1)
													//		% m_config->nbk; ///////////////////increment prio

													removed = 1; //////////at most one request can be removed per cycle per all banks.

													///////////coverage control
													total_access_count[m_dram->id]++;
													total_access_count_all++;
													total_access_count_temp_all++;

													approximated_req_count[m_dram->id]++;
													approximated_req_count_all++;
													approximated_req_count_temp_all++;
													///////////coverage control

													//////////dynamic e
													act_window_total_req_partial[m_dram->id]++;
													act_window_total_req++;
													//////////dynamic e

													/////////autodelay
													temp_bwutil +=
															m_config->BL
																	/ m_config->data_command_freq_ratio; ///overall, not counted in cumulative bw, this corresponds to the increased performance estimated by delayauto.
													/////////autodelay

													/////////////////////handle and increment iterator, erase only works for non-reversed iterators
													iterator =
															std::list<
																	dram_req_t*>::reverse_iterator(
																	m_queue[selected_bank].erase(
																			std::next(
																					iterator).base()));
													(normal_bin_ptr->second).pop_back(); ////////////should check if it's the same request. it's ok for now.

													m_num_pending--; /////////removed from the queue

													if ((normal_bin_ptr->second).empty()) {

														m_bins[selected_bank].erase(
																last_removed_row[m_dram->id][selected_bank]);

														break; ///////////exit when this row is empty
													} else {

														//////////assert(0); //////////////in normal queue this can happen.
													}
													////////////////////handle iterator

													////break;//////////at most one request can be removed per cycle per bank.
													////////////////////remove
												}/////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
											}/////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion
											else {

												++iterator;
											}
										}/////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back
									}///////end of: if (!(normal_bin_ptr->second).empty()) {//////////not empty
									 //////////new removing part from the normal queue with better efficiency

								}////////////////end of:if ((((min_row_size == chosen_e && current_coverage < target_coverage) || min_row_size < chosen_e) && (can_remove || !auto_delay)) || !dynamic_on) {
							}////////end of: if (remove_available) { //////////at least one req is timed up

							if (0) {
								for (int i = 0; i < (int) (m_config->nbk);
										i++) {

									unsigned k = (i + priority) % m_config->nbk;

									for (std::list<dram_req_t*>::reverse_iterator iterator =
											delay_queue[k].rbegin();
											iterator != delay_queue[k].rend();
											++iterator) { ///////////starting from back of delay queue, secondly, find the new valid one based on coverage and priority, rows contains write cannot be removed.

										if (((auto_delay != 2
												&& (*iterator)->delay_time
														>= delay_threshold)
												|| (auto_delay == 2
														&& (*iterator)->delay_time
																<= m_dram->n_cmd_partial))
												&& !(*iterator)->is_echo
												&& (*iterator)->data->get_access_type()
														== GLOBAL_ACC_R) { ///////////////satisfies removing criterion, only remove time-up no-echo ones, echo ones cannot be removed. (also GLOBAL_ACC_R only.)

											dram_req_t *req_temp = (*iterator);
											std::map<unsigned,
													std::list<
															std::list<
																	dram_req_t*>::iterator> >::iterator delay_bin_ptr =
													delay_bins[k].find(
															req_temp->row);
											assert(
													delay_bin_ptr
															!= delay_bins[k].end()); // where did the request go???

											unsigned is_approximable =
													approximate(req_temp);

											if (m_config->remove_all == 0
													&& is_approximable == 0) { //////////remove all is kind of profile mode
													////do nothing

											} else {

												/////////////sanity check and overlap check.
												candidate_0[m_dram->id]++; /////////////this is the found number. (this should be referred to only in the removing case, in motivation it's better data and not accurate.)
												candidate_0_all++; ///////////////////right + wrong************************************
												removed_access_count_per_pc[req_temp->data->get_pc()]++;

												/////////////////////
												delay_queue[k].erase(
														(++iterator).base());
												(delay_bin_ptr->second).pop_back();	////////////should check if it's the same request.
												if ((delay_bin_ptr->second).empty()) {

													delay_bins[k].erase(
															req_temp->row);
												} else {

													assert(0); //////////////right now this cannot happen.
												}
												m_num_pending--;
												////////////////////

												////////////////////remove
												mem_fetch *data = req_temp->data;
												data->set_status(
														IN_PARTITION_MC_RETURNQ,
														gpu_sim_cycle
																+ gpu_tot_sim_cycle);

												if (m_config->redo_approx) { ///////redo with approximate data or not
													data->set_approx(); //////mark this mf as approximated data
												}

												data->set_reply(); //////change the type from request to reply.
												m_dram->returnq->push(data); /////and will proceed to dram_L2_queue later on.

												last_removed_row[m_dram->id][k] =
														req_temp->row; //////////////record last removed row.

												delete req_temp;

												priority = (priority + 1)
														% m_config->nbk; ///////////////////increment prio

												removed = 1; //////////at most one request can be removed per cycle per all banks.

												///////////coverage control
												total_access_count[m_dram->id]++;
												total_access_count_all++;
												total_access_count_temp_all++;

												approximated_req_count[m_dram->id]++;
												approximated_req_count_all++;
												approximated_req_count_temp_all++;
												///////////coverage control

												//////////dynamic e
												act_window_total_req_partial[m_dram->id]++;
												act_window_total_req++;
												//////////dynamic e

												break;//////////at most one request can be removed per cycle per bank.
												////////////////////remove
											}/////////end of: if (m_config->remove_all == 0 && is_approximable == 0) { //////////remove all is kind of profile mode
										}/////////end of: if ((*iterator)->delay_time >= m_config->delay_thresold && !(*iterator)->is_echo) { ///////////////satisfies removing criterion
									}/////////end of: for (std::list<dram_req_t*>::reverse_iterator iterator = delay_queue[k].rbegin(); iterator != delay_queue[k].rend(); ++iterator) { ///////////starting from back
								} /////////end of: for (int k = 0; k < m_config->nbk; k++) {
							} /////////end of: if (0) {

						} /////////end of: if(current_coverage < target_coverage){
					} ///////end of: if (!m_dram->returnq->full()) {
				} //////end of: if (removed == 0) {
			} //////////end of: if (!m_config->delay_only) {

		} /////////end of: if (approx_enabled) {
	} /////////end of: if (m_config->delay_queue_size) {
}
//////////////myedit AMC

void dram_t::scheduler_frfcfs() {
	unsigned mrq_latency;
	frfcfs_scheduler *sched = m_frfcfs_scheduler;

//////////////myedit AMC
	if (!mrqq->empty()
			&& sched->num_pending()
					>= m_config->gpgpu_frfcfs_dram_sched_queue_size
			&& m_config->gpgpu_frfcfs_dram_sched_queue_size > 0) {
		delay_queue_full_all++;
		delay_queue_full[id]++;
	}
//////////////myedit AMC

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

//////////////myedit AMC
	sched->increment_and_remove(n_cmd_partial); ///////////////increment delay and check for row removal. All DRAMs take turns to remove, it's possible that one DRAM removes more than others.

	///////////////////////////////////////////////////////////////////////e profiling
	if (activation_window != 0
			&& total_activation_num[id] % activation_window == 0
			&& total_activation_num[id] != previous_total_activation_num[id]) {

		previous_total_activation_num[id] = total_activation_num[id];

		if (0 && print_profile) {
			std::fprintf(profiling_output,
					"####################################act window partial%u##############################################################\n",
					id);
			std::fprintf(profiling_output,
					"#######bw ####### act_cmd_par%u:%u, act_cycles_par%u:%u ######## act_bwutil_par%u:%.4g, act_bwutil_gread_par%u:%.4g, act_bwutil_gwrite_par%u:%.4g ###### "
							"act_accu_bwutil_par%u:%.4g, act_accu_bwutil_gread_par%u:%.4g, act_accu_bwutil_gwrite_par%u:%.4g\n",
					id, n_cmd_partial, id, act_cmd_partial, id,
					(float) act_bwutil_partial / act_cmd_partial, id,
					(float) act_bwutil_partial_gread / act_cmd_partial, id,
					(float) act_bwutil_partial_gwrite / act_cmd_partial, id,
					(float) bwutil_partial / n_cmd_partial, id,
					(float) bwutil_partial_gread / n_cmd_partial, id,
					(float) bwutil_partial_gwrite / n_cmd_partial);

			float current_coverage_par = 0;
			if (total_access_count[id] != 0) {
				current_coverage_par = (float) (approximated_req_count[id])
						/ (float) (total_access_count[id]);	///////////coverage control (per dram)
			}

			std::fprintf(profiling_output,
					"#######es ####### e1 act_accu_par%u:%u, e2 act_accu_par%u:%u, e3 act_accu_par%u:%u, e4 act_accu_par%u:%u, e5 act_accu_par%u:%u, e6 act_accu_par%u:%u, e7 act_accu_par%u:%u, e8 act_accu_par%u:%u, e9 act_accu_par%u:%u, e10 act_accu_par%u:%u ####### "
							"e1 act_temp_par%u:%u, e2 act_temp_par%u:%u, e3 act_temp_par%u:%u, e4 act_temp_par%u:%u, e5 act_temp_par%u:%u, e6 act_temp_par%u:%u, e7 act_temp_par%u:%u, e8 act_temp_par%u:%u, e9 act_temp_par%u:%u, e10 act_temp_par%u:%u ####### "
							"act_temp_act_par%u:%u, act_temp_req_par%u:%u, act_temp_locality_par%u:%.4g ####### act_accu_act_par%u:%u, act_accu_req_par%u:%u, act_accu_locality_par%u:%.4g ####### "
							"act_total_act_par%u:%u, act_total_req_par%u:%u, act_total_locality_par%u:%.4g ####### act_current_coverage_par%u:%.4g\n",
					id, accu_es_partial[id][0], id, accu_es_partial[id][1], id,
					accu_es_partial[id][2], id, accu_es_partial[id][3], id,
					accu_es_partial[id][4], id, accu_es_partial[id][5], id,
					accu_es_partial[id][6], id, accu_es_partial[id][7], id,
					accu_es_partial[id][8], id, accu_es_partial[id][9], id,
					act_window_es_partial[id][0], id,
					act_window_es_partial[id][1], id,
					act_window_es_partial[id][2], id,
					act_window_es_partial[id][3], id,
					act_window_es_partial[id][4], id,
					act_window_es_partial[id][5], id,
					act_window_es_partial[id][6], id,
					act_window_es_partial[id][7], id,
					act_window_es_partial[id][8], id,
					act_window_es_partial[id][9], id, activation_window, id,
					act_window_total_req_partial[id], id,
					(float) act_window_total_req_partial[id]
							/ activation_window, id, activation_num[id], id,
					overall_access_count[id], id,
					(float) overall_access_count[id] / activation_num[id], id,
					total_activation_num[id], id, total_access_count[id], id,
					(float) total_access_count[id] / total_activation_num[id],
					id, current_coverage_par);
		} /////////end of: if (print_profile) {

		////////////clear partial status
		act_bwutil_partial = 0;
		act_bwutil_partial_gread = 0;
		act_bwutil_partial_gwrite = 0;
		act_cmd_partial = 0;

		act_window_total_req_partial[id] = 0; //////////contains the removed requests
		for (int k = 0; k < 10; k++) {
			act_window_es_partial[id][k] = 0; //////////contains the removed requests
		}
	} ////////////end of: if (total_activation_num[id] % activation_window == 0) {

	if (activation_window != 0
			&& total_activation_num_all % activation_window == 0
			&& total_activation_num_all != previous_total_activation_num_all) {

		previous_total_activation_num_all = total_activation_num_all;

		if (0 && print_profile) {
			std::fprintf(profiling_output,
					"####################################act window overall##############################################################\n");
			std::fprintf(profiling_output,
					"#######bw ####### act_cmd:%u, act_cycles:%u ######## act_bwutil:%.4g, act_bwutil_gread:%.4g, act_bwutil_gwrite:%.4g ###### "
							"act_accu_bwutil:%.4g, act_accu_bwutil_gread:%.4g, act_accu_bwutil_gwrite:%.4g\n",
					n_cmd, act_cmd, (float) act_bwutil / act_cmd,
					(float) act_bwutil_gread / act_cmd,
					(float) act_bwutil_gwrite / act_cmd, (float) bwutil / n_cmd,
					(float) bwutil_global_read / n_cmd,
					(float) bwutil_global_write / n_cmd);

			float current_coverage = 0;
			if (total_access_count_all != 0) {
				current_coverage = (float) (approximated_req_count_all)
						/ (float) (total_access_count_all);	///////////coverage control
			}

			std::fprintf(profiling_output,
					"#######es ####### "
							"e1 act_accu:%u, e2 act_accu:%u, e3 act_accu:%u, e4 act_accu:%u, e5 act_accu:%u, e6 act_accu:%u, e7 act_accu:%u, e8 act_accu:%u, e9 act_accu:%u, e10 act_accu:%u ####### "
							"e1 act_temp:%u, e2 act_temp:%u, e3 act_temp:%u, e4 act_temp:%u, e5 act_temp:%u, e6 act_temp:%u, e7 act_temp:%u, e8 act_temp:%u, e9 act_temp:%u, e10 act_temp:%u ####### "
							"act_temp_act:%u, act_temp_req:%u, act_temp_locality:%.4g, act_current_coverage:%.4g ####### act_accu_act:%u, act_accu_req:%u, act_accu_locality:%.4g ####### "
							"act_total_act:%u, act_total_req:%u, act_total_locality:%.4g\n",
					accu_es[0], accu_es[1], accu_es[2], accu_es[3], accu_es[4],
					accu_es[5], accu_es[6], accu_es[7], accu_es[8], accu_es[9],
					act_window_es[0], act_window_es[1], act_window_es[2],
					act_window_es[3], act_window_es[4], act_window_es[5],
					act_window_es[6], act_window_es[7], act_window_es[8],
					act_window_es[9], activation_window, act_window_total_req,
					(float) act_window_total_req / activation_window,
					current_coverage, activation_num_all,
					overall_access_count_all,
					(float) overall_access_count_all / activation_num_all,
					total_activation_num_all, total_access_count_all,
					(float) total_access_count_all / total_activation_num_all);
		} /////////end of: if (print_profile) {

		////////////clear overall status
		act_bwutil = 0;
		act_bwutil_gread = 0;
		act_bwutil_gwrite = 0;
		act_cmd = 0;

		act_window_total_req = 0;
		for (int k = 0; k < 10; k++) {
			act_window_es[k] = 0; //////////contains the removed requests
		}
	} /////////////end of: if (total_activation_num_all % activation_window == 0) {

	if (request_window != 0 && total_access_count[id] % request_window == 0
			&& total_access_count[id] != previous_total_access_count[id]) {

		previous_total_access_count[id] = total_access_count[id];

		if (0 && print_profile) {
			std::fprintf(profiling_output,
					"####################################req window partial%u##############################################################\n",
					id);
			std::fprintf(profiling_output,
					"#######bw ####### req_cmd_par%u:%u, req_cycles_par%u:%u ######## req_bwutil_par%u:%.4g, req_bwutil_gread_par%u:%.4g, req_bwutil_gwrite_par%u:%.4g ###### "
							"req_accu_bwutil_par%u:%.4g, req_accu_bwutil_gread_par%u:%.4g, req_accu_bwutil_gwrite_par%u:%.4g\n",
					id, n_cmd_partial, id, req_cmd_partial, id,
					(float) req_bwutil_partial / req_cmd_partial, id,
					(float) req_bwutil_partial_gread / req_cmd_partial, id,
					(float) req_bwutil_partial_gwrite / req_cmd_partial, id,
					(float) bwutil_partial / n_cmd_partial, id,
					(float) bwutil_partial_gread / n_cmd_partial, id,
					(float) bwutil_partial_gwrite / n_cmd_partial);

			float current_coverage_par = 0;
			if (total_access_count[id] != 0) {
				current_coverage_par = (float) (approximated_req_count[id])
						/ (float) (total_access_count[id]);	///////////coverage control (per dram)
			}

			std::fprintf(profiling_output,
					"#######es ####### e1 req_accu_par%u:%u, e2 req_accu_par%u:%u, e3 req_accu_par%u:%u, e4 req_accu_par%u:%u, e5 req_accu_par%u:%u, e6 req_accu_par%u:%u, e7 req_accu_par%u:%u, e8 req_accu_par%u:%u, e9 req_accu_par%u:%u, e10 req_accu_par%u:%u ####### "
							"e1 req_temp_par%u:%u, e2 req_temp_par%u:%u, e3 req_temp_par%u:%u, e4 req_temp_par%u:%u, e5 req_temp_par%u:%u, e6 req_temp_par%u:%u, e7 req_temp_par%u:%u, e8 req_temp_par%u:%u, e9 req_temp_par%u:%u, e10 req_temp_par%u:%u ####### "
							"req_temp_act_par%u:%u, req_temp_req_par%u:%u, req_temp_locality_par%u:%.4g ####### req_accu_act_par%u:%u, req_accu_req_par%u:%u, req_accu_locality_par%u:%.4g ####### "
							"req_total_act_par%u:%u, req_total_req_par%u:%u, req_total_locality_par%u:%.4g ####### req_current_coverage_par%u:%.4g\n",
					id, accu_es_partial[id][0], id, accu_es_partial[id][1], id,
					accu_es_partial[id][2], id, accu_es_partial[id][3], id,
					accu_es_partial[id][4], id, accu_es_partial[id][5], id,
					accu_es_partial[id][6], id, accu_es_partial[id][7], id,
					accu_es_partial[id][8], id, accu_es_partial[id][9], id,
					req_window_es_partial[id][0], id,
					req_window_es_partial[id][1], id,
					req_window_es_partial[id][2], id,
					req_window_es_partial[id][3], id,
					req_window_es_partial[id][4], id,
					req_window_es_partial[id][5], id,
					req_window_es_partial[id][6], id,
					req_window_es_partial[id][7], id,
					req_window_es_partial[id][8], id,
					req_window_es_partial[id][9], id,
					req_window_total_act_partial[id], id, request_window, id,
					(float) request_window / req_window_total_act_partial[id],
					id, activation_num[id], id, overall_access_count[id], id,
					(float) overall_access_count[id] / activation_num[id], id,
					total_activation_num[id], id, total_access_count[id], id,
					(float) total_access_count[id] / total_activation_num[id],
					id, current_coverage_par);
		} /////////end of: if (print_profile) {

		////////////clear partial status
		req_bwutil_partial = 0;
		req_bwutil_partial_gread = 0;
		req_bwutil_partial_gwrite = 0;
		req_cmd_partial = 0;

		req_window_total_act_partial[id] = 0; //////////contains the removed requests
		for (int k = 0; k < 10; k++) {
			req_window_es_partial[id][k] = 0; //////////contains the removed requests
		}
	} /////////////end of: if (total_access_count[id] % request_window == 0) {

	if (request_window != 0 && total_access_count_all % request_window == 0
			&& total_access_count_all != previous_total_access_count_all) {

		previous_total_access_count_all = total_access_count_all;

		if (0 && print_profile) {
			std::fprintf(profiling_output,
					"####################################req window overall##############################################################\n");
			std::fprintf(profiling_output,
					"#######bw ####### req_cmd:%u, req_cycles:%u ######## req_bwutil:%.4g, req_bwutil_gread:%.4g, req_bwutil_gwrite:%.4g ###### "
							"req_accu_bwutil:%.4g, req_accu_bwutil_gread:%.4g, req_accu_bwutil_gwrite:%.4g\n",
					n_cmd, req_cmd, (float) req_bwutil / req_cmd,
					(float) req_bwutil_gread / req_cmd,
					(float) req_bwutil_gwrite / req_cmd, (float) bwutil / n_cmd,
					(float) bwutil_global_read / n_cmd,
					(float) bwutil_global_write / n_cmd);

			float current_coverage = 0;
			if (total_access_count_all != 0) {
				current_coverage = (float) (approximated_req_count_all)
						/ (float) (total_access_count_all);	///////////coverage control
			}

			std::fprintf(profiling_output,
					"#######es ####### "
							"e1 req_accu:%u, e2 req_accu:%u, e3 req_accu:%u, e4 req_accu:%u, e5 req_accu:%u, e6 req_accu:%u, e7 req_accu:%u, e8 req_accu:%u, e9 req_accu:%u, e10 req_accu:%u ####### "
							"e1 req_temp:%u, e2 req_temp:%u, e3 req_temp:%u, e4 req_temp:%u, e5 req_temp:%u, e6 req_temp:%u, e7 req_temp:%u, e8 req_temp:%u, e9 req_temp:%u, e10 req_temp:%u ####### "
							"req_temp_act:%u, req_temp_req:%u, req_temp_locality:%.4g, req_current_coverage:%.4g ####### req_accu_act:%u, req_accu_req:%u, req_accu_locality:%.4g ####### "
							"req_total_act:%u, req_total_req:%u, req_total_locality:%.4g\n",
					accu_es[0], accu_es[1], accu_es[2], accu_es[3], accu_es[4],
					accu_es[5], accu_es[6], accu_es[7], accu_es[8], accu_es[9],
					req_window_es[0], req_window_es[1], req_window_es[2],
					req_window_es[3], req_window_es[4], req_window_es[5],
					req_window_es[6], req_window_es[7], req_window_es[8],
					req_window_es[9], req_window_total_act, request_window,
					(float) request_window / req_window_total_act,
					current_coverage, activation_num_all,
					overall_access_count_all,
					(float) overall_access_count_all / activation_num_all,
					total_activation_num_all, total_access_count_all,
					(float) total_access_count_all / total_activation_num_all);
		} /////////end of: if (print_profile) {

		////////////clear overall status
		req_bwutil = 0;
		req_bwutil_gread = 0;
		req_bwutil_gwrite = 0;
		req_cmd = 0;

		req_window_total_act = 0; //////////contains the removed requests
		for (int k = 0; k < 10; k++) {
			req_window_es[k] = 0; //////////contains the removed requests
		}
	} ///////////end of: if (total_access_count_all % request_window == 0) {
///////////////////////////////////////////////////////////////////////e profiling

///////////////////////////////////////////////////////////////////////bw profiling
	if (profiling_cycles_bw != 0
			&& ((auto_delay
					&& (n_cmd_partial - warmup_cycles) % profiling_cycles_bw
							== 0 && n_cmd_partial >= warmup_cycles)
					|| (!auto_delay && n_cmd_partial % profiling_cycles_bw == 0))) {

		if (0 && print_profile) {
			std::fprintf(profiling_output,
					"####################################pf window partial%u##############################################################\n",
					id);
			std::fprintf(profiling_output,
					"#######bw ####### pf_cmd_par%u:%u ###### pf_temp_act_par%u:%u, pf_temp_req_par%u:%u, pf_temp_locality_par%u:%.4g ######  "
							"pf_bwutil_par%u:%.4g, pf_bwutil_gread_par%u:%.4g, pf_bwutil_gwrite_par%u:%.4g ###### "
							"pf_accu_act_par%u:%u, pf_accu_req_par%u:%u, pf_accu_locality_par%u:%.4g ###### "
							"pf_accu_bwutil_par%u:%.4g, pf_accu_bwutil_gread_par%u:%.4g, pf_accu_bwutil_gwrite_par%u:%.4g\n",
					id, n_cmd_partial, id, temp_activation_num[id], id,
					temp_access_count[id], id,
					(float) temp_access_count[id] / temp_activation_num[id], id,
					(float) temp_bwutil_partial / profiling_cycles_bw, id,
					(float) temp_bwutil_partial_gread / profiling_cycles_bw, id,
					(float) temp_bwutil_partial_gwrite / profiling_cycles_bw,
					id, activation_num[id], id, overall_access_count[id], id,
					(float) overall_access_count[id] / activation_num[id], id,
					(float) bwutil_partial / n_cmd_partial, id,
					(float) bwutil_partial_gread / n_cmd_partial, id,
					(float) bwutil_partial_gwrite / n_cmd_partial);

			float current_coverage_par = 0;
			if (total_access_count[id] != 0) {
				current_coverage_par = (float) (approximated_req_count[id])
						/ (float) (total_access_count[id]);	///////////coverage control (per dram)
			}

			std::fprintf(profiling_output,
					"#######es ####### e1 pf_accu_par%u:%u, e2 pf_accu_par%u:%u, e3 pf_accu_par%u:%u, e4 pf_accu_par%u:%u, e5 pf_accu_par%u:%u, e6 pf_accu_par%u:%u, e7 pf_accu_par%u:%u, e8 pf_accu_par%u:%u, e9 pf_accu_par%u:%u, e10 pf_accu_par%u:%u ####### "
							"e1 pf_temp_par%u:%u, e2 pf_temp_par%u:%u, e3 pf_temp_par%u:%u, e4 pf_temp_par%u:%u, e5 pf_temp_par%u:%u, e6 pf_temp_par%u:%u, e7 pf_temp_par%u:%u, e8 pf_temp_par%u:%u, e9 pf_temp_par%u:%u, e10 pf_temp_par%u:%u ####### "
							"pf_accu_act_par%u:%u, pf_accu_req_par%u:%u, pf_accu_locality_par%u:%.4g ####### "
							"pf_total_act_par%u:%u, pf_total_req_par%u:%u, pf_total_locality_par%u:%.4g ####### pf_current_coverage_par%u:%.4g\n",
					id, accu_es_partial[id][0], id, accu_es_partial[id][1], id,
					accu_es_partial[id][2], id, accu_es_partial[id][3], id,
					accu_es_partial[id][4], id, accu_es_partial[id][5], id,
					accu_es_partial[id][6], id, accu_es_partial[id][7], id,
					accu_es_partial[id][8], id, accu_es_partial[id][9], id,
					pf_window_es_partial[id][0], id,
					pf_window_es_partial[id][1], id,
					pf_window_es_partial[id][2], id,
					pf_window_es_partial[id][3], id,
					pf_window_es_partial[id][4], id,
					pf_window_es_partial[id][5], id,
					pf_window_es_partial[id][6], id,
					pf_window_es_partial[id][7], id,
					pf_window_es_partial[id][8], id,
					pf_window_es_partial[id][9], id, activation_num[id], id,
					overall_access_count[id], id,
					(float) overall_access_count[id] / activation_num[id], id,
					total_activation_num[id], id, total_access_count[id], id,
					(float) total_access_count[id] / total_activation_num[id],
					id, current_coverage_par);
		} /////////end of: if (print_profile) {

		////////////clear partial status
		for (int k = 0; k < 10; k++) {
			pf_window_es_partial[id][k] = 0; //////////contains the removed requestsipc
		}

		temp_bwutil_partial = 0; //bw (per DRAM)
		temp_bwutil_partial_gread = 0; //per DRAM
		temp_bwutil_partial_gwrite = 0; //per DRAM

		if (id == 5) {
			/////////////////////////////////overall
			float current_coverage = 0;
			if (total_access_count_all != 0) {
				current_coverage = (float) (approximated_req_count_all)
						/ (float) (total_access_count_all);	///////////coverage control
			}

			if (0 && print_profile) {
				std::fprintf(profiling_output,
						"####################################pf window overall##############################################################\n");
				std::fprintf(profiling_output,
						"#######bw ####### pf_cmd:%u ####### temp_ipc:%12.4f, cumulative_current_ipc:%12.4f, cumulative_tot_ipc:%12.4f ####### "
								"pf_bwutil:%.4g, pf_bwutil_gread:%.4g, pf_bwutil_gwrite:%.4g ####### "
								"pf_accu_bwutil=%.4g, pf_accu_bwutil_gread=%.4g, pf_accu_bwutil_gwrite=%.4g ####### "
								"pf_temp_act:%u, pf_temp_req:%u, pf_temp_locality:%.4g\n",
						n_cmd, (float) temp_gpu_sim_insn / temp_gpu_sim_cycle,
						(float) gpu_sim_insn / gpu_sim_cycle,
						(float) (gpu_tot_sim_insn + gpu_sim_insn)
								/ (gpu_tot_sim_cycle + gpu_sim_cycle),
						(float) temp_bwutil / (6 * profiling_cycles_bw),
						(float) temp_bwutil_global_read
								/ (6 * profiling_cycles_bw),
						(float) temp_bwutil_global_write
								/ (6 * profiling_cycles_bw),
						(float) bwutil / n_cmd,
						(float) bwutil_global_read / n_cmd,
						(float) bwutil_global_write / n_cmd,
						temp_activation_num_all, temp_access_count_all,
						(float) temp_access_count_all
								/ temp_activation_num_all);

				///can add es>10 total.
				std::fprintf(profiling_output,
						"#######es ####### "
								"e1 pf_accu:%u, e2 pf_accu:%u, e3 pf_accu:%u, e4 pf_accu:%u, e5 pf_accu:%u, e6 pf_accu:%u, e7 pf_accu:%u, e8 pf_accu:%u, e9 pf_accu:%u, e10 pf_accu:%u ####### "
								"e1 pf_temp:%u, e2 pf_temp:%u, e3 pf_temp:%u, e4 pf_temp:%u, e5 pf_temp:%u, e6 pf_temp:%u, e7 pf_temp:%u, e8 pf_temp:%u, e9 pf_temp:%u, e10 pf_temp:%u ####### "
								"pf_current_coverage:%.4g ####### pf_accu_act:%u, pf_accu_pf:%u, pf_accu_locality:%.4g ####### "
								"pf_total_act:%u, pf_total_req:%u, pf_total_locality:%.4g\n",
						accu_es[0], accu_es[1], accu_es[2], accu_es[3],
						accu_es[4], accu_es[5], accu_es[6], accu_es[7],
						accu_es[8], accu_es[9], pf_window_es[0],
						pf_window_es[1], pf_window_es[2], pf_window_es[3],
						pf_window_es[4], pf_window_es[5], pf_window_es[6],
						pf_window_es[7], pf_window_es[8], pf_window_es[9],
						current_coverage, activation_num_all,
						overall_access_count_all,
						(float) overall_access_count_all / activation_num_all,
						total_activation_num_all, total_access_count_all,
						(float) total_access_count_all
								/ total_activation_num_all);
			} /////////end of: if (print_profile) {

			////////////clear overall status
			for (int k = 0; k < 10; k++) {
				pf_window_es[k] = 0; //////////contains the removed requests
			}

			///////////////////////////////////auto delay scheme
			if (auto_delay && profiling_done == 0) { ///////////////the overall bw is used.

				if (moving_direction == 4) { /////////////first delay-threshold is tested, deciding the direction.

					if ((float) temp_bwutil / (6 * profiling_cycles_bw)
							>= ((float) min_bw / 100) * baseline_bw) { ////moving right
						if (delay_threshold <= 64) {
							if (delay_threshold < 64) {
								delay_threshold = 64;
							} else {
								delay_threshold = delay_threshold * 2;
							}
						} else {
							if (delay_threshold >= 2048) {
								delay_threshold = 2048;
								profiling_done = 1;
								previous_delay_threshold = delay_threshold;
							} else {
								delay_threshold = delay_threshold + 128;
							}
						}
						moving_direction = 1;
					} else { ////////moving left
						if (delay_threshold <= 128) {
							//delay_threshold = 128; //////////128 added
							//profiling_done = 1; //////////128 added
							//previous_delay_threshold = delay_threshold; //////////128 added

							if (delay_threshold <= 64) {//////////128 removed
								delay_threshold = 64; ///////////just do not delay if too small*********(now 64 instead.)
								profiling_done = 1;
								previous_delay_threshold = delay_threshold;
							} else {
								delay_threshold = delay_threshold / 2;
							} //////////128 removed

						} else {
							delay_threshold = delay_threshold - 128;
						}
						moving_direction = 0;
					}
				} else if (moving_direction == 0) { //////////left
					if ((float) temp_bwutil / (6 * profiling_cycles_bw)
							>= ((float) min_bw / 100) * baseline_bw) {
						profiling_done = 1; /////////so the starting range should better between 64 and 1024.
						previous_delay_threshold = delay_threshold;
					} else {
						if (delay_threshold <= 64) {	//////////lower bound
							delay_threshold = 64; ///////////just do not delay if too small*********(now 64 instead.)
							profiling_done = 1;
							previous_delay_threshold = delay_threshold;
						} else {
							if (delay_threshold <= 128) {
								//delay_threshold = 128; //////////128 added
								//profiling_done = 1; /////////128 added
								//previous_delay_threshold = delay_threshold; //////////128 added
								delay_threshold = delay_threshold / 2;//////////128 removed
							} else {
								delay_threshold = delay_threshold - 128;
							}
						}
					}
				} else if (moving_direction == 1) { //////////right
					if ((float) temp_bwutil / (6 * profiling_cycles_bw)
							>= ((float) min_bw / 100) * baseline_bw) {
						if (delay_threshold >= 2048) { //////////upper bound
							delay_threshold = 2048; ///////////the max delay is 1024
							profiling_done = 1;
							previous_delay_threshold = delay_threshold;
						} else {
							if (delay_threshold <= 64) {
								if (delay_threshold < 64) {
									delay_threshold = 64;
								} else {
									delay_threshold = delay_threshold * 2;
								}
							} else {
								delay_threshold = delay_threshold + 128;
							}
						}
					} else {
						if (delay_threshold <= 128) {
							//delay_threshold = 128; //////////128 added

							if (delay_threshold <= 64) { //////////128 removed
								delay_threshold = 64; ///////////just do not delay if too small*********(now 64 instead.)
							} else {
								delay_threshold = delay_threshold / 2;
							} //////////128 removed

						} else {
							delay_threshold = delay_threshold - 128;
						}
						////////the starting range should better between 64 and 1024.
						profiling_done = 1;
						previous_delay_threshold = delay_threshold;
					}
				} else {
					assert((moving_direction == 2) || (moving_direction == 3));
				} ///////end of: if (moving_direction == 4) {

				if (moving_direction == 3) { ///////////////baseline bw profiling over, testing the initial delay-threshold.
					baseline_bw = (float) temp_bwutil
							/ (6 * profiling_cycles_bw); /////////////////how large should warmup_cycles be?

					delay_threshold = previous_delay_threshold; /////////////////first one from the input ( the range should be [64 , 1024] ).
					moving_direction = 4;
					can_remove = 1; ////////////////controls which state allows remove
				}

				if (moving_direction == 2) { ////////////starting state, going to test the baseline bw.
					delay_threshold = 0;

					moving_direction = 3;
					can_remove = 0; ////////////////controls which state allows remove
				}
			} //////////end of: if (auto_delay && profiling_done == 0) {

			if (profiling_done == 1 && profiling_done_flag == 0) { ////////get profiling done cycle for the first one.

				profiling_done_flag = 1;
				profiling_done_cycle = n_cmd_partial;
			}

			if (profiling_done == 1) {
				cycles_after_profiling_done++;
				can_remove = 1; ////////////////controls which state allows remove
			}

			if (profiling_done == 1
					&& cycles_after_profiling_done >= reprofiling_cycles) { ///////////redo profiling after several cycles

				cycles_after_profiling_done = 0;
				reprofiling_count++;
				profiling_done = 0;
				moving_direction = 2; //////////////starting again after the next interval. In the following window, the delay is still not changed.
			}

			if (print_profile) {
				std::fprintf(profiling_output, "delay_threshold:%u\n",
						delay_threshold);

				std::fprintf(profiling_output, "temp_bwutil:%f\n", temp_bwutil);
			}
			//////////////////////////////////////auto delay scheme

			////////////clear overall status
			for (int j = 0; j < 6; j++) {
				temp_access_count[j] = 0; ////////profile/////////not containing the removed requests
				temp_activation_num[j] = 0; ////////profile/////////not containing the removed requests
			}

			temp_gpu_sim_insn = 0; //ipc (ALL DRAMs)
			temp_gpu_sim_cycle = 0; //ipc (ALL DRAMs)
			temp_access_count_all = 0; //locality/////////not containing the removed requests (ALL DRAMs)
			temp_activation_num_all = 0; //locality/////////not containing the removed requests (ALL DRAMs)

			temp_bwutil = 0; //bw (ALL DRAMs)
			temp_bwutil_global_read = 0; //bw gread (ALL DRAMs)
			temp_bwutil_global_write = 0; //bw gwrite (ALL DRAMs)
		} //////////end of: if (id == 5) { ///////overall
	} ///////////////end of: if ((auto_delay && (n_cmd_partial - warmup_cycles) % profiling_cycles_bw == 0) || (!auto_delay && n_cmd_partial % profiling_cycles_bw == 0)) {
///////////////////////////////////////////////////////////////////////bw profiling

	//////////////////////////////////////dynamic e coverage based scheme in profiling window
	/*
	 if (profiling_cycles_es != 0
	 && ((auto_delay
	 && (n_cmd_partial - warmup_cycles) % profiling_cycles_es
	 == 0 && n_cmd_partial >= warmup_cycles)
	 || (!auto_delay && n_cmd_partial % profiling_cycles_es == 0))) {
	 */

	if (profiling_cycles_es != 0 && n_cmd_partial % profiling_cycles_es == 0) {

		if (id == 5) {

			/////////////////////////////////overall coverage///partial or temporary coverage?
			float current_coverage = 0;
			if (total_access_count_all != 0) {
				current_coverage = (float) (approximated_req_count_all)
						/ (float) (total_access_count_all);	///////////coverage control
			}

			/////////////////////////////////temp coverage///partial coverage?
			float temp_coverage = 0;
			if (total_access_count_temp_all != 0) {
				temp_coverage = (float) (approximated_req_count_temp_all)
						/ (float) (total_access_count_temp_all);///////////coverage control
			}

			//////////////todo: the statistics based one. aw, rw based one. analyze profiling, auto, dynamic results (check launch scripts).// will this work? e start with 1 or 8? print each e and delay?
			///////is it better to be in act/req window? is it better to use statistic based?
			//if (dynamic_on && (previous_can_remove || !auto_delay)) {//////////schedule only if you can remove. it's easier in the profiling window since it's in sync with can_remove. // should we schedule when not removing?
			if (dynamic_on) {//////////schedule only if you can remove. it's easier in the profiling window since it's in sync with can_remove. // should we schedule when not removing?

				if (coverage_sufficiency_decided == 0) {
					if (current_coverage >= 0.98 * target_coverage) {////if it is coverage sufficient.
						coverage_sufficiency_decided = 1;
						if (chosen_e > 12) {
							chosen_e = chosen_e - 10;
						} else {
							chosen_e = 2;
						}
					} else {///////////////////////////////////////////////////////it should keep at 8 forever otherwise.
						////////do nothing
						chosen_e++;						//////unlimited e added
					}
				} else {//////////////////////////coverage_sufficiency_decided != 0;

					if (current_coverage < 0.98 * target_coverage) {
						if (chosen_e < 8) {
							chosen_e++;
						} else {					///////////if chosen_e >= 8
							////////do nothing
							chosen_e++;					//////unlimited e added
						}
					} else if ((m_config->delay_queue_size == 16
							&& current_coverage >= 0.98 * target_coverage
							&& current_coverage <= 1.01 * target_coverage)
							|| (m_config->delay_queue_size == 15
									&& current_coverage
											>= 0.98 * target_coverage
									&& current_coverage
											<= 0.99 * target_coverage)) {
						////////chosen_e should not be changed.

					} else {//////////////current_coverage > 1.02 * target_coverage
						if (chosen_e > 1) {
							chosen_e--;
						} else {/////////////////////////////////////////////chosen_e = 1
							////////do nothing
						}
					}////////////////////end of: if (current_coverage < 0.99 * target_coverage) {
				}////////////////////end of: if (coverage_sufficiency_decided == 0) {

			}/////////////////////////////////////////////////////end of: if (dynamic_on) {

			if (print_profile) {
				std::fprintf(profiling_output,
						"chosen_e:%u, temp_coverage:%.4g\n", chosen_e,
						temp_coverage);
			}

			LT_schedule_count++;
			//if (can_remove == 0) {
			//	previous_can_remove = 0;
			//} else {
			//	previous_can_remove = 1;
			//}
			////////////clear overall status
			approximated_req_count_temp_all = 0;
			total_access_count_temp_all = 0;
		}	/////////////////////////////////////////////end of: if (id == 5) {
	}///////////////////////////////////end of: if (profiling_cycles_es != 0 && n_cmd_partial % profiling_cycles_es == 0) {
	 //////////////////////////////////////dynamic e coverage based scheme in profiling window
//////////////myedit AMC

	dram_req_t *req;
	unsigned i;
	for (i = 0; i < m_config->nbk; i++) {
		unsigned b = (i + prio) % m_config->nbk;
		if (!bk[b]->mrq) { //////////////////////only for banks that are not serving a request. Looking for the first empty bank (with null).

			req = sched->schedule(b, bk[b]->curr_row); ///////////the previous row activated.

			if (req) { ///////////////because of assert( req != NULL && m_num_pending != 0 ); this must be true
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

				break;      ////////////only one bank can be assigned per cycle.
			}
		}
	}
}
