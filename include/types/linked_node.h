#ifndef TYPES_LINKED_NODES_H
#define TYPES_LINKED_NODES_H

/**
 * A node in a link between two objects. Can be used to safely couple two
 * objects together.
 */
struct linked_node {
	struct link *link;
};

#define linked_node_init() \
	((struct linked_node) { \
		.link = NULL \
	})

void linked_node_init_link(struct linked_node *main_node,
		struct linked_node *reference_node);

struct linked_node *linked_nodes_get_sibling(struct linked_node *node);

void linked_node_unlink(struct linked_node *main_node,
		struct linked_node *reference_node);

void linked_node_destroy(struct linked_node *node);

#endif
