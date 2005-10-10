/*
 * Copyright (C) 2000-2005 SWsoft. All rights reserved.
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined by Trolltech AS of Norway and appearing in the file
 * LICENSE.QPL included in the packaging of this file.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _LIST_H_
#define _LIST_H_


struct list_elem;
typedef struct {
	struct list_elem *prev, *next;
} list_head_t;

typedef struct list_elem {
	struct list_elem *prev, *next;
} list_elem_t;

struct str_struct {
	list_elem_t list;
	char *val;
};

typedef struct str_struct str_param;

static inline void list_head_init(list_head_t *head)
{
	head->next = (list_elem_t *)head;
	head->prev = (list_elem_t *)head;
}

static inline void list_elem_init(list_elem_t *entry)
{
	entry->next = entry;
	entry->prev = entry;
}

static inline void list_add(list_elem_t *new, list_head_t *list)
{
	new->next = list->next;
	new->prev = (list_elem_t *)list;
	list->next->prev = new;
	list->next = new;
}

static inline void list_add_tail(list_elem_t *new, list_head_t *list)
{
	new->next = (list_elem_t *)list;
	new->prev = list->prev;
	list->prev->next = new;
	list->prev = new;
}

static inline void list_del(list_elem_t *el)
{
	el->prev->next = el->next;
	el->next->prev = el->prev;
	el->prev = (void *)0x5a5a5a5a;
	el->next = (void *)0xa5a5a5a5;
}

static inline void list_del_init(list_elem_t *el)
{
	el->prev->next = el->next;
	el->next->prev = el->prev;
	list_elem_init(el);
}

static inline int list_is_init(list_head_t *h)
{
        return h->next == NULL;
}

static inline int list_empty(list_head_t *h)
{
	if (list_is_init(h))
		return 1;
	return h->next == (list_elem_t *)h;
}

static inline int list_elem_inserted(list_elem_t *el)
{
	return el->next != el;
}

static inline void list_moveall(list_head_t *src, list_head_t *dst)
{
	list_add((list_elem_t *)dst, src);
	list_del((list_elem_t *)src);
	list_head_init(src);
}

#define list_entry(ptr, type, field)					\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->field)))

#define list_first_entry(head, type, field)				\
	list_entry((head)->next, type, field)

#define list_for_each(entry, head, field)				\
	for (entry = list_entry((head)->next, typeof(*entry), field);\
	     &entry->field != (list_elem_t*)(head);			\
	     entry = list_entry(entry->field.next, typeof(*entry), field))

#define list_for_each_prev(entry, head, field)				\
	for (entry = list_entry((head)->prev, typeof(*entry), field);\
	     &entry->field != (list_elem_t*)(head);			\
	     entry = list_entry(entry->field.prev, typeof(*entry), field))

#define list_for_each_safe(entry, tmp, head, field)			\
	for (entry = list_entry((head)->next, typeof(*entry), field),\
		tmp = list_entry(entry->field.next, typeof(*entry), field); \
	     &entry->field != (list_elem_t*)(head);			\
	     entry = tmp,						\
	        tmp = list_entry(tmp->field.next, typeof(*tmp), field))


char *list2str_c(char *name, char c, list_head_t *head);
char *list2str(char *name, list_head_t *head);
char **list2arg(list_head_t *head);
int add_str_param(list_head_t *head, const char *str);
int add_str_param2(list_head_t *head, char *str);
int add_str2list(list_head_t *head, const char *val);
void free_str_param(list_head_t *head);
int copy_str_param(list_head_t *dst, list_head_t *src);
char *find_str(list_head_t *head, const char *val);
int merge_str_list(int delall, list_head_t *old, list_head_t *add,
        list_head_t *del, list_head_t *merged);
int list_size(list_head_t *head);
#endif /* _LIST_H_ */