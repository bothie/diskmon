/*
 * Copyright 2007 Bodo Thiesen
 *
 * Shamelessly stolen from the linux kernel sources (version 2.6.19)
 */

/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6mmx.c
 *
 * MMX implementation of RAID-6 syndrome functions
 */

#include "raid6.h"
// #include "raid6-linux.h"
#include "raid6x86.h"

#include <assert.h>
#include <string.h>

/* Shared with raid6sse1.c */
const struct raid6_mmx_constants {
	u64 x1d;
} raid6_mmx_constants = {
	0x1d1d1d1d1d1d1d1dULL,
};

#if 0
static int raid6_have_mmx(void)
{
	return 1;
	/* User space test code */
//	u32 features = cpuid_features();
//	return ( (features & (1<<23)) == (1<<23) );
}
#endif

/*
 * Unrolled-by-2 MMX implementation
 */
static void raid6_mmx2_gen_syndromes(unsigned ndisks,size_t bytes,u8 * * dptr) {
	u8 * p;
	u8 * q;
	int z;
	int z0;
	raid6_mmx_save_t sa;
	
	z0=ndisks-3;		/* Highest data disk */
	p =dptr[z0+1];		/* XOR parity */
	q =dptr[z0+2];		/* RS syndrome */
	
	assert(!(bytes&15));
	
	raid6_before_mmx(&sa);
	
	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm5,%mm5");	/* Zero temp */
	asm volatile("pxor %mm7,%mm7"); /* Zero temp */
	
	for (size_t d = 0 ; d < bytes ; d += 16 ) {
		asm volatile("movq %0,%%mm2" : : "m" (dptr[z0][d+0]));   /* P[0] */
		asm volatile("movq %0,%%mm3" : : "m" (dptr[z0][d+8])); /* P[1] */
		asm volatile("movq %mm2,%mm4"); /* Q[0] */
		asm volatile("movq %mm3,%mm6"); /* Q[1] */
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			asm volatile("pcmpgtb %mm4,%mm5");
			asm volatile("pcmpgtb %mm6,%mm7");
			asm volatile("paddb %mm4,%mm4");
			asm volatile("paddb %mm6,%mm6");
			asm volatile("pand %mm0,%mm5");
			asm volatile("pand %mm0,%mm7");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("movq %0,%%mm5" : : "m" (dptr[z][d+0]));
			asm volatile("movq %0,%%mm7" : : "m" (dptr[z][d+8]));
			asm volatile("pxor %mm5,%mm2"); /* P[0] */
			asm volatile("pxor %mm7,%mm3"); /* P[1] */
			asm volatile("pxor %mm5,%mm4"); /* Q[0] */
			asm volatile("pxor %mm7,%mm6"); /* Q[1] */
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm7,%mm7");
		}
		asm volatile("movq %%mm2,%0" : "=m" (p[d+0]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+8]));
		asm volatile("movq %%mm4,%0" : "=m" (q[d+0]));
		asm volatile("movq %%mm6,%0" : "=m" (q[d+8]));
	}
	
	raid6_after_mmx(&sa);
}

/*
 * Unrolled-by-2 MMX implementation
 *
 * For convenience to the caller, this function expects the P parity to be 
 * present in the dptr array but ignores any data of P. So for 6 disks, 
 * the expected call is: raid6_mmx2_gen_syndrome_Q(6,BLK_SIZE,dptr -> {D0,D1,D2,D3,P,Q});
 */
