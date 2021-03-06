// Copyright (c) 2009-2011, Tor M. Aamodt, Tayler Hetherington
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

#include "gpu-cache.h"
#include "stat-tool.h"

///////////////////////myeditbfloat
#include "../cuda-sim/ptx_sim.h"
#include "shader.h"
///////////////////////myeditbfloat

#include <assert.h>

#define MAX_DEFAULT_CACHE_SIZE_MULTIBLIER 4
// used to allocate memory that is large enough to adapt the changes in cache size across kernels

const char * cache_request_status_str(enum cache_request_status status) {
	static const char * static_cache_request_status_str[] = { "HIT",
			"HIT_RESERVED", "MISS", "RESERVATION_FAIL" };

	assert(
			sizeof(static_cache_request_status_str) / sizeof(const char*)
					== NUM_CACHE_REQUEST_STATUS);
	assert(status < NUM_CACHE_REQUEST_STATUS);

	return static_cache_request_status_str[status];
}

unsigned l1d_cache_config::set_index(new_addr_type addr) const {
	unsigned set_index = m_nset; // Default to linear set index function
	unsigned lower_xor = 0;
	unsigned upper_xor = 0;

	switch (m_set_index_function) {
	case FERMI_HASH_SET_FUNCTION:
		/*
		 * Set Indexing function from "A Detailed GPU Cache Model Based on Reuse Distance Theory"
		 * Cedric Nugteren et al.
		 * ISCA 2014
		 */
		if (m_nset == 32 || m_nset == 64) {
			// Lower xor value is bits 7-11
			lower_xor = (addr >> m_line_sz_log2) & 0x1F;

			// Upper xor value is bits 13, 14, 15, 17, and 19
			upper_xor = (addr & 0xE000) >> 13; // Bits 13, 14, 15
			upper_xor |= (addr & 0x20000) >> 14; // Bit 17
			upper_xor |= (addr & 0x80000) >> 15; // Bit 19

			set_index = (lower_xor ^ upper_xor);

			// 48KB cache prepends the set_index with bit 12
			if (m_nset == 64)
				set_index |= (addr & 0x1000) >> 7;

		} else { /* Else incorrect number of sets for the hashing function */
			assert(
					"\nGPGPU-Sim cache configuration error: The number of sets should be "
							"32 or 64 for the hashing set index function.\n"
							&& 0);
		}
		break;

	case CUSTOM_SET_FUNCTION:
		/* No custom set function implemented */
		break;

	case LINEAR_SET_FUNCTION:
		set_index = (addr >> m_line_sz_log2) & (m_nset - 1);
		break;
	}

	// Linear function selected or custom set index function not implemented
	assert(
			(set_index < m_nset)
					&& "\nError: Set index out of bounds. This is caused by "
							"an incorrect or unimplemented custom set index function.\n");

	return set_index;
}

void l2_cache_config::init(linear_to_raw_address_translation *address_mapping) {
	cache_config::init(m_config_string, FuncCachePreferNone);
	m_address_mapping = address_mapping;
}

unsigned l2_cache_config::set_index(new_addr_type addr) const {
	if (!m_address_mapping) {
		return (addr >> m_line_sz_log2) & (m_nset - 1);
	} else {
		// Calculate set index without memory partition bits to reduce set camping
		new_addr_type part_addr = m_address_mapping->partition_address(addr);
		return (part_addr >> m_line_sz_log2) & (m_nset - 1);
	}
}

tag_array::~tag_array() {
	delete[] m_lines;
}

tag_array::tag_array(cache_config &config, int core_id, int type_id,
		cache_block_t* new_lines) :
		m_config(config), m_lines(new_lines) {
	init(core_id, type_id);
}

void tag_array::update_cache_parameters(cache_config &config) {
	m_config = config;
}

tag_array::tag_array(cache_config &config, int core_id, int type_id) :
		m_config(config) {
	//assert( m_config.m_write_policy == READ_ONLY ); Old assert
	m_lines = new cache_block_t[MAX_DEFAULT_CACHE_SIZE_MULTIBLIER
			* config.get_num_lines()];
	init(core_id, type_id);
}

void tag_array::init(int core_id, int type_id) {

	///////myedit AMC
	fill_count = 0;
	///////myedit AMC

	m_access = 0;
	m_miss = 0;
	m_pending_hit = 0;
	m_res_fail = 0;
	// initialize snapshot counters for visualizer
	m_prev_snapshot_access = 0;
	m_prev_snapshot_miss = 0;
	m_prev_snapshot_pending_hit = 0;
	m_core_id = core_id;
	m_type_id = type_id;
}

enum cache_request_status tag_array::probe(new_addr_type addr,
		unsigned &idx) const {
	//assert( m_config.m_write_policy == READ_ONLY );
	unsigned set_index = m_config.set_index(addr);
	new_addr_type tag = m_config.tag(addr);

	unsigned invalid_line = (unsigned) -1;
	unsigned valid_line = (unsigned) -1;
	unsigned valid_timestamp = (unsigned) -1;

	bool all_reserved = true;

	// check for hit or pending hit
	for (unsigned way = 0; way < m_config.m_assoc; way++) {
		unsigned index = set_index * m_config.m_assoc + way;
		cache_block_t *line = &m_lines[index];
		if (line->m_tag == tag) {
			if (line->m_status == RESERVED) {
				idx = index;
				return HIT_RESERVED;
			} else if (line->m_status == VALID) {
				idx = index;
				return HIT;
			} else if (line->m_status == MODIFIED) {
				idx = index;
				return HIT;
			} else {
				assert(line->m_status == INVALID);
			}
		}
		if (line->m_status != RESERVED) {
			all_reserved = false;
			if (line->m_status == INVALID) {
				invalid_line = index;
			} else {
				// valid line : keep track of most appropriate replacement candidate
				if (m_config.m_replacement_policy == LRU) {
					if (line->m_last_access_time < valid_timestamp) {
						valid_timestamp = line->m_last_access_time;
						valid_line = index;
					}
				} else if (m_config.m_replacement_policy == FIFO) {
					if (line->m_alloc_time < valid_timestamp) {
						valid_timestamp = line->m_alloc_time;
						valid_line = index;
					}
				}
			}
		}
	}
	if (all_reserved) {
		assert(m_config.m_alloc_policy == ON_MISS);
		return RESERVATION_FAIL; // miss and not enough space in cache to allocate on miss
	}

	if (invalid_line != (unsigned) -1) {
		idx = invalid_line;
	} else if (valid_line != (unsigned) -1) {
		idx = valid_line;
	} else
		abort(); // if an unreserved block exists, it is either invalid or replaceable

	return MISS;
}

enum cache_request_status tag_array::access(new_addr_type addr, unsigned time,
		unsigned &idx) {
	bool wb = false;
	cache_block_t evicted;
	enum cache_request_status result = access(addr, time, idx, wb, evicted);
	assert(!wb);
	return result;
}

enum cache_request_status tag_array::access(new_addr_type addr, unsigned time,
		unsigned &idx, bool &wb, cache_block_t &evicted) {
	m_access++;
	shader_cache_access_log(m_core_id, m_type_id, 0); // log accesses to cache
	enum cache_request_status status = probe(addr, idx);
	switch (status) {
	case HIT_RESERVED:
		m_pending_hit++;
	case HIT:
		m_lines[idx].m_last_access_time = time;
		break;
	case MISS:
		m_miss++;
		shader_cache_access_log(m_core_id, m_type_id, 1); // log cache misses
		if (m_config.m_alloc_policy == ON_MISS) {
			if (m_lines[idx].m_status == MODIFIED) {
				wb = true;
				evicted = m_lines[idx];
			}
			m_lines[idx].allocate(m_config.tag(addr), m_config.block_addr(addr),
					time);
		}
		break;
	case RESERVATION_FAIL:
		////////////debug
		//fprintf(stdout,
		//		"############debug: reservation fail in tag_array::access()"); //////how could RESERVATION_FAIL get here?
		////////////debug
		m_res_fail++;
		shader_cache_access_log(m_core_id, m_type_id, 1); // log cache misses
		break;
	default:
		fprintf(stderr, "tag_array::access - Error: Unknown"
				"cache_request_status %d\n", status);
		abort();
	}
	return status;
}

