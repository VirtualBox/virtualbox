/* $Id: PGM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor. (Mixing stuff here, not good?)
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/** @page pg_pgm PGM - The Page Manager and Monitor
 *
 * @sa  @ref grp_pgm
 *      @subpage pg_pgm_pool
 *      @subpage pg_pgm_phys
 *
 *
 * @section         sec_pgm_modes           Paging Modes
 *
 * There are three memory contexts: Host Context (HC), Guest Context (GC)
 * and intermediate context.  When talking about paging HC can also be referred
 * to as "host paging", and GC referred to as "shadow paging".
 *
 * We define three basic paging modes: 32-bit, PAE and AMD64. The host paging mode
 * is defined by the host operating system. The mode used in the shadow paging mode
 * depends on the host paging mode and what the mode the guest is currently in. The
 * following relation between the two is defined:
 *
 * @verbatim
     Host > 32-bit |  PAE   | AMD64  |
   Guest  |        |        |        |
   ==v================================
   32-bit   32-bit    PAE     PAE
   -------|--------|--------|--------|
   PAE       PAE      PAE     PAE
   -------|--------|--------|--------|
   AMD64    AMD64    AMD64    AMD64
   -------|--------|--------|--------| @endverbatim
 *
 * All configuration except those in the diagonal (upper left) are expected to
 * require special effort from the switcher (i.e. a bit slower).
 *
 *
 *
 *
 * @section         sec_pgm_shw             The Shadow Memory Context
 *
 *
 *  [..]
 *
 * Because of guest context mappings requires PDPT and PML4 entries to allow
 * writing on AMD64, the two upper levels will have fixed flags whatever the
 * guest is thinking of using there. So, when shadowing the PD level we will
 * calculate the effective flags of PD and all the higher levels. In legacy
 * PAE mode this only applies to the PWT and PCD bits (the rest are
 * ignored/reserved/MBZ). We will ignore those bits for the present.
 *
 *
 *
 * @section         sec_pgm_int             The Intermediate Memory Context
 *
 * The world switch goes thru an intermediate memory context which purpose it is
 * to provide different mappings of the switcher code. All guest mappings are also
 * present in this context.
 *
 * The switcher code is mapped at the same location as on the host, at an
 * identity mapped location (physical equals virtual address), and at the
 * hypervisor location. The identity mapped location is for when the world
 * switches that involves disabling paging.
 *
 * PGM maintain page tables for 32-bit, PAE and AMD64 paging modes. This
 * simplifies switching guest CPU mode and consistency at the cost of more
 * code to do the work. All memory use for those page tables is located below
 * 4GB (this includes page tables for guest context mappings).
 *
 * Note! The intermediate memory context is also used for 64-bit guest
 *       execution on 32-bit hosts.  Because we need to load 64-bit registers
 *       prior to switching to guest context, we need to be in 64-bit mode
 *       first.  So, HM has some 64-bit worker routines in VMMRC.rc that get
 *       invoked via the special world switcher code in LegacyToAMD64.asm.
 *
 *
 * @subsection      subsec_pgm_int_gc       Guest Context Mappings
 *
 * During assignment and relocation of a guest context mapping the intermediate
 * memory context is used to verify the new location.
 *
 * Guest context mappings are currently restricted to below 4GB, for reasons
 * of simplicity. This may change when we implement AMD64 support.
 *
 *
 *
 *
 * @section         sec_pgm_misc            Misc
 *
 *
 * @subsection      sec_pgm_misc_A20        The A20 Gate
 *
 * PGM implements the A20 gate masking when translating a virtual guest address
 * into a physical address for CPU access, i.e. PGMGstGetPage (and friends) and
 * the code reading the guest page table entries during shadowing.  The masking
 * is done consistenly for all CPU modes, paged ones included.  Large pages are
 * also masked correctly.  (On current CPUs, experiments indicates that AMD does
 * not apply A20M in paged modes and intel only does it for the 2nd MB of
 * memory.)
 *
 * The A20 gate implementation is per CPU core.  It can be configured on a per
 * core basis via the keyboard device and PC architecture device.  This is
 * probably not exactly how real CPUs do it, but SMP and A20 isn't a place where
 * guest OSes try pushing things anyway, so who cares.  (On current real systems
 * the A20M signal is probably only sent to the boot CPU and it affects all
 * thread and probably all cores in that package.)
 *
 * The keyboard device and the PC architecture device doesn't OR their A20
 * config bits together, rather they are currently implemented such that they
 * mirror the CPU state.  So, flipping the bit in either of them will change the
 * A20 state.  (On real hardware the bits of the two devices should probably be
 * ORed together to indicate enabled, i.e. both needs to be cleared to disable
 * A20 masking.)
 *
 * The A20 state will change immediately, transmeta fashion.  There is no delays
 * due to buses, wiring or other physical stuff.  (On real hardware there are
 * normally delays, the delays differs between the two devices and probably also
 * between chipsets and CPU generations. Note that it's said that transmeta CPUs
 * does the change immediately like us, they apparently intercept/handles the
 * port accesses in microcode. Neat.)
 *
 * @sa http://en.wikipedia.org/wiki/A20_line#The_80286_and_the_high_memory_area
 *
 *
 * @subsection      subsec_pgm_misc_diff    Differences Between Legacy PAE and Long Mode PAE
 *
 * The differences between legacy PAE and long mode PAE are:
 *      -# PDPE bits 1, 2, 5 and 6 are defined differently. In leagcy mode they are
 *         all marked down as must-be-zero, while in long mode 1, 2 and 5 have the
 *         usual meanings while 6 is ignored (AMD). This means that upon switching to
 *         legacy PAE mode we'll have to clear these bits and when going to long mode
 *         they must be set. This applies to both intermediate and shadow contexts,
 *         however we don't need to do it for the intermediate one since we're
 *         executing with CR0.WP at that time.
 *      -# CR3 allows a 32-byte aligned address in legacy mode, while in long mode
 *         a page aligned one is required.
 *
 *
 * @section         sec_pgm_handlers        Access Handlers
 *
 * Placeholder.
 *
 *
 * @subsection      sec_pgm_handlers_phys   Physical Access Handlers
 *
 * Placeholder.
 *
 *
 * @subsection      sec_pgm_handlers_virt   Virtual Access Handlers (obsolete)
 *
 * We currently implement three types of virtual access handlers:  ALL, WRITE
 * and HYPERVISOR (WRITE). See PGMVIRTHANDLERKIND for some more details.
 *
 * The HYPERVISOR access handlers is kept in a separate tree since it doesn't apply
 * to physical pages (PGMTREES::HyperVirtHandlers) and only needs to be consulted in
 * a special \#PF case. The ALL and WRITE are in the PGMTREES::VirtHandlers tree, the
 * rest of this section is going to be about these handlers.
 *
 * We'll go thru the life cycle of a handler and try make sense of it all, don't know
 * how successful this is gonna be...
 *
 * 1. A handler is registered thru the PGMR3HandlerVirtualRegister and
 * PGMHandlerVirtualRegisterEx APIs. We check for conflicting virtual handlers
 * and create a new node that is inserted into the AVL tree (range key). Then
 * a full PGM resync is flagged (clear pool, sync cr3, update virtual bit of PGMPAGE).
 *
 * 2. The following PGMSyncCR3/SyncCR3 operation will first make invoke HandlerVirtualUpdate.
 *
 * 2a. HandlerVirtualUpdate will will lookup all the pages covered by virtual handlers
 * via the current guest CR3 and update the physical page -> virtual handler
 * translation. Needless to say, this doesn't exactly scale very well. If any changes
 * are detected, it will flag a virtual bit update just like we did on registration.
 * PGMPHYS pages with changes will have their virtual handler state reset to NONE.
 *
 * 2b. The virtual bit update process will iterate all the pages covered by all the
 * virtual handlers and update the PGMPAGE virtual handler state to the max of all
 * virtual handlers on that page.
 *
 * 2c. Back in SyncCR3 we will now flush the entire shadow page cache to make sure
 * we don't miss any alias mappings of the monitored pages.
 *
 * 2d. SyncCR3 will then proceed with syncing the CR3 table.
 *
 * 3. \#PF(np,read) on a page in the range. This will cause it to be synced
 * read-only and resumed if it's a WRITE handler. If it's an ALL handler we
 * will call the handlers like in the next step. If the physical mapping has
 * changed we will - some time in the future - perform a handler callback
 * (optional) and update the physical -> virtual handler cache.
 *
 * 4. \#PF(,write) on a page in the range. This will cause the handler to
 * be invoked.
 *
 * 5. The guest invalidates the page and changes the physical backing or
 * unmaps it. This should cause the invalidation callback to be invoked
 * (it might not yet be 100% perfect). Exactly what happens next... is
 * this where we mess up and end up out of sync for a while?
 *
 * 6. The handler is deregistered by the client via PGMHandlerVirtualDeregister.
 * We will then set all PGMPAGEs in the physical -> virtual handler cache for
 * this handler to NONE and trigger a full PGM resync (basically the same
 * as int step 1). Which means 2 is executed again.
 *
 *
 * @subsubsection   sub_sec_pgm_handler_virt_todo   TODOs
 *
 * There is a bunch of things that needs to be done to make the virtual handlers
 * work 100% correctly and work more efficiently.
 *
 * The first bit hasn't been implemented yet because it's going to slow the
 * whole mess down even more, and besides it seems to be working reliably for
 * our current uses. OTOH, some of the optimizations might end up more or less
 * implementing the missing bits, so we'll see.
 *
 * On the optimization side, the first thing to do is to try avoid unnecessary
 * cache flushing. Then try team up with the shadowing code to track changes
 * in mappings by means of access to them (shadow in), updates to shadows pages,
 * invlpg, and shadow PT discarding (perhaps).
 *
 * Some idea that have popped up for optimization for current and new features:
 *    - bitmap indicating where there are virtual handlers installed.
 *      (4KB => 2**20 pages, page 2**12 => covers 32-bit address space 1:1!)
 *    - Further optimize this by min/max (needs min/max avl getters).
 *    - Shadow page table entry bit (if any left)?
 *
 */


/** @page pg_pgm_phys   PGM Physical Guest Memory Management
 *
 *
 * Objectives:
 *      - Guest RAM over-commitment using memory ballooning,
 *        zero pages and general page sharing.
 *      - Moving or mirroring a VM onto a different physical machine.
 *
 *
 * @section sec_pgmPhys_Definitions       Definitions
 *
 * Allocation chunk - A RTR0MemObjAllocPhysNC or RTR0MemObjAllocPhys allocate
 * memory object and the tracking machinery associated with it.
 *
 *
 *
 *
 * @section sec_pgmPhys_AllocPage         Allocating a page.
 *
 * Initially we map *all* guest memory to the (per VM) zero page, which
 * means that none of the read functions will cause pages to be allocated.
 *
 * Exception, access bit in page tables that have been shared. This must
 * be handled, but we must also make sure PGMGst*Modify doesn't make
 * unnecessary modifications.
 *
 * Allocation points:
 *      - PGMPhysSimpleWriteGCPhys and PGMPhysWrite.
 *      - Replacing a zero page mapping at \#PF.
 *      - Replacing a shared page mapping at \#PF.
 *      - ROM registration (currently MMR3RomRegister).
 *      - VM restore (pgmR3Load).
 *
 * For the first three it would make sense to keep a few pages handy
 * until we've reached the max memory commitment for the VM.
 *
 * For the ROM registration, we know exactly how many pages we need
 * and will request these from ring-0. For restore, we will save
 * the number of non-zero pages in the saved state and allocate
 * them up front. This would allow the ring-0 component to refuse
 * the request if the isn't sufficient memory available for VM use.
 *
 * Btw. for both ROM and restore allocations we won't be requiring
 * zeroed pages as they are going to be filled instantly.
 *
 *
 * @section sec_pgmPhys_FreePage          Freeing a page
 *
 * There are a few points where a page can be freed:
 *      - After being replaced by the zero page.
 *      - After being replaced by a shared page.
 *      - After being ballooned by the guest additions.
 *      - At reset.
 *      - At restore.
 *
 * When freeing one or more pages they will be returned to the ring-0
 * component and replaced by the zero page.
 *
 * The reasoning for clearing out all the pages on reset is that it will
 * return us to the exact same state as on power on, and may thereby help
 * us reduce the memory load on the system. Further it might have a
 * (temporary) positive influence on memory fragmentation (@see subsec_pgmPhys_Fragmentation).
 *
 * On restore, as mention under the allocation topic, pages should be
 * freed / allocated depending on how many is actually required by the
 * new VM state. The simplest approach is to do like on reset, and free
 * all non-ROM pages and then allocate what we need.
 *
 * A measure to prevent some fragmentation, would be to let each allocation
 * chunk have some affinity towards the VM having allocated the most pages
 * from it. Also, try make sure to allocate from allocation chunks that
 * are almost full. Admittedly, both these measures might work counter to
 * our intentions and its probably not worth putting a lot of effort,
 * cpu time or memory into this.
 *
 *
 * @section sec_pgmPhys_SharePage         Sharing a page
 *
 * The basic idea is that there there will be a idle priority kernel
 * thread walking the non-shared VM pages hashing them and looking for
 * pages with the same checksum. If such pages are found, it will compare
 * them byte-by-byte to see if they actually are identical. If found to be
 * identical it will allocate a shared page, copy the content, check that
 * the page didn't change while doing this, and finally request both the
 * VMs to use the shared page instead. If the page is all zeros (special
 * checksum and byte-by-byte check) it will request the VM that owns it
 * to replace it with the zero page.
 *
 * To make this efficient, we will have to make sure not to try share a page
 * that will change its contents soon. This part requires the most work.
 * A simple idea would be to request the VM to write monitor the page for
 * a while to make sure it isn't modified any time soon. Also, it may
 * make sense to skip pages that are being write monitored since this
 * information is readily available to the thread if it works on the
 * per-VM guest memory structures (presently called PGMRAMRANGE).
 *
 *
 * @section sec_pgmPhys_Fragmentation     Fragmentation Concerns and Counter Measures
 *
 * The pages are organized in allocation chunks in ring-0, this is a necessity
 * if we wish to have an OS agnostic approach to this whole thing. (On Linux we
 * could easily work on a page-by-page basis if we liked. Whether this is possible
 * or efficient on NT I don't quite know.) Fragmentation within these chunks may
 * become a problem as part of the idea here is that we wish to return memory to
 * the host system.
 *
 * For instance, starting two VMs at the same time, they will both allocate the
 * guest memory on-demand and if permitted their page allocations will be
 * intermixed. Shut down one of the two VMs and it will be difficult to return
 * any memory to the host system because the page allocation for the two VMs are
 * mixed up in the same allocation chunks.
 *
 * To further complicate matters, when pages are freed because they have been
 * ballooned or become shared/zero the whole idea is that the page is supposed
 * to be reused by another VM or returned to the host system. This will cause
 * allocation chunks to contain pages belonging to different VMs and prevent
 * returning memory to the host when one of those VM shuts down.
 *
 * The only way to really deal with this problem is to move pages. This can
 * either be done at VM shutdown and or by the idle priority worker thread
 * that will be responsible for finding sharable/zero pages. The mechanisms
 * involved for coercing a VM to move a page (or to do it for it) will be
 * the same as when telling it to share/zero a page.
 *
 *
 * @section sec_pgmPhys_Tracking      Tracking Structures And Their Cost
 *
 * There's a difficult balance between keeping the per-page tracking structures
 * (global and guest page) easy to use and keeping them from eating too much
 * memory. We have limited virtual memory resources available when operating in
 * 32-bit kernel space (on 64-bit there'll it's quite a different story). The
 * tracking structures will be attempted designed such that we can deal with up
 * to 32GB of memory on a 32-bit system and essentially unlimited on 64-bit ones.
 *
 *
 * @subsection subsec_pgmPhys_Tracking_Kernel     Kernel Space
 *
 * @see pg_GMM
 *
 * @subsection subsec_pgmPhys_Tracking_PerVM      Per-VM
 *
 * Fixed info is the physical address of the page (HCPhys) and the page id
 * (described above). Theoretically we'll need 48(-12) bits for the HCPhys part.
 * Today we've restricting ourselves to 40(-12) bits because this is the current
 * restrictions of all AMD64 implementations (I think Barcelona will up this
 * to 48(-12) bits, not that it really matters) and I needed the bits for
 * tracking mappings of a page. 48-12 = 36. That leaves 28 bits, which means a
 * decent range for the page id: 2^(28+12) = 1024TB.
 *
 * In additions to these, we'll have to keep maintaining the page flags as we
 * currently do. Although it wouldn't harm to optimize these quite a bit, like
 * for instance the ROM shouldn't depend on having a write handler installed
 * in order for it to become read-only. A RO/RW bit should be considered so
 * that the page syncing code doesn't have to mess about checking multiple
 * flag combinations (ROM || RW handler || write monitored) in order to
 * figure out how to setup a shadow PTE. But this of course, is second
 * priority at present. Current this requires 12 bits, but could probably
 * be optimized to ~8.
 *
 * Then there's the 24 bits used to track which shadow page tables are
 * currently mapping a page for the purpose of speeding up physical
 * access handlers, and thereby the page pool cache. More bit for this
 * purpose wouldn't hurt IIRC.
 *
 * Then there is a new bit in which we need to record what kind of page
 * this is, shared, zero, normal or write-monitored-normal. This'll
 * require 2 bits. One bit might be needed for indicating whether a
 * write monitored page has been written to. And yet another one or
 * two for tracking migration status. 3-4 bits total then.
 *
 * Whatever is left will can be used to record the sharabilitiy of a
 * page. The page checksum will not be stored in the per-VM table as
 * the idle thread will not be permitted to do modifications to it.
 * It will instead have to keep its own working set of potentially
 * shareable pages and their check sums and stuff.
 *
 * For the present we'll keep the current packing of the
 * PGMRAMRANGE::aHCPhys to keep the changes simple, only of course,
 * we'll have to change it to a struct with a total of 128-bits at
 * our disposal.
 *
 * The initial layout will be like this:
 * @verbatim
    RTHCPHYS HCPhys;            The current stuff.
        63:40                   Current shadow PT tracking stuff.
        39:12                   The physical page frame number.
        11:0                    The current flags.
    uint32_t u28PageId : 28;    The page id.
    uint32_t u2State : 2;       The page state { zero, shared, normal, write monitored }.
    uint32_t fWrittenTo : 1;    Whether a write monitored page was written to.
    uint32_t u1Reserved : 1;    Reserved for later.
    uint32_t u32Reserved;       Reserved for later, mostly sharing stats.
 @endverbatim
 *
 * The final layout will be something like this:
 * @verbatim
    RTHCPHYS HCPhys;            The current stuff.
        63:48                   High page id (12+).
        47:12                   The physical page frame number.
        11:0                    Low page id.
    uint32_t fReadOnly : 1;     Whether it's readonly page (rom or monitored in some way).
    uint32_t u3Type : 3;        The page type {RESERVED, MMIO, MMIO2, ROM, shadowed ROM, RAM}.
    uint32_t u2PhysMon : 2;     Physical access handler type {none, read, write, all}.
    uint32_t u2VirtMon : 2;     Virtual access handler type {none, read, write, all}..
    uint32_t u2State : 2;       The page state { zero, shared, normal, write monitored }.
    uint32_t fWrittenTo : 1;    Whether a write monitored page was written to.
    uint32_t u20Reserved : 20;  Reserved for later, mostly sharing stats.
    uint32_t u32Tracking;       The shadow PT tracking stuff, roughly.
 @endverbatim
 *
 * Cost wise, this means we'll double the cost for guest memory. There isn't anyway
 * around that I'm afraid. It means that the cost of dealing out 32GB of memory
 * to one or more VMs is: (32GB >> GUEST_PAGE_SHIFT) * 16 bytes, or 128MBs. Or
 * another example, the VM heap cost when assigning 1GB to a VM will be: 4MB.
 *
 * A couple of cost examples for the total cost per-VM + kernel.
 * 32-bit Windows and 32-bit linux:
 *      1GB guest ram, 256K pages:  4MB +  2MB(+) =   6MB
 *      4GB guest ram, 1M pages:   16MB +  8MB(+) =  24MB
 *     32GB guest ram, 8M pages:  128MB + 64MB(+) = 192MB
 * 64-bit Windows and 64-bit linux:
 *      1GB guest ram, 256K pages:  4MB +  3MB(+) =   7MB
 *      4GB guest ram, 1M pages:   16MB + 12MB(+) =  28MB
 *     32GB guest ram, 8M pages:  128MB + 96MB(+) = 224MB
 *
 * UPDATE - 2007-09-27:
 * Will need a ballooned flag/state too because we cannot
 * trust the guest 100% and reporting the same page as ballooned more
 * than once will put the GMM off balance.
 *
 *
 * @section sec_pgmPhys_Serializing       Serializing Access
 *
 * Initially, we'll try a simple scheme:
 *
 *      - The per-VM RAM tracking structures (PGMRAMRANGE) is only modified
 *        by the EMT thread of that VM while in the pgm critsect.
 *      - Other threads in the VM process that needs to make reliable use of
 *        the per-VM RAM tracking structures will enter the critsect.
 *      - No process external thread or kernel thread will ever try enter
 *        the pgm critical section, as that just won't work.
 *      - The idle thread (and similar threads) doesn't not need 100% reliable
 *        data when performing it tasks as the EMT thread will be the one to
 *        do the actual changes later anyway. So, as long as it only accesses
 *        the main ram range, it can do so by somehow preventing the VM from
 *        being destroyed while it works on it...
 *
 *      - The over-commitment management, including the allocating/freeing
 *        chunks, is serialized by a ring-0 mutex lock (a fast one since the
 *        more mundane mutex implementation is broken on Linux).
 *      - A separate mutex is protecting the set of allocation chunks so
 *        that pages can be shared or/and freed up while some other VM is
 *        allocating more chunks. This mutex can be take from under the other
 *        one, but not the other way around.
 *
 *
 * @section sec_pgmPhys_Request           VM Request interface
 *
 * When in ring-0 it will become necessary to send requests to a VM so it can
 * for instance move a page while defragmenting during VM destroy. The idle
 * thread will make use of this interface to request VMs to setup shared
 * pages and to perform write monitoring of pages.
 *
 * I would propose an interface similar to the current VMReq interface, similar
 * in that it doesn't require locking and that the one sending the request may
 * wait for completion if it wishes to. This shouldn't be very difficult to
 * realize.
 *
 * The requests themselves are also pretty simple. They are basically:
 *      -# Check that some precondition is still true.
 *      -# Do the update.
 *      -# Update all shadow page tables involved with the page.
 *
 * The 3rd step is identical to what we're already doing when updating a
 * physical handler, see pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs.
 *
 *
 *
 * @section sec_pgmPhys_MappingCaches   Mapping Caches
 *
 * In order to be able to map in and out memory and to be able to support
 * guest with more RAM than we've got virtual address space, we'll employing
 * a mapping cache.  Normally ring-0 and ring-3 can share the same cache,
 * however on 32-bit darwin the ring-0 code is running in a different memory
 * context and therefore needs a separate cache.  In raw-mode context we also
 * need a separate cache.  The 32-bit darwin mapping cache and the one for
 * raw-mode context share a lot of code, see PGMRZDYNMAP.
 *
 *
 * @subsection subsec_pgmPhys_MappingCaches_R3  Ring-3
 *
 * We've considered implementing the ring-3 mapping cache page based but found
 * that this was bother some when one had to take into account TLBs+SMP and
 * portability (missing the necessary APIs on several platforms). There were
 * also some performance concerns with this approach which hadn't quite been
 * worked out.
 *
 * Instead, we'll be mapping allocation chunks into the VM process. This simplifies
 * matters greatly quite a bit since we don't need to invent any new ring-0 stuff,
 * only some minor RTR0MEMOBJ mapping stuff. The main concern here is that mapping
 * compared to the previous idea is that mapping or unmapping a 1MB chunk is more
 * costly than a single page, although how much more costly is uncertain. We'll
 * try address this by using a very big cache, preferably bigger than the actual
 * VM RAM size if possible. The current VM RAM sizes should give some idea for
 * 32-bit boxes, while on 64-bit we can probably get away with employing an
 * unlimited cache.
 *
 * The cache have to parts, as already indicated, the ring-3 side and the
 * ring-0 side.
 *
 * The ring-0 will be tied to the page allocator since it will operate on the
 * memory objects it contains. It will therefore require the first ring-0 mutex
 * discussed in @ref sec_pgmPhys_Serializing.  We some double house keeping wrt
 * to who has mapped what I think, since both VMMR0.r0 and RTR0MemObj will keep
 * track of mapping relations
 *
 * The ring-3 part will be protected by the pgm critsect. For simplicity, we'll
 * require anyone that desires to do changes to the mapping cache to do that
 * from within this critsect. Alternatively, we could employ a separate critsect
 * for serializing changes to the mapping cache as this would reduce potential
 * contention with other threads accessing mappings unrelated to the changes
 * that are in process. We can see about this later, contention will show
 * up in the statistics anyway, so it'll be simple to tell.
 *
 * The organization of the ring-3 part will be very much like how the allocation
 * chunks are organized in ring-0, that is in an AVL tree by chunk id. To avoid
 * having to walk the tree all the time, we'll have a couple of lookaside entries
 * like in we do for I/O ports and MMIO in IOM.
 *
 * The simplified flow of a PGMPhysRead/Write function:
 *      -# Enter the PGM critsect.
 *      -# Lookup GCPhys in the ram ranges and get the Page ID.
 *      -# Calc the Allocation Chunk ID from the Page ID.
 *      -# Check the lookaside entries and then the AVL tree for the Chunk ID.
 *         If not found in cache:
 *              -# Call ring-0 and request it to be mapped and supply
 *                 a chunk to be unmapped if the cache is maxed out already.
 *              -# Insert the new mapping into the AVL tree (id + R3 address).
 *      -# Update the relevant lookaside entry and return the mapping address.
 *      -# Do the read/write according to monitoring flags and everything.
 *      -# Leave the critsect.
 *
 *
 * @section sec_pgmPhys_Changes             Changes
 *
 * Breakdown of the changes involved?
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/iom.h>
#include <VBox/sup.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/hm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/uvm.h>
#include "PGMInline.h"

#include <VBox/dbg.h>
#include <VBox/param.h>
#include <VBox/err.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#ifdef RT_OS_LINUX
# include <iprt/linux/sysfs.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Argument package for pgmR3RElocatePhysHnadler, pgmR3RelocateVirtHandler and
 * pgmR3RelocateHyperVirtHandler.
 */