static void raid6_mmx2_gen_syndrome_Q(unsigned disks,size_t bytes,u8 * * dptr) {
//	u8 * p;
	u8 * q;
	int z,z0;
	raid6_mmx_save_t sa;
	
	z0=disks-3;		/* Highest data disk */
//	p =dptr[z0+1];		/* XOR parity -> unused */
	q =dptr[z0+2];		/* RS syndrome */
	
	assert(!(bytes&15));
	
	raid6_before_mmx(&sa);
	
	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	
	for (size_t d = 0 ; d < bytes ; d += 16 ) {
		asm volatile("movq %0,%%mm4" : : "m" (dptr[z0][d+0])); /* Q[0] */
		asm volatile("movq %0,%%mm6" : : "m" (dptr[z0][d+8])); /* Q[1] */
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			asm volatile("pxor %mm5,%mm5");	/* Zero temp */
			asm volatile("pxor %mm7,%mm7"); /* Zero temp */
			asm volatile("pcmpgtb %mm4,%mm5");
			asm volatile("pcmpgtb %mm6,%mm7");
			asm volatile("paddb %mm4,%mm4");
			asm volatile("paddb %mm6,%mm6");
			asm volatile("pand %mm0,%mm5");
			asm volatile("pand %mm0,%mm7");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("movq %0,%%mm5" : : "m" (dptr[z][d+0]));
			asm volatile("movq %0,%%mm7" : : "m" (dptr[z][d+8]));
			asm volatile("pxor %mm5,%mm4"); /* Q[0] */
			asm volatile("pxor %mm7,%mm6"); /* Q[1] */
		}
		asm volatile("movq %%mm4,%0" : "=m" (q[d+0]));
		asm volatile("movq %%mm6,%0" : "=m" (q[d+8]));
	}
	
	raid6_after_mmx(&sa);
}

/*
 * Unrolled-by-4 MMX implementation
 */
static void raid5_mmx2_gen_syndrome(unsigned disks,size_t bytes,u8 * * dptr) {
	u8 * p;
//	u8 * q;
	int z,z0;
	raid6_mmx_save_t sa;
	
	z0=disks-2;		/* Highest data disk */
	p =dptr[z0+1];		/* XOR parity */
	
	assert(!(bytes&31));
	
	raid6_before_mmx(&sa);
	
	for (size_t d=0; d<bytes; d+=32) {
		asm volatile("movq %0,%%mm0" : : "m" (dptr[z0][d+ 0])); /* P[0] */
		asm volatile("movq %0,%%mm1" : : "m" (dptr[z0][d+ 8])); /* P[1] */
		asm volatile("movq %0,%%mm2" : : "m" (dptr[z0][d+16])); /* P[2] */
		asm volatile("movq %0,%%mm3" : : "m" (dptr[z0][d+24])); /* P[3] */
		for (z=z0-1; z>=0; --z) {
			asm volatile("movq %0,%%mm4" : : "m" (dptr[z][d+ 0]));
			asm volatile("movq %0,%%mm5" : : "m" (dptr[z][d+ 8]));
			asm volatile("movq %0,%%mm6" : : "m" (dptr[z][d+16]));
			asm volatile("movq %0,%%mm7" : : "m" (dptr[z][d+24]));
			asm volatile("pxor %mm4,%mm0"); /* P[0] */
			asm volatile("pxor %mm5,%mm1"); /* P[1] */
			asm volatile("pxor %mm6,%mm2"); /* Q[0] */
			asm volatile("pxor %mm7,%mm3"); /* Q[1] */
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[d+ 0]));
		asm volatile("movq %%mm1,%0" : "=m" (p[d+ 8]));
		asm volatile("movq %%mm2,%0" : "=m" (p[d+16]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+24]));
	}
	
	raid6_after_mmx(&sa);
}

/* Recover failure of one data block plus the P block */
void raid6_recover_DP(unsigned ndisks,size_t bytes,unsigned failed,u8 * * ptrs) {
	u8 * p;
	u8 * q;
	u8 * d;
	
	const u8 * qmul; /* Q multiplier table */
	
	p=ptrs[ndisks-2];
	q=ptrs[ndisks-1];
	
	/*
	 * Compute syndrome with zero for the missing data page
	 * Use the dead data page as temporary storage for delta q
	 */
	u8 * tmp=malloc(bytes);
	memset(tmp,0,bytes);
	ptrs[ndisks-1]=d=ptrs[failed];
	ptrs[failed]=tmp;
	
	raid6_mmx2_gen_syndromes(ndisks,bytes,ptrs);
	
	/* Restore pointer table */
	ptrs[failed]=ptrs[ndisks-1];
	ptrs[ndisks-1]=q;
	free(tmp);
	
	/* Now, pick the proper data tables */
	qmul=raid6_gfmul[raid6_gfinv[raid6_gfexp[failed]]];
	
	/* Now do it... */
	while (bytes--) {
		u8 t=qmul[*(q++)^*d];
		*p++^=(*(d++)=t);
	}
}