////////myedit bfloat
void tag_array::truncate_float(mem_fetch *mf) { /////////////must make sure it is float

	actual_truncate++;

	new_addr_type addr = mf->get_addr();
	new_addr_type block_addr = m_config.block_addr(addr);

	/////////////////////////////////////////////////////////////////////////////////////read from nearby address
	mem_fetch *data = mf;
	char * mydata = new char[data->get_data_size()];
	new_addr_type address_limit;

	address_limit = data->get_addr() + data->get_data_size(); //////////////myeditDSN

	if (address_limit <= 0x100000000) {

		//////////////read from global space
		((memory_space_impl<8 * 1024> *) global_memory)->read(
				(block_addr >> 7) << 7, data->get_data_size(), mydata);

		/////////////////////truncate the data (make sure it is little endian)

		/////////// truncation schemes:
		/////////// F32 (1, 8, 23) -> F16 (1, 5, 10) -> F8 (1, 4, 3):
		/////////// F32 (1, 8, 23) -> B16 (1, 8, 7)  -> B8 (1, 5, 2):

		/////////// NF32 (1, 8, 23) -> NF16 (1, 5, 10) -> NF8 (1, 4, 3):
		/////////// NF32 (1, 8, 23) -> NB16 (1, 8, 7) -> NB8 (1, 5, 2):


		/////////// I32 -> IL16 -> IL8 (left remained): NA. Float cannot be stored in int. Int stored in int is proved to be bad for energy and error in exp1. But int can be stored in float and tested in above shemes.
		/////////// I32 -> IR16 -> IR8 (right remained): NA. Float cannot be stored in int. Int stored in int is proved to be bad for energy and error in exp1. But int can be stored in float and tested in above shemes.

		/////// F32: 127 to 1, 0 to -126 (254 to 128, 127 to 1), 0 is subnormal, 255 is inf
		/////// F16: 15 to 1, 0 to -14 (30 to 16, 15 to 1), 0 is subnormal, 31 is inf
		/////// F8: 7 to 1, 0 to -6 (14 to 8, 7 to 1), 0 is subnormal, 15 is inf

		/////// F32: 127 to 1, 0 to -126 (254 to 128, 127 to 1), 0 is subnormal, 255 is inf
		/////// B16: 127 to 1, 0 to -126 (254 to 128, 127 to 1), 0 is subnormal, 255 is inf
		/////// B8: 15 to 1, 0 to -14 (30 to 16, 15 to 1), 0 is subnormal, 31 is inf

		/////// NF32: 127 to 1, 0 to -126 (127 to 1, -0 to -126), 0 is subnormal, 255 is inf
		/////// NF16: 15 to 1, 0 to -14 (15 to 1, -0 to -14), 0 is subnormal, 31 is inf
		/////// NF8: 7 to 1, 0 to -6 (7 to 1, -0 to -6), 0 is subnormal, 15 is inf

		/////// NF32: 127 to 1, 0 to -126 (127 to 1, -0 to -126), 0 is subnormal, 255 is inf
		/////// NB16: 127 to 1, 0 to -126 (127 to 1, -0 to -126), 0 is subnormal, 255 is inf
		/////// NB8: 15 to 1, 0 to -14 (15 to 1, -0 to -14), 0 is subnormal, 31 is inf

		////////scenarios:
		////////scenario 0: No truncation
		////////scenario 1: float32 to float16 and to float8 (depending on truncate_ratio)
		////////scenario 2: float32 to bfloat16 and to bfloat8 (depending on truncate_ratio)
		////////scenario 3: new float32 to new float16 and to new float8 (depending on truncate_ratio)
		////////scenario 4: new float32 to new bfloat16 and to new bfloat8 (depending on truncate_ratio)
		////////scenario 5: profiling mode, truncated value is not written back to memory

		////////DBI toggle: enable/disable DBI
		////////energy profiling toggle: get the number of bit flips and ones, when working with scenario 5, counts for all truncation scenarios are collected. truncated value is not written back to memory.
		////////error profiling toggle: collect the hardware error or not, when working with scenario 5, hardware errors for all truncation scenarios are collected. truncated value is not written back to memory.
		////////question: is bit flips modeled in gpuwattch? If not, how do we model their % memory energy or % system energy based on their count? (Joule per count? find this info)

		////////power model vampire, drampower

		if(mf->get_truncation_scenario() == 0){//////////////In scenario 0, when truncate truncate float to bfloat? ////////////////////myedit highlight: fixed bug, added mf-> later


			if(mf->get_truncate_ratio() == 2){/////////In scenario 0, this is float32 to bfloat16. In other scenarios, this code remains the same even when truncate new_float32 to bfloat16.
				for(int i = 0; i < data->get_data_size(); i += 4){
					////////////////truncate the first two bytes
					mydata[i] = 0;
					mydata[i + 1] = 0;
				}
			}else if(mf->get_truncate_ratio() == 4){

				unsigned char top_byte;
				unsigned char second_byte;
				unsigned char exp_byte;
				int exp_value;
				for(int i = 0; i < data->get_data_size(); i += 4){
					////////////////truncate the first two bytes
					mydata[i] = 0;
					mydata[i + 1] = 0;

					second_byte = mydata[i + 2];
					top_byte = mydata[i + 3];

					exp_byte = ( (top_byte & 127) << 1 ) | (second_byte >> 7); //getting bit values for exp bits
					exp_value = exp_byte - 127;

					if(exp_value == 128 || exp_value == -127){ ///////////////////use zeros for both subnormal and inf cases
						//do nothing
					}else{
						if(exp_value > 0){
							exp_value = exp_value & 15;
						}else{
							exp_value = (exp_value - 1);/////////////0 to -126 correspond -1 to -127 in new format representation, new_format = exp_value - 1.
							exp_value = exp_value & 15;
							exp_value = exp_value + 1;
						}
					}

					exp_byte = exp_value  + 127;

					second_byte = second_byte & 96; ///////truncate the last 5 bits, clear the first bit
					second_byte = second_byte | ( exp_byte << 7 ); ////////assign exp_byte last bit to second_byte first bit
					mydata[i + 2] = second_byte;

					top_byte = top_byte & 128; //////////clear the last 7 bits
					top_byte = top_byte | ( exp_byte >> 1 ); ////////assign exp_byte first 7 bits to top_byte last 7 bits
					mydata[i + 3] = top_byte;
				}
			}//////////////////////end of: if(get_truncation_scenario() == 0){

		}else if(mf->get_truncation_scenario() == 1){//////////////In scenario 1, ////////////////////myedit highlight: fixed bug, added mf-> later

		}

		//////////////write to cache space
		((memory_space_impl<8 * 1024> *) cache_memory)->write_1(
				(block_addr >> 7) << 7, data->get_data_size(), mydata);
	} else {
		printf("out of memory bound of 4gb\n");
	}

	switch (mf->get_type()) {
	case READ_REQUEST:  break;
	case WRITE_REQUEST: break;
	case READ_REPLY:    break;
	case WRITE_ACK:     break;
	default: printf("debug2:############# mf_type=%d", mf->get_type());
	assert (0);
	}

	delete[] mydata;
}
////////myedit bfloat

void tag_array::fill(new_addr_type addr, unsigned time, unsigned predicted) { ////////myedit AMC
	assert(m_config.m_alloc_policy == ON_FILL);
	unsigned idx;
	enum cache_request_status status = probe(addr, idx); ///////////////////wb request on-fill is not even implemented.
	assert(status == MISS); // MSHR should have prevented redundant memory request (possible bug: could this be reservation fail? could this be a hit?) No, only writethrough + on-fill is allowed.
	m_lines[idx].allocate(m_config.tag(addr), m_config.block_addr(addr), time);
	m_lines[idx].fill(time, predicted); ////////myedit AMC
	fill_count++; ////////myedit AMC
}

void tag_array::fill(unsigned index, unsigned time, unsigned predicted) { ////////myedit AMC
	assert(m_config.m_alloc_policy == ON_MISS);
	m_lines[index].fill(time, predicted); ////////myedit AMC ///////could here be a hit since later miss is predicted?
	fill_count++; ////////myedit AMC
}

void tag_array::flush() {
	for (unsigned i = 0; i < m_config.get_num_lines(); i++)
		m_lines[i].m_status = INVALID;
}

float tag_array::windowed_miss_rate() const {
	unsigned n_access = m_access - m_prev_snapshot_access;
	unsigned n_miss = m_miss - m_prev_snapshot_miss;
	// unsigned n_pending_hit = m_pending_hit - m_prev_snapshot_pending_hit;

	float missrate = 0.0f;
	if (n_access != 0)
		missrate = (float) n_miss / n_access;
	return missrate;
}

