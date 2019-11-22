#include "flecs_private.h"

#define ECS_ANNOTATION_LENGTH_MAX (16)

/** Skip spaces when parsing signature */
static
const char *skip_space(
    const char *ptr)
{
    while (isspace(*ptr)) {
        ptr ++;
    }
    return ptr;
}

/** Parse element with a dot-separated qualifier ('CONTAINER.Foo') */
static
char* parse_complex_elem(
    char *bptr,
    ecs_system_expr_elem_kind_t *elem_kind,
    ecs_system_expr_oper_kind_t *oper_kind,
    const char * *source,
    const char *system_id,
    const char *signature)
{
    if (bptr[0] == '!') {
        *oper_kind = EcsOperNot;
        if (!bptr[1]) {
            //ecs_print_error_string(signature, system_id,ecs_strerror(ECS_INVALID_EXPRESSION), bptr);

            int argument_number = 1;
            char error_string[200] = "";
            char error_indicator[200] = "";
            char custom_error_message[200] = "";
            const char *component_id_internal;
            uint position = 0;
            unsigned int i = 0;
            if (bptr) {
                component_id_internal = bptr;
                if (strlen(bptr) == 0) {
                    position = strlen(signature) - 1;
                } else {
                    char *pointer_to_string = strstr(signature, bptr);
                    position = pointer_to_string - signature;
                }
            } else {
                component_id_internal = signature;
            }
            for (; i < position; i++) {
                if (signature[i] == ',') argument_number++;
                error_indicator[i] = '~';
            }
            error_indicator[i] = '^';
            error_indicator[++i] = '\0';

            sprintf(custom_error_message, ecs_strerror(ECS_INVALID_EXPRESSION), component_id_internal);
            sprintf(error_string, "%s at argument #%d. Error: \"%s\"\n%s\n%s\n", system_id ? system_id : __FUNCTION__,
                    argument_number, custom_error_message, signature, error_indicator);
            printf("%s", error_string);

            ecs_abort(ECS_INVALID_EXPRESSION, bptr);
        }
        bptr ++;
    } else if (bptr[0] == '?') {
        *oper_kind = EcsOperOptional;
        if (!bptr[1]) {
            ecs_print_error_string(signature, system_id, ecs_strerror(ECS_INVALID_EXPRESSION), bptr);
            ecs_abort(ECS_INVALID_EXPRESSION, bptr);
        }
        bptr ++;
    }

    *source = NULL;

    char *dot = strchr(bptr, '.');
    if (dot) {
        if (bptr == dot) {
            *elem_kind = EcsFromEmpty;
        } else if (!strncmp(bptr, "CONTAINER", dot - bptr)) {
            *elem_kind = EcsFromContainer;
        } else if (!strncmp(bptr, "SYSTEM", dot - bptr)) {
            *elem_kind = EcsFromSystem;
        } else if (!strncmp(bptr, "SELF", dot - bptr)) {
            /* default */
        } else if (!strncmp(bptr, "OWNED", dot - bptr)) {
            *elem_kind = EcsFromOwned;
        } else if (!strncmp(bptr, "SHARED", dot - bptr)) {
            *elem_kind = EcsFromShared;
        } else if (!strncmp(bptr, "CASCADE", dot - bptr)) {
            *elem_kind = EcsCascade;   
        } else {
            *elem_kind = EcsFromEntity;
            *source = bptr;
        }
        
        bptr = dot + 1;

        if (!bptr[0]) {
            ecs_print_error_string(signature, system_id, ecs_strerror(ECS_INVALID_EXPRESSION), bptr);
            ecs_abort(ECS_INVALID_EXPRESSION, signature);
        }
    }

    return bptr;
}

static
int has_tables(
    ecs_world_t *world,
    ecs_system_expr_elem_kind_t elem_kind,
    ecs_system_expr_oper_kind_t oper_kind,
    ecs_system_expr_inout_kind_t inout_kind,
    const char *component_id,
    const char *source_id,
    void *data)
{
    (void)world;
    (void)oper_kind;
    (void)inout_kind;
    (void)component_id;
    (void)source_id;
    
    bool *needs_matching = data;
    if (elem_kind == EcsFromSelf || 
        elem_kind == EcsFromOwned ||
        elem_kind == EcsFromShared ||
        elem_kind == EcsFromContainer) 
    {
        *needs_matching = true;
    }

    return 0;
}

/* -- Private functions -- */

/* Does expression require that a system matches with tables */
bool ecs_needs_tables(
    ecs_world_t *world,
    const char *signature,
    const char *system_id)
{
    bool needs_matching = false;
    ecs_parse_component_expr(world, signature, has_tables, system_id, &needs_matching);
    return needs_matching;
}

/** Count components in a signature */
uint32_t ecs_columns_count(
    const char *sig)
{
    const char *ptr = sig;
    uint32_t count = 1;

    while ((ptr = strchr(ptr + 1, ','))) {
        count ++;
    }

    return count;
}

static
const char* parse_annotation(
    const char *ptr, 
    ecs_system_expr_inout_kind_t *inout_kind_out)
{
    char *bptr, buffer[ECS_ANNOTATION_LENGTH_MAX + 1];
    char ch;

    ptr = skip_space(ptr);

    for (bptr = buffer; (ch = ptr[0]); ptr ++) {        
        if (ch == ',' || ch == ']') {
            /* Even though currently only one simultaneous annotation is 
             * useful, more annotations may be added in the future. */
            bptr[0] = '\0';

            if (!strcmp(buffer, "in")) {
                *inout_kind_out = EcsIn;
            } else if (!strcmp(buffer, "out")) {
                *inout_kind_out = EcsOut;
            } else if (!strcmp(buffer, "inout")) {
                *inout_kind_out = EcsInOut;
            }

            if (ch == ']') {
                break;
            } else {
                ptr = skip_space(ptr + 1);
            }

            bptr = buffer;
        } else {
            if (bptr - buffer >= ECS_ANNOTATION_LENGTH_MAX) {
                return NULL;
            }

            bptr[0] = ch;
            bptr ++;
        }
    }

    if (!ch) {
        /* Annotation expression cannot be end of column expression */
        return NULL;
    }

    return ptr;
}

