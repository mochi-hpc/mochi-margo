#ifndef __TYPES_H
#define __TYPES_H

#include <mercury.h>

typedef struct int_list {
    int32_t          value;
    struct int_list* next;
} *int_list_t;

static inline hg_return_t hg_proc_int_list_t(hg_proc_t proc, void* data)
{
    hg_return_t ret;
    int_list_t* list = (int_list_t*)data;

    hg_size_t length = 0;
    int_list_t tmp   = NULL;
    int_list_t prev  = NULL;

    switch(hg_proc_get_op(proc)) {

        case HG_ENCODE:
            tmp = *list;
            // find out the length of the list
            while(tmp != NULL) {
                tmp = tmp->next;
                length += 1;
            }
            // write the length
            ret = hg_proc_hg_size_t(proc, &length);
            if(ret != HG_SUCCESS)
                break;
            // write the list
            tmp = *list;
            while(tmp != NULL) {
                ret = hg_proc_int32_t(proc, &tmp->value);
                if(ret != HG_SUCCESS)
                    break;
                tmp = tmp->next;
            }
            break;

        case HG_DECODE:
            // find out the length of the list
            ret = hg_proc_hg_size_t(proc, &length);
            if(ret != HG_SUCCESS)
                break;
            // loop and create list elements
            *list = NULL;
            while(length > 0) {
                tmp = (int_list_t)calloc(1, sizeof(*tmp));
                if(*list == NULL) {
                    *list = tmp;
                }
                if(prev != NULL) {
                    prev->next = tmp;
                }
                ret = hg_proc_int32_t(proc, &tmp->value);
                if(ret != HG_SUCCESS)
                    break;
                prev = tmp;
                length -= 1;
            }
            break;

        case HG_FREE:
            tmp = *list;
            while(tmp != NULL) {
                prev = tmp;
                tmp  = prev->next;
                free(prev);
            }
            ret = HG_SUCCESS;
    }
    return ret;
}

#endif