void tag_array::new_window() {
	m_prev_snapshot_access = m_access;
	m_prev_snapshot_miss = m_miss;
	m_prev_snapshot_pending_hit = m_pending_hit;
}

void tag_array::print(FILE *stream, unsigned &total_access,
		unsigned &total_misses) const {
	m_config.print(stream);
	fprintf(stream,
			"\t\tAccess = %d, Miss = %d (%.3g), PendingHit = %d (%.3g)\n",
			m_access, m_miss, (float) m_miss / m_access, m_pending_hit,
			(float) m_pending_hit / m_access);
	total_misses += m_miss;
	total_access += m_access;
}

void tag_array::get_stats(unsigned &total_access, unsigned &total_misses,
		unsigned &total_hit_res, unsigned &total_res_fail) const {
	// Update statistics from the tag array
	total_access = m_access;
	total_misses = m_miss;
	total_hit_res = m_pending_hit;
	total_res_fail = m_res_fail;
}

bool was_write_sent(const std::list<cache_event> &events) {
	for (std::list<cache_event>::const_iterator e = events.begin();
			e != events.end(); e++) {
		if (*e == WRITE_REQUEST_SENT)
			return true;
	}
	return false;
}

bool was_writeback_sent(const std::list<cache_event> &events) {
	for (std::list<cache_event>::const_iterator e = events.begin();
			e != events.end(); e++) {
		if (*e == WRITE_BACK_REQUEST_SENT)
			return true;
	}
	return false;
}

bool was_read_sent(const std::list<cache_event> &events) {
	for (std::list<cache_event>::const_iterator e = events.begin();
			e != events.end(); e++) {
		if (*e == READ_REQUEST_SENT)
			return true;
	}
	return false;
}
/****************************************************************** MSHR ******************************************************************/

/// Checks if there is a pending request to the lower memory level already
bool mshr_table::probe(new_addr_type block_addr) const {
	table::const_iterator a = m_data.find(block_addr);
	return a != m_data.end();
}

/// Checks if there is space for tracking a new memory access
bool mshr_table::full(new_addr_type block_addr) const {
	table::const_iterator i = m_data.find(block_addr);
	if (i != m_data.end())
		return i->second.m_list.size() >= m_max_merged;
	else
		return m_data.size() >= m_num_entries;
}

/// Add or merge this access
void mshr_table::add(new_addr_type block_addr, mem_fetch *mf) {
	m_data[block_addr].m_list.push_back(mf);
	assert(m_data.size() <= m_num_entries);
	assert(m_data[block_addr].m_list.size() <= m_max_merged);
	// indicate that this MSHR entry contains an atomic operation
	if (mf->isatomic()) {
		m_data[block_addr].m_has_atomic = true;
	}
}

/// Accept a new cache fill response: mark entry ready for processing
void mshr_table::mark_ready(new_addr_type block_addr, bool &has_atomic,
		unsigned is_approximated, int is_l2) { //////////////myedit AMC
	assert(!busy());
	table::iterator a = m_data.find(block_addr);

	//////////////myedit AMC
	a->second.is_approx = is_approximated;
	//////////////myedit AMC

	assert(a != m_data.end()); // don't remove same request twice

	m_current_response.push_back(block_addr);

	has_atomic = a->second.m_has_atomic;
	assert(m_current_response.size() <= m_data.size());
}

/// Returns next ready access
mem_fetch *mshr_table::next_access() {
	assert(access_ready());

	new_addr_type block_addr = m_current_response.front();

	assert(!m_data[block_addr].m_list.empty());
	mem_fetch *result = m_data[block_addr].m_list.front();
	m_data[block_addr].m_list.pop_front();

	//////////////myedit AMC
	if (m_data[block_addr].is_approx) { ////////////do this before erase.
		result->set_approx();  //////myeditDSN: set prediction status for each mf in the mshr_entry, using its is_approx field.
	}
	//////////////myedit AMC

	if (m_data[block_addr].m_list.empty()) {
		// release entry
		m_data.erase(block_addr);
		m_current_response.pop_front();
	}

	return result;
}

void mshr_table::display(FILE *fp) const {
	fprintf(fp, "MSHR contents\n");
	for (table::const_iterator e = m_data.begin(); e != m_data.end(); ++e) {
		unsigned block_addr = e->first;
		fprintf(fp, "MSHR: tag=0x%06x, atomic=%d %zu entries : ", block_addr,
				e->second.m_has_atomic, e->second.m_list.size());
		if (!e->second.m_list.empty()) {
			mem_fetch *mf = e->second.m_list.front();
			fprintf(fp, "%p :", mf);
			mf->print(fp);
		} else {
			fprintf(fp, " no memory requests???\n");
		}
	}
}
/***************************************************************** Caches *****************************************************************/
cache_stats::cache_stats() {
	m_stats.resize(NUM_MEM_ACCESS_TYPE);
	for (unsigned i = 0; i < NUM_MEM_ACCESS_TYPE; ++i) {
		m_stats[i].resize(NUM_CACHE_REQUEST_STATUS, 0);
	}
	m_cache_port_available_cycles = 0;
	m_cache_data_port_busy_cycles = 0;
	m_cache_fill_port_busy_cycles = 0;
}

void cache_stats::clear() {
	///
	/// Zero out all current cache statistics
	///
	for (unsigned i = 0; i < NUM_MEM_ACCESS_TYPE; ++i) {
		std::fill(m_stats[i].begin(), m_stats[i].end(), 0);
	}
	m_cache_port_available_cycles = 0;
	m_cache_data_port_busy_cycles = 0;
	m_cache_fill_port_busy_cycles = 0;
}

void cache_stats::inc_stats(int access_type, int access_outcome) {
	///
	/// Increment the stat corresponding to (access_type, access_outcome) by 1.
	///
	if (!check_valid(access_type, access_outcome))
		assert(0 && "Unknown cache access type or access outcome");

	m_stats[access_type][access_outcome]++;
}

enum cache_request_status cache_stats::select_stats_status(
		enum cache_request_status probe,
		enum cache_request_status access) const {
	///
	/// This function selects how the cache access outcome should be counted. HIT_RESERVED is considered as a MISS
	/// in the cores, however, it should be counted as a HIT_RESERVED in the caches.
	///
	if (probe == HIT_RESERVED && access != RESERVATION_FAIL)
		return probe;
	else
		return access;
}

unsigned &cache_stats::operator()(int access_type, int access_outcome) {
	///
	/// Simple method to read/modify the stat corresponding to (access_type, access_outcome)
	/// Used overloaded () to avoid the need for separate read/write member functions
	///
	if (!check_valid(access_type, access_outcome))
		assert(0 && "Unknown cache access type or access outcome");

	return m_stats[access_type][access_outcome];
}

unsigned cache_stats::operator()(int access_type, int access_outcome) const {
	///
	/// Const accessor into m_stats.
	///
	if (!check_valid(access_type, access_outcome))
		assert(0 && "Unknown cache access type or access outcome");

	return m_stats[access_type][access_outcome];
}

cache_stats cache_stats::operator+(const cache_stats &cs) {
	///
	/// Overloaded + operator to allow for simple stat accumulation
	///
	cache_stats ret;
	for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
		for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
			ret(type, status) = m_stats[type][status] + cs(type, status);
		}
	}
	ret.m_cache_port_available_cycles = m_cache_port_available_cycles
			+ cs.m_cache_port_available_cycles;
	ret.m_cache_data_port_busy_cycles = m_cache_data_port_busy_cycles
			+ cs.m_cache_data_port_busy_cycles;
	ret.m_cache_fill_port_busy_cycles = m_cache_fill_port_busy_cycles
			+ cs.m_cache_fill_port_busy_cycles;
	return ret;
}

cache_stats &cache_stats::operator+=(const cache_stats &cs) {
	///
	/// Overloaded += operator to allow for simple stat accumulation
	///
	for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
		for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
			m_stats[type][status] += cs(type, status);
		}
	}
	m_cache_port_available_cycles += cs.m_cache_port_available_cycles;
	m_cache_data_port_busy_cycles += cs.m_cache_data_port_busy_cycles;
	m_cache_fill_port_busy_cycles += cs.m_cache_fill_port_busy_cycles;
	return *this;
}

