/*
 * Doubly linked list of circular doubly linked lists
 *
 * boxes: listofclists.dll.boxes
 */

#include <stdlib.h>

int __nondet();

int main() {

	struct T {
		struct T* next;
		struct T* prev;
		int data;
	};

	struct T2 {
		struct T head;
		struct T2* next;
		struct T2* prev;
	};

	struct T2* first = NULL;
	struct T2* last = NULL;

	while (__nondet()) {

		struct T2* x = malloc(sizeof(struct T2));
		x->next = NULL;
		x->prev = NULL;
		x->head.next = &x->head;
		x->head.prev = &x->head;
		x->head.data = 0;

		struct T* y = NULL;

		while (__nondet()) {
			y = malloc(sizeof(struct T));
			y->next = x->head.next;
			y->next->prev = y;
			y->prev = &x->head;
			y->data = 0;
			x->head.next = y;
			y = NULL;
		}

		if (!first) {
			first = x;
			last = x;
		} else {
			last->next = x;
			x->prev = last;
			last = x;
		}

	}

	while (first) {

		struct T2* x = first;
		first = first->next;

		struct T* y = x->head.next;
		while (y != &x->head) {
			struct T* z = y;
			y = y->next;
			free(z);
		}

		free(x);

	}

	return 0;

}
