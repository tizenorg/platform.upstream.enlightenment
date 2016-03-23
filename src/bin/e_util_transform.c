#include "e.h"
#define E_UTIL_TRANSFORM_IS_ZERO(p) ((p) > -1e-21 && (p) < 1e-21)

E_API E_Util_Transform*
e_util_transform_new(void)
{
   E_Util_Transform *transform = (E_Util_Transform*)malloc(sizeof(E_Util_Transform));
   if (transform)
     {
        transform->ref_count = 0;
        e_util_transform_init(transform);
        e_util_transform_ref(transform);
     }
   return transform;
}

E_API void
e_util_transform_del(E_Util_Transform *transform)
{
   if (!transform) return;
   e_util_transform_unref(transform);
}

E_API void
e_util_transform_init(E_Util_Transform *transform)
{
   int back_ref_count = 0;
   if (!transform) return;

   back_ref_count = transform->ref_count;
   memset(transform, 0, sizeof(E_Util_Transform));
   transform->scale.value[0] = 1.0;
   transform->scale.value[1] = 1.0;
   transform->scale.value[2] = 1.0;
   transform->changed = EINA_TRUE;
   transform->ref_count = back_ref_count;
}

E_API void
e_util_transform_ref(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->ref_count += 1;
}

E_API void
e_util_transform_unref(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->ref_count -= 1;
   if (transform->ref_count <= 0)
      free(transform);
}

E_API int
e_util_transform_ref_count_get(E_Util_Transform *transform)
{
   if (!transform) return 0;
   return transform->ref_count;
}

E_API void
e_util_transform_move(E_Util_Transform *transform, double x, double y, double z)
{
   if (!transform) return;

   transform->move.value[0] = x;
   transform->move.value[1] = y;
   transform->move.value[2] = z;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_scale(E_Util_Transform *transform, double sx, double sy, double sz)
{
   if (!transform) return;

   transform->scale.value[0] = sx;
   transform->scale.value[1] = sy;
   transform->scale.value[2] = sz;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_rotation(E_Util_Transform *transform, double rx, double ry, double rz)
{
   if (!transform) return;

   transform->rotation.value[0] = rx;
   transform->rotation.value[1] = ry;
   transform->rotation.value[2] = rz;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_source_to_target(E_Util_Transform *transform,
                                  E_Util_Transform_Rect *dest,
                                  E_Util_Transform_Rect *source)
{
   if (!transform) return;
   if (!dest || !source) return;

   e_util_transform_init(transform);

   if ((dest->x != source->x) || (dest->y != source->y))
      e_util_transform_move(transform, dest->x - source->x, dest->y - source->y, 0.0);

   if ((dest->w != source->w) || (dest->w != source->w))
     {
        if (!E_UTIL_TRANSFORM_IS_ZERO(source->w) &&
            !E_UTIL_TRANSFORM_IS_ZERO(source->h))
          {
             e_util_transform_scale(transform, dest->w / source->w, dest->h / source->h, 1.0);
          }
     }

   transform->changed = EINA_TRUE;
}

E_API E_Util_Transform
e_util_transform_merge(E_Util_Transform *trans1, E_Util_Transform *trans2)
{
   E_Util_Transform result;
   int i;

   e_util_transform_init(&result);

   if (!trans1) return result;
   if (!trans2) return result;

   for (i = 0 ; i < 3 ; ++i)
      result.move.value[i] = trans1->move.value[i] + trans2->move.value[i];

   for (i = 0 ; i < 3 ; ++i)
      result.scale.value[i] = trans1->scale.value[i] * trans2->scale.value[i];

   for (i = 0 ; i < 3 ; ++i)
      result.rotation.value[i] = trans1->rotation.value[i] + trans2->rotation.value[i];

   if (trans1->keep_ratio || trans2->keep_ratio)
      result.keep_ratio = EINA_TRUE;

   result.changed = EINA_TRUE;

   return result;
}

E_API E_Util_Transform_Matrix
e_util_transform_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect)
{
   E_Util_Transform_Matrix result;
   e_util_transform_matrix_load_identity(&result);

   if (!transform) return result;
   if (!source_rect) return result;

   double dest_w = source_rect->w * transform->scale.value[0];
   double dest_h = source_rect->h * transform->scale.value[1];

   e_util_transform_matrix_translate(&result, -source_rect->x - source_rect->w / 2.0, -source_rect->y - source_rect->h / 2.0, 0.0);
   e_util_transform_matrix_scale(&result, transform->scale.value[0], transform->scale.value[1], transform->scale.value[2]);

   if (!E_UTIL_TRANSFORM_IS_ZERO(transform->rotation.value[0]))
      e_util_transform_matrix_rotation_x(&result, transform->rotation.value[0]);
   if (!E_UTIL_TRANSFORM_IS_ZERO(transform->rotation.value[1]))
      e_util_transform_matrix_rotation_y(&result, transform->rotation.value[1]);
   if (!E_UTIL_TRANSFORM_IS_ZERO(transform->rotation.value[2]))
      e_util_transform_matrix_rotation_z(&result, transform->rotation.value[2]);

   e_util_transform_matrix_translate(&result, source_rect->x + transform->move.value[0] + (dest_w / 2.0),
                                     source_rect->y + transform->move.value[1] + (dest_h / 2.0), 0.0);

   return result;
}

E_API Eina_Bool
e_util_transform_change_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->changed;
}

E_API void
e_util_transform_change_unset(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->changed = EINA_FALSE;
}

E_API void
e_util_transform_keep_ratio_set(E_Util_Transform *transform, Eina_Bool enable)
{
   if (!transform) return;
   if (transform->keep_ratio == enable) return;
   transform->keep_ratio = enable;
   transform->changed = EINA_TRUE;
}

E_API Eina_Bool
e_util_transform_keep_ratio_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->keep_ratio;
}