void cache_stats::print_stats(FILE *fout, const char *cache_name) const {
	///
	/// Print out each non-zero cache statistic for every memory access type and status
	/// "cache_name" defaults to "Cache_stats" when no argument is provided, otherwise
	/// the provided name is used.
	/// The printed format is "<cache_name>[<request_type>][<request_status>] = <stat_value>"
	///
	std::string m_cache_name = cache_name;
	for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
		for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
			if (m_stats[type][status] > 0) {
				fprintf(fout, "\t%s[%s][%s] = %u\n", m_cache_name.c_str(),
						mem_access_type_str((enum mem_access_type) type),
						cache_request_status_str(
								(enum cache_request_status) status),
						m_stats[type][status]);
			}
		}
	}
}

void cache_sub_stats::print_port_stats(FILE *fout,
		const char *cache_name) const {
	float data_port_util = 0.0f;
	if (port_available_cycles > 0) {
		data_port_util = (float) data_port_busy_cycles / port_available_cycles;
	}
	fprintf(fout, "%s_data_port_util = %.3f\n", cache_name, data_port_util);
	float fill_port_util = 0.0f;
	if (port_available_cycles > 0) {
		fill_port_util = (float) fill_port_busy_cycles / port_available_cycles;
	}
	fprintf(fout, "%s_fill_port_util = %.3f\n", cache_name, fill_port_util);
}

unsigned cache_stats::get_stats(enum mem_access_type *access_type,
		unsigned num_access_type, enum cache_request_status *access_status,
		unsigned num_access_status) const {
	///
	/// Returns a sum of the stats corresponding to each "access_type" and "access_status" pair.
	/// "access_type" is an array of "num_access_type" mem_access_types.
	/// "access_status" is an array of "num_access_status" cache_request_statuses.
	///
	unsigned total = 0;
	for (unsigned type = 0; type < num_access_type; ++type) {
		for (unsigned status = 0; status < num_access_status; ++status) {
			if (!check_valid((int) access_type[type],
					(int) access_status[status]))
				assert(0 && "Unknown cache access type or access outcome");
			total += m_stats[access_type[type]][access_status[status]];
		}
	}
	return total;
}
void cache_stats::get_sub_stats(struct cache_sub_stats &css) const {
	///
	/// Overwrites "css" with the appropriate statistics from this cache.
	///
	struct cache_sub_stats t_css;
	t_css.clear();

	for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
		for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
			if (status == HIT || status == MISS || status == HIT_RESERVED)
				t_css.accesses += m_stats[type][status];

			if (status == MISS)
				t_css.misses += m_stats[type][status];

			if (status == HIT_RESERVED)
				t_css.pending_hits += m_stats[type][status];

			if (status == RESERVATION_FAIL)
				t_css.res_fails += m_stats[type][status];
		}
	}

	t_css.port_available_cycles = m_cache_port_available_cycles;
	t_css.data_port_busy_cycles = m_cache_data_port_busy_cycles;
	t_css.fill_port_busy_cycles = m_cache_fill_port_busy_cycles;

	css = t_css;
}

bool cache_stats::check_valid(int type, int status) const {
	///
	/// Verify a valid access_type/access_status
	///
	if ((type >= 0) && (type < NUM_MEM_ACCESS_TYPE) && (status >= 0)
			&& (status < NUM_CACHE_REQUEST_STATUS))
		return true;
	else
		return false;
}

void cache_stats::sample_cache_port_utility(bool data_port_busy,
		bool fill_port_busy) {
	m_cache_port_available_cycles += 1;
	if (data_port_busy) {
		m_cache_data_port_busy_cycles += 1;
	}
	if (fill_port_busy) {
		m_cache_fill_port_busy_cycles += 1;
	}
}

baseline_cache::bandwidth_management::bandwidth_management(cache_config &config) :
		m_config(config) {
	m_data_port_occupied_cycles = 0;
	m_fill_port_occupied_cycles = 0;
}

/// use the data port based on the outcome and events generated by the mem_fetch request 
void baseline_cache::bandwidth_management::use_data_port(mem_fetch *mf,
		enum cache_request_status outcome,
		const std::list<cache_event> &events) {
	unsigned data_size = mf->get_data_size();
	unsigned port_width = m_config.m_data_port_width;
	switch (outcome) {
	case HIT: {
		unsigned data_cycles = data_size / port_width
				+ ((data_size % port_width > 0) ? 1 : 0);
		m_data_port_occupied_cycles += data_cycles;
	}
		break;
	case HIT_RESERVED:
	case MISS: {
		// the data array is accessed to read out the entire line for write-back
		if (was_writeback_sent(events)) {
			unsigned data_cycles = m_config.m_line_sz / port_width;
			m_data_port_occupied_cycles += data_cycles;
		}
	}
		break;
	case RESERVATION_FAIL:
		// Does not consume any port bandwidth
		break;
	default:
		assert(0);
		break;
	}
}

/// use the fill port 
void baseline_cache::bandwidth_management::use_fill_port(mem_fetch *mf) {
	// assume filling the entire line with the returned request
	unsigned fill_cycles = m_config.m_line_sz / m_config.m_data_port_width;
	m_fill_port_occupied_cycles += fill_cycles;
}

/// called every cache cycle to free up the ports 
void baseline_cache::bandwidth_management::replenish_port_bandwidth() {
	if (m_data_port_occupied_cycles > 0) {
		m_data_port_occupied_cycles -= 1;
	}
	assert(m_data_port_occupied_cycles >= 0);

	if (m_fill_port_occupied_cycles > 0) {
		m_fill_port_occupied_cycles -= 1;
	}
	assert(m_fill_port_occupied_cycles >= 0);
}

/// query for data port availability 
bool baseline_cache::bandwidth_management::data_port_free() const {
	return (m_data_port_occupied_cycles == 0);
}

/// query for fill port availability 
bool baseline_cache::bandwidth_management::fill_port_free() const {
	return (m_fill_port_occupied_cycles == 0);
}

/// Sends next request to lower level of memory
void baseline_cache::cycle() {
	if (!m_miss_queue.empty()) {
		mem_fetch *mf = m_miss_queue.front();
		if (!m_memport->full(mf->size(), mf->get_is_write())) {
			m_miss_queue.pop_front();
			m_memport->push(mf);
		}
	}
	bool data_port_busy = !m_bandwidth_management.data_port_free();
	bool fill_port_busy = !m_bandwidth_management.fill_port_free();
	m_stats.sample_cache_port_utility(data_port_busy, fill_port_busy);
	m_bandwidth_management.replenish_port_bandwidth();
}

/// Interface for response from lower memory level (model bandwidth restictions in caller)
void baseline_cache::fill(mem_fetch *mf, unsigned time) {
	extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
	assert(e != m_extra_mf_fields.end());
	assert(e->second.m_valid);
	mf->set_data_size(e->second.m_data_size);

	/////////////myedit AMC
	if (!mf->is_approximated() || always_fill) {

		if (m_config.m_alloc_policy == ON_MISS) {

			if (m_config.m_write_policy != LOCAL_WB_GLOBAL_WT
					&& mf->is_approximated() ) {	/////////////only l2 can search, l1 is using LOCAL_WB_GLOBAL_WT

					//printf("##########debug:m_name:%s\n", m_name.c_str());/////////////debug
					//fflush (stdout);

				m_tag_array->truncate_float(mf);	//////////////truncate happens before fill
			}

			///////////////////////////////m_tag_array->fill(e->second.m_cache_index,time);
			m_tag_array->fill(e->second.m_cache_index, time,
					mf->is_approximated());	////////myedit amc  ///////////myeditDSN: both l1 and l2 are marked correctly with prediction status.
		} else if (m_config.m_alloc_policy == ON_FILL) {

			if (m_config.m_write_policy != LOCAL_WB_GLOBAL_WT
					&& mf->is_approximated() ) {	/////////////only l2 can search, l1 is using LOCAL_WB_GLOBAL_WT

					//printf("##########debug:m_name:%s\n", m_name.c_str());/////////////debug
					//fflush (stdout);
				m_tag_array->truncate_float(mf);
			}

			///////////////////////////////m_tag_array->fill(e->second.m_block_addr,time);
			m_tag_array->fill(e->second.m_block_addr, time,
					mf->is_approximated());	////////myedit amc  ///////////myeditDSN: both l1 and l2 are marked correctly with prediction status.
		} else {

			abort();
		}
	} else {////////do not fill if it is approximated. (on_fill makes sure it's not allocated and reserved forever.)

		assert(m_config.m_alloc_policy == ON_FILL);	///////////////it cannot be allocated beforehand since we don't know if we will fill it or not (we do not fill the cacheline with approx data.)
	}

	bool has_atomic = false;

	/////////////myedit bfloat: should not delay
	m_mshrs.mark_ready(e->second.m_block_addr, has_atomic, mf->is_approximated(), 0);
	/////////////myedit AMC

	if (has_atomic) {
		assert(m_config.m_alloc_policy == ON_MISS);
		cache_block_t &block = m_tag_array->get_block(e->second.m_cache_index);
		block.m_status = MODIFIED; // mark line as dirty for atomic operation
	}

	m_extra_mf_fields.erase(mf);
	m_bandwidth_management.use_fill_port(mf);
}

