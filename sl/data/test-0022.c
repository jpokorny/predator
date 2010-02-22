#include "../sl.h"
#include <stdlib.h>

#define SLL_NEW(type) \
    ((type *) malloc(sizeof(type)))

typedef struct sll_item sll_item_t;
struct sll_item {
    sll_item_t *next;
};

static sll_item_t* create_item(sll_item_t *list)
{
    sll_item_t *item = SLL_NEW(sll_item_t);
    if (!item)
        abort();

    item->next = (list)
        ? list
        : item;

    SL_PLOT_STACK_FRAME(create_item, "create-done");
    return item;
}

static sll_item_t** insert_item(sll_item_t **plist)
{
    if (*plist) {
        sll_item_t *list = *plist;
        list->next = create_item(list->next);
    } else {
        *plist = create_item(NULL);
    }

    sl_plot("insert-done");
    return plist;
}

static void destroy_cyclic_sll(sll_item_t **plist)
{
    sl_plot_by_ptr(plist, "on-destroy");
    sll_item_t *list = *plist;
    if (list) {
        sll_item_t *item = list->next;
        while (item != list) {
            sll_item_t *next = item->next;
            free(item);
            item = next;
        }
        free(list);
    }
    *plist = NULL;
}

int main()
{
    sll_item_t *list = create_item(NULL);

    int i;
    for (i = 0; i < 128; ++i) {
        if (i)
            insert_item(&list);
        else
            insert_item(insert_item(&list));

        destroy_cyclic_sll(&list);
    }

    sl_plot("before-last-free");
    free(list);
    return 0;
}