E_API E_Util_Transform
e_util_transform_keep_ratio_apply(E_Util_Transform *transform, int origin_w, int origin_h)
{
   int i;
   E_Util_Transform result;
   E_Util_Transform_Vertex move_ver;
   E_Util_Transform_Matrix matrix;

   e_util_transform_vertex_init(&move_ver, 0.0, 0.0, 0.0, 1.0);
   e_util_transform_matrix_load_identity(&matrix);

   if (!transform) return result;

   memcpy(&result, transform, sizeof(E_Util_Transform));

   if (transform->scale.value[0] > transform->scale.value[1])
      result.scale.value[0] = result.scale.value[1] = transform->scale.value[1];
   else
      result.scale.value[0] = result.scale.value[1] = transform->scale.value[0];

   move_ver.vertex[0] += (transform->scale.value[0] - result.scale.value[0]) * origin_w * 0.5;
   move_ver.vertex[1] += (transform->scale.value[1] - result.scale.value[1]) * origin_h * 0.5;

   for(i = 0 ; i < 3 ; ++i)
      result.move.value[i] += move_ver.vertex[i];

   return result;
}

E_API void
e_util_transform_move_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->move.value[0];
   if (y) *y = transform->move.value[1];
   if (z) *z = transform->move.value[2];
}

E_API void
e_util_transform_scale_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->scale.value[0];
   if (y) *y = transform->scale.value[1];
   if (z) *z = transform->scale.value[2];
}

E_API void
e_util_transform_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->rotation.value[0];
   if (y) *y = transform->rotation.value[1];
   if (z) *z = transform->rotation.value[2];
}

E_API void
e_util_transform_log(E_Util_Transform *transform, const char *str)
{
   if (!transform) return;
   if (!str) return;

   printf("[e_util_transform_log : %s\n", str);
   printf("[move     : %2.1f, %2.1f, %2.1f]\n", transform->move.value[0], transform->move.value[1], transform->move.value[2]);
   printf("[scale    : %2.1f, %2.1f, %2.1f]\n", transform->scale.value[0], transform->scale.value[1], transform->scale.value[2]);
   printf("[rotation : %2.1f, %2.1f, %2.1f]\n", transform->rotation.value[0], transform->rotation.value[1], transform->rotation.value[2]);

}

E_API void
e_util_transform_rect_init(E_Util_Transform_Rect *rect, int x, int y, int w, int h)
{
   if (!rect) return;

   rect->x = x;
   rect->y = y;
   rect->w = w;
   rect->h = h;
}

E_API void
e_util_transform_rect_client_rect_get(E_Util_Transform_Rect *rect, E_Client *ec)
{
   if (!rect || !ec) return;
   e_util_transform_rect_init(rect, ec->x, ec->y, ec->w, ec->h);
}