typedef struct PGMRELOCHANDLERARGS
{
    RTGCINTPTR  offDelta;
    PVM         pVM;
} PGMRELOCHANDLERARGS;
/** Pointer to a page access handlere relocation argument package. */
typedef PGMRELOCHANDLERARGS const *PCPGMRELOCHANDLERARGS;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int                pgmR3InitPaging(PVM pVM);
static int                pgmR3InitStats(PVM pVM);
static DECLCALLBACK(void) pgmR3PhysInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) pgmR3InfoMode(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) pgmR3InfoCr3(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#ifdef VBOX_STRICT
static FNVMATSTATE        pgmR3ResetNoMorePhysWritesFlag;
#endif

#ifdef VBOX_WITH_DEBUGGER
static FNDBGCCMD          pgmR3CmdError;
static FNDBGCCMD          pgmR3CmdSync;
static FNDBGCCMD          pgmR3CmdSyncAlways;
# ifdef VBOX_STRICT
static FNDBGCCMD          pgmR3CmdAssertCR3;
# endif
static FNDBGCCMD          pgmR3CmdPhysToFile;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_DEBUGGER
/** Argument descriptors for '.pgmerror' and '.pgmerroroff'. */
static const DBGCVARDESC g_aPgmErrorArgs[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "where",        "Error injection location." },
};

static const DBGCVARDESC g_aPgmPhysToFileArgs[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "file",         "The file name." },
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "nozero",       "If present, zero pages are skipped." },
};

# ifdef DEBUG_sandervl
static const DBGCVARDESC g_aPgmCountPhysWritesArgs[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  1,           1,          DBGCVAR_CAT_STRING,     0,                              "enabled",      "on/off." },
    {  1,           1,          DBGCVAR_CAT_NUMBER_NO_RANGE, 0,                         "interval",     "Interval in ms." },
};
# endif

/** Command descriptors. */
static const DBGCCMD    g_aCmds[] =
{
    /* pszCmd,  cArgsMin, cArgsMax, paArgDesc,                cArgDescs, fFlags, pfnHandler          pszSyntax,          ....pszDescription */
    { "pgmsync",       0, 0,        NULL,                     0,         0,      pgmR3CmdSync,       "",                     "Sync the CR3 page." },
    { "pgmerror",      0, 1,        &g_aPgmErrorArgs[0],      1,         0,      pgmR3CmdError,      "",                     "Enables inject runtime of errors into parts of PGM." },
    { "pgmerroroff",   0, 1,        &g_aPgmErrorArgs[0],      1,         0,      pgmR3CmdError,      "",                     "Disables inject runtime errors into parts of PGM." },
# ifdef VBOX_STRICT
    { "pgmassertcr3",  0, 0,        NULL,                     0,         0,      pgmR3CmdAssertCR3,  "",                     "Check the shadow CR3 mapping." },
#  ifdef VBOX_WITH_PAGE_SHARING
    { "pgmcheckduppages", 0, 0,     NULL,                     0,         0,      pgmR3CmdCheckDuplicatePages,  "",           "Check for duplicate pages in all running VMs." },
    { "pgmsharedmodules", 0, 0,     NULL,                     0,         0,      pgmR3CmdShowSharedModules,  "",             "Print shared modules info." },
#  endif
# endif
    { "pgmsyncalways", 0, 0,        NULL,                     0,         0,      pgmR3CmdSyncAlways, "",                     "Toggle permanent CR3 syncing." },
    { "pgmphystofile", 1, 2,        &g_aPgmPhysToFileArgs[0], 2,         0,      pgmR3CmdPhysToFile, "",                     "Save the physical memory to file." },
};
#endif

#ifdef VBOX_WITH_PGM_NEM_MODE

/**
 * Interface that NEM uses to switch PGM into simplified memory managment mode.
 *
 * This call occurs before PGMR3Init.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) PGMR3EnableNemMode(PVM pVM)
{
    AssertFatal(!PDMCritSectIsInitialized(&pVM->pgm.s.CritSectX));
    if (!pVM->pgm.s.fNemMode)
    {
        LogRel(("PGM: Enabling NEM mode\n"));
        pVM->pgm.s.fNemMode = true;
    }
}


/**
 * Checks whether the simplificed memory management mode for NEM is enabled.
 *
 * @returns true if enabled, false if not.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(bool)    PGMR3IsNemModeEnabled(PVM pVM)
{
    return pVM->pgm.s.fNemMode;
}

#endif /* VBOX_WITH_PGM_NEM_MODE */

/**
 * Initiates the paging of VM.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(int) PGMR3Init(PVM pVM)
{
    LogFlow(("PGMR3Init:\n"));
    PCFGMNODE pCfgPGM = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/PGM");
    int rc;

    /*
     * Assert alignment and sizes.
     */
    AssertCompile(sizeof(pVM->pgm.s) <= sizeof(pVM->pgm.padding));
    AssertCompile(sizeof(pVM->apCpusR3[0]->pgm.s) <= sizeof(pVM->apCpusR3[0]->pgm.padding));
    AssertCompileMemberAlignment(PGM, CritSectX, sizeof(uintptr_t));

    /*
     * If we're in driveless mode we have to use the simplified memory mode.
     */
    bool const fDriverless = SUPR3IsDriverless();
    if (fDriverless)
    {
#ifdef VBOX_WITH_PGM_NEM_MODE
        if (!pVM->pgm.s.fNemMode)
        {
            LogRel(("PGM: Enabling NEM mode (driverless)\n"));
            pVM->pgm.s.fNemMode = true;
        }
#else
        return VMR3SetError(pVM->pUVM, VERR_SUP_DRIVERLESS, RT_SRC_POS,
                            "Driverless requires that VBox is built with VBOX_WITH_PGM_NEM_MODE defined");
#endif
    }

    /*
     * Init the structure.
     */
    /*pVM->pgm.s.fRestoreRomPagesAtReset = false;*/

    for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.aHandyPages); i++)
    {
        pVM->pgm.s.aHandyPages[i].HCPhysGCPhys  = NIL_GMMPAGEDESC_PHYS;
        pVM->pgm.s.aHandyPages[i].fZeroed       = false;
        pVM->pgm.s.aHandyPages[i].idPage        = NIL_GMM_PAGEID;
        pVM->pgm.s.aHandyPages[i].idSharedPage  = NIL_GMM_PAGEID;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.aLargeHandyPage); i++)
    {
        pVM->pgm.s.aLargeHandyPage[i].HCPhysGCPhys  = NIL_GMMPAGEDESC_PHYS;
        pVM->pgm.s.aLargeHandyPage[i].fZeroed       = false;
        pVM->pgm.s.aLargeHandyPage[i].idPage        = NIL_GMM_PAGEID;
        pVM->pgm.s.aLargeHandyPage[i].idSharedPage  = NIL_GMM_PAGEID;
    }

    AssertReleaseReturn(pVM->pgm.s.cPhysHandlerTypes == 0, VERR_WRONG_ORDER);
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pgm.s.aPhysHandlerTypes); i++)
    {
        if (fDriverless)
            pVM->pgm.s.aPhysHandlerTypes[i].hType  = i | (RTRandU64() & ~(uint64_t)PGMPHYSHANDLERTYPE_IDX_MASK);
        pVM->pgm.s.aPhysHandlerTypes[i].enmKind    = PGMPHYSHANDLERKIND_INVALID;
        pVM->pgm.s.aPhysHandlerTypes[i].pfnHandler = pgmR3HandlerPhysicalHandlerInvalid;
    }

    /* Init the per-CPU part. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        PPGMCPU pPGM = &pVCpu->pgm.s;

        pPGM->enmShadowMode     = PGMMODE_INVALID;
        pPGM->enmGuestMode      = PGMMODE_INVALID;
        pPGM->enmGuestSlatMode  = PGMSLAT_INVALID;
        pPGM->idxGuestModeData  = UINT8_MAX;
        pPGM->idxShadowModeData = UINT8_MAX;
        pPGM->idxBothModeData   = UINT8_MAX;

        pPGM->GCPhysCR3         = NIL_RTGCPHYS;
        pPGM->GCPhysNstGstCR3   = NIL_RTGCPHYS;
        pPGM->GCPhysPaeCR3      = NIL_RTGCPHYS;

        pPGM->pGst32BitPdR3     = NULL;
        pPGM->pGstPaePdptR3     = NULL;
        pPGM->pGstAmd64Pml4R3   = NULL;
        pPGM->pGst32BitPdR0     = NIL_RTR0PTR;
        pPGM->pGstPaePdptR0     = NIL_RTR0PTR;
        pPGM->pGstAmd64Pml4R0   = NIL_RTR0PTR;
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        pPGM->pGstEptPml4R3     = NULL;
        pPGM->pGstEptPml4R0     = NIL_RTR0PTR;
        pPGM->uEptPtr           = 0;
#endif
        for (unsigned i = 0; i < RT_ELEMENTS(pVCpu->pgm.s.apGstPaePDsR3); i++)
        {
            pPGM->apGstPaePDsR3[i]             = NULL;
            pPGM->apGstPaePDsR0[i]             = NIL_RTR0PTR;
            pPGM->aGCPhysGstPaePDs[i]          = NIL_RTGCPHYS;
        }

        pPGM->fA20Enabled      = true;
        pPGM->GCPhysA20Mask    = ~((RTGCPHYS)!pPGM->fA20Enabled << 20);
    }

    pVM->pgm.s.enmHostMode      = SUPPAGINGMODE_INVALID;
    pVM->pgm.s.GCPhys4MBPSEMask = RT_BIT_64(32) - 1; /* default; checked later */

    rc = CFGMR3QueryBoolDef(CFGMR3GetRoot(pVM), "RamPreAlloc", &pVM->pgm.s.fRamPreAlloc,
#ifdef VBOX_WITH_PREALLOC_RAM_BY_DEFAULT
                            true
#else
                            false
#endif
                           );
    AssertLogRelRCReturn(rc, rc);

#if HC_ARCH_BITS == 32
# ifdef RT_OS_DARWIN
    rc = CFGMR3QueryU32Def(pCfgPGM, "MaxRing3Chunks", &pVM->pgm.s.ChunkR3Map.cMax, _1G / GMM_CHUNK_SIZE * 3);
# else
    rc = CFGMR3QueryU32Def(pCfgPGM, "MaxRing3Chunks", &pVM->pgm.s.ChunkR3Map.cMax, _1G / GMM_CHUNK_SIZE);
# endif
#else
    rc = CFGMR3QueryU32Def(pCfgPGM, "MaxRing3Chunks", &pVM->pgm.s.ChunkR3Map.cMax, UINT32_MAX);
#endif
    AssertLogRelRCReturn(rc, rc);
    for (uint32_t i = 0; i < RT_ELEMENTS(pVM->pgm.s.ChunkR3Map.Tlb.aEntries); i++)
        pVM->pgm.s.ChunkR3Map.Tlb.aEntries[i].idChunk = NIL_GMM_CHUNKID;

    /*
     * Get the configured RAM size - to estimate saved state size.
     */
    uint64_t    cbRam;
    rc = CFGMR3QueryU64(CFGMR3GetRoot(pVM), "RamSize", &cbRam);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        cbRam = 0;
    else if (RT_SUCCESS(rc))
    {
        if (cbRam < GUEST_PAGE_SIZE)
            cbRam = 0;
        cbRam = RT_ALIGN_64(cbRam, GUEST_PAGE_SIZE);
    }
    else
    {
        AssertMsgFailed(("Configuration error: Failed to query integer \"RamSize\", rc=%Rrc.\n", rc));
        return rc;
    }

    /*
     * Check for PCI pass-through and other configurables.
     */
    rc = CFGMR3QueryBoolDef(pCfgPGM, "PciPassThrough", &pVM->pgm.s.fPciPassthrough, false);
    AssertMsgRCReturn(rc, ("Configuration error: Failed to query integer \"PciPassThrough\", rc=%Rrc.\n", rc), rc);
    AssertLogRelReturn(!pVM->pgm.s.fPciPassthrough || pVM->pgm.s.fRamPreAlloc, VERR_INVALID_PARAMETER);

    rc = CFGMR3QueryBoolDef(CFGMR3GetRoot(pVM), "PageFusionAllowed", &pVM->pgm.s.fPageFusionAllowed, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/PGM/ZeroRamPagesOnReset, boolean, true}
     * Whether to clear RAM pages on (hard) reset. */
    rc = CFGMR3QueryBoolDef(pCfgPGM, "ZeroRamPagesOnReset", &pVM->pgm.s.fZeroRamPagesOnReset, true);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Register callbacks, string formatters and the saved state data unit.
     */
