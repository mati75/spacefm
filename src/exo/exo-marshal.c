/*
 *
 * License: See COPYING file
 *
 */

#include <stdbool.h>

#include <glib-object.h>

/* WARNING: This code accesses GValues directly, which is UNSUPPORTED API.
 *          Do not access GValues directly in your code. Instead, use the
 *          g_value_get_*() functions
 */

#define g_marshal_value_peek_int(v)    (v)->data[0].v_int
#define g_marshal_value_peek_enum(v)   (v)->data[0].v_long
#define g_marshal_value_peek_object(v) (v)->data[0].v_pointer

/* VOID:OBJECT,OBJECT (exo-marshal.list:2) */
void _exo_marshal_VOID__OBJECT_OBJECT(GClosure* closure, GValue* return_value,
                                      unsigned int n_param_values, const GValue* param_values,
                                      void* invocation_hint, void* marshal_data)
{
    typedef void (
        *GMarshalFunc_VOID__OBJECT_OBJECT)(void* data1, void* arg1, void* arg2, void* data2);
    GCClosure* cc = (GCClosure*)closure;
    void* data1;
    void* data2;
    GMarshalFunc_VOID__OBJECT_OBJECT callback;

    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_VOID__OBJECT_OBJECT)(marshal_data ? marshal_data : cc->callback);

    callback(data1,
             g_marshal_value_peek_object(param_values + 1),
             g_marshal_value_peek_object(param_values + 2),
             data2);
}

/* BOOLEAN:VOID (exo-marshal.list:5) */
void _exo_marshal_BOOLEAN__VOID(GClosure* closure, GValue* return_value,
                                unsigned int n_param_values, const GValue* param_values,
                                void* invocation_hint, void* marshal_data)
{
    typedef bool (*GMarshalFunc_BOOLEAN__VOID)(void* data1, void* data2);
    GMarshalFunc_BOOLEAN__VOID callback;
    GCClosure* cc = (GCClosure*)closure;
    void* data1;
    void* data2;
    bool v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 1);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_BOOLEAN__VOID)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1, data2);

    g_value_set_boolean(return_value, v_return);
}

/* BOOLEAN:ENUM,INT (exo-marshal.list:6) */
void _exo_marshal_BOOLEAN__ENUM_INT(GClosure* closure, GValue* return_value,
                                    unsigned int n_param_values, const GValue* param_values,
                                    void* invocation_hint, void* marshal_data)
{
    typedef bool (*GMarshalFunc_BOOLEAN__ENUM_INT)(void* data1, int arg1, int arg2, void* data2);
    GCClosure* cc = (GCClosure*)closure;
    void* data1;
    void* data2;
    GMarshalFunc_BOOLEAN__ENUM_INT callback;
    bool v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_BOOLEAN__ENUM_INT)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1,
                        g_marshal_value_peek_enum(param_values + 1),
                        g_marshal_value_peek_int(param_values + 2),
                        data2);

    g_value_set_boolean(return_value, v_return);
}