/// Checks if mf is waiting to be filled by lower memory level
bool baseline_cache::waiting_for_fill(mem_fetch *mf) {
	extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
	return e != m_extra_mf_fields.end();
}

void baseline_cache::print(FILE *fp, unsigned &accesses,
		unsigned &misses) const {
	fprintf(fp, "Cache %s:\t", m_name.c_str());
	m_tag_array->print(fp, accesses, misses);
}

void baseline_cache::display_state(FILE *fp) const {
	fprintf(fp, "Cache %s:\n", m_name.c_str());
	m_mshrs.display(fp);
	fprintf(fp, "\n");
}

/// Read miss handler without writeback
void baseline_cache::send_read_request(new_addr_type addr,
		new_addr_type block_addr, unsigned cache_index, mem_fetch *mf,
		unsigned time, bool &do_miss, std::list<cache_event> &events,
		bool read_only, bool wa) {

	bool wb = false;
	cache_block_t e;
	send_read_request(addr, block_addr, cache_index, mf, time, do_miss, wb, e,
			events, read_only, wa);
}

//////////////////myedit predictor
//void baseline_cache::send_approx_request(){
//}
//////////////////myedit predictor

/// Read miss handler. Check MSHR hit or MSHR available
void baseline_cache::send_read_request(new_addr_type addr,
		new_addr_type block_addr, unsigned cache_index, mem_fetch *mf,
		unsigned time, bool &do_miss, bool &wb, cache_block_t &evicted,
		std::list<cache_event> &events, bool read_only, bool wa) {

	bool mshr_hit = m_mshrs.probe(block_addr);
	bool mshr_avail = !m_mshrs.full(block_addr);
	if (mshr_hit && mshr_avail) {
		if (read_only)
			m_tag_array->access(block_addr, time, cache_index);
		else
			m_tag_array->access(block_addr, time, cache_index, wb, evicted);

		m_mshrs.add(block_addr, mf);
		do_miss = true;
	} else if (!mshr_hit && mshr_avail
			&& (m_miss_queue.size() < m_config.m_miss_queue_size)) {
		if (read_only)
			m_tag_array->access(block_addr, time, cache_index);
		else
			m_tag_array->access(block_addr, time, cache_index, wb, evicted);

		m_mshrs.add(block_addr, mf);
		m_extra_mf_fields[mf] = extra_mf_fields(block_addr, cache_index,
				mf->get_data_size());
		mf->set_data_size(m_config.get_line_sz());
		m_miss_queue.push_back(mf);
		mf->set_status(m_miss_queue_status, time);
		if (!wa)
			events.push_back(READ_REQUEST_SENT);
		do_miss = true;
	}
}

/// Sends write request to lower level memory (write or writeback)
void data_cache::send_write_request(mem_fetch *mf, cache_event request,
		unsigned time, std::list<cache_event> &events) {
	events.push_back(request);
	m_miss_queue.push_back(mf);
	mf->set_status(m_miss_queue_status, time);
}

/****** Write-hit functions (Set by config file) ******/

/// Write-back hit: Mark block as modified
cache_request_status data_cache::wr_hit_wb(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	/*
	 if (mf->get_sid() == 0) {
	 printf(
	 "-----------------wr_hit_wb! time:%llu, cache_index:%u, wr:1---------------------\n",
	 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index);

	 }
	 */
	new_addr_type block_addr = m_config.block_addr(addr);
	m_tag_array->access(block_addr, time, cache_index); // update LRU state
	cache_block_t &block = m_tag_array->get_block(cache_index);
	block.m_status = MODIFIED;
	//block.is_predicted = 0; /////////myedit AMC

	return HIT;
}

/// Write-through hit: Directly send request to lower level memory
cache_request_status data_cache::wr_hit_wt(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	if (miss_queue_full(0))
		return RESERVATION_FAIL; // cannot handle request this cycle

	new_addr_type block_addr = m_config.block_addr(addr);
	m_tag_array->access(block_addr, time, cache_index); // update LRU state
	cache_block_t &block = m_tag_array->get_block(cache_index);
	block.m_status = MODIFIED;
	//block.is_predicted = 0; /////////myedit amc //////////////myeditDSN: write shall not change the prediction status,
	//////////////////because there might be unchanged parts which still remain their status. we write to both cache and global space and use one of them accordingly.

	// generate a write-through
	send_write_request(mf, WRITE_REQUEST_SENT, time, events);

	return HIT;
}

/// Write-evict hit: Send request to lower level memory and invalidate corresponding block
cache_request_status data_cache::wr_hit_we(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	/*
	 if (mf->get_sid() == 0) {
	 printf(
	 "-----------------wr_hit_we! time:%llu, cache_index:%u, wr:1---------------------\n",
	 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index);

	 }
	 */
	if (miss_queue_full(0))
		return RESERVATION_FAIL; // cannot handle request this cycle

	// generate a write-through/evict
	cache_block_t &block = m_tag_array->get_block(cache_index);
	send_write_request(mf, WRITE_REQUEST_SENT, time, events);

	// Invalidate block
	block.m_status = INVALID;

	return HIT;
}

/// Global write-evict, local write-back: Useful for private caches
enum cache_request_status data_cache::wr_hit_global_we_local_wb(
		new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	bool evict = (mf->get_access_type() == GLOBAL_ACC_W); // evict a line that hits on global memory write
	if (evict)
		return wr_hit_we(addr, cache_index, mf, time, events, status); // Write-evict
	else
		return wr_hit_wb(addr, cache_index, mf, time, events, status); // Write-back
}

/****** Write-miss functions (Set by config file) ******/

