/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma GCC push_options
#pragma GCC optimize ("-O2")

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>

#include "AmigaGprof.h"

#define __libnix__ 1
#include "stabs.h"

#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>

void _monstartup(void);
void _moncleanup(void);
__saveallregs void mcount(void);
int profil(char *buf, size_t bufsiz,
                  size_t offset, unsigned int scale);

struct profile_data {
  unsigned short *data;
  size_t count;
  size_t offset;
  volatile size_t last_pc;
};


extern const char const _stext;
extern const char const _etext;

#if 0
asm ("
	.globl	mcount
mcount:	movel	a1,-(sp)	| Save A1 as it might be used to store the
	bsr	__mcount	| address of a structure
	movel	(sp)+,a1
	rts
");
#endif

void moncontrol(int mode);

static unsigned int	*store_last_pc;
static unsigned int	dummy;
    /*
     *	froms is actually a bunch of unsigned shorts indexing tos
     */
static int		profiling = 3;
static int profile_started;
static int profile_saved;
static unsigned short	*froms;
static struct tostruct	*tos = 0;
static long		tolimit = 0;
static char		*s_lowpc = 0;
static char		*s_highpc = 0;
static unsigned long	s_textsize = 0;

static int	ssiz;
static char	*sbuf;
static int	s_scale;
    /* see profil(2) where this is describe (incorrectly) */
#define		SCALE_1_TO_1	0x10000L

#define	MSG "No space for profiling buffer(s)\n"

static inline void *
alloc(int s)
{
  void *res = malloc (s);
  if (res)
    bzero (res, s);
  return res;
}

void _monstartup(void)
{
    int			monsize;
    char		*buffer;
    register int	o;

    char *lowpc;
    char *highpc;

    /* Both the libnix INIT list and the launcher may call us. Never replace
     * buffers underneath an already installed interrupt sampler. */
    if (profile_started)
        return;
    profile_started = 1;

	/*
	 *	round lowpc and highpc to multiples of the density we're using
	 *	so the rest of the scaling (here and in gprof) stays in ints.
	 */
    lowpc = (char *)
	    ROUNDDOWN((unsigned)&_stext, HISTFRACTION*sizeof(HISTCOUNTER));
    s_lowpc = lowpc;
    highpc = (char *)
	    ROUNDUP((unsigned)&_etext, HISTFRACTION*sizeof(HISTCOUNTER));
    s_highpc = highpc;
    s_textsize = highpc - lowpc;
    monsize = (s_textsize / HISTFRACTION)*sizeof(short) + sizeof(struct phdr);
    buffer = alloc( monsize );
    if ( buffer == 0 ) {
	write( 2 , MSG , sizeof(MSG) );
	return;
    }
    froms = (unsigned short *) alloc ( s_textsize / HASHFRACTION );
    if ( froms == 0 ) {
	write( 2 , MSG , sizeof(MSG) );
	froms = 0;
	return;
    }
    tolimit = s_textsize * ARCDENSITY / 100;
    if ( tolimit < MINARCS ) {
	tolimit = MINARCS;
    } else if ( tolimit > 65534 ) {
	tolimit = 65534;
    }
    tos = (struct tostruct *) alloc( tolimit * sizeof( struct tostruct ) );
    if ( tos == 0 ) {
	write( 2 , MSG , sizeof(MSG) );
	froms = 0;
	tos = 0;
	return;
    }
    tos[0].link = 0;
    sbuf = buffer;
    ssiz = monsize;
    ( (struct phdr *) buffer ) -> lpc = (void *)(lowpc - (&_stext - 0)); // was - 4 - why?
    ( (struct phdr *) buffer ) -> hpc = (void *)(highpc - (&_stext - 0));// was - 4 - why?
    ( (struct phdr *) buffer ) -> ncnt = ssiz;
    monsize -= sizeof(struct phdr);
    if ( monsize <= 0 )
	return;
    o = highpc - lowpc;
    if( monsize < o )
    {
	int quot = o / monsize;

	if (quot >= 0x10000)
		s_scale = 1;
	else if (quot >= 0x100)
		s_scale = 0x10000 / quot;
	else if (o >= 0x800000)
		s_scale = 0x1000000 / (o / (monsize >> 8));
	else
		s_scale = 0x1000000 / ((o << 8) / monsize);
    }
    else
	s_scale = SCALE_1_TO_1;
    moncontrol(1);
}

void _moncleanup(void)
{
    FILE *		f;
    int			fromindex;
    int			endfrom;
    char		*frompc;
    int			toindex;
    struct rawarc	rawarc;

    /* Autotimedemo/F10 save before graphics teardown; normal exit may follow. */
    if (!profile_started || profile_saved)
        return;
    moncontrol(0);
    if (!sbuf || !froms || !tos)
        return;
    f = fopen( "PROGDIR:gmon.out" , "wb");
    if ( !f) {
	perror( "mcount: gmon.out" );
	return;
    }
#ifdef DEBUG_VERSION
	fprintf( stderr , "[mcleanup] sbuf 0x%x ssiz %d\n" , sbuf , ssiz );
#endif
    fwrite(sbuf , ssiz, 1 , f);
    endfrom = s_textsize / (HASHFRACTION * sizeof(*froms));
    for ( fromindex = 0 ; fromindex < endfrom ; fromindex++ ) {
	if ( froms[fromindex] == 0 ) {
	    continue;
	}
	frompc = (void *)(s_lowpc + (fromindex * HASHFRACTION * sizeof(*froms)) - (&_stext - 0));
	for (toindex=froms[fromindex]; toindex!=0; toindex=tos[toindex].link) {
#ifdef DEBUG_VERSION
		fprintf( stderr ,
			"[mcleanup] frompc 0x%x selfpc 0x%x count %d\n" ,
			frompc , tos[toindex].selfpc , tos[toindex].count );
#endif
	    rawarc.raw_frompc = (unsigned long) frompc;
	    rawarc.raw_selfpc = (unsigned long) tos[toindex].selfpc;
	    rawarc.raw_count = tos[toindex].count;
	    fwrite(&rawarc , sizeof rawarc, 1, f);
	}
    }
    {
        int failed = ferror(f);
        if (fclose(f) != 0)
            failed = 1;
        if (!failed)
            profile_saved = 1;
    }
}

__saveallregs void mcount(void)
{
	register char			*selfpc;
	register unsigned short		*frompcindex;
	register struct tostruct	*top;
	register struct tostruct	*prevtop;
	register long			toindex;

	/*
	 *	find the return address for mcount,
	 *	and the return address for mcount's caller.
	 */

	/* selfpc = pc pushed by mcount call.
	   This identifies the function that was just entered.  */
	selfpc = (char *) __builtin_return_address (0);
	/* frompcindex = pc in preceding frame.
	   This identifies the caller of the function just entered.  */
	frompcindex = (void *) __builtin_return_address (1);
	/* Time sampling must continue even if the call-arc table overflows and
	 * disables further call-graph accounting. */
	if (store_last_pc)
		*store_last_pc = (unsigned)selfpc;
	/*
	 *	check that we are profiling
	 *	and that we aren't recursively invoked.
	 */
	if (profiling) {
		goto out;
	}
	profiling++;
	selfpc = selfpc - (int)(&_stext - 0);
	/*
	 *	check that frompcindex is a reasonable pc value.
	 *	for example:	signal catchers get called from the stack,
	 *			not from text space.  too bad.
	 */
#ifdef DEBUG_VERSION
		fprintf (stderr, "from $%x, self $%x (low = $%x)\n",
			 frompcindex, selfpc, s_lowpc);
#endif
	frompcindex = (unsigned short *)((long)frompcindex - (long)s_lowpc);
	if ((unsigned long)frompcindex > s_textsize) {

		goto done;
	}
	frompcindex =
	    &froms[((long)frompcindex) / (HASHFRACTION * sizeof(*froms))];
	toindex = *frompcindex;
#ifdef DEBUG_VERSION
		fprintf (stderr, "frompcindex $%x, froms $%x, toindex = $%x\n",
			 frompcindex, froms, toindex);
#endif
	if (toindex == 0) {
		/*
		 *	first time traversing this arc
		 */
		toindex = ++tos[0].link;
		if (toindex >= tolimit) {
			goto overflow;
		}
		*frompcindex = toindex;
		top = &tos[toindex];
		top->selfpc = selfpc;
		top->count = 1;
		top->link = 0;
		goto done;
	}
	top = &tos[toindex];
	if (top->selfpc == selfpc) {
		/*
		 *	arc at front of chain; usual case.
		 */
		top->count++;
		goto done;
	}
	/*
	 *	have to go looking down chain for it.
	 *	top points to what we are looking at,
	 *	prevtop points to previous top.
	 *	we know it is not at the head of the chain.
	 */
	for (; /* goto done */; ) {
		if (top->link == 0) {
			/*
			 *	top is end of the chain and none of the chain
			 *	had top->selfpc == selfpc.
			 *	so we allocate a new tostruct
			 *	and link it to the head of the chain.
			 */
			toindex = ++tos[0].link;
			if (toindex >= tolimit) {
				goto overflow;
			}
			top = &tos[toindex];
			top->selfpc = selfpc;
			top->count = 1;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}
		/*
		 *	otherwise, check the next arc on the chain.
		 */
		prevtop = top;
		top = &tos[top->link];
		if (top->selfpc == selfpc) {
			/*
			 *	there it is.
			 *	increment its count
			 *	move it to the head of the chain.
			 */
			top->count++;
			toindex = prevtop->link;
			prevtop->link = top->link;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}

	}
done:
	profiling--;
	/* and fall through */
out:
	return;		/* normal return restores saved registers */

overflow:
	profiling++; /* halt further profiling */
#   define	TOLIMIT	"mcount: tos overflow\n"
	write(2, TOLIMIT, sizeof(TOLIMIT));
	goto out;
}



/*
 * Control profiling
 *	profiling is what mcount checks to see if
 *	all the data structures are ready.
 */
void moncontrol(int mode)
{
    if (mode) {
	/* start */
	store_last_pc = (unsigned *)profil(sbuf + sizeof(struct phdr),
			       ssiz - sizeof(struct phdr), (int)s_lowpc, s_scale);
        if (store_last_pc == NULL)
            store_last_pc = &dummy;
	profiling = 0;
    } else {
	/* stop */
	profil((char *)0, 0, 0, 0);
	profiling = 3;
    }
}

ADD2INIT(_monstartup,-4);
ADD2EXIT(_moncleanup,-4);

struct profile_data vbdata;
static volatile int sampler_running;
static volatile unsigned long sampler_samples;
static volatile unsigned long sampler_foreign;
/* Exec's original Level 3 handler; the assembly stub jumps here when done.
 * Not static: referenced by name from the asm block. */
unsigned long gprof_old_lvl3 = 0;
static unsigned long *lvl3_vector;
static unsigned short *histogram_shadow;
static unsigned short *histogram_output;
static size_t histogram_bytes;

/* Every sample outside our text is logged with its PC, so the host script can
 * attribute it to minigl.library, the Warp3D driver, ROM or idle.  Flags: bit 0
 * = interrupted in supervisor mode, bit 1 = ThisTask was our own task. */
struct foreign_sample {
  unsigned long pc;
  unsigned short flags;
  unsigned short pad;
};
#define FOREIGN_LOG_MAX 65536
static struct foreign_sample *foreign_log;
static volatile unsigned long foreign_log_count;
static struct Task *sampler_task;

/* Record one sample at the given interrupted program counter.  Called from
 * supervisor context, so it must not touch anything but our own buffers.
 * Not static: the assembly entry below calls it by symbol name. */
void RecordSamplePC(unsigned long pc, unsigned long sr) {
  if (!sampler_running || !vbdata.data)
    return;
  if (pc >= vbdata.offset) {
    const size_t index = (pc - vbdata.offset) >> 1;
    if (index < vbdata.count) {
      ++vbdata.data[index];
      ++sampler_samples;
      return;
    }
  }
  /* PC outside our text segment: Kickstart, a library, or an idle loop.  These
   * are counted but not placed, so the in-text percentages stay honest. */
  ++sampler_foreign;
  if (foreign_log && foreign_log_count < FOREIGN_LOG_MAX) {
    struct foreign_sample *s = &foreign_log[foreign_log_count++];
    s->pc = pc;
    s->flags = ((sr & 0x2000) ? 1 : 0) | (SysBase->ThisTask == sampler_task ? 2 : 0);
  }
}

/* Restart the measurement window, e.g. once the timedemo flyby begins, so that
 * package loading does not dominate the profile.  Call counts from -pg builds
 * are not reset; the histogram and foreign log are. */
void AmigaGprofReset(void) {
  if (!vbdata.data)
    return;
  Disable();
  memset(vbdata.data, 0, vbdata.count * sizeof(*vbdata.data));
  sampler_samples = 0;
  sampler_foreign = 0;
  foreign_log_count = 0;
  Enable();
}

/* Approximate each loaded library's code range from its jump table targets.
 * Internal (non-exported) functions can lie outside [min,max]; the host
 * script assigns a PC to the nearest library below it within a slack.
 * Runs under Forbid, so it only formats into a static buffer (no file I/O). */
static char library_text[16384];
static size_t library_text_len;
static char library_vectors[8192];
static size_t library_vectors_len;

static void WriteLibraryRanges(struct List *list, const char *kind) {
  struct Node *node;
  for (node = list->lh_Head; node->ln_Succ; node = node->ln_Succ) {
    struct Library *lib = (struct Library *)node;
    unsigned long lo = 0xffffffffUL, hi = 0;
    const unsigned short count = lib->lib_NegSize / 6;
    unsigned short i;
    int n;
    for (i = 1; i <= count; ++i) {
      const unsigned char *entry = (const unsigned char *)lib - 6 * i;
      unsigned long target;
      if (*(const unsigned short *)entry != 0x4ef9) /* JMP abs.l */
        continue;
      target = *(const unsigned long *)(entry + 2);
      /* Fingerprints of actual loaded code anchor offline HUNK symbols.
       * No allocation or file access while traversing the library lists. */
      if (target && i <= 12 && node->ln_Name &&
          (!strcmp(node->ln_Name, "minigl.library") ||
           !strcmp(node->ln_Name, "Warp3D.library")) &&
          library_vectors_len + 160 < sizeof(library_vectors)) {
        unsigned int j;
        library_vectors_len += snprintf(library_vectors + library_vectors_len,
            sizeof(library_vectors) - library_vectors_len,
            "%s %u %08lx ", node->ln_Name, i, target);
        for (j = 0; j < 32; ++j)
          library_vectors_len += snprintf(library_vectors + library_vectors_len,
              sizeof(library_vectors) - library_vectors_len, "%02x",
              (unsigned int)((const unsigned char *)target)[j]);
        library_vectors[library_vectors_len++] = '\n';
      }
      if (target < lo) lo = target;
      if (target > hi) hi = target;
    }
    if (!hi || library_text_len >= sizeof(library_text) - 1)
      continue;
    n = snprintf(library_text + library_text_len,
                 sizeof(library_text) - library_text_len,
                 "%s %08lx %08lx %08lx %u.%u %s\n", kind, (unsigned long)lib,
                 lo, hi, lib->lib_Version, lib->lib_Revision,
                 node->ln_Name ? node->ln_Name : "?");
    if (n > 0)
      library_text_len += (size_t)n;
    if (library_text_len >= sizeof(library_text))
      library_text_len = sizeof(library_text) - 1;
  }
}

static void WriteForeignLog(void) {
  FILE *out = fopen("PROGDIR:gprof-foreign.bin", "wb");
  if (out) {
    if (foreign_log)
      fwrite(foreign_log, sizeof(*foreign_log), foreign_log_count, out);
    fclose(out);
  }
  library_text_len = 0;
  library_vectors_len = 0;
  Forbid();
  WriteLibraryRanges(&SysBase->LibList, "library");
  WriteLibraryRanges(&SysBase->DeviceList, "device");
  WriteLibraryRanges(&SysBase->ResourceList, "resource");
  Permit();
  out = fopen("PROGDIR:gprof-vectors.txt", "w");
  if (out) {
    fwrite(library_vectors, 1, library_vectors_len, out);
    fclose(out);
  }
  out = fopen("PROGDIR:gprof-libs.txt", "w");
  if (out) {
    fprintf(out, "text %08lx %08lx %08lx 0.0 Unreal\n",
            (unsigned long)&_stext, (unsigned long)&_stext, (unsigned long)&_etext);
    fwrite(library_text, 1, library_text_len, out);
    fclose(out);
  }
}

/* Level 3 (VBlank) autovector handler, spliced in ahead of exec's own.
 *
 * An AddIntServer() server cannot be used for sampling: exec's dispatcher
 * takes the exception itself and then calls the server chain as ordinary
 * subroutines from ROM, so the "interrupted PC" a server can see is just a
 * return address inside exec (observed: 0x0030xxxx).  Hooking the CPU vector
 * puts us ahead of that dispatcher, where the frame the CPU pushed is still
 * untouched at the top of the supervisor stack.
 *
 * On entry, straight from the CPU, the supervisor stack holds:
 *     0(sp)  = status register of the interrupted context (word)
 *     2(sp)  = PC of the interrupted context (longword)
 *     6(sp)  = format/vector word
 *
 * This must stay in assembly: a C function's prologue would move sp by an
 * amount the source cannot account for.  Nothing here touches sp before the
 * PC is read.  The frame is only read, never modified, and control then falls
 * through to exec's original handler so the system keeps working. */
asm(
"	.text\n"
"	.align	2\n"
"	.globl	_AmigaGprofLvl3Entry\n"
"_AmigaGprofLvl3Entry:\n"
"	movem.l	%d0-%d1/%a0-%a1,-(%sp)\n"	/* scratch regs; sp now +16 */
"	moveq	#0,%d0\n"
"	move.w	16(%sp),%d0\n"			/* interrupted SR from frame */
"	move.l	%d0,-(%sp)\n"			/* arg 2; frame now at +20 */
"	move.l	20+2(%sp),%d0\n"		/* interrupted PC from frame */
"	move.l	%d0,-(%sp)\n"			/* arg 1 */
"	jsr	_RecordSamplePC\n"
"	addq.l	#8,%sp\n"
"	movem.l	(%sp)+,%d0-%d1/%a0-%a1\n"
"	move.l	_gprof_old_lvl3,-(%sp)\n"	/* chain to exec's handler */
"	rts\n"
);
extern void AmigaGprofLvl3Entry(void);

/* Read the 68010+ vector base register.  Runs via Supervisor(). */
asm(
"	.text\n"
"	.align	2\n"
"	.globl	_AmigaGprofGetVBR\n"
"_AmigaGprofGetVBR:\n"
"	movec	%vbr,%d0\n"
"	rte\n"
);
extern unsigned long AmigaGprofGetVBR(void);

int profil(char *buf, size_t bufsiz,
                  size_t offset, unsigned int scale) {
  if (buf) {
      // install interrupt if not running
      if (vbdata.data)
	return -1;

      /* The interrupt server writes into this buffer at VBlank rate, so it is
       * allocated from public memory and never moved while sampling. */
      histogram_shadow = (unsigned short *)alloc(bufsiz);
      if (!histogram_shadow)
        return 0;
      histogram_output = (unsigned short *)buf;
      histogram_bytes = bufsiz;
      vbdata.data = histogram_shadow;
      vbdata.count = bufsiz>>1;
      vbdata.offset = offset;
      vbdata.last_pc = offset;

      sampler_samples = 0;
      sampler_foreign = 0;
      /* Public memory: written from the interrupt, never moved. Optional. */
      foreign_log = (struct foreign_sample *)AllocMem(
          FOREIGN_LOG_MAX * sizeof(*foreign_log), MEMF_PUBLIC | MEMF_CLEAR);
      foreign_log_count = 0;
      sampler_task = FindTask(NULL);
      /* Arm before AddIntServer: the first VBlank can fire immediately. */
      sampler_running = 1;

      /* Splice into the Level 3 autovector (offset 0x6C = vector 27) ahead of
       * exec's dispatcher.  Disable() keeps a VBlank from firing while the
       * vector is half-written. */
      {
        unsigned long vbr = (unsigned long)Supervisor(
            (ULONG (*)())AmigaGprofGetVBR);
        lvl3_vector = (unsigned long *)(vbr + 0x6C);
        Disable();
        gprof_old_lvl3 = *lvl3_vector;
        *lvl3_vector = (unsigned long)AmigaGprofLvl3Entry;
        Enable();
      }

      /* The sampler now reads the real interrupted PC out of the exception
       * frame, so mcount no longer needs to publish anything.  Returning NULL
       * makes moncontrol point store_last_pc at its dummy sink. */
      return 0;
  }

  if (!vbdata.data)
    return -1;

  sampler_running = 0;
  /* Restore exec's vector before touching the buffer the handler writes into.
   * Leaving our handler installed past program exit would take the machine
   * down, so this must happen on every path out. */
  if (lvl3_vector) {
    Disable();
    *lvl3_vector = gprof_old_lvl3;
    Enable();
    lvl3_vector = 0;
  }
  WriteForeignLog();
  if (foreign_log) {
    FreeMem(foreign_log, FOREIGN_LOG_MAX * sizeof(*foreign_log));
    foreign_log = 0;
  }
  if (histogram_shadow && histogram_output && histogram_bytes)
    memcpy(histogram_output, histogram_shadow, histogram_bytes);
  if (histogram_shadow && histogram_bytes) {
    FILE *raw = fopen("PROGDIR:gprof-histogram.bin", "w");
    if (raw) {
      fwrite(histogram_shadow, histogram_bytes, 1, raw);
      fclose(raw);
    }
  }
  {
    FILE *status = fopen("PROGDIR:gprof-sampler.log", "w");
    if (status) {
      /* in_text vs foreign tells you at a glance whether the profile is
       * meaningful: all-foreign means offset/scale are wrong, zero samples
       * means the server never ran. */
      const unsigned long span = (unsigned long)vbdata.count * 2;
      fprintf(status,
              "samples=%lu foreign=%lu logged=%lu offset=%08lx count=%lu range=%08lx..%08lx\n",
              sampler_samples, sampler_foreign, foreign_log_count,
              (unsigned long)vbdata.offset, (unsigned long)vbdata.count,
              (unsigned long)vbdata.offset,
              (unsigned long)vbdata.offset + span);
      /* oldlvl3 is the quickest check that the vector hook actually engaged:
       * it must be a ROM address, not zero. */
      fprintf(status, "stext=%08lx etext=%08lx oldlvl3=%08lx\n",
              (unsigned long)&_stext, (unsigned long)&_etext,
              gprof_old_lvl3);
      fclose(status);
    }
  }
  vbdata.count = 0;
  vbdata.data = 0;
  free(histogram_shadow);
  histogram_shadow = 0;
  histogram_output = 0;
  histogram_bytes = 0;
  return 0;
}

#pragma GCC pop_options
