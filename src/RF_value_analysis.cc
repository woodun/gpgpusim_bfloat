#ifndef RF_ANALYSIS_INCLUDED//include guard.
#define RF_ANALYSIS_INCLUDED

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "cuda-sim/ptx_ir.h"
#include "cuda-sim/ptx_sim.h"

	std::FILE * debug = std::fopen("/home/scratch/hwang/debug.txt", "w");
	//Change this to the location you need.

static void print_reg(FILE *fp, std::string name, ptx_reg_t value,
		const symbol *sym) {
	fprintf(fp, "register name:%8s   ,", name.c_str());
	if (sym == NULL) {
		fprintf(fp, "<unknown type> 0x%llx\n", (unsigned long long) value.u64);
		return;
	}
	const type_info *t = sym->type();
	if (t == NULL) {
		fprintf(fp, "<unknown type> 0x%llx\n", (unsigned long long) value.u64);
		return;
	}
	type_info_key ti = t->get_key();

	switch (ti.scalar_type()) {
	case 298:
		fprintf(fp, "register type:.s8  ,value:%d\n", value.s8);
		break;
	case 299:
		fprintf(fp, "register type:.s16 ,value:%d\n", value.s16);
		break;
	case 300:
		fprintf(fp, "register type:.s32  ,value:%d\n", value.s32);
		break;
	case 301:
		fprintf(fp, "register type:.s64  ,value:%Ld\n", value.s64);
		break;
	case 302:
		fprintf(fp, "register type:.u8   ,value:%u [0x%02x]\n", value.u8, (unsigned) value.u8);
		break;
	case 303:
		fprintf(fp, "register type:.u16  ,value:%u [0x%04x]\n", value.u16, (unsigned) value.u16);
		break;
	case 304:
		fprintf(fp, "register type:.u32  ,value:%u [0x%08x]\n", value.u32, (unsigned) value.u32);
		break;
	case 305:
		fprintf(fp, "register type:.u64  ,value:%llu [0x%llx]\n", value.u64, value.u64);
		break;
	case 306:
		fprintf(fp, "register type:.f16  ,value:%f [0x%04x]\n", value.f16, (unsigned) value.u16);
		break;
	case 307:
		fprintf(fp, "register type:.f32  ,value:%.15lf [0x%08x]\n", value.f32, value.u32);
		break;
	case 308:
		fprintf(fp, "register type:.f64  ,value:%.15le [0x%016llx]\n", value.f64, value.u64);
		break;
	case 310:
		fprintf(fp, "register type:.b8   ,value:0x%02x\n", (unsigned) value.u8);
		break;
	case 311:
		fprintf(fp, "register type:.b16  ,value:0x%04x\n", (unsigned) value.u16);
		break;
	case 312:
		fprintf(fp, "register type:.b32  ,value:0x%08x\n", (unsigned) value.u32);
		break;
	case 313:
		fprintf(fp, "register type:.b64  ,value:0x%llx\n", (unsigned long long) value.u64);
		break;
	case 316:
		fprintf(fp, "register type:.pred  ,value:%u\n", (unsigned) value.pred);
		break;
	default:
		fprintf(fp, "non-scalar type\n");
		break;
	}
	fflush(fp);
}

void print_RF(int thread_id, bool single_thread, const symbol *reg,
		const ptx_thread_info *thread, const ptx_reg_t &value,
		unsigned long long time_stamp) {

	if (thread->get_hw_tid() == thread_id || !single_thread) {
		std::fprintf(debug,
				"bank_id:%d,arch_reg_num:%d,reg_num:%d,name:%s,size:%d,warp_id:%d,thread_id:%d,cycle:%llu\n",
				(reg->arch_reg_num() + thread->get_hw_wid()) % 16,
				reg->arch_reg_num(), reg->reg_num(), reg->name().c_str(),
				reg->get_size_in_bytes(), thread->get_hw_wid(),
				thread->get_hw_tid(), time_stamp);

		print_reg(debug, reg->name(), value, reg);
	}
}
#endif