/* Recover two failed data blocks. */
void raid6_recover_D2(unsigned ndisks,size_t bytes,unsigned failed1,unsigned failed2,u8 * * ptrs) {
	u8 * p=ptrs[ndisks-2];
	u8 * q=ptrs[ndisks-1];
	u8 * dp;
	u8 * dq;
	u8 px, qx, db;
	const u8 *pbmul; /* P multiplier table for B data */
	const u8 *qmul; /* Q multiplier table (for both) */
	
	/*
	 * Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for
	 * delta p and delta q
	 */
	u8 * tmp=malloc(bytes);
	memset(tmp,0,bytes);
	
	ptrs[ndisks-2]=dp=ptrs[failed1];
	ptrs[ndisks-1]=dq=ptrs[failed2];
	ptrs[failed1]=tmp;
	ptrs[failed2]=tmp;
	
	raid6_mmx2_gen_syndromes(ndisks,bytes,ptrs);
	
	/* Restore pointer table */
	ptrs[failed1] =ptrs[ndisks-2];
	ptrs[failed2] =ptrs[ndisks-1];
	ptrs[ndisks-2]=p;
	ptrs[ndisks-1]=q;
	
	free(tmp);
	
	/* Now, pick the proper data tables */
	pbmul=raid6_gfmul[raid6_gfexi[failed2-failed1]];
	qmul =raid6_gfmul[raid6_gfinv[raid6_gfexp[failed1]^raid6_gfexp[failed2]]];
	
	/* Now do it... */
	while (bytes--) {
		px   =*p^*dp;
		qx   =qmul[*q^*dq];
		db=pbmul[px]^qx;
		*dq++=db;    /* Reconstructed B */
		*dp++=db^px; /* Reconstructed A */
		p++;
		q++;
	}
}

/*
 * Blocks 0..ndisks-1 are data blocks, ndisks (p) and ndisks+1 (q) are the 
 * parities. nfailed is the number of failed disks, failmap is a pointer to 
 * the table containing the numbers of the failed disk numbers. The variables 
 * bytes and ptrs are as in gen_syndromes.
 */
static void raid6_recover(unsigned ndisks,unsigned nfailed,unsigned * failmap,size_t bytes,u8 * * ptrs) {
	if (nfailed>2 || ndisks<nfailed || !nfailed) return;
	
	if (nfailed==1) {
		/* One D was lost - recover using P (Q will be ignored) */
		if (failmap[0]<ndisks-2) {
			// eprintf("D%i was lost\n",failmap[0]);
			u8 * p=ptrs[ndisks-2];
			u8 * f=ptrs[failmap[0]];
			ptrs[failmap[0]]=p;
			ptrs[ndisks-2]=f;
			raid5_mmx2_gen_syndrome(ndisks-1,bytes,ptrs);
			ptrs[failmap[0]]=f;
			ptrs[ndisks-2]=p;
			return;
		}
		/* P was lost - just recalculate */
		if (failmap[0]==ndisks-2) {
			// eprintf("P was lost\n");
			raid5_mmx2_gen_syndrome(ndisks-1,bytes,ptrs);
			return;
		}
		/* Q was lost - just recalculate */
		// eprintf("Q was lost\n");
		raid6_mmx2_gen_syndrome_Q(ndisks,bytes,ptrs);
		return;
	} else {
		unsigned local_failmap[2];
		if (failmap[0]>failmap[1]) {
			local_failmap[0]=failmap[1];
			local_failmap[1]=failmap[0];
			failmap=local_failmap;
		}
		if (failmap[1]==ndisks-1) {
			/*
			 * Q is one of the failed disks. The rest of the 
			 * problem is the standard RAID5 problem already 
			 * handled properly in the one-disk-failed-case, so 
			 * just let that code take care of this case.
			 */
			raid6_recover(ndisks,nfailed-1,failmap,bytes,ptrs);
			/* Don't forget to recalculate Q */
			raid6_mmx2_gen_syndrome_Q(ndisks,bytes,ptrs);
			return;
		}
		if (failmap[1]==ndisks-2) {
			/* D+P recovery */
			raid6_recover_DP(ndisks,bytes,failmap[0],ptrs);
			return;
		}
		/* D+D recovery */
		raid6_recover_D2(ndisks,bytes,failmap[0],failmap[1],ptrs);
		return;
	}
}

const struct raid6_calls raid6_mmxx2 = {
	raid6_mmx2_gen_syndromes,
	raid6_recover
};
