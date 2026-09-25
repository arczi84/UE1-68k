/* Custom linker script for AmigaOS 68k UE1 build.
 *
 * Based on the m68k-amigaos-ld default script, with one change: the C++ global
 * constructor pointers (emitted by GCC into per-object `.list___CTOR_LIST__`
 * fragments) are gathered into one dense, contiguous array bracketed by the
 * symbols ___UE_CTOR_START / ___UE_CTOR_END.
 *
 * We need this because libnix's crt (-noixemul) never walks the C++
 * constructor list, so main() runs them itself (see AmigaRunStaticCtors in
 * SDLLaunch.cpp). The stock `CONSTRUCTORS` directive produces a fragmented
 * list (per-object headers, 0 separators, padding gaps) with no end symbol,
 * which can't be walked reliably. A dense [start,end) array can.
 *
 * The default script already collects `.list_*` (which includes
 * `.list___CTOR_LIST__`) into .text via SORT_BY_NAME(.list_*); we pull the
 * CTOR fragments out explicitly first and bracket them, so they form a clean
 * pointer array. The remaining `.list_*` catch-all still handles EH frames etc.
 */

OUTPUT_FORMAT("amiga")
OUTPUT_ARCH(m68k)
SEARCH_DIR("/opt/amiga/m68k-amigaos/lib");
SECTIONS
{
  PROVIDE(___machtype = 0x0);
  . = 0x0;
  .text :
  {
    __stext = .;
    *(.text)
    *(.text.main)
    *(.text*)
    *(_*)
    *(.rodata*)
    *(.data.rel.ro*)
    *(.gnu.linkonce.t.*)
    *(.gnu.linkonce.r.*)
    *(.gcc_except_table*)
    *(SORT_BY_NAME(.list___EH_FRAME*))

    /* Gather the C++ constructor fragments into one dense, contiguous pointer
     * array, bracketed for AmigaRunStaticCtors(). Each input .o contributes a
     * 4-byte .list___CTOR_LIST__ pointing at its _GLOBAL__sub_ init function.
     * This pattern must come BEFORE the .list_* catch-all so the ctor
     * fragments bind here (first match wins) instead of being scattered. */
    . = ALIGN(4);
    ___UE_CTOR_START = .;
    *(.list___CTOR_LIST__)
    ___UE_CTOR_END = .;

    *(SORT_BY_NAME(.list_*))
    *(.end_of_lists)
    ___datadata_relocs = .;
    __etext = .;
  }
  ___text_size = SIZEOF(.text);
  . = ALIGN(0x0);
  .data :
  {
    __sdata = .;
    *(.data)
    *(SORT_BY_NAME(.data.*))
    /* NOTE: no CONSTRUCTORS here on purpose — the ctor fragments are gathered
     * and bracketed in .text above. Keeping CONSTRUCTORS too would emit a
     * second, separate __CTOR_LIST__ and split the constructors. */
    *(SORT_BY_NAME(.dlist___EH_FRAME_OBJECT*))
    *(SORT_BY_NAME(.dlist_*))
    *(.end_of_dlists)
    *(.data.__EH_FRAME_OBJECT__*)
    *(.gnu.linkonce.d.*)
    __edata = .;
  }
  ___data_size = SIZEOF(.data);
  .bss :
  {
    __bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    __end = .;
  }
  ___bss_size = SIZEOF(.bss);
  .datachip :
  {
    *(.datachip)
  }
  .bsschip :
  {
    *(.bsschip)
  }
  .dwarf :
  {
    __debug_frame_start = .;
    *(.debug_frame)
    __debug_info_start = .;
    *(.debug_info)
    __debug_abbrev_start = .;
    *(.debug_abbrev)
    __debug_loclists_start = .;
    *(.debug_loclists)
    /* GCC 6.5 runtime archives also carry older DWARF location/range lists.
     * Leaving these as orphan output sections makes the current Amiga linker
     * emit standalone HUNK_DEBUG + relocations as loadable segments, which
     * LoadSeg cannot load. Collect them with the rest of the debug data. */
    __debug_loc_start = .;
    *(.debug_loc)
    __debug_ranges_start = .;
    *(.debug_ranges)
    __debug_aranges_start = .;
    *(.debug_aranges)
    __debug_rgnlists_start = .;
    *(.debug_rnglists)
    __debug_line_start = .;
    *(.debug_line)
    __debug_str_start = .;
    *(.debug_str)
    __debug_line_str_start = .;
    *(.debug_line_str)
    *(.debug_*)
  }
}