/// Write-allocate miss: Send write request to lower level memory
// and send a read request for the same block
enum cache_request_status data_cache::wr_miss_wa(new_addr_type addr, //////////for wa, writethrough should send a write, a read, and a MODIFIED(optional), with no wb.
		unsigned cache_index, mem_fetch *mf, unsigned time, ///////////bug:writeback should send no-write(error), a read, a MODIFIED(error),with wb(optional).
		std::list<cache_event> &events, enum cache_request_status status) { //////////////myeditDSN: probably write-miss policies and write-hit policies are different things.
	new_addr_type block_addr = m_config.block_addr(addr);

	// Write allocate, maximum 3 requests (write miss, read request, write back request)
	// Conservatively ensure the worst-case request can be handled this cycle
	bool mshr_hit = m_mshrs.probe(block_addr);
	bool mshr_avail = !m_mshrs.full(block_addr);
	if (miss_queue_full(2)
			|| (!(mshr_hit && mshr_avail)
					&& !(!mshr_hit && mshr_avail
							&& (m_miss_queue.size() < m_config.m_miss_queue_size))))
		return RESERVATION_FAIL;

	send_write_request(mf, WRITE_REQUEST_SENT, time, events);////myquestion:how come write-back cache send this?
	///////////////////////////////answer: write hit policies and write miss policies are different. //////////////myeditDSN
	////////////////////on write miss it first send off the write to lower level of memory (instead of jamming the cache), then read the entire cache line back to the reserved block.

	// Tries to send write allocate request, returns true on success and false on failure
	//if(!send_write_allocate(mf, addr, block_addr, cache_index, time, events))
	//    return RESERVATION_FAIL;

	const mem_access_t *ma = new mem_access_t(m_wr_alloc_type,
			mf->get_addr(), /////L2_WR_ALLOC_R ///////myquestion:will m_wr_alloc_type mark modified? no.
			mf->get_data_size(),
			false, // Now performing a read
			mf->get_access_warp_mask(), mf->get_access_byte_mask(),
			mf->is_access_atomic(), mf->get_access_thread_correspondance());

	mem_fetch *n_mf = new mem_fetch(*ma, NULL, mf->get_ctrl_size(),
			mf->get_wid(), mf->get_sid(), mf->get_tpc(), mf->get_mem_config());

	bool do_miss = false;
	bool wb = false;
	cache_block_t evicted;

	// Send read request resulting from write miss
	send_read_request(addr, block_addr, cache_index, n_mf, time, do_miss, wb, ///myeditDSN: it is first written to lower level of memory, and read back from it. Not modified after that.
			evicted, events, false, true); /////bug: a write would change the status to be MODIFIED after this read fills, however it doesn't.

	if (do_miss) {
		// If evicted block is modified and not a write-through (a write-through already modified lower level)
		if (wb && (m_config.m_write_policy != WRITE_THROUGH)) { /////myquestion:how about write evict? can it use either of wb or wt? /////myeditDSN: not relevant. see answers above.
			mem_fetch *wb = m_memfetch_creator->alloc(evicted.m_block_addr,
					m_wrbk_type, m_config.get_line_sz(), true);
			m_miss_queue.push_back(wb);	////////myquestion:will write miss's read fill mark modified? why do not events.push_back(request); ? /////myeditDSN: see answer above.
			wb->set_status(m_miss_queue_status, time);

			////////////myeditDSN
			////////////////////////////////here we only cover the l2 to dram case, because l1 is using we. (we only need to fix l1 if l1 uses wb)
			/////////////myedit bfloat: think carefully, when write back to the dram, do you write the whole thing or just write the bfloat (also just store the bfloat part of the st operation in cache)?

			if(m_config.m_write_policy != LOCAL_WB_GLOBAL_WT
					&& evicted.is_predicted == 1){ ////////////////////only l2 need to do the copy from cache space to global space
				///////////////////myedit highlight: for a fetch-on-write cache (gtx480), it does not have the modified-words mask, therefore, whole cache line must be written back to the dram.
				//////////////////////////////////// however, for our favor, we can assume that it does have the modified-words mask, and can switch to write-validate mode (titanx).
				//////////////////////////////////// just that it uses the fetch-on-write to avoid the unreadable lines when read hit.
				//////////////////////////////////// if that is the case, then the following code to write the modified predicted line back to the dram when evicted is not required.
				//////////////////////////////////// this is because in st_impl( in instructions.cc, the modified words are written to both cache and global space already.
				//////////////////////////////////// so if we only write according to the modified-words mask, copying both the modified part and the remaining predicted part is unnecessary.

				/////////////////////////////read from cache space
				mem_fetch *data = wb;
				char * mydata = new char[data->get_data_size()];
				new_addr_type address_limit;

				address_limit = data->get_addr() + data->get_data_size();

				if (address_limit <= 0x100000000) {
					/////////////////////////////read from cache space
					((memory_space_impl<8 * 1024> *) cache_memory)->read(
								(data->get_addr() >> 7) << 7, data->get_data_size(), mydata);

					/////////////////////////////write to global space
					((memory_space_impl<8 * 1024> *) global_memory)->write_1(
							(data->get_addr() >> 7) << 7, data->get_data_size(), mydata);

				} else {
					printf("out of memory bound of 4gb\n");
				}

				delete[] mydata;
				/////////////////////////////write to global space
			}
			////////////myeditDSN
		}
		return MISS;
	}

	return RESERVATION_FAIL;
}

