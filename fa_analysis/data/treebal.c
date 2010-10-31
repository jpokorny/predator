#include <stdlib.h>

int main() {

    struct TreeNode {
        struct TreeNode* left;
        struct TreeNode* right;
    };

    struct StackItem {
        struct StackItem* next;
	struct TreeNode* node;
    };

    struct TreeNode* root = malloc(sizeof(*root)), *n, *nt;
    root->left = NULL;
    root->right = NULL;

    while (__nondet()) {
        n = root;
	while (n->left) {
	    if (__nondet())
		n = n->left;
	    else
		n = n->right;
	}
	n->left = malloc(sizeof(*n));
	n->left->left = NULL;
	n->left->right = NULL;
	n->right = malloc(sizeof(*n));
	n->right->left = NULL;
	n->right->right = NULL;
    }
/*
    struct StackItem* s = malloc(sizeof(*s)), *st;
    s->next = NULL;
    s->node = root;

    while (s != NULL) {
        st = s;
        s = s->next;
	n = st->node;
	free(st);
        if (__nondet()) {
	    nt = malloc(sizeof(*nt));
	    nt->left = NULL;
	    nt->right = NULL;
    	    n->left = nt;
	    st = malloc(sizeof(*st));
	    st->next = s;
	    st->node = nt;
	    s = st;
	}
        if (__nondet()) {
	    nt = malloc(sizeof(*nt));
	    nt->left = NULL;
	    nt->right = NULL;
    	    n->right = nt;
	    st = malloc(sizeof(*st));
	    st->next = s;
	    st->node = nt;
	    s = st;
	}
    }
*/
    n = NULL;
    nt = NULL;

    struct StackItem* s = malloc(sizeof(*s)), * st;
    s->next = NULL;
    s->node = root;

    while (s != NULL) {
        st = s;
        s = s->next;
	n = st->node;
	free(st);
	if (n->left) {
	    st = malloc(sizeof(*st));
	    st->next = s;
	    st->node = n->left;
	    s = st;
	}
	if (n->right) {
	    st = malloc(sizeof(*st));
	    st->next = s;
	    st->node = n->right;
	    s = st;
	}
	free(n);
    }

    return 0;
}