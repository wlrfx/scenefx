#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "scenefx/types/linked_node.h"

struct link {
    struct linked_node *node_1;
    struct linked_node *node_2;
};

static struct link *link_init(struct linked_node *node_1,
        struct linked_node *node_2) {
    struct link *link = calloc(1, sizeof(*link));
    if (!link) {
        wlr_log(WLR_ERROR, "Failed to allocate link!");
        return NULL;
    }

    link->node_1 = node_1;
    link->node_2 = node_2;

    return link;
}

//
// Linked Node
//

bool linked_nodes_are_linked(struct linked_node *node_1,
        struct linked_node *node_2) {
    return linked_node_initialized(node_1) && linked_node_initialized(node_2)
        && node_1->link == node_2->link;
}

void linked_node_init_link(struct linked_node *node_1,
        struct linked_node *node_2) {
    if (linked_node_initialized(node_1) || linked_node_initialized(node_2)) {
        assert(linked_nodes_are_linked(node_1, node_2));
        return;
    }

    struct link *link = link_init(node_1, node_2);
    node_1->link = link;
    node_2->link = link;
}

struct linked_node *linked_nodes_get_sibling(struct linked_node *node) {
    if (!linked_node_initialized(node)) {
        return NULL;
    }
    assert(node == node->link->node_1 || node == node->link->node_2);

    struct link *link = node->link;
    if (link->node_1 == node) {
        return link->node_2;
    }
    return link->node_1;
}

void linked_node_unlink(struct linked_node *node_1,
        struct linked_node *node_2) {
    assert(linked_nodes_are_linked(node_1, node_2));

    struct link *link = node_1->link;
    node_1->link = NULL;
    node_2->link = NULL;
    free(link);
}

void linked_node_destroy(struct linked_node *node) {
    if (!linked_node_initialized(node)) {
        return;
    }
    assert(node == node->link->node_1 || node == node->link->node_2);
    linked_node_unlink(node->link->node_1, node->link->node_2);
}