#ifdef VBOX_STRICT
    VMR3AtStateRegister(pVM->pUVM, pgmR3ResetNoMorePhysWritesFlag, NULL);
#endif
    PGMRegisterStringFormatTypes();

    rc = pgmR3InitSavedState(pVM, cbRam);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the PGM critical section and flush the phys TLBs
     */
    rc = PDMR3CritSectInit(pVM, &pVM->pgm.s.CritSectX, RT_SRC_POS, "PGM");
    AssertRCReturn(rc, rc);

    pgmR3PhysChunkInvalidateTLB(pVM, false /*fInRendezvous*/); /* includes pgmPhysInvalidatePageMapTLB call */

    /*
     * For the time being we sport a full set of handy pages in addition to the base
     * memory to simplify things.
     */
    rc = MMR3ReserveHandyPages(pVM, RT_ELEMENTS(pVM->pgm.s.aHandyPages)); /** @todo this should be changed to PGM_HANDY_PAGES_MIN but this needs proper testing... */
    AssertRCReturn(rc, rc);

    /*
     * Setup the zero page (HCPHysZeroPg is set by ring-0).
     */
    RT_ZERO(pVM->pgm.s.abZeroPg); /* paranoia */
    if (fDriverless)
        pVM->pgm.s.HCPhysZeroPg = _4G - GUEST_PAGE_SIZE * 2 /* fake to avoid PGM_PAGE_INIT_ZERO assertion */;
    AssertRelease(pVM->pgm.s.HCPhysZeroPg != NIL_RTHCPHYS);
    AssertRelease(pVM->pgm.s.HCPhysZeroPg != 0);
    Log(("HCPhysZeroPg=%RHp abZeroPg=%p\n", pVM->pgm.s.HCPhysZeroPg, pVM->pgm.s.abZeroPg));

    /*
     * Setup the invalid MMIO page (HCPhysMmioPg is set by ring-0).
     * (The invalid bits in HCPhysInvMmioPg are set later on init complete.)
     */
    ASMMemFill32(pVM->pgm.s.abMmioPg, sizeof(pVM->pgm.s.abMmioPg), 0xfeedface);
    if (fDriverless)
        pVM->pgm.s.HCPhysMmioPg = _4G - GUEST_PAGE_SIZE * 3 /* fake to avoid PGM_PAGE_INIT_ZERO assertion */;
    AssertRelease(pVM->pgm.s.HCPhysMmioPg != NIL_RTHCPHYS);
    AssertRelease(pVM->pgm.s.HCPhysMmioPg != 0);
    pVM->pgm.s.HCPhysInvMmioPg = pVM->pgm.s.HCPhysMmioPg;
    Log(("HCPhysInvMmioPg=%RHp abMmioPg=%p\n", pVM->pgm.s.HCPhysMmioPg, pVM->pgm.s.abMmioPg));

    /*
     * Initialize physical access handlers.
     */
    /** @cfgm{/PGM/MaxPhysicalAccessHandlers, uint32_t, 32, 65536, 6144}
     * Number of physical access handlers allowed (subject to rounding).  This is
     * managed as one time allocation during initializations.  The default is
     * lower for a driverless setup. */
    /** @todo can lower it for nested paging too, at least when there is no
     *        nested guest involved. */
    uint32_t cAccessHandlers = 0;
    rc = CFGMR3QueryU32Def(pCfgPGM, "MaxPhysicalAccessHandlers", &cAccessHandlers, !fDriverless ? 6144 : 640);
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelMsgStmt(cAccessHandlers >= 32, ("cAccessHandlers=%#x, min 32\n", cAccessHandlers), cAccessHandlers = 32);
    AssertLogRelMsgStmt(cAccessHandlers <= _64K, ("cAccessHandlers=%#x, max 65536\n", cAccessHandlers), cAccessHandlers = _64K);
    if (!fDriverless)
    {
        rc = VMMR3CallR0(pVM, VMMR0_DO_PGM_PHYS_HANDLER_INIT, cAccessHandlers, NULL);
        AssertRCReturn(rc, rc);
        AssertPtr(pVM->pgm.s.pPhysHandlerTree);
        AssertPtr(pVM->pgm.s.PhysHandlerAllocator.m_paNodes);
        AssertPtr(pVM->pgm.s.PhysHandlerAllocator.m_pbmAlloc);
    }
    else
    {
        uint32_t       cbTreeAndBitmap = 0;
        uint32_t const cbTotalAligned  = pgmHandlerPhysicalCalcTableSizes(&cAccessHandlers, &cbTreeAndBitmap);
        uint8_t       *pb = NULL;
        rc = SUPR3PageAlloc(cbTotalAligned >> HOST_PAGE_SHIFT, 0, (void **)&pb);
        AssertLogRelRCReturn(rc, rc);

        pVM->pgm.s.PhysHandlerAllocator.initSlabAllocator(cAccessHandlers, (PPGMPHYSHANDLER)&pb[cbTreeAndBitmap],
                                                          (uint64_t *)&pb[sizeof(PGMPHYSHANDLERTREE)]);
        pVM->pgm.s.pPhysHandlerTree = (PPGMPHYSHANDLERTREE)pb;
        pVM->pgm.s.pPhysHandlerTree->initWithAllocator(&pVM->pgm.s.PhysHandlerAllocator);
    }

    /*
     * Register the physical access handler protecting ROMs.
     */
    if (RT_SUCCESS(rc))
        /** @todo why isn't pgmPhysRomWriteHandler registered for ring-0?   */
        rc = PGMR3HandlerPhysicalTypeRegister(pVM, PGMPHYSHANDLERKIND_WRITE, 0 /*fFlags*/, pgmPhysRomWriteHandler,
                                              "ROM write protection", &pVM->pgm.s.hRomPhysHandlerType);

    /*
     * Register the physical access handler doing dirty MMIO2 tracing.
     */
    if (RT_SUCCESS(rc))
        rc = PGMR3HandlerPhysicalTypeRegister(pVM, PGMPHYSHANDLERKIND_WRITE, PGMPHYSHANDLER_F_KEEP_PGM_LOCK,
                                              pgmPhysMmio2WriteHandler, "MMIO2 dirty page tracing",
                                              &pVM->pgm.s.hMmio2DirtyPhysHandlerType);

    /*
     * Init the paging.
     */
    if (RT_SUCCESS(rc))
        rc = pgmR3InitPaging(pVM);

    /*
     * Init the page pool.
     */
    if (RT_SUCCESS(rc))
        rc = pgmR3PoolInit(pVM);

    if (RT_SUCCESS(rc))
    {
        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[i];
            rc = PGMHCChangeMode(pVM, pVCpu, PGMMODE_REAL, false /* fForce */);
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Info & statistics
         */
        DBGFR3InfoRegisterInternalEx(pVM, "mode",
                                     "Shows the current paging mode. "
                                     "Recognizes 'all', 'guest', 'shadow' and 'host' as arguments, defaulting to 'all' if nothing is given.",
                                     pgmR3InfoMode,
                                     DBGFINFO_FLAGS_ALL_EMTS);
        DBGFR3InfoRegisterInternal(pVM, "pgmcr3",
                                   "Dumps all the entries in the top level paging table. No arguments.",
                                   pgmR3InfoCr3);
        DBGFR3InfoRegisterInternal(pVM, "phys",
                                   "Dumps all the physical address ranges. Pass 'verbose' to get more details.",
                                   pgmR3PhysInfo);
        DBGFR3InfoRegisterInternal(pVM, "handlers",
                                   "Dumps physical, virtual and hyper virtual handlers. "
                                   "Pass 'phys', 'virt', 'hyper' as argument if only one kind is wanted."
                                   "Add 'nost' if the statistics are unwanted, use together with 'all' or explicit selection.",
                                   pgmR3InfoHandlers);

        pgmR3InitStats(pVM);

#ifdef VBOX_WITH_DEBUGGER
        /*
         * Debugger commands.
         */
        static bool s_fRegisteredCmds = false;
        if (!s_fRegisteredCmds)
        {
            int rc2 = DBGCRegisterCommands(&g_aCmds[0], RT_ELEMENTS(g_aCmds));
            if (RT_SUCCESS(rc2))
                s_fRegisteredCmds = true;
        }
#endif

#ifdef RT_OS_LINUX
        /*
         * Log the /proc/sys/vm/max_map_count value on linux as that is
         * frequently giving us grief when too low.
         */
        int64_t const cGuessNeeded = MMR3PhysGetRamSize(pVM) / _2M + 16384 /*guesstimate*/;
        int64_t       cMaxMapCount = 0;
        int rc2 = RTLinuxSysFsReadIntFile(10, &cMaxMapCount, "/proc/sys/vm/max_map_count");
        LogRel(("PGM: /proc/sys/vm/max_map_count = %RI64 (rc2=%Rrc); cGuessNeeded=%RI64\n", cMaxMapCount, rc2, cGuessNeeded));
        if (RT_SUCCESS(rc2) && cMaxMapCount < cGuessNeeded)
            LogRel(("PGM: WARNING!!\n"
                    "PGM: WARNING!! Please increase /proc/sys/vm/max_map_count to at least %RI64 (or reduce the amount of RAM assigned to the VM)!\n"
                    "PGM: WARNING!!\n", cMaxMapCount));

#endif

        return VINF_SUCCESS;
    }

    /* Almost no cleanup necessary, MM frees all memory. */
    PDMR3CritSectDelete(pVM, &pVM->pgm.s.CritSectX);

    return rc;
}


/**
 * Init paging.
 *
 * Since we need to check what mode the host is operating in before we can choose
 * the right paging functions for the host we have to delay this until R0 has
 * been initialized.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
static int pgmR3InitPaging(PVM pVM)
{
    /*
     * Force a recalculation of modes and switcher so everyone gets notified.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];

        pVCpu->pgm.s.enmShadowMode     = PGMMODE_INVALID;
        pVCpu->pgm.s.enmGuestMode      = PGMMODE_INVALID;
        pVCpu->pgm.s.enmGuestSlatMode  = PGMSLAT_INVALID;
        pVCpu->pgm.s.idxGuestModeData  = UINT8_MAX;
        pVCpu->pgm.s.idxShadowModeData = UINT8_MAX;
        pVCpu->pgm.s.idxBothModeData   = UINT8_MAX;
    }

    pVM->pgm.s.enmHostMode   = SUPPAGINGMODE_INVALID;

    /*
     * Initialize paging workers and mode from current host mode
     * and the guest running in real mode.
     */
    pVM->pgm.s.enmHostMode = SUPR3GetPagingMode();
    switch (pVM->pgm.s.enmHostMode)
    {
        case SUPPAGINGMODE_32_BIT:
        case SUPPAGINGMODE_32_BIT_GLOBAL:
        case SUPPAGINGMODE_PAE:
        case SUPPAGINGMODE_PAE_GLOBAL:
        case SUPPAGINGMODE_PAE_NX:
        case SUPPAGINGMODE_PAE_GLOBAL_NX:

        case SUPPAGINGMODE_AMD64:
        case SUPPAGINGMODE_AMD64_GLOBAL:
        case SUPPAGINGMODE_AMD64_NX:
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
            if (ARCH_BITS != 64)
            {
                AssertMsgFailed(("Host mode %d (64-bit) is not supported by non-64bit builds\n", pVM->pgm.s.enmHostMode));
                LogRel(("PGM: Host mode %d (64-bit) is not supported by non-64bit builds\n", pVM->pgm.s.enmHostMode));
                return VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE;
            }
            break;
#if !defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86)
        case SUPPAGINGMODE_INVALID:
            pVM->pgm.s.enmHostMode = SUPPAGINGMODE_AMD64_GLOBAL_NX;
            break;
#endif
        default:
            AssertMsgFailed(("Host mode %d is not supported\n", pVM->pgm.s.enmHostMode));
            return VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE;
    }

    LogFlow(("pgmR3InitPaging: returns successfully\n"));
#if HC_ARCH_BITS == 64 && 0
    LogRel(("PGM: HCPhysInterPD=%RHp HCPhysInterPaePDPT=%RHp HCPhysInterPaePML4=%RHp\n",
            pVM->pgm.s.HCPhysInterPD, pVM->pgm.s.HCPhysInterPaePDPT, pVM->pgm.s.HCPhysInterPaePML4));
    LogRel(("PGM: apInterPTs={%RHp,%RHp} apInterPaePTs={%RHp,%RHp} apInterPaePDs={%RHp,%RHp,%RHp,%RHp} pInterPaePDPT64=%RHp\n",
            MMPage2Phys(pVM, pVM->pgm.s.apInterPTs[0]),    MMPage2Phys(pVM, pVM->pgm.s.apInterPTs[1]),
            MMPage2Phys(pVM, pVM->pgm.s.apInterPaePTs[0]), MMPage2Phys(pVM, pVM->pgm.s.apInterPaePTs[1]),
            MMPage2Phys(pVM, pVM->pgm.s.apInterPaePDs[0]), MMPage2Phys(pVM, pVM->pgm.s.apInterPaePDs[1]), MMPage2Phys(pVM, pVM->pgm.s.apInterPaePDs[2]), MMPage2Phys(pVM, pVM->pgm.s.apInterPaePDs[3]),
            MMPage2Phys(pVM, pVM->pgm.s.pInterPaePDPT64)));
#endif

    /*
     * Log the host paging mode. It may come in handy.
     */
    const char *pszHostMode;
    switch (pVM->pgm.s.enmHostMode)
    {
        case SUPPAGINGMODE_32_BIT:              pszHostMode = "32-bit"; break;
        case SUPPAGINGMODE_32_BIT_GLOBAL:       pszHostMode = "32-bit+PGE"; break;
        case SUPPAGINGMODE_PAE:                 pszHostMode = "PAE"; break;
        case SUPPAGINGMODE_PAE_GLOBAL:          pszHostMode = "PAE+PGE"; break;
        case SUPPAGINGMODE_PAE_NX:              pszHostMode = "PAE+NXE"; break;
        case SUPPAGINGMODE_PAE_GLOBAL_NX:       pszHostMode = "PAE+PGE+NXE"; break;
        case SUPPAGINGMODE_AMD64:               pszHostMode = "AMD64"; break;
        case SUPPAGINGMODE_AMD64_GLOBAL:        pszHostMode = "AMD64+PGE"; break;
        case SUPPAGINGMODE_AMD64_NX:            pszHostMode = "AMD64+NX"; break;
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:     pszHostMode = "AMD64+PGE+NX"; break;
        default:                                pszHostMode = "???"; break;
    }
    LogRel(("PGM: Host paging mode: %s\n", pszHostMode));

    return VINF_SUCCESS;
}


/**
 * Init statistics
 * @returns VBox status code.
 */
