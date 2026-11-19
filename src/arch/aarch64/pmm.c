#include "pmm.h"
#include "console.h"
#include <stddef.h>
#include "platform.h"
#include "dma_pool.h"

static struct dma_pool g_dma;
static int g_dma_init;

static uintptr_t arm_pmm_curr = SOMA_PMM_BASE;
static size_t arm_frames_used = 0;
static const size_t arm_total_frames = 49152; /* 192 MiB de frames libres */

void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr)
{
    (void)mb_magic; (void)mb_info_addr;
    arm_pmm_curr = SOMA_PMM_BASE;
    arm_frames_used = 0;
}

uintptr_t pmm_alloc_frame(void)
{
    if (arm_frames_used >= arm_total_frames) return 0;
    uintptr_t frame = arm_pmm_curr;
    arm_pmm_curr += PMM_FRAME_SIZE;
    arm_frames_used++;
    return frame;
}

/* PARCHE 062: marcos contiguos (el reparto es lineal, asi que basta con avanzar). Los usa el controlador xHCI para su DMA. */
uintptr_t pmm_alloc_contig(size_t nframes)
{
    if (!nframes || arm_frames_used + nframes > arm_total_frames) return 0;
    uintptr_t base = arm_pmm_curr;
    arm_pmm_curr += nframes * PMM_FRAME_SIZE;
    arm_frames_used += nframes;
    return base;
}

uintptr_t pmm_alloc_dma(size_t nframes)
{
    if (!g_dma_init) { dma_pool_init(&g_dma, SOMA_DMA_POOL_BASE, SOMA_DMA_POOL_SIZE); g_dma_init = 1; }
    uintptr_t p = dma_pool_alloc(&g_dma, nframes);
    return p ? p : pmm_alloc_contig(nframes);
}

void pmm_free_frame(uintptr_t phys_addr)
{
    (void)phys_addr;
    if (arm_frames_used > 0) arm_frames_used--;
}

void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames)
{
    if (total_frames) *total_frames = arm_total_frames;
    if (used_frames)  *used_frames  = arm_frames_used;
    if (free_frames)  *free_frames  = (arm_total_frames > arm_frames_used) ? (arm_total_frames - arm_frames_used) : 0;
}

void pmm_dump_stats(void) {}
int pmm_test_self(void) { return 1; }

int pmm_bench(void) { kprint("[PMMBENCH] no aplica en AArch64 (asignador lineal)\n"); return 1; }
