/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm64.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/* ═══════════════════════════════════════════════════════════════════
 * [STUDENT WORK] HELPER: pte_lookup
 *
 * Mục đích: Duyệt cây bảng trang 5 cấp (PGD→P4D→PUD→PMD→PT) để tìm
 * hoặc tạo con trỏ tới PTE thực sự của page number `pgn`.
 *
 * Logic "Demand Allocation":
 *   - init_mm chỉ cấp phát mm->pgd (512 entries × 8B = 4KB).
 *   - Các cấp P4D/PUD/PMD/PT để NULL ban đầu.
 *   - Khi alloc=1: nếu entry của cấp trên == 0 (chưa có bảng cấp dưới),
 *     malloc 512-entry table cho đúng nhánh đó, lưu địa chỉ vào entry.
 *   - Khi alloc=0: chỉ đọc, trả NULL nếu nhánh chưa tồn tại.
 *
 * Encoding: entry = (addr_t)(uintptr_t)(pointer_to_sub_table)
 *           Vì addr_t = uint64_t (8 bytes) đủ chứa pointer 64-bit.
 *           Giải mã: sub_table = (addr_t *)(uintptr_t)(entry)
 * ═══════════════════════════════════════════════════════════════════ */
#define PAGING64_LEVEL_SZ 512

static addr_t *pte_lookup(struct mm_struct *mm, addr_t pgn, int alloc)
{
  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);

  if (mm->pgd == NULL) return NULL;

  /* Cấp 5 → 4: PGD → P4D */
  if (mm->pgd[pgd_i] == 0) {
    if (!alloc) return NULL;
    mm->pgd[pgd_i] =
      (addr_t)(uintptr_t)calloc(PAGING64_LEVEL_SZ, sizeof(addr_t));
    if (mm->pgd[pgd_i] == 0) return NULL;
  }
  addr_t *p4d_tbl = (addr_t *)(uintptr_t)mm->pgd[pgd_i];
  mm->p4d = p4d_tbl;

  /* Cấp 4 → 3: P4D → PUD */
  if (p4d_tbl[p4d_i] == 0) {
    if (!alloc) return NULL;
    p4d_tbl[p4d_i] =
      (addr_t)(uintptr_t)calloc(PAGING64_LEVEL_SZ, sizeof(addr_t));
    if (p4d_tbl[p4d_i] == 0) return NULL;
  }
  addr_t *pud_tbl = (addr_t *)(uintptr_t)p4d_tbl[p4d_i];
  mm->pud = pud_tbl;

  /* Cấp 3 → 2: PUD → PMD */
  if (pud_tbl[pud_i] == 0) {
    if (!alloc) return NULL;
    pud_tbl[pud_i] =
      (addr_t)(uintptr_t)calloc(PAGING64_LEVEL_SZ, sizeof(addr_t));
    if (pud_tbl[pud_i] == 0) return NULL;
  }
  addr_t *pmd_tbl = (addr_t *)(uintptr_t)pud_tbl[pud_i];
  mm->pmd = pmd_tbl;

  /* Cấp 2 → 1: PMD → PT (leaf – chứa PTE thực sự) */
  if (pmd_tbl[pmd_i] == 0) {
    if (!alloc) return NULL;
    pmd_tbl[pmd_i] =
      (addr_t)(uintptr_t)calloc(PAGING64_LEVEL_SZ, sizeof(addr_t));
    if (pmd_tbl[pmd_i] == 0) return NULL;
  }
  addr_t *pt_tbl = (addr_t *)(uintptr_t)pmd_tbl[pmd_i];
  mm->pt = pt_tbl;

  /* Trả về con trỏ tới PTE của trang pgn */
  return &pt_tbl[pt_i];
}

/* ═══════════════════════════════════════════════════════════════════
 * init_pte - Initialize PTE entry
 * [KHÔNG THAY ĐỔI – giữ nguyên code gốc]
 * ═══════════════════════════════════════════════════════════════════ */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * get_pd_from_address - Parse address to 5 page directory level
 * [STUDENT WORK] Implement the page directories mapping
 *
 * Logic: Dùng mask + shift (định nghĩa trong mm64.h) để bóc tách
 * từng nhóm bit của địa chỉ ảo 64-bit thành 5 chỉ số.
 * Bit layout (Bảng 3 tài liệu):
 *   PGD: bits 56-48, P4D: bits 47-39, PUD: bits 38-30,
 *   PMD: bits 29-21, PT:  bits 20-12, offset: bits 11-0
 * ═══════════════════════════════════════════════════════════════════ */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d,
                         addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* [STUDENT WORK] Bóc tách 5 chỉ số page directory từ địa chỉ ảo */
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt  = (addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT;

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * [KHÔNG THAY ĐỔI – giữ nguyên code gốc]
 * ═══════════════════════════════════════════════════════════════════ */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d,
                         addr_t* pud, addr_t* pmd, addr_t* pt)
{
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                              pgd, p4d, pud, pmd, pt);
}