static int pgmR3InitStats(PVM pVM)
{
    PPGM pPGM = &pVM->pgm.s;
    int  rc;

    /*
     * Release statistics.
     */
    /* Common - misc variables */
    STAM_REL_REG(pVM, &pPGM->cAllPages,                          STAMTYPE_U32,     "/PGM/Page/cAllPages",                STAMUNIT_COUNT,     "The total number of pages.");
    STAM_REL_REG(pVM, &pPGM->cPrivatePages,                      STAMTYPE_U32,     "/PGM/Page/cPrivatePages",            STAMUNIT_COUNT,     "The number of private pages.");
    STAM_REL_REG(pVM, &pPGM->cSharedPages,                       STAMTYPE_U32,     "/PGM/Page/cSharedPages",             STAMUNIT_COUNT,     "The number of shared pages.");
    STAM_REL_REG(pVM, &pPGM->cReusedSharedPages,                 STAMTYPE_U32,     "/PGM/Page/cReusedSharedPages",       STAMUNIT_COUNT,     "The number of reused shared pages.");
    STAM_REL_REG(pVM, &pPGM->cZeroPages,                         STAMTYPE_U32,     "/PGM/Page/cZeroPages",               STAMUNIT_COUNT,     "The number of zero backed pages.");
    STAM_REL_REG(pVM, &pPGM->cPureMmioPages,                     STAMTYPE_U32,     "/PGM/Page/cPureMmioPages",           STAMUNIT_COUNT,     "The number of pure MMIO pages.");
    STAM_REL_REG(pVM, &pPGM->cMonitoredPages,                    STAMTYPE_U32,     "/PGM/Page/cMonitoredPages",          STAMUNIT_COUNT,     "The number of write monitored pages.");
    STAM_REL_REG(pVM, &pPGM->cWrittenToPages,                    STAMTYPE_U32,     "/PGM/Page/cWrittenToPages",          STAMUNIT_COUNT,     "The number of previously write monitored pages that have been written to.");
    STAM_REL_REG(pVM, &pPGM->cWriteLockedPages,                  STAMTYPE_U32,     "/PGM/Page/cWriteLockedPages",        STAMUNIT_COUNT,     "The number of write(/read) locked pages.");
    STAM_REL_REG(pVM, &pPGM->cReadLockedPages,                   STAMTYPE_U32,     "/PGM/Page/cReadLockedPages",         STAMUNIT_COUNT,     "The number of read (only) locked pages.");
    STAM_REL_REG(pVM, &pPGM->cBalloonedPages,                    STAMTYPE_U32,     "/PGM/Page/cBalloonedPages",          STAMUNIT_COUNT,     "The number of ballooned pages.");
    STAM_REL_REG(pVM, &pPGM->cHandyPages,                        STAMTYPE_U32,     "/PGM/Page/cHandyPages",              STAMUNIT_COUNT,     "The number of handy pages (not included in cAllPages).");
    STAM_REL_REG(pVM, &pPGM->cLargePages,                        STAMTYPE_U32,     "/PGM/Page/cLargePages",              STAMUNIT_COUNT,     "The number of large pages allocated (includes disabled).");
    STAM_REL_REG(pVM, &pPGM->cLargePagesDisabled,                STAMTYPE_U32,     "/PGM/Page/cLargePagesDisabled",      STAMUNIT_COUNT,     "The number of disabled large pages.");
    STAM_REL_REG(pVM, &pPGM->ChunkR3Map.c,                       STAMTYPE_U32,     "/PGM/ChunkR3Map/c",                  STAMUNIT_COUNT,     "Number of mapped chunks.");
    STAM_REL_REG(pVM, &pPGM->ChunkR3Map.cMax,                    STAMTYPE_U32,     "/PGM/ChunkR3Map/cMax",               STAMUNIT_COUNT,     "Maximum number of mapped chunks.");
    STAM_REL_REG(pVM, &pPGM->cMappedChunks,                      STAMTYPE_U32,     "/PGM/ChunkR3Map/Mapped",             STAMUNIT_COUNT,     "Number of times we mapped a chunk.");
    STAM_REL_REG(pVM, &pPGM->cUnmappedChunks,                    STAMTYPE_U32,     "/PGM/ChunkR3Map/Unmapped",           STAMUNIT_COUNT,     "Number of times we unmapped a chunk.");

    STAM_REL_REG(pVM, &pPGM->StatLargePageReused,                STAMTYPE_COUNTER, "/PGM/LargePage/Reused",              STAMUNIT_OCCURENCES, "The number of times we've reused a large page.");
    STAM_REL_REG(pVM, &pPGM->StatLargePageRefused,               STAMTYPE_COUNTER, "/PGM/LargePage/Refused",             STAMUNIT_OCCURENCES, "The number of times we couldn't use a large page.");
    STAM_REL_REG(pVM, &pPGM->StatLargePageRecheck,               STAMTYPE_COUNTER, "/PGM/LargePage/Recheck",             STAMUNIT_OCCURENCES, "The number of times we've rechecked a disabled large page.");

    STAM_REL_REG(pVM, &pPGM->StatShModCheck,                     STAMTYPE_PROFILE, "/PGM/ShMod/Check",                   STAMUNIT_TICKS_PER_CALL, "Profiles the shared module checking.");
    STAM_REL_REG(pVM, &pPGM->StatMmio2QueryAndResetDirtyBitmap,  STAMTYPE_PROFILE, "/PGM/Mmio2QueryAndResetDirtyBitmap", STAMUNIT_TICKS_PER_CALL, "Profiles calls to PGMR3PhysMmio2QueryAndResetDirtyBitmap (sans locking).");

    /* Live save */
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.fActive,              STAMTYPE_U8,      "/PGM/LiveSave/fActive",              STAMUNIT_COUNT,     "Active or not.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.cIgnoredPages,        STAMTYPE_U32,     "/PGM/LiveSave/cIgnoredPages",        STAMUNIT_COUNT,     "The number of ignored pages in the RAM ranges (i.e. MMIO, MMIO2 and ROM).");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.cDirtyPagesLong,      STAMTYPE_U32,     "/PGM/LiveSave/cDirtyPagesLong",      STAMUNIT_COUNT,     "Longer term dirty page average.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.cDirtyPagesShort,     STAMTYPE_U32,     "/PGM/LiveSave/cDirtyPagesShort",     STAMUNIT_COUNT,     "Short term dirty page average.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.cPagesPerSecond,      STAMTYPE_U32,     "/PGM/LiveSave/cPagesPerSecond",      STAMUNIT_COUNT,     "Pages per second.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.cSavedPages,          STAMTYPE_U64,     "/PGM/LiveSave/cSavedPages",          STAMUNIT_COUNT,     "The total number of saved pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Ram.cReadyPages,      STAMTYPE_U32,     "/PGM/LiveSave/Ram/cReadPages",       STAMUNIT_COUNT,     "RAM: Ready pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Ram.cDirtyPages,      STAMTYPE_U32,     "/PGM/LiveSave/Ram/cDirtyPages",      STAMUNIT_COUNT,     "RAM: Dirty pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Ram.cZeroPages,       STAMTYPE_U32,     "/PGM/LiveSave/Ram/cZeroPages",       STAMUNIT_COUNT,     "RAM: Ready zero pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Ram.cMonitoredPages,  STAMTYPE_U32,     "/PGM/LiveSave/Ram/cMonitoredPages",  STAMUNIT_COUNT,     "RAM: Write monitored pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Rom.cReadyPages,      STAMTYPE_U32,     "/PGM/LiveSave/Rom/cReadPages",       STAMUNIT_COUNT,     "ROM: Ready pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Rom.cDirtyPages,      STAMTYPE_U32,     "/PGM/LiveSave/Rom/cDirtyPages",      STAMUNIT_COUNT,     "ROM: Dirty pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Rom.cZeroPages,       STAMTYPE_U32,     "/PGM/LiveSave/Rom/cZeroPages",       STAMUNIT_COUNT,     "ROM: Ready zero pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Rom.cMonitoredPages,  STAMTYPE_U32,     "/PGM/LiveSave/Rom/cMonitoredPages",  STAMUNIT_COUNT,     "ROM: Write monitored pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Mmio2.cReadyPages,    STAMTYPE_U32,     "/PGM/LiveSave/Mmio2/cReadPages",     STAMUNIT_COUNT,     "MMIO2: Ready pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Mmio2.cDirtyPages,    STAMTYPE_U32,     "/PGM/LiveSave/Mmio2/cDirtyPages",    STAMUNIT_COUNT,     "MMIO2: Dirty pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Mmio2.cZeroPages,     STAMTYPE_U32,     "/PGM/LiveSave/Mmio2/cZeroPages",     STAMUNIT_COUNT,     "MMIO2: Ready zero pages.");
    STAM_REL_REG_USED(pVM, &pPGM->LiveSave.Mmio2.cMonitoredPages,STAMTYPE_U32,     "/PGM/LiveSave/Mmio2/cMonitoredPages",STAMUNIT_COUNT,     "MMIO2: Write monitored pages.");

#define PGM_REG_COUNTER(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b); \
        AssertRC(rc);

#define PGM_REG_U64(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b); \
        AssertRC(rc);

#define PGM_REG_U64_RESET(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b); \
        AssertRC(rc);

#define PGM_REG_U32(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b); \
        AssertRC(rc);

#define PGM_REG_COUNTER_BYTES(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES, c, b); \
        AssertRC(rc);

#define PGM_REG_PROFILE(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, c, b); \
        AssertRC(rc);
#define PGM_REG_PROFILE_NS(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, c, b); \
        AssertRC(rc);

#ifdef VBOX_WITH_STATISTICS
    PGMSTATS *pStats = &pPGM->Stats;
#endif

    PGM_REG_PROFILE_NS(&pPGM->StatLargePageAlloc,               "/PGM/LargePage/Alloc",               "Time spent by the host OS for large page allocation.");
    PGM_REG_COUNTER(&pPGM->StatLargePageAllocFailed,            "/PGM/LargePage/AllocFailed",         "Number of allocation failures.");
    PGM_REG_COUNTER(&pPGM->StatLargePageOverflow,               "/PGM/LargePage/Overflow",            "The number of times allocating a large page took too long.");
    PGM_REG_COUNTER(&pPGM->StatLargePageTlbFlush,               "/PGM/LargePage/TlbFlush",            "The number of times a full VCPU TLB flush was required after a large allocation.");
    PGM_REG_COUNTER(&pPGM->StatLargePageZeroEvict,              "/PGM/LargePage/ZeroEvict",           "The number of zero page mappings we had to evict when allocating a large page.");
#ifdef VBOX_WITH_STATISTICS
    PGM_REG_PROFILE(&pStats->StatLargePageAlloc2,               "/PGM/LargePage/Alloc2",              "Time spent allocating large pages.");
    PGM_REG_PROFILE(&pStats->StatLargePageSetup,                "/PGM/LargePage/Setup",               "Time spent setting up the newly allocated large pages.");
    PGM_REG_PROFILE(&pStats->StatR3IsValidLargePage,            "/PGM/LargePage/IsValidR3",           "pgmPhysIsValidLargePage profiling - R3.");
    PGM_REG_PROFILE(&pStats->StatRZIsValidLargePage,            "/PGM/LargePage/IsValidRZ",           "pgmPhysIsValidLargePage profiling - RZ.");

    PGM_REG_COUNTER(&pStats->StatR3DetectedConflicts,           "/PGM/R3/DetectedConflicts",          "The number of times PGMR3CheckMappingConflicts() detected a conflict.");
    PGM_REG_PROFILE(&pStats->StatR3ResolveConflict,             "/PGM/R3/ResolveConflict",            "pgmR3SyncPTResolveConflict() profiling (includes the entire relocation).");
    PGM_REG_COUNTER(&pStats->StatR3PhysRead,                    "/PGM/R3/Phys/Read",                  "The number of times PGMPhysRead was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatR3PhysReadBytes,         "/PGM/R3/Phys/Read/Bytes",            "The number of bytes read by PGMPhysRead.");
    PGM_REG_COUNTER(&pStats->StatR3PhysWrite,                   "/PGM/R3/Phys/Write",                 "The number of times PGMPhysWrite was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatR3PhysWriteBytes,        "/PGM/R3/Phys/Write/Bytes",           "The number of bytes written by PGMPhysWrite.");
    PGM_REG_COUNTER(&pStats->StatR3PhysSimpleRead,              "/PGM/R3/Phys/Simple/Read",           "The number of times PGMPhysSimpleReadGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatR3PhysSimpleReadBytes,   "/PGM/R3/Phys/Simple/Read/Bytes",     "The number of bytes read by PGMPhysSimpleReadGCPtr.");
    PGM_REG_COUNTER(&pStats->StatR3PhysSimpleWrite,             "/PGM/R3/Phys/Simple/Write",          "The number of times PGMPhysSimpleWriteGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatR3PhysSimpleWriteBytes,  "/PGM/R3/Phys/Simple/Write/Bytes",    "The number of bytes written by PGMPhysSimpleWriteGCPtr.");

    PGM_REG_COUNTER(&pStats->StatRZChunkR3MapTlbHits,           "/PGM/ChunkR3Map/TlbHitsRZ",          "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatRZChunkR3MapTlbMisses,         "/PGM/ChunkR3Map/TlbMissesRZ",        "TLB misses.");
    PGM_REG_PROFILE(&pStats->StatChunkAging,                    "/PGM/ChunkR3Map/Map/Aging",          "Chunk aging profiling.");
    PGM_REG_PROFILE(&pStats->StatChunkFindCandidate,            "/PGM/ChunkR3Map/Map/Find",           "Chunk unmap find profiling.");
    PGM_REG_PROFILE(&pStats->StatChunkUnmap,                    "/PGM/ChunkR3Map/Map/Unmap",          "Chunk unmap of address space profiling.");
    PGM_REG_PROFILE(&pStats->StatChunkMap,                      "/PGM/ChunkR3Map/Map/Map",            "Chunk map of address space profiling.");

    PGM_REG_COUNTER(&pStats->StatRZPageMapTlbHits,              "/PGM/RZ/Page/MapTlbHits",            "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatRZPageMapTlbMisses,            "/PGM/RZ/Page/MapTlbMisses",          "TLB misses.");
    PGM_REG_COUNTER(&pStats->StatR3ChunkR3MapTlbHits,           "/PGM/ChunkR3Map/TlbHitsR3",          "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatR3ChunkR3MapTlbMisses,         "/PGM/ChunkR3Map/TlbMissesR3",        "TLB misses.");
    PGM_REG_COUNTER(&pStats->StatR3PageMapTlbHits,              "/PGM/R3/Page/MapTlbHits",            "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatR3PageMapTlbMisses,            "/PGM/R3/Page/MapTlbMisses",          "TLB misses.");
    PGM_REG_COUNTER(&pStats->StatPageMapTlbFlushes,             "/PGM/R3/Page/MapTlbFlushes",         "TLB flushes (all contexts).");
    PGM_REG_COUNTER(&pStats->StatPageMapTlbFlushEntry,          "/PGM/R3/Page/MapTlbFlushEntry",      "TLB entry flushes (all contexts).");

    PGM_REG_COUNTER(&pStats->StatRZRamRangeTlbHits,             "/PGM/RZ/RamRange/TlbHits",           "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatRZRamRangeTlbMisses,           "/PGM/RZ/RamRange/TlbMisses",         "TLB misses.");
    PGM_REG_COUNTER(&pStats->StatR3RamRangeTlbHits,             "/PGM/R3/RamRange/TlbHits",           "TLB hits.");
    PGM_REG_COUNTER(&pStats->StatR3RamRangeTlbMisses,           "/PGM/R3/RamRange/TlbMisses",         "TLB misses.");

    PGM_REG_COUNTER(&pStats->StatRZPhysHandlerReset,            "/PGM/RZ/PhysHandlerReset",           "The number of times PGMHandlerPhysicalReset is called.");
    PGM_REG_COUNTER(&pStats->StatR3PhysHandlerReset,            "/PGM/R3/PhysHandlerReset",           "The number of times PGMHandlerPhysicalReset is called.");
    PGM_REG_COUNTER(&pStats->StatRZPhysHandlerLookupHits,       "/PGM/RZ/PhysHandlerLookupHits",      "The number of cache hits when looking up physical handlers.");
    PGM_REG_COUNTER(&pStats->StatR3PhysHandlerLookupHits,       "/PGM/R3/PhysHandlerLookupHits",      "The number of cache hits when looking up physical handlers.");
    PGM_REG_COUNTER(&pStats->StatRZPhysHandlerLookupMisses,     "/PGM/RZ/PhysHandlerLookupMisses",    "The number of cache misses when looking up physical handlers.");
    PGM_REG_COUNTER(&pStats->StatR3PhysHandlerLookupMisses,     "/PGM/R3/PhysHandlerLookupMisses",    "The number of cache misses when looking up physical handlers.");
#endif /* VBOX_WITH_STATISTICS */
    PPGMPHYSHANDLERTREE pPhysHndlTree = pVM->pgm.s.pPhysHandlerTree;
    PGM_REG_U32(&pPhysHndlTree->m_cErrors,                      "/PGM/PhysHandlerTree/ErrorsTree",    "Physical access handler tree errors.");
    PGM_REG_U32(&pVM->pgm.s.PhysHandlerAllocator.m_cErrors,     "/PGM/PhysHandlerTree/ErrorsAllocatorR3", "Physical access handler tree allocator errors (ring-3 only).");
    PGM_REG_U64_RESET(&pPhysHndlTree->m_cInserts,               "/PGM/PhysHandlerTree/Inserts",       "Physical access handler tree inserts.");
    PGM_REG_U32(&pVM->pgm.s.PhysHandlerAllocator.m_cNodes,      "/PGM/PhysHandlerTree/MaxHandlers",   "Max physical access handlers.");
    PGM_REG_U64_RESET(&pPhysHndlTree->m_cRemovals,              "/PGM/PhysHandlerTree/Removals",      "Physical access handler tree removals.");
    PGM_REG_U64_RESET(&pPhysHndlTree->m_cRebalancingOperations, "/PGM/PhysHandlerTree/RebalancingOperations", "Physical access handler tree rebalancing transformations.");

#ifdef VBOX_WITH_STATISTICS
    PGM_REG_COUNTER(&pStats->StatRZPageReplaceShared,           "/PGM/RZ/Page/ReplacedShared",        "Times a shared page was replaced.");
    PGM_REG_COUNTER(&pStats->StatRZPageReplaceZero,             "/PGM/RZ/Page/ReplacedZero",          "Times the zero page was replaced.");
/// @todo    PGM_REG_COUNTER(&pStats->StatRZPageHandyAllocs,             "/PGM/RZ/Page/HandyAllocs",               "Number of times we've allocated more handy pages.");
    PGM_REG_COUNTER(&pStats->StatR3PageReplaceShared,           "/PGM/R3/Page/ReplacedShared",        "Times a shared page was replaced.");
    PGM_REG_COUNTER(&pStats->StatR3PageReplaceZero,             "/PGM/R3/Page/ReplacedZero",          "Times the zero page was replaced.");
/// @todo    PGM_REG_COUNTER(&pStats->StatR3PageHandyAllocs,             "/PGM/R3/Page/HandyAllocs",               "Number of times we've allocated more handy pages.");

    PGM_REG_COUNTER(&pStats->StatRZPhysRead,                    "/PGM/RZ/Phys/Read",                  "The number of times PGMPhysRead was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRZPhysReadBytes,         "/PGM/RZ/Phys/Read/Bytes",            "The number of bytes read by PGMPhysRead.");
    PGM_REG_COUNTER(&pStats->StatRZPhysWrite,                   "/PGM/RZ/Phys/Write",                 "The number of times PGMPhysWrite was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRZPhysWriteBytes,        "/PGM/RZ/Phys/Write/Bytes",           "The number of bytes written by PGMPhysWrite.");
    PGM_REG_COUNTER(&pStats->StatRZPhysSimpleRead,              "/PGM/RZ/Phys/Simple/Read",           "The number of times PGMPhysSimpleReadGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRZPhysSimpleReadBytes,   "/PGM/RZ/Phys/Simple/Read/Bytes",     "The number of bytes read by PGMPhysSimpleReadGCPtr.");
    PGM_REG_COUNTER(&pStats->StatRZPhysSimpleWrite,             "/PGM/RZ/Phys/Simple/Write",          "The number of times PGMPhysSimpleWriteGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRZPhysSimpleWriteBytes,  "/PGM/RZ/Phys/Simple/Write/Bytes",    "The number of bytes written by PGMPhysSimpleWriteGCPtr.");

    /* GC only: */
    PGM_REG_COUNTER(&pStats->StatRCInvlPgConflict,              "/PGM/RC/InvlPgConflict",             "Number of times PGMInvalidatePage() detected a mapping conflict.");
    PGM_REG_COUNTER(&pStats->StatRCInvlPgSyncMonCR3,            "/PGM/RC/InvlPgSyncMonitorCR3",       "Number of times PGMInvalidatePage() ran into PGM_SYNC_MONITOR_CR3.");

    PGM_REG_COUNTER(&pStats->StatRCPhysRead,                    "/PGM/RC/Phys/Read",                  "The number of times PGMPhysRead was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRCPhysReadBytes,         "/PGM/RC/Phys/Read/Bytes",            "The number of bytes read by PGMPhysRead.");
    PGM_REG_COUNTER(&pStats->StatRCPhysWrite,                   "/PGM/RC/Phys/Write",                 "The number of times PGMPhysWrite was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRCPhysWriteBytes,        "/PGM/RC/Phys/Write/Bytes",           "The number of bytes written by PGMPhysWrite.");
    PGM_REG_COUNTER(&pStats->StatRCPhysSimpleRead,              "/PGM/RC/Phys/Simple/Read",           "The number of times PGMPhysSimpleReadGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRCPhysSimpleReadBytes,   "/PGM/RC/Phys/Simple/Read/Bytes",     "The number of bytes read by PGMPhysSimpleReadGCPtr.");
    PGM_REG_COUNTER(&pStats->StatRCPhysSimpleWrite,             "/PGM/RC/Phys/Simple/Write",          "The number of times PGMPhysSimpleWriteGCPtr was called.");
    PGM_REG_COUNTER_BYTES(&pStats->StatRCPhysSimpleWriteBytes,  "/PGM/RC/Phys/Simple/Write/Bytes",    "The number of bytes written by PGMPhysSimpleWriteGCPtr.");

    PGM_REG_COUNTER(&pStats->StatTrackVirgin,                   "/PGM/Track/Virgin",                  "The number of first time shadowings");
    PGM_REG_COUNTER(&pStats->StatTrackAliased,                  "/PGM/Track/Aliased",                 "The number of times switching to cRef2, i.e. the page is being shadowed by two PTs.");
    PGM_REG_COUNTER(&pStats->StatTrackAliasedMany,              "/PGM/Track/AliasedMany",             "The number of times we're tracking using cRef2.");
    PGM_REG_COUNTER(&pStats->StatTrackAliasedLots,              "/PGM/Track/AliasedLots",             "The number of times we're hitting pages which has overflowed cRef2");
    PGM_REG_COUNTER(&pStats->StatTrackOverflows,                "/PGM/Track/Overflows",               "The number of times the extent list grows too long.");
    PGM_REG_COUNTER(&pStats->StatTrackNoExtentsLeft,            "/PGM/Track/NoExtentLeft",            "The number of times the extent list was exhausted.");
    PGM_REG_PROFILE(&pStats->StatTrackDeref,                    "/PGM/Track/Deref",                   "Profiling of SyncPageWorkerTrackDeref (expensive).");
#endif

#undef PGM_REG_COUNTER
#undef PGM_REG_U64
#undef PGM_REG_U64_RESET
#undef PGM_REG_U32
#undef PGM_REG_PROFILE
#undef PGM_REG_PROFILE_NS

    /*
     * Note! The layout below matches the member layout exactly!
     */

    /*
     * Common - stats
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PPGMCPU pPgmCpu = &pVM->apCpusR3[idCpu]->pgm.s;

#define PGM_REG_COUNTER(a, b, c) \
    rc = STAMR3RegisterF(pVM, a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b, idCpu); \
    AssertRC(rc);
#define PGM_REG_PROFILE(a, b, c) \
    rc = STAMR3RegisterF(pVM, a, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, c, b, idCpu); \
    AssertRC(rc);

        PGM_REG_COUNTER(&pPgmCpu->cGuestModeChanges, "/PGM/CPU%u/cGuestModeChanges",  "Number of guest mode changes.");
        PGM_REG_COUNTER(&pPgmCpu->cA20Changes, "/PGM/CPU%u/cA20Changes",  "Number of A20 gate changes.");

        PGM_REG_COUNTER(&pPgmCpu->StatRZRamRangeTlbMisses,              "/PGM/CPU%u/RZ/RamRange/TlbMisses",         "TLB misses (lockless).");
        PGM_REG_COUNTER(&pPgmCpu->StatRZRamRangeTlbLocking,             "/PGM/CPU%u/RZ/RamRange/TlbLocking",        "Lockless TLB failed, falling back on locked lookup.");
        PGM_REG_COUNTER(&pPgmCpu->StatRZPageMapTlbMisses,               "/PGM/CPU%u/RZ/Page/MapTlbMisses",          "Lockless page map TLB failed, falling back on locked lookup.");

        PGM_REG_COUNTER(&pPgmCpu->StatR3RamRangeTlbMisses,              "/PGM/CPU%u/R3/RamRange/TlbMisses",         "TLB misses (lockless).");
        PGM_REG_COUNTER(&pPgmCpu->StatR3RamRangeTlbLocking,             "/PGM/CPU%u/R3/RamRange/TlbLocking",        "Lockless TLB failed, falling back on locked lookup.");
        PGM_REG_COUNTER(&pPgmCpu->StatR3PageMapTlbMisses,               "/PGM/CPU%u/R3/Page/MapTlbMisses",          "Lockless page map TLB failed, falling back on locked lookup.");

#ifdef VBOX_WITH_STATISTICS
        PGMCPUSTATS *pCpuStats = &pVM->apCpusR3[idCpu]->pgm.s.Stats;

# if 0 /* rarely useful; leave for debugging. */
        for (unsigned j = 0; j < RT_ELEMENTS(pPgmCpu->StatSyncPtPD); j++)
            STAMR3RegisterF(pVM, &pCpuStats->StatSyncPtPD[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                            "The number of SyncPT per PD n.", "/PGM/CPU%u/PDSyncPT/%04X", i, j);
        for (unsigned j = 0; j < RT_ELEMENTS(pCpuStats->StatSyncPagePD); j++)
            STAMR3RegisterF(pVM, &pCpuStats->StatSyncPagePD[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                            "The number of SyncPage per PD n.", "/PGM/CPU%u/PDSyncPage/%04X", i, j);
# endif
        /* R0 only: */
        PGM_REG_PROFILE(&pCpuStats->StatR0NpMiscfg,                     "/PGM/CPU%u/R0/NpMiscfg",                     "PGMR0Trap0eHandlerNPMisconfig() profiling.");
        PGM_REG_COUNTER(&pCpuStats->StatR0NpMiscfgSyncPage,             "/PGM/CPU%u/R0/NpMiscfgSyncPage",             "SyncPage calls from PGMR0Trap0eHandlerNPMisconfig().");

        /* RZ only: */
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0e,                      "/PGM/CPU%u/RZ/Trap0e",                     "Profiling of the PGMTrap0eHandler() body.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2Ballooned,        "/PGM/CPU%u/RZ/Trap0e/Time2/Ballooned",         "Profiling of the Trap0eHandler body when the cause is read access to a ballooned page.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2DirtyAndAccessed, "/PGM/CPU%u/RZ/Trap0e/Time2/DirtyAndAccessedBits", "Profiling of the Trap0eHandler body when the cause is dirty and/or accessed bit emulation.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2GuestTrap,        "/PGM/CPU%u/RZ/Trap0e/Time2/GuestTrap",         "Profiling of the Trap0eHandler body when the cause is a guest trap.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2HndPhys,          "/PGM/CPU%u/RZ/Trap0e/Time2/HandlerPhysical",   "Profiling of the Trap0eHandler body when the cause is a physical handler.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2HndUnhandled,     "/PGM/CPU%u/RZ/Trap0e/Time2/HandlerUnhandled",  "Profiling of the Trap0eHandler body when the cause is access outside the monitored areas of a monitored page.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2InvalidPhys,      "/PGM/CPU%u/RZ/Trap0e/Time2/InvalidPhys",       "Profiling of the Trap0eHandler body when the cause is access to an invalid physical guest address.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2MakeWritable,     "/PGM/CPU%u/RZ/Trap0e/Time2/MakeWritable",      "Profiling of the Trap0eHandler body when the cause is that a page needed to be made writeable.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2Misc,             "/PGM/CPU%u/RZ/Trap0e/Time2/Misc",              "Profiling of the Trap0eHandler body when the cause is not known.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2OutOfSync,        "/PGM/CPU%u/RZ/Trap0e/Time2/OutOfSync",         "Profiling of the Trap0eHandler body when the cause is an out-of-sync page.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2OutOfSyncHndPhys, "/PGM/CPU%u/RZ/Trap0e/Time2/OutOfSyncHndPhys",  "Profiling of the Trap0eHandler body when the cause is an out-of-sync physical handler page.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2OutOfSyncHndObs,  "/PGM/CPU%u/RZ/Trap0e/Time2/OutOfSyncObsHnd",   "Profiling of the Trap0eHandler body when the cause is an obsolete handler page.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2PageZeroing,      "/PGM/CPU%u/RZ/Trap0e/Time2/PageZeroing",       "Profiling of the Trap0eHandler body when the cause is that a zero page is being zeroed.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2SyncPT,           "/PGM/CPU%u/RZ/Trap0e/Time2/SyncPT",            "Profiling of the Trap0eHandler body when the cause is lazy syncing of a PT.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2WPEmulation,      "/PGM/CPU%u/RZ/Trap0e/Time2/WPEmulation",       "Profiling of the Trap0eHandler body when the cause is CR0.WP emulation.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2Wp0RoUsHack,      "/PGM/CPU%u/RZ/Trap0e/Time2/WP0R0USHack",       "Profiling of the Trap0eHandler body when the cause is CR0.WP and netware hack to be enabled.");
        PGM_REG_PROFILE(&pCpuStats->StatRZTrap0eTime2Wp0RoUsUnhack,    "/PGM/CPU%u/RZ/Trap0e/Time2/WP0R0USUnhack",     "Profiling of the Trap0eHandler body when the cause is CR0.WP and netware hack to be disabled.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eConflicts,             "/PGM/CPU%u/RZ/Trap0e/Conflicts",               "The number of times #PF was caused by an undetected conflict.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersOutOfSync,     "/PGM/CPU%u/RZ/Trap0e/Handlers/OutOfSync",      "Number of traps due to out-of-sync handled pages.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersPhysAll,       "/PGM/CPU%u/RZ/Trap0e/Handlers/PhysAll",        "Number of traps due to physical all-access handlers.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersPhysAllOpt,    "/PGM/CPU%u/RZ/Trap0e/Handlers/PhysAllOpt",     "Number of the physical all-access handler traps using the optimization.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersPhysWrite,     "/PGM/CPU%u/RZ/Trap0e/Handlers/PhysWrite",      "Number of traps due to physical write-access handlers.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersUnhandled,     "/PGM/CPU%u/RZ/Trap0e/Handlers/Unhandled",      "Number of traps due to access outside range of monitored page(s).");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eHandlersInvalid,       "/PGM/CPU%u/RZ/Trap0e/Handlers/Invalid",        "Number of traps due to access to invalid physical memory.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSNotPresentRead,      "/PGM/CPU%u/RZ/Trap0e/Err/User/NPRead",         "Number of user mode not present read page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSNotPresentWrite,     "/PGM/CPU%u/RZ/Trap0e/Err/User/NPWrite",        "Number of user mode not present write page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSWrite,               "/PGM/CPU%u/RZ/Trap0e/Err/User/Write",          "Number of user mode write page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSReserved,            "/PGM/CPU%u/RZ/Trap0e/Err/User/Reserved",       "Number of user mode reserved bit page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSNXE,                 "/PGM/CPU%u/RZ/Trap0e/Err/User/NXE",            "Number of user mode NXE page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eUSRead,                "/PGM/CPU%u/RZ/Trap0e/Err/User/Read",           "Number of user mode read page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eSVNotPresentRead,      "/PGM/CPU%u/RZ/Trap0e/Err/Supervisor/NPRead",   "Number of supervisor mode not present read page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eSVNotPresentWrite,     "/PGM/CPU%u/RZ/Trap0e/Err/Supervisor/NPWrite",  "Number of supervisor mode not present write page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eSVWrite,               "/PGM/CPU%u/RZ/Trap0e/Err/Supervisor/Write",    "Number of supervisor mode write page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eSVReserved,            "/PGM/CPU%u/RZ/Trap0e/Err/Supervisor/Reserved", "Number of supervisor mode reserved bit page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eSNXE,                  "/PGM/CPU%u/RZ/Trap0e/Err/Supervisor/NXE",      "Number of supervisor mode NXE page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eGuestPF,               "/PGM/CPU%u/RZ/Trap0e/GuestPF",                 "Number of real guest page faults.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eWPEmulInRZ,            "/PGM/CPU%u/RZ/Trap0e/WP/InRZ",                 "Number of guest page faults due to X86_CR0_WP emulation.");
        PGM_REG_COUNTER(&pCpuStats->StatRZTrap0eWPEmulToR3,            "/PGM/CPU%u/RZ/Trap0e/WP/ToR3",                 "Number of guest page faults due to X86_CR0_WP emulation (forward to R3 for emulation).");
#if 0 /* rarely useful; leave for debugging. */
        for (unsigned j = 0; j < RT_ELEMENTS(pCpuStats->StatRZTrap0ePD); j++)
            STAMR3RegisterF(pVM, &pCpuStats->StatRZTrap0ePD[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                            "The number of traps in page directory n.", "/PGM/CPU%u/RZ/Trap0e/PD/%04X", i, j);
#endif
        PGM_REG_COUNTER(&pCpuStats->StatRZGuestCR3WriteHandled,        "/PGM/CPU%u/RZ/CR3WriteHandled",                "The number of times the Guest CR3 change was successfully handled.");
        PGM_REG_COUNTER(&pCpuStats->StatRZGuestCR3WriteUnhandled,      "/PGM/CPU%u/RZ/CR3WriteUnhandled",              "The number of times the Guest CR3 change was passed back to the recompiler.");
        PGM_REG_COUNTER(&pCpuStats->StatRZGuestCR3WriteConflict,       "/PGM/CPU%u/RZ/CR3WriteConflict",               "The number of times the Guest CR3 monitoring detected a conflict.");
        PGM_REG_COUNTER(&pCpuStats->StatRZGuestROMWriteHandled,        "/PGM/CPU%u/RZ/ROMWriteHandled",                "The number of times the Guest ROM change was successfully handled.");
        PGM_REG_COUNTER(&pCpuStats->StatRZGuestROMWriteUnhandled,      "/PGM/CPU%u/RZ/ROMWriteUnhandled",              "The number of times the Guest ROM change was passed back to the recompiler.");

        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapMigrateInvlPg,         "/PGM/CPU%u/RZ/DynMap/MigrateInvlPg",            "invlpg count in PGMR0DynMapMigrateAutoSet.");
        PGM_REG_PROFILE(&pCpuStats->StatRZDynMapGCPageInl,             "/PGM/CPU%u/RZ/DynMap/PageGCPageInl",            "Calls to pgmR0DynMapGCPageInlined.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapGCPageInlHits,         "/PGM/CPU%u/RZ/DynMap/PageGCPageInl/Hits",       "Hash table lookup hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapGCPageInlMisses,       "/PGM/CPU%u/RZ/DynMap/PageGCPageInl/Misses",     "Misses that falls back to the code common.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapGCPageInlRamHits,      "/PGM/CPU%u/RZ/DynMap/PageGCPageInl/RamHits",    "1st ram range hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapGCPageInlRamMisses,    "/PGM/CPU%u/RZ/DynMap/PageGCPageInl/RamMisses",  "1st ram range misses, takes slow path.");
        PGM_REG_PROFILE(&pCpuStats->StatRZDynMapHCPageInl,             "/PGM/CPU%u/RZ/DynMap/PageHCPageInl",            "Calls to pgmRZDynMapHCPageInlined.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapHCPageInlHits,         "/PGM/CPU%u/RZ/DynMap/PageHCPageInl/Hits",       "Hash table lookup hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapHCPageInlMisses,       "/PGM/CPU%u/RZ/DynMap/PageHCPageInl/Misses",     "Misses that falls back to the code common.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPage,                  "/PGM/CPU%u/RZ/DynMap/Page",                     "Calls to pgmR0DynMapPage");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapSetOptimize,           "/PGM/CPU%u/RZ/DynMap/Page/SetOptimize",         "Calls to pgmRZDynMapOptimizeAutoSet.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapSetSearchFlushes,      "/PGM/CPU%u/RZ/DynMap/Page/SetSearchFlushes",    "Set search restoring to subset flushes.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapSetSearchHits,         "/PGM/CPU%u/RZ/DynMap/Page/SetSearchHits",       "Set search hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapSetSearchMisses,       "/PGM/CPU%u/RZ/DynMap/Page/SetSearchMisses",     "Set search misses.");
        PGM_REG_PROFILE(&pCpuStats->StatRZDynMapHCPage,                "/PGM/CPU%u/RZ/DynMap/Page/HCPage",              "Calls to pgmRZDynMapHCPageCommon (ring-0).");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageHits0,             "/PGM/CPU%u/RZ/DynMap/Page/Hits0",               "Hits at iPage+0");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageHits1,             "/PGM/CPU%u/RZ/DynMap/Page/Hits1",               "Hits at iPage+1");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageHits2,             "/PGM/CPU%u/RZ/DynMap/Page/Hits2",               "Hits at iPage+2");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageInvlPg,            "/PGM/CPU%u/RZ/DynMap/Page/InvlPg",              "invlpg count in pgmR0DynMapPageSlow.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageSlow,              "/PGM/CPU%u/RZ/DynMap/Page/Slow",                "Calls to pgmR0DynMapPageSlow - subtract this from pgmR0DynMapPage to get 1st level hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageSlowLoopHits,      "/PGM/CPU%u/RZ/DynMap/Page/SlowLoopHits" ,       "Hits in the loop path.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageSlowLoopMisses,    "/PGM/CPU%u/RZ/DynMap/Page/SlowLoopMisses",      "Misses in the loop path. NonLoopMisses = Slow - SlowLoopHit - SlowLoopMisses");
        //PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPageSlowLostHits,      "/PGM/CPU%u/R0/DynMap/Page/SlowLostHits",        "Lost hits.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapSubsets,               "/PGM/CPU%u/RZ/DynMap/Subsets",                  "Times PGMRZDynMapPushAutoSubset was called.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDynMapPopFlushes,            "/PGM/CPU%u/RZ/DynMap/SubsetPopFlushes",         "Times PGMRZDynMapPopAutoSubset flushes the subset.");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[0],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct000..09",      "00-09% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[1],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct010..19",      "10-19% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[2],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct020..29",      "20-29% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[3],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct030..39",      "30-39% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[4],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct040..49",      "40-49% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[5],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct050..59",      "50-59% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[6],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct060..69",      "60-69% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[7],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct070..79",      "70-79% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[8],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct080..89",      "80-89% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[9],      "/PGM/CPU%u/RZ/DynMap/SetFilledPct090..99",      "90-99% filled (RC: min(set-size, dynmap-size))");
        PGM_REG_COUNTER(&pCpuStats->aStatRZDynMapSetFilledPct[10],     "/PGM/CPU%u/RZ/DynMap/SetFilledPct100",          "100% filled (RC: min(set-size, dynmap-size))");

        /* HC only: */

        /* RZ & R3: */
        PGM_REG_PROFILE(&pCpuStats->StatRZSyncCR3,                     "/PGM/CPU%u/RZ/SyncCR3",                        "Profiling of the PGMSyncCR3() body.");
        PGM_REG_PROFILE(&pCpuStats->StatRZSyncCR3Handlers,             "/PGM/CPU%u/RZ/SyncCR3/Handlers",               "Profiling of the PGMSyncCR3() update handler section.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3Global,               "/PGM/CPU%u/RZ/SyncCR3/Global",                 "The number of global CR3 syncs.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3NotGlobal,            "/PGM/CPU%u/RZ/SyncCR3/NotGlobal",              "The number of non-global CR3 syncs.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstCacheHit,          "/PGM/CPU%u/RZ/SyncCR3/DstChacheHit",           "The number of times we got some kind of a cache hit.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstFreed,             "/PGM/CPU%u/RZ/SyncCR3/DstFreed",               "The number of times we've had to free a shadow entry.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstFreedSrcNP,        "/PGM/CPU%u/RZ/SyncCR3/DstFreedSrcNP",          "The number of times we've had to free a shadow entry for which the source entry was not present.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstNotPresent,        "/PGM/CPU%u/RZ/SyncCR3/DstNotPresent",          "The number of times we've encountered a not present shadow entry for a present guest entry.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstSkippedGlobalPD,   "/PGM/CPU%u/RZ/SyncCR3/DstSkippedGlobalPD",     "The number of times a global page directory wasn't flushed.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncCR3DstSkippedGlobalPT,   "/PGM/CPU%u/RZ/SyncCR3/DstSkippedGlobalPT",     "The number of times a page table with only global entries wasn't flushed.");
        PGM_REG_PROFILE(&pCpuStats->StatRZSyncPT,                      "/PGM/CPU%u/RZ/SyncPT",                         "Profiling of the pfnSyncPT() body.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncPTFailed,                "/PGM/CPU%u/RZ/SyncPT/Failed",                  "The number of times pfnSyncPT() failed.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncPT4K,                    "/PGM/CPU%u/RZ/SyncPT/4K",                      "Nr of 4K PT syncs");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncPT4M,                    "/PGM/CPU%u/RZ/SyncPT/4M",                      "Nr of 4M PT syncs");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncPagePDNAs,               "/PGM/CPU%u/RZ/SyncPagePDNAs",                  "The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit.");
        PGM_REG_COUNTER(&pCpuStats->StatRZSyncPagePDOutOfSync,         "/PGM/CPU%u/RZ/SyncPagePDOutOfSync",            "The number of time we've encountered an out-of-sync PD in SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatRZAccessedPage,                "/PGM/CPU%u/RZ/AccessedPage",               "The number of pages marked not present for accessed bit emulation.");
        PGM_REG_PROFILE(&pCpuStats->StatRZDirtyBitTracking,            "/PGM/CPU%u/RZ/DirtyPage",                  "Profiling the dirty bit tracking in CheckPageFault().");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyPage,                   "/PGM/CPU%u/RZ/DirtyPage/Mark",             "The number of pages marked read-only for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyPageBig,                "/PGM/CPU%u/RZ/DirtyPage/MarkBig",          "The number of 4MB pages marked read-only for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyPageSkipped,            "/PGM/CPU%u/RZ/DirtyPage/Skipped",          "The number of pages already dirty or readonly.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyPageTrap,               "/PGM/CPU%u/RZ/DirtyPage/Trap",             "The number of traps generated for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyPageStale,              "/PGM/CPU%u/RZ/DirtyPage/Stale",            "The number of traps generated for dirty bit tracking (stale tlb entries).");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtiedPage,                 "/PGM/CPU%u/RZ/DirtyPage/SetDirty",         "The number of pages marked dirty because of write accesses.");
        PGM_REG_COUNTER(&pCpuStats->StatRZDirtyTrackRealPF,            "/PGM/CPU%u/RZ/DirtyPage/RealPF",           "The number of real pages faults during dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageAlreadyDirty,            "/PGM/CPU%u/RZ/DirtyPage/AlreadySet",       "The number of pages already marked dirty because of write accesses.");
        PGM_REG_PROFILE(&pCpuStats->StatRZInvalidatePage,              "/PGM/CPU%u/RZ/InvalidatePage",             "PGMInvalidatePage() profiling.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePage4KBPages,      "/PGM/CPU%u/RZ/InvalidatePage/4KBPages",    "The number of times PGMInvalidatePage() was called for a 4KB page.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePage4MBPages,      "/PGM/CPU%u/RZ/InvalidatePage/4MBPages",    "The number of times PGMInvalidatePage() was called for a 4MB page.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePage4MBPagesSkip,  "/PGM/CPU%u/RZ/InvalidatePage/4MBPagesSkip","The number of times PGMInvalidatePage() skipped a 4MB page.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePagePDNAs,         "/PGM/CPU%u/RZ/InvalidatePage/PDNAs",       "The number of times PGMInvalidatePage() was called for a not accessed page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePagePDNPs,         "/PGM/CPU%u/RZ/InvalidatePage/PDNPs",       "The number of times PGMInvalidatePage() was called for a not present page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePagePDOutOfSync,   "/PGM/CPU%u/RZ/InvalidatePage/PDOutOfSync", "The number of times PGMInvalidatePage() was called for an out of sync page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePageSizeChanges,   "/PGM/CPU%u/RZ/InvalidatePage/SizeChanges", "The number of times PGMInvalidatePage() was called on a page size change (4KB <-> 2/4MB).");
        PGM_REG_COUNTER(&pCpuStats->StatRZInvalidatePageSkipped,       "/PGM/CPU%u/RZ/InvalidatePage/Skipped",     "The number of times PGMInvalidatePage() was skipped due to not present shw or pending pending SyncCR3.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageOutOfSyncSupervisor,     "/PGM/CPU%u/RZ/OutOfSync/SuperVisor",       "Number of traps due to pages out of sync (P) and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageOutOfSyncUser,           "/PGM/CPU%u/RZ/OutOfSync/User",             "Number of traps due to pages out of sync (P) and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageOutOfSyncSupervisorWrite,"/PGM/CPU%u/RZ/OutOfSync/SuperVisorWrite",  "Number of traps due to pages out of sync (RW) and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageOutOfSyncUserWrite,      "/PGM/CPU%u/RZ/OutOfSync/UserWrite",        "Number of traps due to pages out of sync (RW) and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageOutOfSyncBallloon,       "/PGM/CPU%u/RZ/OutOfSync/Balloon",          "The number of times a ballooned page was accessed (read).");
        PGM_REG_PROFILE(&pCpuStats->StatRZPrefetch,                    "/PGM/CPU%u/RZ/Prefetch",                   "PGMPrefetchPage profiling.");
        PGM_REG_PROFILE(&pCpuStats->StatRZFlushTLB,                    "/PGM/CPU%u/RZ/FlushTLB",                   "Profiling of the PGMFlushTLB() body.");
        PGM_REG_COUNTER(&pCpuStats->StatRZFlushTLBNewCR3,              "/PGM/CPU%u/RZ/FlushTLB/NewCR3",            "The number of times PGMFlushTLB was called with a new CR3, non-global. (switch)");
        PGM_REG_COUNTER(&pCpuStats->StatRZFlushTLBNewCR3Global,        "/PGM/CPU%u/RZ/FlushTLB/NewCR3Global",      "The number of times PGMFlushTLB was called with a new CR3, global. (switch)");
        PGM_REG_COUNTER(&pCpuStats->StatRZFlushTLBSameCR3,             "/PGM/CPU%u/RZ/FlushTLB/SameCR3",           "The number of times PGMFlushTLB was called with the same CR3, non-global. (flush)");
        PGM_REG_COUNTER(&pCpuStats->StatRZFlushTLBSameCR3Global,       "/PGM/CPU%u/RZ/FlushTLB/SameCR3Global",     "The number of times PGMFlushTLB was called with the same CR3, global. (flush)");
        PGM_REG_PROFILE(&pCpuStats->StatRZGstModifyPage,               "/PGM/CPU%u/RZ/GstModifyPage",              "Profiling of the PGMGstModifyPage() body.");
        PGM_REG_COUNTER(&pCpuStats->StatRZRamRangeTlbHits,             "/PGM/CPU%u/RZ/RamRange/TlbHits",           "TLB hits (lockless).");
        PGM_REG_COUNTER(&pCpuStats->StatRZPageMapTlbHits,              "/PGM/CPU%u/RZ/Page/MapTlbHits",            "TLB hits (lockless).");

        PGM_REG_PROFILE(&pCpuStats->StatR3SyncCR3,                     "/PGM/CPU%u/R3/SyncCR3",                        "Profiling of the PGMSyncCR3() body.");
        PGM_REG_PROFILE(&pCpuStats->StatR3SyncCR3Handlers,             "/PGM/CPU%u/R3/SyncCR3/Handlers",               "Profiling of the PGMSyncCR3() update handler section.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3Global,               "/PGM/CPU%u/R3/SyncCR3/Global",                 "The number of global CR3 syncs.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3NotGlobal,            "/PGM/CPU%u/R3/SyncCR3/NotGlobal",              "The number of non-global CR3 syncs.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstCacheHit,          "/PGM/CPU%u/R3/SyncCR3/DstChacheHit",           "The number of times we got some kind of a cache hit.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstFreed,             "/PGM/CPU%u/R3/SyncCR3/DstFreed",               "The number of times we've had to free a shadow entry.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstFreedSrcNP,        "/PGM/CPU%u/R3/SyncCR3/DstFreedSrcNP",          "The number of times we've had to free a shadow entry for which the source entry was not present.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstNotPresent,        "/PGM/CPU%u/R3/SyncCR3/DstNotPresent",          "The number of times we've encountered a not present shadow entry for a present guest entry.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstSkippedGlobalPD,   "/PGM/CPU%u/R3/SyncCR3/DstSkippedGlobalPD",     "The number of times a global page directory wasn't flushed.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncCR3DstSkippedGlobalPT,   "/PGM/CPU%u/R3/SyncCR3/DstSkippedGlobalPT",     "The number of times a page table with only global entries wasn't flushed.");
        PGM_REG_PROFILE(&pCpuStats->StatR3SyncPT,                      "/PGM/CPU%u/R3/SyncPT",                         "Profiling of the pfnSyncPT() body.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncPTFailed,                "/PGM/CPU%u/R3/SyncPT/Failed",                  "The number of times pfnSyncPT() failed.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncPT4K,                    "/PGM/CPU%u/R3/SyncPT/4K",                      "Nr of 4K PT syncs");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncPT4M,                    "/PGM/CPU%u/R3/SyncPT/4M",                      "Nr of 4M PT syncs");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncPagePDNAs,               "/PGM/CPU%u/R3/SyncPagePDNAs",                  "The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit.");
        PGM_REG_COUNTER(&pCpuStats->StatR3SyncPagePDOutOfSync,         "/PGM/CPU%u/R3/SyncPagePDOutOfSync",            "The number of time we've encountered an out-of-sync PD in SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatR3AccessedPage,                "/PGM/CPU%u/R3/AccessedPage",               "The number of pages marked not present for accessed bit emulation.");
        PGM_REG_PROFILE(&pCpuStats->StatR3DirtyBitTracking,            "/PGM/CPU%u/R3/DirtyPage",                  "Profiling the dirty bit tracking in CheckPageFault().");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtyPage,                   "/PGM/CPU%u/R3/DirtyPage/Mark",             "The number of pages marked read-only for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtyPageBig,                "/PGM/CPU%u/R3/DirtyPage/MarkBig",          "The number of 4MB pages marked read-only for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtyPageSkipped,            "/PGM/CPU%u/R3/DirtyPage/Skipped",          "The number of pages already dirty or readonly.");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtyPageTrap,               "/PGM/CPU%u/R3/DirtyPage/Trap",             "The number of traps generated for dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtiedPage,                 "/PGM/CPU%u/R3/DirtyPage/SetDirty",         "The number of pages marked dirty because of write accesses.");
        PGM_REG_COUNTER(&pCpuStats->StatR3DirtyTrackRealPF,            "/PGM/CPU%u/R3/DirtyPage/RealPF",           "The number of real pages faults during dirty bit tracking.");
        PGM_REG_COUNTER(&pCpuStats->StatR3PageAlreadyDirty,            "/PGM/CPU%u/R3/DirtyPage/AlreadySet",       "The number of pages already marked dirty because of write accesses.");
        PGM_REG_PROFILE(&pCpuStats->StatR3InvalidatePage,              "/PGM/CPU%u/R3/InvalidatePage",             "PGMInvalidatePage() profiling.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePage4KBPages,      "/PGM/CPU%u/R3/InvalidatePage/4KBPages",    "The number of times PGMInvalidatePage() was called for a 4KB page.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePage4MBPages,      "/PGM/CPU%u/R3/InvalidatePage/4MBPages",    "The number of times PGMInvalidatePage() was called for a 4MB page.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePage4MBPagesSkip,  "/PGM/CPU%u/R3/InvalidatePage/4MBPagesSkip","The number of times PGMInvalidatePage() skipped a 4MB page.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePagePDNAs,         "/PGM/CPU%u/R3/InvalidatePage/PDNAs",       "The number of times PGMInvalidatePage() was called for a not accessed page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePagePDNPs,         "/PGM/CPU%u/R3/InvalidatePage/PDNPs",       "The number of times PGMInvalidatePage() was called for a not present page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePagePDOutOfSync,   "/PGM/CPU%u/R3/InvalidatePage/PDOutOfSync", "The number of times PGMInvalidatePage() was called for an out of sync page directory.");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePageSizeChanges,   "/PGM/CPU%u/R3/InvalidatePage/SizeChanges", "The number of times PGMInvalidatePage() was called on a page size change (4KB <-> 2/4MB).");
        PGM_REG_COUNTER(&pCpuStats->StatR3InvalidatePageSkipped,       "/PGM/CPU%u/R3/InvalidatePage/Skipped",     "The number of times PGMInvalidatePage() was skipped due to not present shw or pending pending SyncCR3.");
        PGM_REG_COUNTER(&pCpuStats->StatR3PageOutOfSyncSupervisor,     "/PGM/CPU%u/R3/OutOfSync/SuperVisor",       "Number of traps due to pages out of sync and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatR3PageOutOfSyncUser,           "/PGM/CPU%u/R3/OutOfSync/User",             "Number of traps due to pages out of sync and times VerifyAccessSyncPage calls SyncPage.");
        PGM_REG_COUNTER(&pCpuStats->StatR3PageOutOfSyncBallloon,       "/PGM/CPU%u/R3/OutOfSync/Balloon",          "The number of times a ballooned page was accessed (read).");
        PGM_REG_PROFILE(&pCpuStats->StatR3Prefetch,                    "/PGM/CPU%u/R3/Prefetch",                   "PGMPrefetchPage profiling.");
        PGM_REG_PROFILE(&pCpuStats->StatR3FlushTLB,                    "/PGM/CPU%u/R3/FlushTLB",                   "Profiling of the PGMFlushTLB() body.");
        PGM_REG_COUNTER(&pCpuStats->StatR3FlushTLBNewCR3,              "/PGM/CPU%u/R3/FlushTLB/NewCR3",            "The number of times PGMFlushTLB was called with a new CR3, non-global. (switch)");
        PGM_REG_COUNTER(&pCpuStats->StatR3FlushTLBNewCR3Global,        "/PGM/CPU%u/R3/FlushTLB/NewCR3Global",      "The number of times PGMFlushTLB was called with a new CR3, global. (switch)");
        PGM_REG_COUNTER(&pCpuStats->StatR3FlushTLBSameCR3,             "/PGM/CPU%u/R3/FlushTLB/SameCR3",           "The number of times PGMFlushTLB was called with the same CR3, non-global. (flush)");
        PGM_REG_COUNTER(&pCpuStats->StatR3FlushTLBSameCR3Global,       "/PGM/CPU%u/R3/FlushTLB/SameCR3Global",     "The number of times PGMFlushTLB was called with the same CR3, global. (flush)");
        PGM_REG_PROFILE(&pCpuStats->StatR3GstModifyPage,               "/PGM/CPU%u/R3/GstModifyPage",              "Profiling of the PGMGstModifyPage() body.");
        PGM_REG_COUNTER(&pCpuStats->StatR3RamRangeTlbHits,             "/PGM/CPU%u/R3/RamRange/TlbHits",           "TLB hits (lockless).");
        PGM_REG_COUNTER(&pCpuStats->StatR3PageMapTlbHits,              "/PGM/CPU%u/R3/Page/MapTlbHits",            "TLB hits (lockless).");
#endif /* VBOX_WITH_STATISTICS */

#undef PGM_REG_PROFILE
#undef PGM_REG_COUNTER
    }

    return VINF_SUCCESS;
}


/**
 * Ring-3 init finalizing.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) PGMR3InitFinalize(PVM pVM)
{
    /*
     * Determine the max physical address width (MAXPHYADDR) and apply it to
     * all the mask members and stuff.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t cMaxPhysAddrWidth;
    uint32_t uMaxExtLeaf = ASMCpuId_EAX(0x80000000);
    if (   uMaxExtLeaf >= 0x80000008
        && uMaxExtLeaf <= 0x80000fff)
    {
        cMaxPhysAddrWidth = ASMCpuId_EAX(0x80000008) & 0xff;
        LogRel(("PGM: The CPU physical address width is %u bits\n", cMaxPhysAddrWidth));
        cMaxPhysAddrWidth = RT_MIN(52, cMaxPhysAddrWidth);
        pVM->pgm.s.fLessThan52PhysicalAddressBits = cMaxPhysAddrWidth < 52;
        for (uint32_t iBit = cMaxPhysAddrWidth; iBit < 52; iBit++)
            pVM->pgm.s.HCPhysInvMmioPg |= RT_BIT_64(iBit);
    }
    else
    {
        LogRel(("PGM: ASSUMING CPU physical address width of 48 bits (uMaxExtLeaf=%#x)\n", uMaxExtLeaf));
        cMaxPhysAddrWidth = 48;
        pVM->pgm.s.fLessThan52PhysicalAddressBits = true;
        pVM->pgm.s.HCPhysInvMmioPg |= UINT64_C(0x000f0000000000);
    }
    /* Disabled the below assertion -- triggers 24 vs 39 on my Intel Skylake box for a 32-bit (Guest-type Other/Unknown) VM. */
    //AssertMsg(pVM->cpum.ro.GuestFeatures.cMaxPhysAddrWidth == cMaxPhysAddrWidth,
    //          ("CPUM %u - PGM %u\n", pVM->cpum.ro.GuestFeatures.cMaxPhysAddrWidth, cMaxPhysAddrWidth));
#else
    uint32_t const cMaxPhysAddrWidth = pVM->cpum.ro.GuestFeatures.cMaxPhysAddrWidth;
    LogRel(("PGM: The (guest) CPU physical address width is %u bits\n", cMaxPhysAddrWidth));
#endif

    /** @todo query from CPUM. */
    pVM->pgm.s.GCPhysInvAddrMask = 0;
    for (uint32_t iBit = cMaxPhysAddrWidth; iBit < 64; iBit++)
        pVM->pgm.s.GCPhysInvAddrMask |= RT_BIT_64(iBit);

    /*
     * Initialize the invalid paging entry masks, assuming NX is disabled.
     */
    uint64_t fMbzPageFrameMask = pVM->pgm.s.GCPhysInvAddrMask & UINT64_C(0x000ffffffffff000);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    uint64_t const fEptVpidCap = CPUMGetGuestIa32VmxEptVpidCap(pVM->apCpusR3[0]);   /* should be identical for all VCPUs */
    uint64_t const fGstEptMbzBigPdeMask   = EPT_PDE2M_MBZ_MASK
                                          | (RT_BF_GET(fEptVpidCap, VMX_BF_EPT_VPID_CAP_PDE_2M) ^ 1) << EPT_E_BIT_LEAF;
    uint64_t const fGstEptMbzBigPdpteMask = EPT_PDPTE1G_MBZ_MASK
                                          | (RT_BF_GET(fEptVpidCap, VMX_BF_EPT_VPID_CAP_PDPTE_1G) ^ 1) << EPT_E_BIT_LEAF;
    //uint64_t const GCPhysRsvdAddrMask     = pVM->pgm.s.GCPhysInvAddrMask & UINT64_C(0x000fffffffffffff); /* bits 63:52 ignored */
#endif
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /** @todo The manuals are not entirely clear whether the physical
         *        address width is relevant.  See table 5-9 in the intel
         *        manual vs the PDE4M descriptions.  Write testcase (NP). */
        pVCpu->pgm.s.fGst32BitMbzBigPdeMask   = ((uint32_t)(fMbzPageFrameMask >> (32 - 13)) & X86_PDE4M_PG_HIGH_MASK)
                                              | X86_PDE4M_MBZ_MASK;

        pVCpu->pgm.s.fGstPaeMbzPteMask        = fMbzPageFrameMask | X86_PTE_PAE_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstPaeMbzPdeMask        = fMbzPageFrameMask | X86_PDE_PAE_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstPaeMbzBigPdeMask     = fMbzPageFrameMask | X86_PDE2M_PAE_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstPaeMbzPdpeMask       = fMbzPageFrameMask | X86_PDPE_PAE_MBZ_MASK;

        pVCpu->pgm.s.fGstAmd64MbzPteMask      = fMbzPageFrameMask | X86_PTE_LM_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdeMask      = fMbzPageFrameMask | X86_PDE_LM_MBZ_MASK_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdeMask   = fMbzPageFrameMask | X86_PDE2M_LM_MBZ_MASK_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdpeMask     = fMbzPageFrameMask | X86_PDPE_LM_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdpeMask  = fMbzPageFrameMask | X86_PDPE1G_LM_MBZ_MASK_NO_NX;
        pVCpu->pgm.s.fGstAmd64MbzPml4eMask    = fMbzPageFrameMask | X86_PML4E_MBZ_MASK_NO_NX;

        pVCpu->pgm.s.fGst64ShadowedPteMask    = X86_PTE_P   | X86_PTE_RW   | X86_PTE_US   | X86_PTE_G | X86_PTE_A | X86_PTE_D;
        pVCpu->pgm.s.fGst64ShadowedPdeMask    = X86_PDE_P   | X86_PDE_RW   | X86_PDE_US   | X86_PDE_A;
        pVCpu->pgm.s.fGst64ShadowedBigPdeMask = X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_A;
        pVCpu->pgm.s.fGst64ShadowedBigPde4PteMask
            = X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_G | X86_PDE4M_A | X86_PDE4M_D;
        pVCpu->pgm.s.fGstAmd64ShadowedPdpeMask  = X86_PDPE_P  | X86_PDPE_RW  | X86_PDPE_US  | X86_PDPE_A;
        pVCpu->pgm.s.fGstAmd64ShadowedPml4eMask = X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A;

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        pVCpu->pgm.s.uEptVpidCapMsr           = fEptVpidCap;
        pVCpu->pgm.s.fGstEptMbzPteMask        = fMbzPageFrameMask | EPT_PTE_MBZ_MASK;
        pVCpu->pgm.s.fGstEptMbzPdeMask        = fMbzPageFrameMask | EPT_PDE_MBZ_MASK;
        pVCpu->pgm.s.fGstEptMbzBigPdeMask     = fMbzPageFrameMask | fGstEptMbzBigPdeMask;
        pVCpu->pgm.s.fGstEptMbzPdpteMask      = fMbzPageFrameMask | EPT_PDPTE_MBZ_MASK;
        pVCpu->pgm.s.fGstEptMbzBigPdpteMask   = fMbzPageFrameMask | fGstEptMbzBigPdpteMask;
        pVCpu->pgm.s.fGstEptMbzPml4eMask      = fMbzPageFrameMask | EPT_PML4E_MBZ_MASK;

        /* If any of the features in the assert below are enabled, additional bits would need to be shadowed. */
        Assert(   !pVM->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt
               && !pVM->cpum.ro.GuestFeatures.fVmxSppEpt
               && !pVM->cpum.ro.GuestFeatures.fVmxEptXcptVe
               && !(fEptVpidCap & MSR_IA32_VMX_EPT_VPID_CAP_ACCESS_DIRTY));
        /* We currently do -not- shadow reserved bits in guest page tables but instead trap them using non-present permissions,
           see todo in (NestedSyncPT). */
        pVCpu->pgm.s.fGstEptShadowedPteMask    = EPT_PRESENT_MASK;
        pVCpu->pgm.s.fGstEptShadowedPdeMask    = EPT_PRESENT_MASK;
        pVCpu->pgm.s.fGstEptShadowedBigPdeMask = EPT_PRESENT_MASK | EPT_E_LEAF;
        pVCpu->pgm.s.fGstEptShadowedPdpteMask  = EPT_PRESENT_MASK;
        pVCpu->pgm.s.fGstEptShadowedPml4eMask  = EPT_PRESENT_MASK | EPT_PML4E_MBZ_MASK;
        /* If mode-based execute control for EPT is enabled, we would need to include bit 10 in the present mask. */
        pVCpu->pgm.s.fGstEptPresentMask        = EPT_PRESENT_MASK;
#endif
    }

    /*
     * Note that AMD uses all the 8 reserved bits for the address (so 40 bits in total);
     * Intel only goes up to 36 bits, so we stick to 36 as well.
     * Update: More recent intel manuals specifies 40 bits just like AMD.
     */
    uint32_t u32Dummy, u32Features;
    CPUMGetGuestCpuId(VMMGetCpu(pVM), 1, 0, -1 /*f64BitMode*/, &u32Dummy, &u32Dummy, &u32Dummy, &u32Features);
    if (u32Features & X86_CPUID_FEATURE_EDX_PSE36)
        pVM->pgm.s.GCPhys4MBPSEMask = RT_BIT_64(RT_MAX(36, cMaxPhysAddrWidth)) - 1;
    else
        pVM->pgm.s.GCPhys4MBPSEMask = RT_BIT_64(32) - 1;

    /*
     * Allocate memory if we're supposed to do that.
     */
    int rc = VINF_SUCCESS;
    if (pVM->pgm.s.fRamPreAlloc)
        rc = pgmR3PhysRamPreAllocate(pVM);

    //pgmLogState(pVM);
    LogRel(("PGM: PGMR3InitFinalize: 4 MB PSE mask %RGp -> %Rrc\n", pVM->pgm.s.GCPhys4MBPSEMask, rc));
    return rc;
}


/**
 * Init phase completed callback.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             What has been completed.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) PGMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    switch (enmWhat)
    {
        case VMINITCOMPLETED_HM:
#ifdef VBOX_WITH_PCI_PASSTHROUGH
            if (pVM->pgm.s.fPciPassthrough)
            {
                AssertLogRelReturn(pVM->pgm.s.fRamPreAlloc, VERR_PCI_PASSTHROUGH_NO_RAM_PREALLOC);
                AssertLogRelReturn(HMIsEnabled(pVM), VERR_PCI_PASSTHROUGH_NO_HM);
                AssertLogRelReturn(HMIsNestedPagingActive(pVM), VERR_PCI_PASSTHROUGH_NO_NESTED_PAGING);

                /*
                 * Report assignments to the IOMMU (hope that's good enough for now).
                 */
                if (pVM->pgm.s.fPciPassthrough)
                {
                    int rc = VMMR3CallR0(pVM, VMMR0_DO_PGM_PHYS_SETUP_IOMMU, 0, NULL);
                    AssertRCReturn(rc, rc);
                }
            }
#else
            AssertLogRelReturn(!pVM->pgm.s.fPciPassthrough, VERR_PGM_PCI_PASSTHRU_MISCONFIG);
#endif
            break;

        default:
            /* shut up gcc */
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this component.
 *
 * This function will be called at init and whenever the VMM need to relocate it
 * self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3DECL(void) PGMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    LogFlow(("PGMR3Relocate: offDelta=%RGv\n", offDelta));

    /*
     * Paging stuff.
     */

    /* Shadow, guest and both mode switch & relocation for each VCPU. */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU  pVCpu = pVM->apCpusR3[i];

        uintptr_t idxShw = pVCpu->pgm.s.idxShadowModeData;
        if (   idxShw < RT_ELEMENTS(g_aPgmShadowModeData)
            && g_aPgmShadowModeData[idxShw].pfnRelocate)
            g_aPgmShadowModeData[idxShw].pfnRelocate(pVCpu, offDelta);
        else
            AssertFailed();

        uintptr_t const idxGst = pVCpu->pgm.s.idxGuestModeData;
        if (   idxGst < RT_ELEMENTS(g_aPgmGuestModeData)
            && g_aPgmGuestModeData[idxGst].pfnRelocate)
            g_aPgmGuestModeData[idxGst].pfnRelocate(pVCpu, offDelta);
        else
            AssertFailed();
    }

    /*
     * The page pool.
     */
    pgmR3PoolRelocate(pVM);
}


/**
 * Resets a virtual CPU when unplugged.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMR3DECL(void) PGMR3ResetCpu(PVM pVM, PVMCPU pVCpu)
{
    uintptr_t const idxGst = pVCpu->pgm.s.idxGuestModeData;
    if (   idxGst < RT_ELEMENTS(g_aPgmGuestModeData)
        && g_aPgmGuestModeData[idxGst].pfnExit)
    {
        int rc = g_aPgmGuestModeData[idxGst].pfnExit(pVCpu);
        AssertReleaseRC(rc);
    }
    pVCpu->pgm.s.GCPhysCR3 = NIL_RTGCPHYS;
    pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
    pVCpu->pgm.s.GCPhysPaeCR3 = NIL_RTGCPHYS;

    int rc = PGMHCChangeMode(pVM, pVCpu, PGMMODE_REAL, false /* fForce */);
    AssertReleaseRC(rc);

    STAM_REL_COUNTER_RESET(&pVCpu->pgm.s.cGuestModeChanges);

    pgmR3PoolResetUnpluggedCpu(pVM, pVCpu);

    /*
     * Re-init other members.
     */
    pVCpu->pgm.s.fA20Enabled = true;
    pVCpu->pgm.s.GCPhysA20Mask = ~((RTGCPHYS)!pVCpu->pgm.s.fA20Enabled << 20);

    /*
     * Clear the FFs PGM owns.
     */
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);
}


/**
 * The VM is being reset.
 *
 * For the PGM component this means that any PD write monitors
 * needs to be removed.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) PGMR3Reset(PVM pVM)
{
    LogFlow(("PGMR3Reset:\n"));
    VM_ASSERT_EMT(pVM);

    PGM_LOCK_VOID(pVM);

    /*
     * Exit the guest paging mode before the pgm pool gets reset.
     * Important to clean up the amd64 case.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU          pVCpu  = pVM->apCpusR3[i];
        uintptr_t const idxGst = pVCpu->pgm.s.idxGuestModeData;
        if (   idxGst < RT_ELEMENTS(g_aPgmGuestModeData)
            && g_aPgmGuestModeData[idxGst].pfnExit)
        {
            int rc = g_aPgmGuestModeData[idxGst].pfnExit(pVCpu);
            AssertReleaseRC(rc);
        }
        pVCpu->pgm.s.GCPhysCR3 = NIL_RTGCPHYS;
        pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
    }

#ifdef DEBUG
    DBGFR3_INFO_LOG_SAFE(pVM, "mappings", NULL);
    DBGFR3_INFO_LOG_SAFE(pVM, "handlers", "all nostat");
#endif

    /*
     * Switch mode back to real mode. (Before resetting the pgm pool!)
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU  pVCpu = pVM->apCpusR3[i];

        int rc = PGMHCChangeMode(pVM, pVCpu, PGMMODE_REAL, false /* fForce */);
        AssertReleaseRC(rc);

        STAM_REL_COUNTER_RESET(&pVCpu->pgm.s.cGuestModeChanges);
        STAM_REL_COUNTER_RESET(&pVCpu->pgm.s.cA20Changes);
    }

    /*
     * Reset the shadow page pool.
     */
    pgmR3PoolReset(pVM);

    /*
     * Re-init various other members and clear the FFs that PGM owns.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];

        pVCpu->pgm.s.fGst32BitPageSizeExtension = false;
        PGMNotifyNxeChanged(pVCpu, false);

        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);

#if !defined(VBOX_VMM_TARGET_ARMV8)
        if (!pVCpu->pgm.s.fA20Enabled)
        {
            pVCpu->pgm.s.fA20Enabled = true;
            pVCpu->pgm.s.GCPhysA20Mask = ~((RTGCPHYS)!pVCpu->pgm.s.fA20Enabled << 20);
# ifdef PGM_WITH_A20
            VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
            pgmR3RefreshShadowModeAfterA20Change(pVCpu);
            HMFlushTlb(pVCpu);
# endif
        }
#endif
    }

    //pgmLogState(pVM);
    PGM_UNLOCK(pVM);
}


/**
 * Memory setup after VM construction or reset.
 *
 * @param   pVM         The cross context VM structure.
 * @param   fAtReset    Indicates the context, after reset if @c true or after
 *                      construction if @c false.
 */
VMMR3_INT_DECL(void) PGMR3MemSetup(PVM pVM, bool fAtReset)
{
    if (fAtReset)
    {
        PGM_LOCK_VOID(pVM);

        int rc = pgmR3PhysRamZeroAll(pVM);
        AssertReleaseRC(rc);

        rc = pgmR3PhysRomReset(pVM);
        AssertReleaseRC(rc);

        PGM_UNLOCK(pVM);
    }
}


#ifdef VBOX_STRICT
/**
 * VM state change callback for clearing fNoMorePhysWrites after
 * a snapshot has been created.
 */
static DECLCALLBACK(void) pgmR3ResetNoMorePhysWritesFlag(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState,
                                                         VMSTATE enmOldState, void *pvUser)
{
    if (   enmState == VMSTATE_RUNNING
        || enmState == VMSTATE_RESUMING)
        pUVM->pVM->pgm.s.fNoMorePhysWrites = false;
    RT_NOREF(pVMM, enmOldState, pvUser);
}
#endif

/**
 * Private API to reset fNoMorePhysWrites.
 */
VMMR3_INT_DECL(void) PGMR3ResetNoMorePhysWritesFlag(PVM pVM)
{
    pVM->pgm.s.fNoMorePhysWrites = false;
}

/**
 * Terminates the PGM.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(int) PGMR3Term(PVM pVM)
{
    /* Must free shared pages here. */
    PGM_LOCK_VOID(pVM);
    pgmR3PhysRamTerm(pVM);
    pgmR3PhysRomTerm(pVM);
    PGM_UNLOCK(pVM);

    PGMDeregisterStringFormatTypes();
    return PDMR3CritSectDelete(pVM, &pVM->pgm.s.CritSectX);
}


/**
 * Show paging mode.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     "all" (default), "guest", "shadow" or "host".
 */
static DECLCALLBACK(void) pgmR3InfoMode(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /* digest argument. */
    bool fGuest, fShadow, fHost;
    if (pszArgs)
        pszArgs = RTStrStripL(pszArgs);
    if (!pszArgs || !*pszArgs || strstr(pszArgs, "all"))
        fShadow = fHost = fGuest = true;
    else
    {
        fShadow = fHost = fGuest = false;
        if (strstr(pszArgs, "guest"))
            fGuest = true;
        if (strstr(pszArgs, "shadow"))
            fShadow = true;
        if (strstr(pszArgs, "host"))
            fHost = true;
    }

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];


    /* print info. */
    if (fGuest)
    {
        pHlp->pfnPrintf(pHlp, "Guest paging mode (VCPU #%u):  %s (changed %RU64 times), A20 %s (changed %RU64 times)\n",
                        pVCpu->idCpu, PGMGetModeName(pVCpu->pgm.s.enmGuestMode), pVCpu->pgm.s.cGuestModeChanges.c,
                        pVCpu->pgm.s.fA20Enabled ? "enabled" : "disabled", pVCpu->pgm.s.cA20Changes.c);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        if (pVCpu->pgm.s.enmGuestSlatMode != PGMSLAT_INVALID)
            pHlp->pfnPrintf(pHlp, "Guest SLAT mode (VCPU #%u): %s\n", pVCpu->idCpu,
                            PGMGetSlatModeName(pVCpu->pgm.s.enmGuestSlatMode));
#endif
    }
    if (fShadow)
        pHlp->pfnPrintf(pHlp, "Shadow paging mode (VCPU #%u): %s\n", pVCpu->idCpu, PGMGetModeName(pVCpu->pgm.s.enmShadowMode));
    if (fHost)
    {
        const char *psz;
        switch (pVM->pgm.s.enmHostMode)
        {
            case SUPPAGINGMODE_INVALID:             psz = "invalid"; break;
            case SUPPAGINGMODE_32_BIT:              psz = "32-bit"; break;
            case SUPPAGINGMODE_32_BIT_GLOBAL:       psz = "32-bit+G"; break;
            case SUPPAGINGMODE_PAE:                 psz = "PAE"; break;
            case SUPPAGINGMODE_PAE_GLOBAL:          psz = "PAE+G"; break;
            case SUPPAGINGMODE_PAE_NX:              psz = "PAE+NX"; break;
            case SUPPAGINGMODE_PAE_GLOBAL_NX:       psz = "PAE+G+NX"; break;
            case SUPPAGINGMODE_AMD64:               psz = "AMD64"; break;
            case SUPPAGINGMODE_AMD64_GLOBAL:        psz = "AMD64+G"; break;
            case SUPPAGINGMODE_AMD64_NX:            psz = "AMD64+NX"; break;
            case SUPPAGINGMODE_AMD64_GLOBAL_NX:     psz = "AMD64+G+NX"; break;
            default:                                psz = "unknown"; break;
        }
        pHlp->pfnPrintf(pHlp, "Host paging mode:             %s\n", psz);
    }
}


/**
 * Display the RAM range info.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) pgmR3PhysInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    bool const fVerbose = pszArgs && strstr(pszArgs, "verbose") != NULL;

    pHlp->pfnPrintf(pHlp,
                    "RAM ranges (pVM=%p)\n"
                    "%.*s %.*s\n",
                    pVM,
                    sizeof(RTGCPHYS) * 4 + 1, "GC Phys Range                    ",
                    sizeof(RTHCPTR) * 2,      "pbR3            ");

    /*
     * Traverse the lookup table so we only display mapped MMIO and get it in sorted order.
     */
    uint32_t const cRamRangeLookupEntries = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries,
                                                   RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    for (uint32_t idxLookup = 0; idxLookup < cRamRangeLookupEntries; idxLookup++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        AssertContinue(idRamRange < RT_ELEMENTS(pVM->pgm.s.apRamRanges));
        PPGMRAMRANGE const pCur = pVM->pgm.s.apRamRanges[idRamRange];
        if (pCur != NULL)   { /*likely*/ }
        else                continue;

        pHlp->pfnPrintf(pHlp,
                        "%RGp-%RGp %RHv %s\n",
                        pCur->GCPhys,
                        pCur->GCPhysLast,
                        pCur->pbR3,
                        pCur->pszDesc);
        if (fVerbose)
        {
            RTGCPHYS const cPages = pCur->cb >> X86_PAGE_SHIFT;
            RTGCPHYS iPage = 0;
            while (iPage < cPages)
            {
                RTGCPHYS const    iFirstPage = iPage;
                PGMPAGETYPE const enmType    = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]);
                do
                    iPage++;
                while (iPage < cPages && (PGMPAGETYPE)PGM_PAGE_GET_TYPE(&pCur->aPages[iPage]) == enmType);
                const char *pszType;
                const char *pszMore = NULL;
                switch (enmType)
                {
                    case PGMPAGETYPE_RAM:
                        pszType = "RAM";
                        break;

                    case PGMPAGETYPE_MMIO2:
                        pszType = "MMIO2";
                        break;

                    case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                        pszType = "MMIO2-alias-MMIO";
                        break;

                    case PGMPAGETYPE_SPECIAL_ALIAS_MMIO:
                        pszType = "special-alias-MMIO";
                        break;

                    case PGMPAGETYPE_ROM_SHADOW:
                    case PGMPAGETYPE_ROM:
                    {
                        pszType = enmType == PGMPAGETYPE_ROM_SHADOW ? "ROM-shadowed" : "ROM";

                        RTGCPHYS const GCPhysFirstPg = iFirstPage << GUEST_PAGE_SHIFT;
                        uint32_t const cRomRanges    = RT_MIN(pVM->pgm.s.cRomRanges, RT_ELEMENTS(pVM->pgm.s.apRomRanges));
                        for (uint32_t idxRom = 0; idxRom < cRomRanges; idxRom++)
                        {
                            PPGMROMRANGE const pRomRange = pVM->pgm.s.apRomRanges[idxRom];
                            if (   pRomRange
                                && GCPhysFirstPg <  pRomRange->GCPhysLast
                                && GCPhysFirstPg >= pRomRange->GCPhys)
                            {
                                pszMore = pRomRange->pszDesc;
                                break;
                            }
                        }
                        break;
                    }

                    case PGMPAGETYPE_MMIO:
                    {
                        pszType = "MMIO";
                        PGM_LOCK_VOID(pVM);
                        PPGMPHYSHANDLER pHandler;
                        int rc = pgmHandlerPhysicalLookup(pVM, iFirstPage * X86_PAGE_SIZE, &pHandler);
                        if (RT_SUCCESS(rc))
                            pszMore = pHandler->pszDesc;
                        PGM_UNLOCK(pVM);
                        break;
                    }

                    case PGMPAGETYPE_INVALID:
                        pszType = "invalid";
                        break;

                    default:
                        pszType = "bad";
                        break;
                }
                if (pszMore)
                    pHlp->pfnPrintf(pHlp, "    %RGp-%RGp %-20s %s\n",
                                    pCur->GCPhys + iFirstPage * X86_PAGE_SIZE,
                                    pCur->GCPhys + iPage      * X86_PAGE_SIZE - 1,
                                    pszType, pszMore);
                else
                    pHlp->pfnPrintf(pHlp, "    %RGp-%RGp %s\n",
                                    pCur->GCPhys + iFirstPage * X86_PAGE_SIZE,
                                    pCur->GCPhys + iPage      * X86_PAGE_SIZE - 1,
                                    pszType);

            }
        }
    }
}