/** Parse component expression */
int ecs_parse_component_expr(
    ecs_world_t *world,
    const char *sig,
    ecs_parse_action_t action,
    const char *system_id,
    void *ctx)
{
    size_t len = strlen(sig);
    const char *ptr;
    char ch, *bptr, *buffer = ecs_os_malloc(len + 1);
    ecs_assert(buffer != NULL, ECS_OUT_OF_MEMORY, NULL);

    bool complex_expr = false;
    bool prev_is_0 = false;
    ecs_system_expr_elem_kind_t elem_kind = EcsFromSelf;
    ecs_system_expr_oper_kind_t oper_kind = EcsOperAnd;
    ecs_system_expr_inout_kind_t inout_kind = EcsInOut;
    const char *source;

    for (bptr = buffer, ch = sig[0], ptr = sig; ch; ptr++) {
        ptr = skip_space(ptr);
        ch = *ptr;

        if (prev_is_0) {
            /* 0 can only apppear by itself */
            ecs_print_error_string(sig, system_id,ecs_strerror(ECS_ZERO_CAN_ONLY_APPEAR_BY_ITSELF), bptr);
            ecs_abort(ECS_INVALID_SIGNATURE, ecs_strerror(ECS_ZERO_CAN_ONLY_APPEAR_BY_ITSELF));
        }

        if (ch == '[') {
            /* Annotations should appear at the beginning of a column */
            if (bptr != buffer) {
                ecs_abort(ECS_INVALID_SIGNATURE, sig);
            }

            ptr = parse_annotation(ptr + 1, &inout_kind);
            if (!bptr) {
                ecs_abort(ECS_INVALID_SIGNATURE, sig);
            }
        
        } else if (ch == ',' || ch == '|' || ch == '\0') {
            /* Separators should not appear after an empty column */
            if (bptr == buffer) {
                ecs_print_error_string(sig, system_id, ecs_strerror(ECS_INVALID_SIGNATURE), bptr);
                ecs_abort(ECS_INVALID_SIGNATURE, sig);
            }

            *bptr = '\0';
            bptr = buffer;

            source = NULL;

            if (complex_expr) {
                ecs_system_expr_oper_kind_t prev_oper_kind = oper_kind;
                bptr = parse_complex_elem(bptr, &elem_kind, &oper_kind, &source, system_id, sig);

                if (oper_kind == EcsOperNot && prev_oper_kind == EcsOperOr) {
                    ecs_print_error_string( sig, system_id,ecs_strerror(ECS_CANT_USE_NOT_IN_OR_EXPRESSION), bptr);
                    ecs_abort(ECS_INVALID_EXPRESSION, NULL);
                }
            }

           if (oper_kind == EcsOperOr) {
                if (elem_kind == EcsFromEmpty) {
                    /* Cannot OR handles */
                    ecs_print_error_string(sig, system_id,ecs_strerror(ECS_CANT_USE_OR_WITH_EMPTY_FROM_EXPRESSION), bptr);
                    ecs_abort(ECS_INVALID_EXPRESSION, NULL);
                }
            }            

            if (!strcmp(bptr, "0")) {
                if (bptr != buffer) {
                    /* 0 can only appear by itself */
                    ecs_print_error_string(sig, system_id, ecs_strerror(ECS_ZERO_CAN_ONLY_APPEAR_BY_ITSELF), bptr);
                    ecs_abort(ECS_INVALID_EXPRESSION, NULL);
                }

                elem_kind = EcsFromEmpty;
                prev_is_0 = true;
//            } else if(strcmp(bptr, "CHILDOF") && strcmp(bptr, "INSTANCEOF")){
//                /* Lookup component handly by string identifier */
//                ecs_entity_t component = ecs_lookup(world, bptr);
//                if (!component) {
//                    ecs_print_error_string(sig, system_id, ecs_strerror(ECS_UNSESOLVED_COMPONENT_NAME), bptr);
//                    ecs_abort(ECS_INVALID_COMPONENT_ID, bptr);
//                }
            }

            char *source_id = NULL;
            if (source) {
                char *dot = strchr(source, '.');
                source_id = ecs_os_malloc(dot - source + 1);
                ecs_assert(source_id != NULL, ECS_OUT_OF_MEMORY, NULL);

                strncpy(source_id, source, dot - source);
                source_id[dot - source] = '\0';
            }

            int ret;
            if ((ret = action(
                world, elem_kind, oper_kind, inout_kind, bptr, source_id, ctx)))
            {
                ecs_abort(ret, sig);
            }

            if (source_id) {
                ecs_os_free(source_id);
            }

            /* Reset variables for next column */
            complex_expr = false;
            elem_kind = EcsFromSelf;

            if (ch == '|') {
                oper_kind = EcsOperOr;
            } else {
                oper_kind = EcsOperAnd;
            }

            inout_kind = EcsInOut;

            bptr = buffer;
        } else {
            *bptr = ch;
            bptr ++;

            if (ch == '.' || ch == '!' || ch == '?') {
                complex_expr = true;
            }
        }
    }

    ecs_os_free(buffer);
    return 0;
}
