#include "buddy.h"
#include <stddef.h>

#define MAX_RANK 16
#define MIN_RANK 1
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1u << PAGE_SHIFT)
#define MAX_PAGES  (1 << (MAX_RANK - 1))

struct node {
    struct node *next;
    struct node *prev;
};

static void       *base = NULL;
static int         total_pages = 0;
static signed char ranks[MAX_PAGES];
static struct node *free_list[MAX_RANK + 1];
static int         free_counts[MAX_RANK + 1];

static inline unsigned long ptr_to_off(void *p)
{
    return ((unsigned long)p - (unsigned long)base) >> PAGE_SHIFT;
}

static inline int is_page_aligned(void *p)
{
    return (((unsigned long)p - (unsigned long)base) & (PAGE_SIZE - 1)) == 0;
}

static inline void *off_to_ptr(int off)
{
    return (void *)((unsigned long)base + ((unsigned long)off << PAGE_SHIFT));
}

static inline int rank_pages(int rank)
{
    return 1 << (rank - 1);
}

static void list_add(int rank, struct node *n)
{
    n->next = free_list[rank];
    n->prev = NULL;
    if (free_list[rank])
        free_list[rank]->prev = n;
    free_list[rank] = n;
    free_counts[rank]++;
}

static void list_remove(struct node *n, int rank)
{
    if (n->prev)
        n->prev->next = n->next;
    else
        free_list[rank] = n->next;
    if (n->next)
        n->next->prev = n->prev;
    free_counts[rank]--;
}

int init_page(void *p, int pgcount)
{
    int rank, pages, off;

    if (p == NULL || pgcount <= 0 || pgcount > MAX_PAGES)
        return -EINVAL;

    base = p;
    total_pages = pgcount;

    for (rank = 0; rank <= MAX_RANK; ++rank) {
        free_list[rank] = NULL;
        free_counts[rank] = 0;
    }

    for (off = 0; off < total_pages; ++off)
        ranks[off] = 0;

    off = 0;
    while (pgcount > 0) {
        for (rank = MAX_RANK; rank >= MIN_RANK; --rank) {
            pages = rank_pages(rank);
            if (pages <= pgcount) {
                int i;
                for (i = 0; i < pages; ++i)
                    ranks[off + i] = -rank;
                list_add(rank, (struct node *)off_to_ptr(off));
                off += pages;
                pgcount -= pages;
                break;
            }
        }
    }

    return OK;
}

void *alloc_pages(int rank)
{
    int r, pages, off, i;
    int best_rank = -1;
    unsigned long best_off = 0;
    struct node *best_node = NULL;
    struct node *n;

    if (rank < MIN_RANK || rank > MAX_RANK)
        return ERR_PTR(-EINVAL);

    for (r = rank; r <= MAX_RANK; ++r) {
        for (n = free_list[r]; n != NULL; n = n->next) {
            unsigned long o = ptr_to_off(n);
            if (best_node == NULL || o < best_off) {
                best_rank = r;
                best_off = o;
                best_node = n;
            }
        }
    }
    if (best_node == NULL)
        return ERR_PTR(-ENOSPC);

    r = best_rank;
    list_remove(best_node, r);
    off = (int)best_off;

    while (r > rank) {
        int buddy_off;
        r--;
        pages = rank_pages(r);
        buddy_off = off + pages;
        for (i = 0; i < pages; ++i)
            ranks[buddy_off + i] = -r;
        list_add(r, (struct node *)off_to_ptr(buddy_off));
    }

    pages = rank_pages(rank);
    for (i = 0; i < pages; ++i)
        ranks[off + i] = rank;

    return off_to_ptr(off);
}

int return_pages(void *p)
{
    int off, rank, pages, cur, buddy_off, i;
    struct node *bn;

    if (p == NULL || base == NULL)
        return -EINVAL;
    if (!is_page_aligned(p))
        return -EINVAL;

    off = (int)ptr_to_off(p);
    if (off < 0 || off >= total_pages)
        return -EINVAL;

    rank = ranks[off];
    if (rank <= 0 || rank > MAX_RANK)
        return -EINVAL;

    pages = rank_pages(rank);
    if (off % pages != 0)
        return -EINVAL;

    cur = rank;
    while (cur < MAX_RANK) {
        buddy_off = off ^ rank_pages(cur);
        if (buddy_off < 0 || buddy_off >= total_pages)
            break;
        if (ranks[buddy_off] != -cur)
            break;
        bn = (struct node *)off_to_ptr(buddy_off);
        list_remove(bn, cur);
        if (buddy_off < off)
            off = buddy_off;
        cur++;
    }

    pages = rank_pages(cur);
    for (i = 0; i < pages; ++i)
        ranks[off + i] = -cur;
    list_add(cur, (struct node *)off_to_ptr(off));

    return OK;
}

int query_ranks(void *p)
{
    int off, rank;

    if (p == NULL || base == NULL)
        return -EINVAL;
    if (!is_page_aligned(p))
        return -EINVAL;

    off = (int)ptr_to_off(p);
    if (off < 0 || off >= total_pages)
        return -EINVAL;

    rank = ranks[off];
    if (rank == 0)
        return -EINVAL;
    if (rank < 0)
        rank = -rank;

    return rank;
}

int query_page_counts(int rank)
{
    if (rank < MIN_RANK || rank > MAX_RANK)
        return -EINVAL;

    return free_counts[rank];
}