/**
 * Dump the page directory to the log.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) pgmR3InfoCr3(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /** @todo SMP support!! */
    PVMCPU pVCpu = pVM->apCpusR3[0];

/** @todo fix this! Convert the PGMR3DumpHierarchyHC functions to do guest stuff. */
    /* Big pages supported? */
    const bool fPSE  = !!(CPUMGetGuestCR4(pVCpu) & X86_CR4_PSE);

    /* Global pages supported? */
    const bool fPGE = !!(CPUMGetGuestCR4(pVCpu) & X86_CR4_PGE);

    NOREF(pszArgs);

    /*
     * Get page directory addresses.
     */
    PGM_LOCK_VOID(pVM);
    PX86PD     pPDSrc = pgmGstGet32bitPDPtr(pVCpu);
    Assert(pPDSrc);

    /*
     * Iterate the page directory.
     */
    for (unsigned iPD = 0; iPD < RT_ELEMENTS(pPDSrc->a); iPD++)
    {
        X86PDE PdeSrc = pPDSrc->a[iPD];
        if (PdeSrc.u & X86_PDE_P)
        {
            if ((PdeSrc.u & X86_PDE_PS) && fPSE)
                pHlp->pfnPrintf(pHlp,
                                "%04X - %RGp P=%d U=%d RW=%d G=%d - BIG\n",
                                iPD,
                                pgmGstGet4MBPhysPage(pVM, PdeSrc), PdeSrc.u & X86_PDE_P, !!(PdeSrc.u & X86_PDE_US),
                                !!(PdeSrc.u & X86_PDE_RW), (PdeSrc.u & X86_PDE4M_G) && fPGE);
            else
                pHlp->pfnPrintf(pHlp,
                                "%04X - %RGp P=%d U=%d RW=%d [G=%d]\n",
                                iPD,
                                (RTGCPHYS)(PdeSrc.u & X86_PDE_PG_MASK), PdeSrc.u & X86_PDE_P, !!(PdeSrc.u & X86_PDE_US),
                                !!(PdeSrc.u & X86_PDE_RW), (PdeSrc.u & X86_PDE4M_G) && fPGE);
        }
    }
    PGM_UNLOCK(pVM);
}


