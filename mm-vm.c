/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/* Forward declaration for the ram mapping helper defined in mm64.c */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum,
                  struct vm_rg_struct *ret_rg);

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node_at_brk - get vm area node at break pointer
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: raw requested size (bytes)
 *@alignedsz: page-aligned size computed by caller (used to advance sbrk)
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid,
                                              addr_t size, addr_t alignedsz)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (cur_vma == NULL)
    return NULL;

  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL)
    return NULL;

  
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end   = newrg->rg_start + size;   /* [FIX 1] dùng size, không phải alignedsz */
  newrg->rg_next  = NULL;

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: proposed start of range
 *@vmaend:   proposed end   of range
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid,
                              addr_t vmastart, addr_t vmaend)
{
  if (vmastart >= vmaend)
    return -1;

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL)
    return -1;

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  while (vma != NULL)
  {
    if (vma != cur_area)
    {
      /* Two ranges [x1,x2) and [y1,y2) overlap iff x1<y2 && y1<x2 */
      if (vmastart < vma->vm_end && vma->vm_start < vmaend)
        return -1;
    }
    vma = vma->vm_next;
  }
  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size (raw bytes)
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  addr_t inc_amt    = PAGING_PAGE_ALIGNSZ(inc_sz);
  int    incnumpage = (int)(inc_amt / PAGING_PAGESZ);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  addr_t old_end = cur_vma->vm_end;
  addr_t new_end = old_end + inc_amt;

  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL)
    return -1;

  if (validate_overlap_vm_area(caller, vmaid,
                               cur_vma->vm_start,
                               new_end) < 0)
  {
    free(newrg);
    return -1; /* overlap detected, refuse extension */
  }

  if (vm_map_ram(caller, old_end, new_end,
                 old_end, incnumpage, newrg) < 0)
  {
    free(newrg);
    return -1; /* physical mapping failed */
  }

  /* Commit: extend vm_end */
  cur_vma->vm_end = new_end;

  cur_vma->sbrk = new_end;   

  free(newrg);
  return 0;
}

// #endif