/* ═══════════════════════════════════════════════════════════════════
 * pte_set_swap - Set PTE entry for swapped page
 * [STUDENT WORK] Thay thế dummy malloc bằng pte_lookup thực sự
 *
 * Logic:
 *   Gọi pte_lookup(alloc=1) để lấy con trỏ tới PTE thực sự trong
 *   cây bảng trang 5 cấp. Sau đó ghi thông tin swap vào PTE đó.
 *   Xoá PTE về 0 trước khi set để tránh bit rác từ lần trước.
 * ═══════════════════════════════════════════════════════════════════ */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  /* [STUDENT WORK] Dùng pte_lookup thay vì dummy malloc */
  addr_t *pte = pte_lookup(caller->krnl->mm, pgn, 1);
  if (pte == NULL) return -1;

  *pte = 0; /* xoá bit rác trước khi ghi */
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * pte_set_fpn - Set PTE entry for on-line page
 * [STUDENT WORK] Thay thế dummy malloc bằng pte_lookup thực sự
 *
 * Logic: Giống pte_set_swap nhưng ghi FPN thay vì swap info.
 *        CLRBIT SWAPPED để đánh dấu trang đang online trong RAM.
 * ═══════════════════════════════════════════════════════════════════ */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  /* [STUDENT WORK] Dùng pte_lookup thay vì dummy malloc */
  addr_t *pte = pte_lookup(caller->krnl->mm, pgn, 1);
  if (pte == NULL) return -1;

  *pte = 0; /* xoá bit rác trước khi ghi */
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * pte_get_entry - Get PTE page table entry
 * [STUDENT WORK] Duyệt cây bảng trang để đọc PTE thực sự
 *
 * Logic: Gọi pte_lookup(alloc=0) – chỉ đọc, không tạo bảng mới.
 *        Trả về 0 nếu trang chưa được map.
 * ═══════════════════════════════════════════════════════════════════ */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  /* [STUDENT WORK] Dùng pte_lookup thay vì local variable không dùng được */
  addr_t *pte = pte_lookup(caller->krnl->mm, pgn, 0);
  if (pte == NULL) return 0;
  return (uint32_t)(*pte);
}

/* ═══════════════════════════════════════════════════════════════════
 * pte_set_entry - Set PTE page table entry (raw value)
 * [STUDENT WORK] Dùng pte_lookup thay vì index thẳng pgd[pgn]
 *
 * Logic: Gọi pte_lookup(alloc=1) rồi ghi giá trị thô pte_val vào.
 *        Caller chịu trách nhiệm đảm bảo pte_val hợp lệ.
 * ═══════════════════════════════════════════════════════════════════ */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  /* [STUDENT WORK] Dùng pte_lookup thay vì krnl->mm->pgd[pgn] trực tiếp
   * (pgd trong MM64 không phải flat array indexed bởi pgn toàn cục) */
  addr_t *pte = pte_lookup(caller->krnl->mm, pgn, 1);
  if (pte == NULL) return -1;
  *pte = (addr_t)pte_val;
  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * vmap_pgd_memset - Zero out a range of PTEs
 * [STUDENT WORK] Implement memset cho dải page table entries
 *
 * Logic: Với mỗi trang trong [addr, addr+pgnum*PAGESZ), tính pgn
 *        rồi gọi pte_set_entry để ghi 0. Chỉ tạo bảng trang khi
 *        thực sự cần (demand allocation qua pte_lookup bên trong).
 * ═══════════════════════════════════════════════════════════════════ */