E_API E_Util_Transform_Rect_Vertex
e_util_transform_rect_to_vertices(E_Util_Transform_Rect *rect)
{
   E_Util_Transform_Rect_Vertex result;
   int i;

   e_util_transform_vertices_init(&result);

   if (!rect) return result;

   // LT, RT, RB, LB
   result.vertices[0].vertex[0] = rect->x;
   result.vertices[0].vertex[1] = rect->y;

   result.vertices[1].vertex[0] = rect->x + rect->w;
   result.vertices[1].vertex[1] = rect->y;

   result.vertices[2].vertex[0] = rect->x + rect->w;
   result.vertices[2].vertex[1] = rect->y + rect->h;

   result.vertices[3].vertex[0] = rect->x;
   result.vertices[3].vertex[1] = rect->y + rect->h;

   for (i = 0 ; i < 4 ; ++i)
     {
        result.vertices[i].vertex[2] = 1.0;
        result.vertices[i].vertex[3] = 1.0;
     }

   return result;
}


E_API void
e_util_transform_vertex_init(E_Util_Transform_Vertex *vertex, double x, double y, double z, double w)
{
   if (!vertex) return;

   vertex->vertex[0] = x;
   vertex->vertex[1] = y;
   vertex->vertex[2] = z;
   vertex->vertex[3] = w;
}

E_API void
e_util_transform_vertex_pos_get(E_Util_Transform_Vertex *vertex, double *x, double *y, double *z, double *w)
{
   if (!vertex) return;

   if (x) *x = vertex->vertex[0];
   if (y) *y = vertex->vertex[1];
   if (z) *z = vertex->vertex[2];
   if (w) *w = vertex->vertex[3];
}

E_API void
e_util_transform_vertices_init(E_Util_Transform_Rect_Vertex *vertices)
{
   int i;
   if (!vertices) return;

   for (i = 0 ; i < 4 ; ++i)
      e_util_transform_vertex_init(&vertices->vertices[i], 0.0, 0.0, 0.0, 1.0);
}

E_API E_Util_Transform_Rect
e_util_transform_vertices_to_rect(E_Util_Transform_Rect_Vertex *vertices)
{
   E_Util_Transform_Rect result;
   e_util_transform_rect_init(&result, 0, 0, 0, 0);

   if (vertices)
     {
        result.x = (int)(vertices->vertices[0].vertex[0] + 0.5);
        result.y = (int)(vertices->vertices[0].vertex[1] + 0.5);
        result.w = (int)(vertices->vertices[2].vertex[0] - vertices->vertices[0].vertex[0] + 0.5);
        result.h = (int)(vertices->vertices[2].vertex[1] - vertices->vertices[0].vertex[1] + 0.5);
     }

   return result;
}

E_API void
e_util_transform_vertices_pos_get(E_Util_Transform_Rect_Vertex *vertices, int index,
                                  double *x, double *y, double *z, double *w)
{
   if (!vertices) return;
   if (index < 0 || index >= 4) return;

   e_util_transform_vertex_pos_get(&vertices->vertices[index], x, y, z, w);
}

E_API void
e_util_transform_matrix_load_identity(E_Util_Transform_Matrix *matrix)
{
   if (!matrix) return;

   matrix->mat[0][0] = 1; matrix->mat[0][1] = 0; matrix->mat[0][2] = 0; matrix->mat[0][3] = 0;
   matrix->mat[1][0] = 0; matrix->mat[1][1] = 1; matrix->mat[1][2] = 0; matrix->mat[1][3] = 0;
   matrix->mat[2][0] = 0; matrix->mat[2][1] = 0; matrix->mat[2][2] = 1; matrix->mat[2][3] = 0;
   matrix->mat[3][0] = 0; matrix->mat[3][1] = 0; matrix->mat[3][2] = 0; matrix->mat[3][3] = 1;
}

