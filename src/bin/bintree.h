#ifndef _BOOL_EXP_BINTREE_H_
#define _BOOL_EXP_BINTREE_H_

typedef struct _BINARY_TREE_NODE * BINARY_TREE_NODE;
typedef struct _BINARY_TREE * BINARY_TREE;
typedef int    (*BINTREE_TRAVERSE_FUNC)(BINARY_TREE tree, BINARY_TREE_NODE node, BINARY_TREE_NODE parent, void * arg);

BINARY_TREE      bintree_create_tree(int data_size);
BINARY_TREE_NODE bintree_create_node(BINARY_TREE tree);

BINARY_TREE_NODE bintree_get_head(BINARY_TREE tree);
void             bintree_set_head(BINARY_TREE tree, BINARY_TREE_NODE head);
void             bintree_set_left_child(BINARY_TREE_NODE node, BINARY_TREE_NODE child);
void             bintree_set_right_child(BINARY_TREE_NODE node, BINARY_TREE_NODE child);

BINARY_TREE_NODE bintree_get_left_child(BINARY_TREE_NODE node);
BINARY_TREE_NODE bintree_get_right_child(BINARY_TREE_NODE node);
void            *bintree_get_node_data(BINARY_TREE_NODE node);

void bintree_remove_node(BINARY_TREE_NODE node);
void bintree_remove_node_recursive(BINARY_TREE_NODE node);
void bintree_destroy_tree(BINARY_TREE tree);

void bintree_inorder_traverse(BINARY_TREE tree, BINTREE_TRAVERSE_FUNC func, void * arg);
void bintree_postorder_traverse(BINARY_TREE tree, BINTREE_TRAVERSE_FUNC func, void * arg);

#endif /* _BOOL_EXP_BINTREE_H_ */
