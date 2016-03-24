#ifdef E_TYPEDEFS

typedef struct _E_Util_Transform_Value       E_Util_Transform_Value;
typedef struct _E_Util_Transform             E_Util_Transform;
typedef struct _E_Util_Transform_Rect        E_Util_Transform_Rect;
typedef struct _E_Util_Transform_Vertex      E_Util_Transform_Vertex;
typedef struct _E_Util_Transform_Rect_Vertex E_Util_Transform_Rect_Vertex;
typedef struct _E_Util_Transform_Matrix      E_Util_Transform_Matrix;

#else
#ifndef E_UTIL_TRANSFORM_H_
#define E_UTIL_TRANSFORM_H_

struct _E_Util_Transform_Value
{
   double value[3];
};

struct _E_Util_Transform
{
   E_Util_Transform_Value scale;
   E_Util_Transform_Value move;
   E_Util_Transform_Value rotation;
   int                    ref_count;
   Eina_Bool              keep_ratio;
   Eina_Bool              changed;
};

struct _E_Util_Transform_Rect
{
   int x;
   int y;
   int w;
   int h;
};

struct _E_Util_Transform_Vertex
{
   double vertex[4];
};

struct _E_Util_Transform_Rect_Vertex
{
   E_Util_Transform_Vertex vertices[4];
};

struct _E_Util_Transform_Matrix
{
   double mat[4][4];
};

E_API E_Util_Transform            *e_util_transform_new(void);
E_API void                         e_util_transform_del(E_Util_Transform *transform);
E_API void                         e_util_transform_ref(E_Util_Transform *transform);
E_API void                         e_util_transform_unref(E_Util_Transform *transform);
E_API int                          e_util_transform_ref_count_get(E_Util_Transform *transform);
E_API void                         e_util_transform_init(E_Util_Transform *transform);
E_API void                         e_util_transform_move(E_Util_Transform *transform, double x, double y, double z);
E_API void                         e_util_transform_scale(E_Util_Transform *transform, double sx, double sy, double sz);
E_API void                         e_util_transform_rotation(E_Util_Transform *transform, double rx, double ry, double rz);
E_API void                         e_util_transform_source_to_target(E_Util_Transform *transform,
                                                                     E_Util_Transform_Rect *dest,
                                                                     E_Util_Transform_Rect *source);
E_API E_Util_Transform             e_util_transform_merge(E_Util_Transform *trans1, E_Util_Transform *trans2);
E_API E_Util_Transform_Matrix      e_util_transform_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect);
E_API Eina_Bool                    e_util_transform_change_get(E_Util_Transform *transform);
E_API void                         e_util_transform_change_unset(E_Util_Transform *transform);
E_API void                         e_util_transform_keep_ratio_set(E_Util_Transform *transform, Eina_Bool enable);
E_API Eina_Bool                    e_util_transform_keep_ratio_get(E_Util_Transform *transform);
E_API E_Util_Transform             e_util_transform_keep_ratio_apply(E_Util_Transform *transform, int origin_w, int origin_h);
E_API void                         e_util_transform_move_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_scale_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_log(E_Util_Transform *transform, const char *str);

E_API void                         e_util_transform_rect_init(E_Util_Transform_Rect *rect, int x, int y, int w, int h);
E_API void                         e_util_transform_rect_client_rect_get(E_Util_Transform_Rect *rect, E_Client *ec);
E_API E_Util_Transform_Rect_Vertex e_util_transform_rect_to_vertices(E_Util_Transform_Rect *rect);

E_API void                         e_util_transform_vertex_init(E_Util_Transform_Vertex *vertex, double x, double y, double z, double w);
E_API void                         e_util_transform_vertex_pos_get(E_Util_Transform_Vertex *vertex, double *x, double *y, double *z, double *w);

E_API void                         e_util_transform_vertices_init(E_Util_Transform_Rect_Vertex *vertices);
E_API E_Util_Transform_Rect        e_util_transform_vertices_to_rect(E_Util_Transform_Rect_Vertex *vertex);
E_API void                         e_util_transform_vertices_pos_get(E_Util_Transform_Rect_Vertex *vertices, int index,
                                                                     double *x, double *y, double *z, double *w);

E_API void                         e_util_transform_matrix_load_identity(E_Util_Transform_Matrix *matrix);
E_API void                         e_util_transform_matrix_translate(E_Util_Transform_Matrix *matrix, double x, double y, double z);
E_API void                         e_util_transform_matrix_rotation_x(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_rotation_y(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_rotation_z(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_scale(E_Util_Transform_Matrix *matrix, double sx, double sy, double sz);
E_API E_Util_Transform_Matrix      e_util_transform_matrix_multiply(E_Util_Transform_Matrix *matrix1,
                                                                    E_Util_Transform_Matrix *matrix2);
E_API E_Util_Transform_Vertex      e_util_transform_matrix_multiply_vertex(E_Util_Transform_Matrix *matrix,
                                                                           E_Util_Transform_Vertex *vertex);
E_API E_Util_Transform_Rect_Vertex e_util_transform_matrix_multiply_rect_vertex(E_Util_Transform_Matrix *matrix,
                                                                                E_Util_Transform_Rect_Vertex *vertices);
E_API Eina_Bool                    e_util_transform_matrix_equal_check(E_Util_Transform_Matrix *matrix,
                                                                       E_Util_Transform_Matrix *matrix2);
#endif
#endif