/**
 * Called by pgmPoolFlushAllInt prior to flushing the pool.
 *
 * @returns VBox status code, fully asserted.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
int pgmR3ExitShadowModeBeforePoolFlush(PVMCPU pVCpu)
{
    /* Unmap the old CR3 value before flushing everything. */
    int       rc     = VINF_SUCCESS;
    uintptr_t idxBth = pVCpu->pgm.s.idxBothModeData;
    if (   idxBth < RT_ELEMENTS(g_aPgmBothModeData)
        && g_aPgmBothModeData[idxBth].pfnUnmapCR3)
    {
        rc = g_aPgmBothModeData[idxBth].pfnUnmapCR3(pVCpu);
        AssertRC(rc);
    }

    /* Exit the current shadow paging mode as well; nested paging and EPT use a root CR3 which will get flushed here. */
    uintptr_t idxShw = pVCpu->pgm.s.idxShadowModeData;
    if (   idxShw < RT_ELEMENTS(g_aPgmShadowModeData)
        && g_aPgmShadowModeData[idxShw].pfnExit)
    {
        rc = g_aPgmShadowModeData[idxShw].pfnExit(pVCpu);
        AssertMsgRCReturn(rc, ("Exit failed for shadow mode %d: %Rrc\n", pVCpu->pgm.s.enmShadowMode, rc), rc);
    }

    Assert(pVCpu->pgm.s.pShwPageCR3R3 == NULL);
    return rc;
}


