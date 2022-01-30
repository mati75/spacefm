/*
 *
 * License: See COPYING file
 *
 */

#ifndef ___EXO_MARSHAL_MARSHAL_H__
#define ___EXO_MARSHAL_MARSHAL_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* VOID:OBJECT,OBJECT (exo-marshal.list:2) */
extern void _exo_marshal_VOID__OBJECT_OBJECT(GClosure* closure, GValue* return_value,
                                             unsigned int n_param_values,
                                             const GValue* param_values, void* invocation_hint,
                                             void* marshal_data);

/* BOOLEAN:VOID (exo-marshal.list:5) */
extern void _exo_marshal_BOOLEAN__VOID(GClosure* closure, GValue* return_value,
                                       unsigned int n_param_values, const GValue* param_values,
                                       void* invocation_hint, void* marshal_data);

/* BOOLEAN:ENUM,INT (exo-marshal.list:6) */
extern void _exo_marshal_BOOLEAN__ENUM_INT(GClosure* closure, GValue* return_value,
                                           unsigned int n_param_values, const GValue* param_values,
                                           void* invocation_hint, void* marshal_data);

G_END_DECLS

#endif /* ___exo_marshal_MARSHAL_H__ */