/// No write-allocate miss: Simply send write request to lower level memory
enum cache_request_status data_cache::wr_miss_no_wa(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {	//////////for no_wa, misses are all the same, they have to be writethrough. There's only difference in hit.
	if (miss_queue_full(0))
		return RESERVATION_FAIL; // cannot handle request this cycle

	// on miss, generate write through (no write buffering -- too many threads for that)
	send_write_request(mf, WRITE_REQUEST_SENT, time, events);

	return MISS;
}

/****** Read hit functions (Set by config file) ******/

/// Baseline read hit: Update LRU status of block.
// Special case for atomic instructions -> Mark block as modified
enum cache_request_status data_cache::rd_hit_base(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	new_addr_type block_addr = m_config.block_addr(addr);
	m_tag_array->access(block_addr, time, cache_index);
	// Atomics treated as global read/write requests - Perform read, mark line as
	// MODIFIED
	if (mf->isatomic()) {
		assert(mf->get_access_type() == GLOBAL_ACC_R);
		cache_block_t &block = m_tag_array->get_block(cache_index);
		block.m_status = MODIFIED; // mark line as dirty
	}
	return HIT;
}

/****** Read miss functions (Set by config file) ******/

/// Baseline read miss: Send read request to lower level memory,
// perform write-back as necessary
enum cache_request_status data_cache::rd_miss_base(new_addr_type addr,
		unsigned cache_index, mem_fetch *mf, unsigned time,
		std::list<cache_event> &events, enum cache_request_status status) {
	if (miss_queue_full(1))
		// cannot handle request this cycle
		// (might need to generate two requests)
		return RESERVATION_FAIL;

	new_addr_type block_addr = m_config.block_addr(addr);
	bool do_miss = false;
	bool wb = false;
	cache_block_t evicted;
	send_read_request(addr, block_addr, cache_index, mf, time, do_miss, wb,
			evicted, events, false, false);

	if (do_miss) {
		// If evicted block is modified and not a write-through
		// (already modified lower level)
		if (wb && (m_config.m_write_policy != WRITE_THROUGH)) {
			mem_fetch *wb = m_memfetch_creator->alloc(evicted.m_block_addr,
					m_wrbk_type, m_config.get_line_sz(), true);
			send_write_request(wb, WRITE_BACK_REQUEST_SENT, time, events);

			////////////myeditDSN
			////////////////////////////////here we only cover the l2 to dram case, because l1 is using we. (we only need to fix l1 if l1 uses wb)
			if(m_config.m_write_policy != LOCAL_WB_GLOBAL_WT
					&& evicted.is_predicted == 1){ ////////////////////only l2 need to do the copy from cache space to global space
				///////////////////myedit highlight: for a fetch-on-write cache (gtx480), it does not have the modified-words mask, therefore, whole cache line must be written back to the dram.
				//////////////////////////////////// however, for our favor, we can assume that it does have the modified-words mask, and can switch to write-validate mode (titanx).
				//////////////////////////////////// just that it uses the fetch-on-write to avoid the unreadable lines when read hit.
				//////////////////////////////////// if that is the case, then the following code to write the modified predicted line back to the dram when evicted is not required.
				//////////////////////////////////// this is because in st_impl( in instructions.cc, the modified words are written to both cache and global space already.
				//////////////////////////////////// so if we only write according to the modified-words mask, copying both the modified part and the remaining predicted part is unnecessary.

				/////////////////////////////read from cache space
				mem_fetch *data = wb;
				char * mydata = new char[data->get_data_size()];
				new_addr_type address_limit;

				address_limit = data->get_addr() + data->get_data_size();

				if (address_limit <= 0x100000000) {
					/////////////////////////////read from cache space
					((memory_space_impl<8 * 1024> *) cache_memory)->read(
								(data->get_addr() >> 7) << 7, data->get_data_size(), mydata);

					/////////////////////////////write to global space
					((memory_space_impl<8 * 1024> *) global_memory)->write_1(
							(data->get_addr() >> 7) << 7, data->get_data_size(), mydata);

				} else {
					printf("out of memory bound of 4gb\n");
				}

				delete[] mydata;
				/////////////////////////////write to global space
			}
			////////////myeditDSN

		}
		return MISS;
	}
	return RESERVATION_FAIL;
}

/// Access cache for read_only_cache: returns RESERVATION_FAIL if
// request could not be accepted (for any reason)
enum cache_request_status read_only_cache::access(new_addr_type addr,
		mem_fetch *mf, unsigned time, std::list<cache_event> &events) {
	assert(mf->get_data_size() <= m_config.get_line_sz());
	assert(m_config.m_write_policy == READ_ONLY);
	assert(!mf->get_is_write());
	new_addr_type block_addr = m_config.block_addr(addr);
	unsigned cache_index = (unsigned) -1;
	enum cache_request_status status = m_tag_array->probe(block_addr,
			cache_index);
	enum cache_request_status cache_status = RESERVATION_FAIL;

	if (status == HIT) {
		cache_status = m_tag_array->access(block_addr, time, cache_index); // update LRU state
	} else if (status != RESERVATION_FAIL) {
		if (!miss_queue_full(0)) {
			bool do_miss = false;
			send_read_request(addr, block_addr, cache_index, mf, time, do_miss,
					events, true, false);
			if (do_miss)
				cache_status = MISS;
			else
				cache_status = RESERVATION_FAIL;
		} else {
			cache_status = RESERVATION_FAIL;
		}
	}

	m_stats.inc_stats(mf->get_access_type(),
			m_stats.select_stats_status(status, cache_status));
	return cache_status;
}

//! A general function that takes the result of a tag_array probe
//  and performs the correspding functions based on the cache configuration
//  The access function calls this function
enum cache_request_status data_cache::process_tag_probe(bool wr,
		enum cache_request_status probe_status, new_addr_type addr,
		unsigned cache_index, mem_fetch* mf, unsigned time,
		std::list<cache_event>& events) {
	// Each function pointer ( m_[rd/wr]_[hit/miss] ) is set in the
	// data_cache constructor to reflect the corresponding cache configuration
	// options. Function pointers were used to avoid many long conditional
	// branches resulting from many cache configuration options.
	cache_request_status access_status = probe_status;
	if (wr) { // Write
		if (probe_status == HIT) {
			/*
			 if (mf->get_sid() == 0) {
			 printf(
			 "-----------------HIT! time:%llu, cache_index:%u, wr:%u---------------------\n",
			 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index, wr);

			 }
			 */
			access_status = (this->*m_wr_hit)(addr, cache_index, mf, time,
					events, probe_status);
		} else if (probe_status != RESERVATION_FAIL) {
			/*
			 if (mf->get_sid() == 0) {
			 printf(
			 "-----------------MISS! time:%llu, cache_index:%u, wr:%u---------------------\n",
			 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index, wr);

			 }
			 */
			access_status = (this->*m_wr_miss)(addr, cache_index, mf, time,
					events, probe_status);
		}
	} else { // Read
		if (probe_status == HIT) {
			/*
			 if (mf->get_sid() == 0) {
			 printf(
			 "-----------------HIT! time:%llu, cache_index:%u, wr:%u---------------------\n",
			 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index, wr);

			 }
			 */
			access_status = (this->*m_rd_hit)(addr, cache_index, mf, time,
					events, probe_status);
		} else if (probe_status != RESERVATION_FAIL) {
			/*
			 if (mf->get_sid() == 0) {
			 printf(
			 "-----------------MISS! time:%llu, cache_index:%u, wr:%u---------------------\n",
			 gpu_tot_sim_cycle + gpu_sim_cycle, cache_index, wr);

			 }
			 */
			access_status = (this->*m_rd_miss)(addr, cache_index, mf, time,
					events, probe_status);
		}
	}

	m_bandwidth_management.use_data_port(mf, access_status, events);
	return access_status;
}

// Both the L1 and L2 currently use the same access function.
// Differentiation between the two caches is done through configuration
// of caching policies.
// Both the L1 and L2 override this function to provide a means of
// performing actions specific to each cache when such actions are implemnted.
enum cache_request_status data_cache::access(new_addr_type addr, mem_fetch *mf,
		unsigned time, std::list<cache_event> &events) {

	assert(mf->get_data_size() <= m_config.get_line_sz());
	bool wr = mf->get_is_write();
	new_addr_type block_addr = m_config.block_addr(addr);
	unsigned cache_index = (unsigned) -1;
	enum cache_request_status probe_status = m_tag_array->probe(block_addr,
			cache_index);
	enum cache_request_status access_status = process_tag_probe(wr,
			probe_status, addr, cache_index, mf, time, events);
	m_stats.inc_stats(mf->get_access_type(),
			m_stats.select_stats_status(probe_status, access_status));
	return access_status;
}

//////////////myeditpredictor
void tag_array::allocate_and_fill_prediction(unsigned index, unsigned time,
		new_addr_type addr) {
	m_lines[index].allocate(m_config.tag(addr), m_config.block_addr(addr),
			time);
	m_lines[index].fill_predicted(time);

	assert(m_lines[index].m_status != MODIFIED);

	m_access++;
	shader_cache_access_log(m_core_id, m_type_id, 0); // log accesses to cache
}

unsigned tag_array::is_predicted(unsigned index) {
	return m_lines[index].is_predicted;
}
//////////////myeditpredictor

/// This is meant to model the first level data cache in Fermi.
/// It is write-evict (global) or write-back (local) at the
/// granularity of individual blocks (Set by GPGPU-Sim configuration file)
/// (the policy used in fermi according to the CUDA manual)
enum cache_request_status l1_cache::access(new_addr_type addr, mem_fetch *mf,
		unsigned time, std::list<cache_event> &events) {

	//////////////myeditDSN: process l1 hit approx
	/////note: which space to write to for writes: wb: first write to cache space, then global space; wt: write to both cache space and global space; we: write to global space directly.
	////////places to change for write: write op (for read after write) & memory writes (for eviction policies)
	////////write op: write to both cache and global; L2 writeback: predicted: write from cache to global, accurate: do not write from cache to global.
	////////check: is is_predicted correctly set when read?
	///////can we prevent approximate data from being written to the memory? we, wt could, wb has to change accurate to approx in global space.
	/////is there read for cache write-misses for we, wt or wb? if so, do they have to write to lower levels in 128 bytes chunks?
	/////what is the write mf size? decided by memory_coalescing_arch_13_reduce_and_send(, 64 or 128.
	/////but since no read is done before write when write miss, actually the size should be the size of the changed part.
	////////////what does write hit reserved do? write request itself never get registered in the MSHR or reserve any cache block. so the reserved block hit is allocated for the read.
	/////it is treated indifferently as write miss (send write directly). therefore, it is not clear what will happen if the read reply is already on its way back when sending the write.
	/////key: check when write hit or when write miss evict, if the whole chunk is brought down or not.
	/////so wb has to write the entire chunk. we and wt's send_write_request(, if can only send the changed, then sending the whole chunk can be avoided.
	/////If it is a hit at L2, then write evict does not need to send the whole chunk. If it is a miss at L2, then write evict also does not need to send the whole chunk.
	//////////////todo: 1. do nothing for write miss. ##ok 2. in write op write to both cache and global space. ##ok 3. do nothing for wt, we. ##ok
	////////////////////4. copy from cache space to global space for wb evicted modified in L2. ##ok 5. make sure read low and write low (not needed if l1 uses we) change approx mark. ##ok
	////////////////////6. re-execute for l1 hit-approx. ##ok
	///////////ps: for downstream propagation, since L1 is we, we only need to copy from cache space to global space for approx evicted-write.
	///////////////for upstream propagation, check if l1 and l2's marks are set correctly.
	///////////The only case when a full cache line is written,
	///////////is when a modified cache line is evicted under the write back policy (produced only by write hits, write misses do not produce Modified).
	///////////Fortunately, l1 is not wb. If l1 uses wb, for the case of the generated write-back writes from l1, l2 has to mark their prediction status and
	///////////copy from cache space to global space if l2 also sends them to the dram.
	//////////////myedit bfloat: (when write approximate cache line back to dram, bfloat seems to be similar as lmc)
	//////////////When error is caused in L1, we should use the same number of L1 cache spaces as the number of SMs.
	//////////////When using two separate cache spaces for both L1 and L2, st_impl does not need to write to L1 since it is write evict. And st_impl always write to global space.
	//////////////st_impl should also directly write to L2, indicating that write evict always write directly to L2.
	//////////////However, read accurate from L2 or DRAM should use global space (do nothing), read approx from L2 should redo with L2 space.
	//////////////Read accurate + inject_error_L1 from L2 or DRAM should copy from global to L1 space and inject error,
	//////////////read approx + inject_error_L1 from L2 should copy from L2 space to L1 space and inject error.

	enum cache_request_status access_status = data_cache::access(addr, mf, time, events);

	if (access_status == HIT && mf->get_access_type() == GLOBAL_ACC_R ) { //////////hit a predicted data, then redo the load with data from cache space

		//////////get cache_index
		new_addr_type block_addr = m_config.block_addr(addr);
		unsigned cache_index = (unsigned) -1;

		m_tag_array->probe(	block_addr, cache_index );
		//////////get cache_index

		if (m_tag_array->is_predicted(cache_index) == 1) {

			if (redo_in_l1) {

				actual_redo++;

				unsigned is_ld = 1;
				for (unsigned t = 0; t < 32; t++) {

					unsigned data_starting_index_of_thread_in_line =
							(mf->get_access_thread_correspondance())[t]; /////data starting indices that belong to this line and belong to this warp

					if (data_starting_index_of_thread_in_line > 0) {
						if ( mf->get_inst().active(t) ) { ////////////which threads are requesting this line? must redo accordingly.

							unsigned tid = 32 * ( mf->get_inst().warp_id() ) + t; ////////////////////myquestion:is tid local to the CTA?
							is_ld = m_core->m_thread[tid]->ptx_is_ld_at_pc( mf->get_pc() ); ///////////////only if this instruction is ld that it can be used to approximate

							if (is_ld == 0) {
								break;
							}
						} /////end of: if (mf->get_inst().active(thread_of_warp_in_line - 1))
					} /////end of: if (thread_of_warp_in_line > 0)
				} /////end of: for (unsigned t = 0; t < 32; t++)

				if (is_ld == 1) {

					for (unsigned t = 0; t < 32; t++) {

						unsigned data_starting_index_of_thread_in_line = /////data indices that belong to this line and belong to this warp
								(mf->get_access_thread_correspondance())[t];

						if (data_starting_index_of_thread_in_line > 0) { ///////////0 means null, and the real id is: thread_of_warp_in_line - 1

							if ( mf->get_inst().active(t) ) { ////////////which threads are requesting this line? must redo accordingly. input range (0 - 31).

								unsigned tid = 32 * ( mf->get_inst().warp_id() ) + t; ////////////////////myquestion:is tid local to the CTA?
								m_core->m_thread[tid]->ptx_exec_ld_at_pc( mf->get_pc() ); /////redo load

							} ////////////end of: if (mf->get_inst().active(thread_of_warp_in_line - 1))
						} ////////end of: if (thread_of_warp_in_line > 0)
					} ////////end of: for (unsigned t = 0; t < 32; t++)
				} //////end of: if (is_ld == 1)
			}/////////end of: if (redo_in_l1) {

		} /////end of: if (m_tag_array->is_predicted(cache_index) == 1) {
	}/////end of: if (access_status == HIT && mf->get_access_type() == GLOBAL_ACC_R ) {

	return access_status;
	//return data_cache::access(addr, mf, time, events);
	//////////////myeditDSN: process l1 hit approx
}

// The l2 cache access function calls the base data_cache access
// implementation.  When the L2 needs to diverge from L1, L2 specific
// changes should be made here.
enum cache_request_status l2_cache::access(new_addr_type addr, mem_fetch *mf,
		unsigned time, std::list<cache_event> &events) {

	//////////////myeditDSN: process l2 hit approx
	enum cache_request_status access_status = data_cache::access(addr, mf, time, events);

	if (access_status == HIT && mf->get_access_type() == GLOBAL_ACC_R ) { //////////hit a predicted data, then return a predicted data to the higher level

		//////////get cache_index
		new_addr_type block_addr = m_config.block_addr(addr);
		unsigned cache_index = (unsigned) -1;

		m_tag_array->probe(	block_addr, cache_index);
		//////////get cache_index

		if (m_tag_array->is_predicted(cache_index) == 1) {
			mf->set_approx(); //////////this mf is what will be pushed back to the l2_inct_queue.
		}
	}

	return access_status;
	//return data_cache::access(addr, mf, time, events);
	//////////////myeditDSN: process l2 hit approx
}

/// Access function for tex_cache
/// return values: RESERVATION_FAIL if request could not be accepted
/// otherwise returns HIT_RESERVED or MISS; NOTE: *never* returns HIT
/// since unlike a normal CPU cache, a "HIT" in texture cache does not
/// mean the data is ready (still need to get through fragment fifo)
enum cache_request_status tex_cache::access(new_addr_type addr, mem_fetch *mf,
		unsigned time, std::list<cache_event> &events) {
	if (m_fragment_fifo.full() || m_request_fifo.full() || m_rob.full())
		return RESERVATION_FAIL;

	assert(mf->get_data_size() <= m_config.get_line_sz());

// at this point, we will accept the request : access tags and immediately allocate line
	new_addr_type block_addr = m_config.block_addr(addr);
	unsigned cache_index = (unsigned) -1;
	enum cache_request_status status = m_tags.access(block_addr, time,
			cache_index);
	enum cache_request_status cache_status = RESERVATION_FAIL;
	assert(status != RESERVATION_FAIL);
	assert(status != HIT_RESERVED); // as far as tags are concerned: HIT or MISS
	m_fragment_fifo.push(
			fragment_entry(mf, cache_index, status == MISS,
					mf->get_data_size()));
	if (status == MISS) {
		// we need to send a memory request...
		unsigned rob_index = m_rob.push(rob_entry(cache_index, mf, block_addr));
		m_extra_mf_fields[mf] = extra_mf_fields(rob_index);
		mf->set_data_size(m_config.get_line_sz());
		m_tags.fill(cache_index, time, 0); ////////myedit amc // mark block as valid
		m_request_fifo.push(mf);
		mf->set_status(m_request_queue_status, time);
		events.push_back(READ_REQUEST_SENT);
		cache_status = MISS;
	} else {
		// the value *will* *be* in the cache already
		cache_status = HIT_RESERVED;
	}
	m_stats.inc_stats(mf->get_access_type(),
			m_stats.select_stats_status(status, cache_status));
	return cache_status;
}

void tex_cache::cycle() {
// send next request to lower level of memory
	if (!m_request_fifo.empty()) {
		mem_fetch *mf = m_request_fifo.peek();
		if (!m_memport->full(mf->get_ctrl_size(), false)) {
			m_request_fifo.pop();
			m_memport->push(mf);
		}
	}
// read ready lines from cache
	if (!m_fragment_fifo.empty() && !m_result_fifo.full()) {
		const fragment_entry &e = m_fragment_fifo.peek();
		if (e.m_miss) {
			// check head of reorder buffer to see if data is back from memory
			unsigned rob_index = m_rob.next_pop_index();
			const rob_entry &r = m_rob.peek(rob_index);
			assert(r.m_request == e.m_request);
			assert(
					r.m_block_addr
							== m_config.block_addr(e.m_request->get_addr()));
			if (r.m_ready) {
				assert(r.m_index == e.m_cache_index);
				m_cache[r.m_index].m_valid = true;
				m_cache[r.m_index].m_block_addr = r.m_block_addr;
				m_result_fifo.push(e.m_request);
				m_rob.pop();
				m_fragment_fifo.pop();
			}
		} else {
			// hit:
			assert(m_cache[e.m_cache_index].m_valid);
			assert(
					m_cache[e.m_cache_index].m_block_addr
							== m_config.block_addr(e.m_request->get_addr()));
			m_result_fifo.push(e.m_request);
			m_fragment_fifo.pop();
		}
	}
}

/// Place returning cache block into reorder buffer
void tex_cache::fill(mem_fetch *mf, unsigned time) {
	extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
	assert(e != m_extra_mf_fields.end());
	assert(e->second.m_valid);
	assert(!m_rob.empty());
	mf->set_status(m_rob_status, time);

	unsigned rob_index = e->second.m_rob_index;
	rob_entry &r = m_rob.peek(rob_index);
	assert(!r.m_ready);
	r.m_ready = true;
	r.m_time = time;
	assert(r.m_block_addr == m_config.block_addr(mf->get_addr()));
}

void tex_cache::display_state(FILE *fp) const {
	fprintf(fp, "%s (texture cache) state:\n", m_name.c_str());
	fprintf(fp, "fragment fifo entries  = %u / %u\n", m_fragment_fifo.size(),
			m_fragment_fifo.capacity());
	fprintf(fp, "reorder buffer entries = %u / %u\n", m_rob.size(),
			m_rob.capacity());
	fprintf(fp, "request fifo entries   = %u / %u\n", m_request_fifo.size(),
			m_request_fifo.capacity());
	if (!m_rob.empty())
		fprintf(fp, "reorder buffer contents:\n");
	for (int n = m_rob.size() - 1; n >= 0; n--) {
		unsigned index = (m_rob.next_pop_index() + n) % m_rob.capacity();
		const rob_entry &r = m_rob.peek(index);
		fprintf(fp, "tex rob[%3d] : %s ", index,
				(r.m_ready ? "ready  " : "pending"));
		if (r.m_ready)
			fprintf(fp, "@%6u", r.m_time);
		else
			fprintf(fp, "       ");
		fprintf(fp, "[idx=%4u]", r.m_index);
		r.m_request->print(fp, false);
	}
	if (!m_fragment_fifo.empty()) {
		fprintf(fp, "fragment fifo (oldest) :");
		fragment_entry &f = m_fragment_fifo.peek();
		fprintf(fp, "%s:          ", f.m_miss ? "miss" : "hit ");
		f.m_request->print(fp, false);
	}
}
/******************************************************************************************************************************************/