/**
 * Called by pgmPoolFlushAllInt after flushing the pool.
 *
 * @returns VBox status code, fully asserted.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
int pgmR3ReEnterShadowModeAfterPoolFlush(PVM pVM, PVMCPU pVCpu)
{
    pVCpu->pgm.s.enmShadowMode = PGMMODE_INVALID;
    int rc = PGMHCChangeMode(pVM, pVCpu, PGMGetGuestMode(pVCpu), false /* fForce */);
    Assert(VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3));
    AssertRCReturn(rc, rc);
    AssertRCSuccessReturn(rc, VERR_IPE_UNEXPECTED_INFO_STATUS);

    Assert(pVCpu->pgm.s.pShwPageCR3R3 != NULL || pVCpu->pgm.s.enmShadowMode == PGMMODE_NONE);
    AssertMsg(   pVCpu->pgm.s.enmShadowMode >= PGMMODE_NESTED_32BIT
              || CPUMGetHyperCR3(pVCpu) == PGMGetHyperCR3(pVCpu),
              ("%RHp != %RHp %s\n", (RTHCPHYS)CPUMGetHyperCR3(pVCpu), PGMGetHyperCR3(pVCpu), PGMGetModeName(pVCpu->pgm.s.enmShadowMode)));
    return rc;
}


