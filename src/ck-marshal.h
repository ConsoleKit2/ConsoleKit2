
#ifndef __ck_marshal_MARSHAL_H__
#define __ck_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:UINT,STRING (ck-marshal.list:1) */
extern void ck_marshal_VOID__UINT_STRING (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

/* BOOLEAN:POINTER (ck-marshal.list:2) */
extern void ck_marshal_BOOLEAN__POINTER (GClosure     *closure,
                                         GValue       *return_value,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint,
                                         gpointer      marshal_data);

G_END_DECLS

#endif /* __ck_marshal_MARSHAL_H__ */