int vmap_pgd_memset(struct pcb_t *caller,
                     addr_t addr,
                     int pgnum)
{
  /* [STUDENT WORK] Implement memset dải PTE về 0 */
  int pgit;
  addr_t pgn = addr / PAGING_PAGESZ;

  for (pgit = 0; pgit < pgnum; pgit++)
    pte_set_entry(caller, pgn + pgit, 0);

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * vmap_page_range - Map a list of frames to a virtual address range
 * [STUDENT WORK] Implement ánh xạ frames vào page table
 *
 * Logic:
 *   1. Ghi ret_rg: rg_start=addr, rg_end=addr+pgnum*PAGESZ
 *      (check NULL trước để tránh crash)
 *   2. Với mỗi frame trong danh sách `frames`:
 *      - Tính pgn = base_pgn + i
 *      - Gọi pte_set_fpn: ghi pgn→fpn vào page table (demand alloc)
 *      - Gọi enlist_pgn_node: thêm pgn vào fifo_pgn để hỗ trợ
 *        page replacement về sau
 * ═══════════════════════════════════════════════════════════════════ */
addr_t vmap_page_range(struct pcb_t *caller,
                        addr_t addr,
                        int pgnum,
                        struct framephy_struct *frames,
                        struct vm_rg_struct *ret_rg)
{
  /* [STUDENT WORK] Implement map frames → page table */

  /* 1. Ghi boundary của region được map (check NULL) */
  if (ret_rg != NULL) {
    ret_rg->rg_start = addr;
    ret_rg->rg_end   = addr + (addr_t)pgnum * PAGING_PAGESZ;
  }

  /* 2. Duyệt danh sách frames, ghi PTE cho từng trang */
  struct framephy_struct *fpit = frames;
  int pgit;
  addr_t pgn = addr / PAGING_PAGESZ;

  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++) {
    /* Ghi PTE: trang pgn+pgit → frame fpit->fpn */
    pte_set_fpn(caller, pgn + pgit, fpit->fpn);

    /* Tracking cho page replacement (FIFO) */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);

    fpit = fpit->fp_next;
  }

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * alloc_pages_range - Allocate req_pgnum physical frames from RAM
 * [STUDENT WORK] Implement cấp phát frame vật lý
 *
 * Logic:
 *   Lặp req_pgnum lần, mỗi lần:
 *     - Gọi MEMPHY_get_freefp để lấy frame tự do từ mram
 *     - Nếu có → tạo framephy_struct, nối vào danh sách (tail append
 *       để giữ thứ tự, tránh reverse)
 *     - Nếu không có → trả về -3000 (OOM signal cho vm_map_ram)
 * ═══════════════════════════════════════════════════════════════════ */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum,
                          struct framephy_struct **frm_lst)
{
  /* [STUDENT WORK] Implement cấp phát req_pgnum frames từ mram */
  addr_t fpn;
  int pgit;
  struct framephy_struct *head = NULL, *tail = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++) {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0) {
      struct framephy_struct *newfp = malloc(sizeof(struct framephy_struct));
      if (newfp == NULL) return -3000;

      newfp->fpn     = fpn;
      newfp->fp_next = NULL;

      /* Tail-append để giữ thứ tự cấp phát */
      if (head == NULL) head = tail = newfp;
      else { tail->fp_next = newfp; tail = newfp; }

    } else {
      /* Không còn frame tự do → báo OOM cho caller */
      return -3000;
    }
  }

  *frm_lst = head;
  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * vm_map_ram - Map virtual area to physical RAM
 * [STUDENT WORK] Uncomment dòng alloc_pages_range và xử lý OOM
 *
 * Logic:
 *   1. Gọi alloc_pages_range để xin frames từ RAM
 *   2. Nếu lỗi (< 0) → return -1
 *   3. Nếu thành công → gọi vmap_page_range để ghi PTE
 * ═══════════════════════════════════════════════════════════════════ */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                   addr_t mapstart, int incpgnum,
                   struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc;

  /* [STUDENT WORK] Gọi alloc_pages_range (dòng này bị comment ở code gốc) */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  if (ret_alloc == -3000)
    return -1; /* Out of memory */

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * __swap_cp_page - Copy page content between memphy devices
 * [KHÔNG THAY ĐỔI – giữ nguyên code gốc]
 * ═══════════════════════════════════════════════════════════════════ */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                    struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++) {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }
  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * init_mm - Initialize empty Memory Management instance
 * [STUDENT WORK] Implement tất cả các TODO
 *
 * Logic:
 *   1. Cấp phát PGD (512 entries) – cấp cao nhất của bảng trang 5 cấp
 *      Các cấp còn lại (p4d/pud/pmd/pt) = NULL → demand allocation
 *   2. Khởi tạo VMA0: vm_id=0, vm_start=vm_end=sbrk=0
 *   3. vm_freerg_list = NULL trước khi enlist (tránh UB)
 *   4. Gắn VMA0 vào mm->mmap
 *   5. Khởi tạo mm->fifo_pgn = NULL, mm->kcpooltbl = NULL
 * ═══════════════════════════════════════════════════════════════════ */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (vma0 == NULL) return -1;

  /* [STUDENT WORK] Cấp phát PGD – chỉ cấp phát cấp cao nhất
   * Demand allocation: p4d/pud/pmd/pt sẽ được malloc trong pte_lookup
   * khi có mapping thực sự đi qua nhánh đó */
  mm->pgd = calloc(PAGING64_LEVEL_SZ, sizeof(addr_t));
  if (mm->pgd == NULL) { free(vma0); return -1; }
  mm->p4d = NULL; /* demand-allocated khi cần */
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  /* [STUDENT WORK] Khởi tạo VMA0 mặc định */
  vma0->vm_id    = 0;
  vma0->vm_start = 0;
  vma0->vm_end   = vma0->vm_start;
  vma0->sbrk     = vma0->vm_start;

  /* [STUDENT WORK] Phải NULL trước khi enlist – tránh rg_next = rác */
  vma0->vm_freerg_list = NULL;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  if (first_rg == NULL) { free(vma0); free(mm->pgd); return -1; }
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* [STUDENT WORK] Gắn VMA0 vào danh sách và cập nhật các field mm */
  vma0->vm_next = NULL;
  vma0->vm_mm   = mm;
  mm->mmap      = vma0;
  mm->fifo_pgn  = NULL;
  mm->kcpooltbl = NULL;

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * print_pgtbl - Dump page table entries in [start, end)
 * [STUDENT WORK] Implement duyệt và in page table
 *
 * Logic: Tính pgn_start và pgn_end từ địa chỉ byte.
 *        Với mỗi pgn, dùng pte_get_entry (= pte_lookup alloc=0)
 *        để đọc PTE. In ra trạng thái: not mapped / present / swap.
 * ═══════════════════════════════════════════════════════════════════ */
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  /* [STUDENT WORK] Implement dump page table */
  addr_t pgn_start = start / PAGING_PAGESZ;
  addr_t pgn_end   = end   / PAGING_PAGESZ;

  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  get_pd_from_address(start, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  printf("[PGTBL] [" FORMAT_ADDR "-" FORMAT_ADDR ") "
         "PGD[%lu] P4D[%lu] PUD[%lu] PMD[%lu] PT[%lu]\n",
         start, end,
         (unsigned long)pgd_i, (unsigned long)p4d_i,
         (unsigned long)pud_i, (unsigned long)pmd_i,
         (unsigned long)pt_i);

  for (addr_t pgn = pgn_start; pgn <= pgn_end; pgn++) {
    uint32_t val = pte_get_entry(caller, pgn);
    if (val == 0) {
      printf("  PTE[" FORMAT_ADDR "] = <not mapped>\n", pgn);
    } else if (val & PAGING_PTE_SWAPPED_MASK) {
      printf("  PTE[" FORMAT_ADDR "] = SWAPPED  swptyp=%u swpoff=%u\n",
             pgn,
             (unsigned)(GETVAL(val, PAGING_PTE_SWPTYP_MASK,
                               PAGING_PTE_SWPTYP_LOBIT)),
             (unsigned)(GETVAL(val, PAGING_PTE_SWPOFF_MASK,
                               PAGING_PTE_SWPOFF_LOBIT)));
    } else {
      printf("  PTE[" FORMAT_ADDR "] = frame=%u  PA=0x%x\n",
             pgn,
             (unsigned)GETVAL(val, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT),
             (unsigned)(GETVAL(val, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT)
                        * PAGING_PAGESZ));
    }
  }
  return 0;
}


struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  if (rgnode == NULL) return NULL;
  rgnode->rg_start = rg_start;
  rgnode->rg_end   = rg_end;
  rgnode->rg_next  = NULL;
  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;
  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  if (pnode == NULL) return -1;
  pnode->pgn     = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;
  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;
  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (fp != NULL) { printf("fp[" FORMAT_ADDR "]\n", fp->fpn); fp = fp->fp_next; }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;
  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL) {
    printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;
  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL) {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL) {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}


#endif  //def MM64