/**
 * Called by PGMR3PhysSetA20 after changing the A20 state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
void pgmR3RefreshShadowModeAfterA20Change(PVMCPU pVCpu)
{
    /** @todo Probably doing a bit too much here. */
    int rc = pgmR3ExitShadowModeBeforePoolFlush(pVCpu);
    AssertReleaseRC(rc);
    rc = pgmR3ReEnterShadowModeAfterPoolFlush(pVCpu->CTX_SUFF(pVM), pVCpu);
    AssertReleaseRC(rc);
}


#ifdef VBOX_WITH_DEBUGGER

/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmerror' and '.pgmerroroff' commands.}
 */
static DECLCALLBACK(int)  pgmR3CmdError(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    PVM pVM = pUVM->pVM;
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 0 || (cArgs == 1 && paArgs[0].enmType == DBGCVAR_TYPE_STRING));

    if (!cArgs)
    {
        /*
         * Print the list of error injection locations with status.
         */
        DBGCCmdHlpPrintf(pCmdHlp, "PGM error inject locations:\n");
        DBGCCmdHlpPrintf(pCmdHlp, "  handy - %RTbool\n", pVM->pgm.s.fErrInjHandyPages);
    }
    else
    {
        /*
         * String switch on where to inject the error.
         */
        bool const  fNewState = !strcmp(pCmd->pszCmd, "pgmerror");
        const char *pszWhere = paArgs[0].u.pszString;
        if (!strcmp(pszWhere, "handy"))
            ASMAtomicWriteBool(&pVM->pgm.s.fErrInjHandyPages, fNewState);
        else
            return DBGCCmdHlpPrintf(pCmdHlp, "error: Invalid 'where' value: %s.\n", pszWhere);
        DBGCCmdHlpPrintf(pCmdHlp, "done\n");
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmsync' command.}
 */
static DECLCALLBACK(int) pgmR3CmdSync(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, DBGCCmdHlpGetCurrentCpu(pCmdHlp));
    if (!pVCpu)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid CPU ID");

    /*
     * Force page directory sync.
     */
    VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);

    int rc = DBGCCmdHlpPrintf(pCmdHlp, "Forcing page directory sync.\n");
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}

#ifdef VBOX_STRICT

/**
 * EMT callback for pgmR3CmdAssertCR3.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   pcErrors    Where to return the error count.
 */
static DECLCALLBACK(int) pgmR3CmdAssertCR3EmtWorker(PUVM pUVM, unsigned *pcErrors)
{
    PVM     pVM   = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    PVMCPU  pVCpu = VMMGetCpu(pVM);

    *pcErrors = PGMAssertCR3(pVM, pVCpu, CPUMGetGuestCR3(pVCpu), CPUMGetGuestCR4(pVCpu));

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmassertcr3' command.}
 */
static DECLCALLBACK(int) pgmR3CmdAssertCR3(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);

    int rc = DBGCCmdHlpPrintf(pCmdHlp, "Checking shadow CR3 page tables for consistency.\n");
    if (RT_FAILURE(rc))
        return rc;

    unsigned cErrors = 0;
    rc = VMR3ReqCallWaitU(pUVM, DBGCCmdHlpGetCurrentCpu(pCmdHlp), (PFNRT)pgmR3CmdAssertCR3EmtWorker, 2, pUVM, &cErrors);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "VMR3ReqCallWaitU failed: %Rrc", rc);
    if (cErrors > 0)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "PGMAssertCR3: %u error(s)", cErrors);
    return DBGCCmdHlpPrintf(pCmdHlp, "PGMAssertCR3: OK\n");
}

#endif /* VBOX_STRICT */

/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmsyncalways' command.}
 */
static DECLCALLBACK(int) pgmR3CmdSyncAlways(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, DBGCCmdHlpGetCurrentCpu(pCmdHlp));
    if (!pVCpu)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid CPU ID");

    /*
     * Force page directory sync.
     */
    int rc;
    if (pVCpu->pgm.s.fSyncFlags & PGM_SYNC_ALWAYS)
    {
        ASMAtomicAndU32(&pVCpu->pgm.s.fSyncFlags, ~PGM_SYNC_ALWAYS);
        rc = DBGCCmdHlpPrintf(pCmdHlp, "Disabled permanent forced page directory syncing.\n");
    }
    else
    {
        ASMAtomicOrU32(&pVCpu->pgm.s.fSyncFlags, PGM_SYNC_ALWAYS);
        VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
        rc = DBGCCmdHlpPrintf(pCmdHlp, "Enabled permanent forced page directory syncing.\n");
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmphystofile' command.}
 */
static DECLCALLBACK(int) pgmR3CmdPhysToFile(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /*
     * Validate input.
     */
    NOREF(pCmd);
    DBGC_CMDHLP_REQ_UVM_RET(pCmdHlp, pCmd, pUVM);
    PVM pVM = pUVM->pVM;
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 1 || cArgs == 2);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, paArgs[0].enmType == DBGCVAR_TYPE_STRING);
    if (cArgs == 2)
    {
        DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 1, paArgs[1].enmType == DBGCVAR_TYPE_STRING);
        if (strcmp(paArgs[1].u.pszString, "nozero"))
            return DBGCCmdHlpFail(pCmdHlp, pCmd, "Invalid 2nd argument '%s', must be 'nozero'.\n", paArgs[1].u.pszString);
    }
    bool fIncZeroPgs = cArgs < 2;

    /*
     * Open the output file and get the ram parameters.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, paArgs[0].u.pszString, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpPrintf(pCmdHlp, "error: RTFileOpen(,'%s',) -> %Rrc.\n", paArgs[0].u.pszString, rc);

    uint32_t cbRamHole = 0;
    CFGMR3QueryU32Def(CFGMR3GetRootU(pUVM), "RamHoleSize", &cbRamHole, MM_RAM_HOLE_SIZE_DEFAULT);
    uint64_t cbRam     = 0;
    CFGMR3QueryU64Def(CFGMR3GetRootU(pUVM), "RamSize", &cbRam, 0);
    RTGCPHYS GCPhysEnd = cbRam + cbRamHole;

    /*
     * Dump the physical memory, page by page.
     */
    RTGCPHYS    GCPhys = 0;
    char        abZeroPg[GUEST_PAGE_SIZE];
    RT_ZERO(abZeroPg);

    PGM_LOCK_VOID(pVM);

    uint32_t const cRamRangeLookupEntries = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries,
                                                   RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    for (uint32_t idxLookup = 0; idxLookup < cRamRangeLookupEntries && RT_SUCCESS(rc); idxLookup++)
    {
        if (PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]) >= GCPhysEnd)
            break;
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        AssertContinue(idRamRange < RT_ELEMENTS(pVM->pgm.s.apRamRanges));
        PPGMRAMRANGE const pRam = pVM->pgm.s.apRamRanges[idRamRange];
        AssertContinue(pRam);
        Assert(pRam->GCPhys == PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]));

        /* fill the gap */
        if (pRam->GCPhys > GCPhys && fIncZeroPgs)
        {
            while (pRam->GCPhys > GCPhys && RT_SUCCESS(rc))
            {
                rc = RTFileWrite(hFile, abZeroPg, GUEST_PAGE_SIZE, NULL);
                GCPhys += GUEST_PAGE_SIZE;
            }
        }

        PCPGMPAGE pPage = &pRam->aPages[0];
        while (GCPhys < pRam->GCPhysLast && RT_SUCCESS(rc))
        {
            if (    PGM_PAGE_IS_ZERO(pPage)
                ||  PGM_PAGE_IS_BALLOONED(pPage))
            {
                if (fIncZeroPgs)
                {
                    rc = RTFileWrite(hFile, abZeroPg, GUEST_PAGE_SIZE, NULL);
                    if (RT_FAILURE(rc))
                        DBGCCmdHlpPrintf(pCmdHlp, "error: RTFileWrite -> %Rrc at GCPhys=%RGp.\n", rc, GCPhys);
                }
            }
            else
            {
                switch (PGM_PAGE_GET_TYPE(pPage))
                {
                    case PGMPAGETYPE_RAM:
                    case PGMPAGETYPE_ROM_SHADOW: /* trouble?? */
                    case PGMPAGETYPE_ROM:
                    case PGMPAGETYPE_MMIO2:
                    {
                        void const     *pvPage;
                        PGMPAGEMAPLOCK  Lock;
                        rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhys, &pvPage, &Lock);
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTFileWrite(hFile, pvPage, GUEST_PAGE_SIZE, NULL);
                            PGMPhysReleasePageMappingLock(pVM, &Lock);
                            if (RT_FAILURE(rc))
                                DBGCCmdHlpPrintf(pCmdHlp, "error: RTFileWrite -> %Rrc at GCPhys=%RGp.\n", rc, GCPhys);
                        }
                        else
                            DBGCCmdHlpPrintf(pCmdHlp, "error: PGMPhysGCPhys2CCPtrReadOnly -> %Rrc at GCPhys=%RGp.\n", rc, GCPhys);
                        break;
                    }

                    default:
                        AssertFailed();
                        RT_FALL_THRU();
                    case PGMPAGETYPE_MMIO:
                    case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                    case PGMPAGETYPE_SPECIAL_ALIAS_MMIO:
                        if (fIncZeroPgs)
                        {
                            rc = RTFileWrite(hFile, abZeroPg, GUEST_PAGE_SIZE, NULL);
                            if (RT_FAILURE(rc))
                                DBGCCmdHlpPrintf(pCmdHlp, "error: RTFileWrite -> %Rrc at GCPhys=%RGp.\n", rc, GCPhys);
                        }
                        break;
                }
            }


            /* advance */
            GCPhys += GUEST_PAGE_SIZE;
            pPage++;
        }
    }
    PGM_UNLOCK(pVM);

    RTFileClose(hFile);
    if (RT_SUCCESS(rc))
        return DBGCCmdHlpPrintf(pCmdHlp, "Successfully saved physical memory to '%s'.\n", paArgs[0].u.pszString);
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_DEBUGGER */

/**
 * pvUser argument of the pgmR3CheckIntegrity*Node callbacks.
 */
typedef struct PGMCHECKINTARGS
{
    bool                    fLeftToRight;    /**< true: left-to-right; false: right-to-left. */
    uint32_t                cErrors;
    PPGMPHYSHANDLER         pPrevPhys;
    PVM                     pVM;
} PGMCHECKINTARGS, *PPGMCHECKINTARGS;

/**
 * Validate a node in the physical handler tree.
 *
 * @returns 0 on if ok, other wise 1.
 * @param   pNode       The handler node.
 * @param   pvUser      pVM.
 */
static DECLCALLBACK(int) pgmR3CheckIntegrityPhysHandlerNode(PPGMPHYSHANDLER pNode, void *pvUser)
{
    PPGMCHECKINTARGS pArgs = (PPGMCHECKINTARGS)pvUser;

    AssertLogRelMsgReturnStmt(!((uintptr_t)pNode & 7), ("pNode=%p\n", pNode), pArgs->cErrors++, VERR_INVALID_POINTER);

    AssertLogRelMsgStmt(pNode->Key <= pNode->KeyLast,
                        ("pNode=%p %RGp-%RGp %s\n", pNode, pNode->Key, pNode->KeyLast, pNode->pszDesc),
                        pArgs->cErrors++);

    AssertLogRelMsgStmt(   !pArgs->pPrevPhys
                        || (  pArgs->fLeftToRight
                            ? pArgs->pPrevPhys->KeyLast < pNode->Key
                            : pArgs->pPrevPhys->KeyLast > pNode->Key),
                        ("pPrevPhys=%p %RGp-%RGp %s\n"
                         "    pNode=%p %RGp-%RGp %s\n",
                         pArgs->pPrevPhys, pArgs->pPrevPhys->Key, pArgs->pPrevPhys->KeyLast, pArgs->pPrevPhys->pszDesc,
                         pNode, pNode->Key, pNode->KeyLast, pNode->pszDesc),
                        pArgs->cErrors++);

    pArgs->pPrevPhys = pNode;
    return 0;
}


/**
 * Perform an integrity check on the PGM component.
 *
 * @returns VINF_SUCCESS if everything is fine.
 * @returns VBox error status after asserting on integrity breach.
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(int) PGMR3CheckIntegrity(PVM pVM)
{
    /*
     * Check the trees.
     */
    PGMCHECKINTARGS Args = { true, 0, NULL, pVM };
    int rc = pVM->pgm.s.pPhysHandlerTree->doWithAllFromLeft(&pVM->pgm.s.PhysHandlerAllocator,
                                                            pgmR3CheckIntegrityPhysHandlerNode, &Args);
    AssertLogRelRCReturn(rc, rc);

    Args.fLeftToRight = false;
    Args.pPrevPhys    = NULL;
    rc = pVM->pgm.s.pPhysHandlerTree->doWithAllFromRight(&pVM->pgm.s.PhysHandlerAllocator,
                                                         pgmR3CheckIntegrityPhysHandlerNode, &Args);
    AssertLogRelMsgReturn(pVM->pgm.s.pPhysHandlerTree->m_cErrors == 0,
                          ("m_cErrors=%#x\n", pVM->pgm.s.pPhysHandlerTree->m_cErrors == 0),
                          VERR_INTERNAL_ERROR);

    return Args.cErrors == 0 ? VINF_SUCCESS : VERR_INTERNAL_ERROR;
}