E_API void
e_util_transform_matrix_translate(E_Util_Transform_Matrix *matrix, double x, double y, double z)
{
   E_Util_Transform_Matrix source;

   if (!matrix) return;

   source = *matrix;

   //	| 1 0 0 dx|     |m00 m01 m02 m03|
   //	| 0 1 0 dy|     |m10 m11 m12 m13|
   //	| 0 0 1 dz|  *  |m20 m21 m22 m23|
   //	| 0 0 0 1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = source.mat[0][0] + x * source.mat[3][0];
   matrix->mat[0][1] = source.mat[0][1] + x * source.mat[3][1];
   matrix->mat[0][2] = source.mat[0][2] + x * source.mat[3][2];
   matrix->mat[0][3] = source.mat[0][3] + x * source.mat[3][3];

   matrix->mat[1][0] = source.mat[1][0] + y * source.mat[3][0];
   matrix->mat[1][1] = source.mat[1][1] + y * source.mat[3][1];
   matrix->mat[1][2] = source.mat[1][2] + y * source.mat[3][2];
   matrix->mat[1][3] = source.mat[1][3] + y * source.mat[3][3];

   matrix->mat[2][0] = source.mat[2][0] + z * source.mat[3][0];
   matrix->mat[2][1] = source.mat[2][1] + z * source.mat[3][1];
   matrix->mat[2][2] = source.mat[2][2] + z * source.mat[3][2];
   matrix->mat[2][3] = source.mat[2][3] + z * source.mat[3][3];
}

E_API void
e_util_transform_matrix_rotation_x(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   // | 1  0  0  0 |     |m00 m01 m02 m03|
   // | 0  c -s  0 |     |m10 m11 m12 m13|
   // | 0  s  c  0 | *   |m20 m21 m22 m23|
   // | 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[1][0] = c * source.mat[1][0] + (-s) * source.mat[2][0];
   matrix->mat[1][1] = c * source.mat[1][1] + (-s) * source.mat[2][1];
   matrix->mat[1][2] = c * source.mat[1][2] + (-s) * source.mat[2][2];
   matrix->mat[1][3] = c * source.mat[1][3] + (-s) * source.mat[2][3];

   matrix->mat[2][0] = s * source.mat[1][0] + c * source.mat[2][0];
   matrix->mat[2][1] = s * source.mat[1][1] + c * source.mat[2][1];
   matrix->mat[2][2] = s * source.mat[1][2] + c * source.mat[2][2];
   matrix->mat[2][3] = s * source.mat[1][3] + c * source.mat[2][3];
}

E_API void
e_util_transform_matrix_rotation_y(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   // | c  0  s  0 |     |m00 m01 m02 m03|
   // | 0  1  0  0 |     |m10 m11 m12 m13|
   // |-s  0  c  0 | *   |m20 m21 m22 m23|
   // | 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = c * source.mat[0][0] + s * source.mat[2][0];
   matrix->mat[0][1] = c * source.mat[0][1] + s * source.mat[2][1];
   matrix->mat[0][2] = c * source.mat[0][2] + s * source.mat[2][2];
   matrix->mat[0][3] = c * source.mat[0][3] + s * source.mat[2][3];

   matrix->mat[2][0] = (-s) * source.mat[0][0] + c * source.mat[2][0];
   matrix->mat[2][0] = (-s) * source.mat[0][1] + c * source.mat[2][1];
   matrix->mat[2][0] = (-s) * source.mat[0][2] + c * source.mat[2][2];
   matrix->mat[2][0] = (-s) * source.mat[0][3] + c * source.mat[2][3];
}

E_API void
e_util_transform_matrix_rotation_z(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   //	| c -s  0  0 |     |m00 m01 m02 m03|
   //	| s  c  0  0 |     |m10 m11 m12 m13|
   //	| 0  0  1  0 |  *  |m20 m21 m22 m23|
   //	| 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = c * source.mat[0][0] + (-s) * source.mat[1][0];
   matrix->mat[0][1] = c * source.mat[0][1] + (-s) * source.mat[1][1];
   matrix->mat[0][2] = c * source.mat[0][2] + (-s) * source.mat[1][2];
   matrix->mat[0][3] = c * source.mat[0][3] + (-s) * source.mat[1][3];

   matrix->mat[1][0] = s * source.mat[0][0] + c * source.mat[1][0];
   matrix->mat[1][1] = s * source.mat[0][1] + c * source.mat[1][1];
   matrix->mat[1][2] = s * source.mat[0][2] + c * source.mat[1][2];
   matrix->mat[1][3] = s * source.mat[0][3] + c * source.mat[1][3];
}

