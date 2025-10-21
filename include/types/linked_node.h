#ifndef TYPES_LINKED_NODES_H
#define TYPES_LINKED_NODES_H
#include <wayland-util.h>
#include <stdbool.h>

/**
 * A node in a link between two objects. Can be used to safely couple two
 * objects together.
 */
struct linked_node {
	struct link *link;
};

/**
 * A list of nodes linked between two objects. Can be used to safely couple 1 object to a list of objects
 *
 * Note: this object is mostly for type safety
 */
struct linked_node_list {
	struct wl_list list;
};

/**
 * A child of a linked_node_list
 *
 * Note: this object is mostly for type safety
 */
struct linked_node_list_child {
	struct linked_node_list_entry *link;
};

/**
 * The list entry for the linked_node_list, which also functions as the link
 */
struct linked_node_list_entry {
	struct wl_list link;
	struct linked_node_list *list;
	struct linked_node_list_child *child;
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

#define linked_list_node_child_init() \
	((struct linked_node_list_child) { \
	.link = NULL \
	})

void linked_node_list_init(struct linked_node_list *list);

bool linked_node_list_is_linked(struct linked_node_list *linked_list, struct linked_node_list_child* node);

bool linked_node_list_is_empty(struct linked_node_list *linked_list);

void linked_node_list_init_link(struct linked_node_list *linked_list, struct linked_node_list_child* node);

struct linked_node_list *linked_node_list_get_parent(struct linked_node_list_child *child);

void linked_node_list_unlink(struct linked_node_list *linked_list, struct linked_node_list_child* node);

void linked_node_list_child_destroy(struct linked_node_list_child *child);

void linked_node_list_destroy(struct linked_node_list *linked_list);

#define linked_node_list_for_each(tmp, pos, linked_list, linked_list_link) \
	struct linked_node_list_entry *tmp; \
	wl_list_for_each(tmp, &(linked_list)->list, link) if ((pos = wl_container_of(tmp->child, pos, linked_list_link)) || true)

#endif
