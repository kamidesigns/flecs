#include <singleton.h>
#include <stdio.h>

// This example shows how to use singleton components in queries.

// Singleton component
typedef struct {
    double value; 
} Gravity;

// Entity component
typedef struct {
    double x, y;
} Velocity;

int main(int argc, char *argv[]) {
    ecs_world_t *ecs = ecs_init_w_args(argc, argv);

    ECS_COMPONENT(ecs, Gravity);
    ECS_COMPONENT(ecs, Velocity);

    // Set singleton
    ecs_singleton_set(ecs, Gravity, { 9.81 });

    ecs_entity_t e1 = ecs_new_entity(ecs, "e1");
    ecs_entity_t e2 = ecs_new_entity(ecs, "e2");
    ecs_entity_t e3 = ecs_new_entity(ecs, "e3");

    // Set Velocity
    ecs_set(ecs, e1, Velocity, {0, 0});
    ecs_set(ecs, e2, Velocity, {0, 1});
    ecs_set(ecs, e3, Velocity, {0, 2});

    // Create query that matches Gravity as singleton
    ecs_query_t *q = ecs_query(ecs, {
        .filter.terms = {
            { .id = ecs_id(Velocity) },
            // A singleton is a component matched on itself
            { .id = ecs_id(Gravity), .src.id =  ecs_id(Gravity) }
        },

        // Optional, but lets us iterate entities in bulk even though the query
        // returns fields with mixed counts (see query manual).
        .filter.instanced = true
    });

    ecs_iter_t it = ecs_query_iter(ecs, q);
    while (ecs_query_next(&it)) {
        Velocity *v = ecs_field(&it, Velocity, 1);
        Gravity *g = ecs_field(&it, Gravity, 2);

        for (int i = 0; i < it.count; i ++) {
            // Make sure to access g as a pointer, as we only have a single
            // instance of the Gravity component.
            v[i].y += g->value;

            printf("%s velocity is {%f, %f}\n",
                ecs_get_name(ecs, it.entities[i]), v[i].x, v[i].y);
        }
    }

    // Output
    //   e1 velocity is {0.000000, 9.810000}
    //   e2 velocity is {0.000000, 10.810000}
    //   e3 velocity is {0.000000, 11.810000}

    ecs_fini(ecs);
}