E_API void
e_util_transform_matrix_scale(E_Util_Transform_Matrix *matrix, double sx, double sy, double sz)
{
   E_Util_Transform_Matrix source;

   if (!matrix) return;

   source = *matrix;

   //	| sx 0 0 0|     |m00 m01 m02 m03|
   //	| 0 sy 0 0|     |m10 m11 m12 m13|
   //	| 0 0 sz 0|  *  |m20 m21 m22 m23|
   //	| 0 0  0 1|     |m30 m31 m32 m33|

   matrix->mat[0][0] = sx * source.mat[0][0];
   matrix->mat[0][1] = sx * source.mat[0][1];
   matrix->mat[0][2] = sx * source.mat[0][2];
   matrix->mat[0][3] = sx * source.mat[0][3];

   matrix->mat[1][0] = sy * source.mat[1][0];
   matrix->mat[1][1] = sy * source.mat[1][1];
   matrix->mat[1][2] = sy * source.mat[1][2];
   matrix->mat[1][3] = sy * source.mat[1][3];

   matrix->mat[2][0] = sz * source.mat[2][0];
   matrix->mat[2][1] = sz * source.mat[2][1];
   matrix->mat[2][2] = sz * source.mat[2][2];
   matrix->mat[2][3] = sz * source.mat[2][3];
}

E_API E_Util_Transform_Matrix
e_util_transform_matrix_multiply(E_Util_Transform_Matrix *matrix1,
                                 E_Util_Transform_Matrix *matrix2)
{
   E_Util_Transform_Matrix result;
   int row, col, i;
   e_util_transform_matrix_load_identity(&result);

   if (!matrix1) return result;
   if (!matrix2) return result;
   // |m00 m01 m02 m03|   |m00 m01 m02 m03|
   // |m10 m11 m12 m13|   |m10 m11 m12 m13|
   // |m20 m21 m22 m23| * |m20 m21 m22 m23|
   // |m30 m31 m32 m33|   |m30 m31 m32 m33|

   for (row = 0 ; row < 4 ; ++row)
     {
        for (col = 0 ; col < 4; ++col)
          {
             double sum = 0.0;

             for (i = 0 ; i < 4 ; ++i)
               {
                  sum += matrix1->mat[row][i] * matrix2->mat[i][col];
               }

             result.mat[row][col] = sum;
          }
     }

   return result;
}

E_API E_Util_Transform_Vertex
e_util_transform_matrix_multiply_vertex(E_Util_Transform_Matrix *matrix,
                                        E_Util_Transform_Vertex *vertex)
{
   E_Util_Transform_Vertex result;
   int row, col;

   e_util_transform_vertex_init(&result, 0.0, 0.0, 0.0, 1.0);
   if (!vertex) return result;
   if (!matrix) return result;

   // |m00 m01 m02 m03|   |x|
   // |m10 m11 m12 m13|   |y|
   // |m20 m21 m22 m23| * |z|
   // |m30 m31 m32 m33|   |w|

   for (row = 0 ; row < 4 ; ++row)
     {
        double sum = 0.0;

        for (col = 0 ; col < 4; ++col)
          {
             sum += matrix->mat[row][col] * vertex->vertex[col];
          }

        result.vertex[row] = sum;
     }

   return result;
}

E_API E_Util_Transform_Rect_Vertex
e_util_transform_matrix_multiply_rect_vertex(E_Util_Transform_Matrix *matrix,
                                             E_Util_Transform_Rect_Vertex *vertices)
{
   E_Util_Transform_Rect_Vertex result;
   int i;
   e_util_transform_vertices_init(&result);

   if (!matrix) return result;
   if (!vertices) return result;

   for (i = 0 ; i < 4 ; ++i)
      result.vertices[i] = e_util_transform_matrix_multiply_vertex(matrix, &vertices->vertices[i]);

   return result;
}

E_API Eina_Bool
e_util_transform_matrix_equal_check(E_Util_Transform_Matrix *matrix,
                                    E_Util_Transform_Matrix *matrix2)
{
   int row, col;
   Eina_Bool result = EINA_TRUE;
   if (!matrix || !matrix2) return EINA_FALSE;

   for (row = 0 ; row < 4 && result ; ++row)
     {
        for (col = 0 ; col < 4 ; ++col)
          {
             if (matrix->mat[row][col] != matrix2->mat[row][col])
               {
                  result = EINA_FALSE;
                  break;
               }
          }
     }

   return result;
}
