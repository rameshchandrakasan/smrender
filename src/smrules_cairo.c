/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_CAIRO

#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <ctype.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#include <cairo.h>
#ifdef CAIRO_HAS_FT_FONT
#include <cairo-ft.h>
#endif
#include <cairo-pdf.h>
#define mm2unit(x) mm2ptf(x)
#define THINLINE rdata_px_unit(1, U_PT)
#define mm2wu(x) ((x) == 0.0 ? THINLINE : mm2unit(x))

#include "smrender_dev.h"
#include "smcoast.h"
#include "rdata.h"
//#include "memimg.h"

#define COL_COMPD(x,y) ((double) (((x) >> (y)) & 0xff) / 255.0)
#define REDD(x) COL_COMPD(x, 16)
#define GREEND(x) COL_COMPD(x, 8)
#define BLUED(x) COL_COMPD(x, 0)
#define ALPHAD(x) (1.0 - COL_COMPD(x & 0x7f000000, 23))


static cairo_surface_t *sfc_;


static cairo_status_t cro_log_status(cairo_t *ctx)
{
   cairo_status_t e;

   if ((e = cairo_status(ctx)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "error in libcairo: %s", cairo_status_to_string(e));

   return e;
}


static void cro_set_source_color(cairo_t *ctx, int col)
{
   cairo_set_source_rgba(ctx, REDD(col), GREEND(col), BLUED(col), ALPHAD(col));
}


static cairo_rectangle_t ext_;


static cairo_surface_t *cro_surface(void)
{
   cairo_surface_t *sfc;
   cairo_status_t e;

   sfc = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext_);
   if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(e));
      return NULL;
   }
   cairo_surface_set_fallback_resolution(sfc, rdata_dpi(), rdata_dpi());
   return sfc;
}


void init_main_image(struct rdata *rd, const char *bg)
{
   cairo_t *ctx;

   ext_.x = 0;
   ext_.y = 0;
   ext_.width = rdata_width(U_PT);
   ext_.height = rdata_height(U_PT);

   if ((sfc_ = cro_surface()) == NULL)
      exit(EXIT_FAILURE);

   if (bg != NULL)
      set_color("bgcolor", parse_color(bg));

   ctx = cairo_create(sfc_);
   cro_set_source_color(ctx, parse_color("bgcolor"));
   cairo_paint(ctx);
   cairo_destroy(ctx);

   log_msg(LOG_DEBUG, "background color is set to 0x%08x", parse_color("bgcolor"));
}


static cairo_status_t cro_write_func(void *closure, const unsigned char *data, unsigned int length)
{
   return fwrite(data, length, 1, closure) == 1 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}


void save_main_image(FILE *f, int ftype)
{
   cairo_surface_t *sfc;
   cairo_status_t e;
   cairo_t *dst;

   log_msg(LOG_INFO, "saving image (ftype = %d)", ftype);

   switch (ftype)
   {
      case FTYPE_PNG:
         sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rdata_width(U_PX), rdata_height(U_PX));
         dst = cairo_create(sfc);
         cro_log_status(dst);
         cairo_scale(dst, (double) rdata_dpi() / 72, (double) rdata_dpi() / 72);
         cairo_set_source_surface(dst, sfc_, 0, 0);
         cairo_paint(dst);
         cairo_destroy(dst);
         if ((e = cairo_surface_write_to_png_stream(sfc, cro_write_func, f)) != CAIRO_STATUS_SUCCESS)
            log_msg(LOG_ERR, "failed to save png image: %s", cairo_status_to_string(e));
         cairo_surface_destroy(sfc);
         return;

      case FTYPE_PDF:
         log_debug("width = %.2f pt, height = %.2f pt", rdata_width(U_PT), rdata_height(U_PT));
         sfc = cairo_pdf_surface_create_for_stream(cro_write_func, f, rdata_width(U_PT), rdata_height(U_PT));
         cairo_pdf_surface_restrict_to_version(sfc, CAIRO_PDF_VERSION_1_4);
         dst = cairo_create(sfc);
         cro_log_status(dst);
         cairo_set_source_surface(dst, sfc_, 0, 0);
         cairo_paint(dst);
         cairo_show_page(dst);
         cairo_destroy(dst);
         cairo_surface_destroy(sfc);
         return;
   }
 
   log_msg(LOG_WARN, "cannot save image, file type %d not implemented yet", ftype);
}


int save_image(const char *s, void *img, int ftype)
{
   if (!ftype)
      return cairo_surface_write_to_png(img, s) == CAIRO_STATUS_SUCCESS ? 0 : -1;

   // FIXME
   log_msg(LOG_ERR, "other file types than png not implemented yet");
   return -1;
}


int get_pixel(struct rdata *rd, int x, int y)
{
   return 0;
}


void *create_tile(void)
{
   return NULL;
}


void delete_tile(void *img)
{
}


void cut_tile(const struct bbox *bb, void *img)
{
}


void reduce_resolution(struct rdata *rd)
{
}


int cro_pixel_pos(int x, int y, int s)
{
   return x * sizeof (uint32_t) + y * s;
}


int cro_get_pixel(cairo_surface_t *sfc, int x, int y)
{
   unsigned char *data;

   /*if (x < 0 || y < 0 || x >= cairo_image_surface_get_width(sfc) || x >= cairo_image_surface_get_height(sfc))
      return 0;*/

   cairo_surface_flush(sfc);
   if ((data = cairo_image_surface_get_data(sfc)) == NULL)
      return 0;

   return *(data + cro_pixel_pos(x, y, cairo_image_surface_get_stride(sfc)));
}


int act_draw_ini(smrule_t *r)
{
   struct actDraw *d;
   double a;
   char *s;

   // just to be on the safe side
   if ((r->oo->type != OSM_WAY) && (r->oo->type != OSM_REL))
   {
      log_msg(LOG_WARN, "'draw' may be applied to ways or relations only");
      return 1;
   }

   if ((d = malloc(sizeof(*d))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memset(d, 0, sizeof(*d));
   r->data = d;

   // parse fill settings
   if ((s = get_param("color", NULL, r->act)) != NULL)
   {
      d->fill.col = parse_color(s);
      d->fill.used = 1;
   }
   if (get_param("width", &d->fill.width, r->act) == NULL)
      d->fill.width = 0;
   d->fill.style = parse_style(get_param("style", NULL, r->act));

   // parse border settings
   if ((s = get_param("bcolor", NULL, r->act)) != NULL)
   {
      d->border.col = parse_color(s);
      d->border.used = 1;
   }
   if (get_param("bwidth", &d->border.width, r->act) == NULL)
      d->border.width = 0;
   d->border.style = parse_style(get_param("bstyle", NULL, r->act));

   // honor direction of ways
   if (get_param("directional", &a, r->act) == NULL)
      a = 0;
   d->directional = a != 0;

   if (get_param("ignore_open", &a, r->act) == NULL)
      a = 0;
   d->collect_open = a == 0;

   d->wl = init_wlist();

   d->ctx = cairo_create(sfc_);
   if (cro_log_status(d->ctx) != CAIRO_STATUS_SUCCESS)
   {
      free(d);
      r->data = NULL;
      return -1;
   }
   cairo_push_group(d->ctx);

   sm_threaded(r);

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

   return 0;
}


/*! Create a cairo path from a way.
 */
static void cro_poly_line(const osm_way_t *w, cairo_t *ctx)
{
   osm_node_t *n;
   double x, y;
   int i;

   cairo_new_path(ctx);
   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld at pos %d does not exist", (long) w->ref[i], (long) w->obj.id, i);
         continue;
      }

      geo2pxf(n->lon, n->lat, &x, &y);
      x = rdata_px_unit(x, U_PT);
      y = rdata_px_unit(y, U_PT);
      cairo_line_to(ctx, x, y);
   }
}


/*! Calculate the linewidth.
 *  @param d Pointer to struct actDraw.
 *  @param border 0 if fill width for open polygons needed or 1 for border width.
 *  @param closed 0 if open polygon or 1 if closed.
 *
 * Possible combinations of fill widths
 *                  | open fill  | open border | closed fill | closed border
 *  b_used,  f_used | fw         | 2bw+fw      | -           | 2bw
 *  b_used, !f_used | -          |  bw 1)      | -           |  bw
 * !b_used,  f_used | fw         | -           | -           | -
 * !b_used, !f_used | -          | -           | -           | -
 *
 * remark 1) this could also be 2bw.
 */
static double cro_border_width(const struct actDraw *d, int closed)
{
   if (!d->fill.used)
      return mm2wu(d->border.width);

   if (!closed)
      return mm2wu(2 * d->border.width) + mm2wu(d->fill.width);

   return mm2wu(2 * d->border.width);
}


static double cro_fill_width(const struct actDraw *d)
{
   return mm2wu(d->fill.width);
}


/*! Render the way properly to the cairo context.
 */
static void render_poly_line(cairo_t *ctx, const struct actDraw *d, const osm_way_t *w, int cw)
{
   if (d->border.used)
   {
      cro_set_source_color(ctx, d->border.col);
      cairo_set_line_width(ctx, cro_border_width(d, is_closed_poly(w)));
      cro_poly_line(w, ctx);
      cairo_stroke(ctx);
   }

   if (d->fill.used)
   {
      cro_poly_line(w, ctx);
      if (cw)  // this should only be allowed if it is a closed polygon
      {
         //log_debug("cw: clearing");
         cairo_save(ctx);
         cairo_set_operator(ctx, CAIRO_OPERATOR_CLEAR);
         cairo_fill(ctx);
         cairo_restore(ctx);
      }
      else
      {
         //log_debug("ccw: filling with #%08x", d->fill.col);
         cro_set_source_color(ctx, d->fill.col);
         if (is_closed_poly(w))
            cairo_fill(ctx);
         else
         {
            cairo_set_line_width(ctx, cro_fill_width(d));
            cairo_stroke(ctx);
         }
      }
   }
}
 

int act_draw_main(smrule_t *r, osm_obj_t *o)
{
#ifdef WITH_THREADS
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
   struct actDraw *d = r->data;
   osm_way_t *w;
   int i, e;

   if (o->type == OSM_WAY)
   {
      if (!is_closed_poly((osm_way_t*) o))
      {
         if (!d->collect_open)
            return 0;

         render_poly_line(d->ctx, d, (osm_way_t*) o, 0);
         return 0;
      }

      if (!d->directional)
      {
         render_poly_line(d->ctx, d, (osm_way_t*) o, 0);
         return 0;
      }

#ifdef WITH_THREADS
      pthread_mutex_lock(&mutex);
#endif
      (void) gather_poly0((osm_way_t*) o, &d->wl);
#ifdef WITH_THREADS
      pthread_mutex_unlock(&mutex);
#endif
      return 0;
   }
   else if (o->type == OSM_REL)
   {
      for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
      {
         if (((osm_rel_t*) o)->mem[i].type != OSM_WAY)
            continue;
         if ((w = get_object(OSM_WAY, ((osm_rel_t*) o)->mem[i].id)) == NULL)
            //FIXME: error message may be output here
            continue;
         if ((e = act_draw_main(r, (osm_obj_t*) w)) < 0)
            return e;
         if (e)
            log_msg(LOG_WARN, "draw(way from relation) returned %d", e);
      }
      return 0;
   }

   log_msg(LOG_WARN, "draw() may not be applied to object type %d", o->type);
   return 1;
}


int act_draw_fini(smrule_t *r)
{
   struct actDraw *d = r->data;
   //cairo_t *dst, *ctx;
   int i;

   cairo_pop_group_to_source(d->ctx);
   cairo_paint(d->ctx);

   if (d->directional)
   {
      log_debug("rendering directional polygons (ref_cnt = %d)", d->wl->ref_cnt);
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         if (is_closed_poly(d->wl->ref[i].w))
         {
            poly_area(d->wl->ref[i].w, NULL, &d->wl->ref[i].area);
            if (d->wl->ref[i].area < 0)
            {
               d->wl->ref[i].area = fabs(d->wl->ref[i].area);
               d->wl->ref[i].cw = d->directional;
            }
         }
      }
      qsort(d->wl->ref, d->wl->ref_cnt, sizeof(struct poly), (int(*)(const void *, const void *)) compare_poly_area);

      cairo_push_group(d->ctx);
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         log_debug("cw = %d, area = %f", d->wl->ref[i].cw, d->wl->ref[i].area);
         render_poly_line(d->ctx, d, d->wl->ref[i].w, d->wl->ref[i].cw);

      }
      cairo_pop_group_to_source(d->ctx);
      cairo_paint(d->ctx);
   }

   cairo_destroy(d->ctx);
   free(d);
   r->data = NULL;

   return 0;
}


int act_cap_ini(smrule_t *r)
{
   struct actCaption cap;
   char *s;
#ifdef CAIRO_HAS_FC_FONT
   cairo_font_face_t *cfc;
   FcPattern *pat;
#endif

   memset(&cap, 0, sizeof(cap));

   if ((cap.font = get_param("font", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'font' missing");
      return 1;
   }
   if (get_param("size", &cap.size, r->act) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'size' missing");
      return 1;
   }
   if ((cap.key = get_param("key", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'key' missing");
      return 1;
   }
   if (*cap.key == '*')
   {
      cap.key++;
      cap.pos |= POS_UC;
   }
   if ((s = get_param("color", NULL, r->act)) != NULL)
      cap.col = parse_color(s);
   if ((s = get_param("angle", &cap.angle, r->act)) != NULL)
   {
      if (!strcmp(s, "auto"))
      {
         cap.angle = NAN;
         cap.rot.autocol = parse_color("bgcolor");
         if ((s = get_param("auto-color", NULL, r->act)) != NULL)
         {
            cap.rot.autocol = parse_color(s);
         }
         if ((s = get_param("weight", &cap.rot.weight, r->act)) == NULL)
            cap.rot.weight = 1;
         (void) get_param("phase", &cap.rot.phase, r->act);
      }
   }
   if ((s = get_param("halign", NULL, r->act)) != NULL)
   {
      if (!strcmp(s, "east"))
         cap.pos |= POS_E;
      else if (!strcmp(s, "west"))
         cap.pos |= POS_W;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }
   if ((s = get_param("valign", NULL, r->act)) != NULL)
   {
      if (!strcmp(s, "north"))
         cap.pos |= POS_N;
      else if (!strcmp(s, "south"))
         cap.pos |= POS_S;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }

   cap.ctx = cairo_create(sfc_);
   if (cro_log_status(cap.ctx) != CAIRO_STATUS_SUCCESS)
      return -1;

#ifdef CAIRO_HAS_FC_FONT
   if ((pat = FcNameParse((FcChar8*) cap.font)) == NULL)
   {
      log_msg(LOG_ERR, "FcNameParse(\"%s\") failed", cap.font);
      return -1;
   }
   cfc = cairo_ft_font_face_create_for_pattern(pat);
   FcPatternDestroy(pat);
   cairo_set_font_face(cap.ctx, cfc);
   cairo_font_face_destroy(cfc); 
#else
   cairo_select_font_face (cap.ctx, cap.font, 0, 0);
#endif

   cro_set_source_color(cap.ctx, cap.col);
   //cairo_set_font_size(cap.ctx, mm2unit(cap.size));

   cairo_push_group(cap.ctx);

   if ((r->data = malloc(sizeof(cap))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   // activate multi-threading if angle is not "auto"
   if (!isnan(cap.angle))
      sm_threaded(r);

   log_msg(LOG_DEBUG, "%04x, %08x, '%s', '%s', %.1f, %.1f, {%.1f, %08x, %.1f}",
         cap.pos, cap.col, cap.font, cap.key, cap.size, cap.angle,
         cap.rot.phase, cap.rot.autocol, cap.rot.weight);
   memcpy(r->data, &cap, sizeof(cap));
   return 0;
}


#if 0
static double find_angle(cairo_surface_t *src)
{
   // FIXME: this is not finished yet!
   struct diff_vec *dv;
   //gdImage *cap_img;
   int x, y, rx, ry, ox, oy, off;
   int n;

   if ((m = match_attr((osm_obj_t*) n, cap->key, NULL)) == -1)
   {
      //log_debug("node %ld has no caption tag '%s'", nd->nd.id, rl->rule.cap.key);
      return 0;
   }

   if ((v = malloc(n->obj.otag[m].v.len + 1)) == NULL)
      log_msg(LOG_ERR, "failed to copy caption string: %s", strerror(errno)), exit(EXIT_FAILURE);
   memcpy(v, n->obj.otag[m].v.buf, n->obj.otag[m].v.len);
   v[n->obj.otag[m].v.len] = '\0';

   if (cap->pos & POS_UC)
      for (x = 0; x < n->obj.otag[m].v.len; x++)
         v[x] = toupper((unsigned) v[x]);

   geo2pxf(n->lon, n->lat, &x, &y);
   x = rdata_px_unit(x, U_PT);
   y = rdata_px_unit(y, U_PT);

   if ((n = get_diff_vec(sfc_, src, x, y, MAX_OFFSET, 10, &dv)) == -1)
      return -1;
      
   weight_diff_vec(dv, n * MAX_OFFSET, DEG2RAD(cap->rot.phase), cap->rot.weight);
   index_diff_vec(dv, n * MAX_OFFSET);
   qsort(dv, n * MAX_OFFSET, sizeof(*dv), (int(*)(const void*, const void*)) cmp_dv);

   m = diff_vec_count_eq(dv, n * MAX_OFFSET);
   if (m > 1)
   {
      //log_debug("m = %d", m);
      ma = RAD2DEG((dv[0].dv_angle + dv[m - 1].dv_angle) / 2.0);
      //ma = (dv[0].dv_angle + dv[m - 1].dv_angle) / 2.0;
      off = (dv[0].dv_x + dv[m - 1].dv_x) / 2;
   }
   else
   {
      ma = RAD2DEG(dv->dv_angle);
      //ma = dv->dv_angle;
      off = dv->dv_x;
   }

   oy =(br[1] - br[5]) / DIVX;
   if ((ma < 90) || (ma >= 270))
   {
      ox = off;
   }
   else
   {
      ma -= 180;
      ox = br[0] - br[2] - off;
   }
   log_debug("ma = %.1f, off = %d, ox = %d, oy = %d", ma, off, ox, oy);
}
#endif


static void strupper(char *s)
{
   if (s == NULL)
      return;

   for (; *s != '\0'; s++)
      *s = toupper(/*(unsigned)*/ *s);
}


#define POS_OFFSET mm2ptf(1)
static void pos_offset(const cairo_text_extents_t *tx, int pos, double *ox, double *oy)
{
   switch (pos & 0x3)
   {
      case POS_N:
         *oy = 0 - POS_OFFSET;
         break;

      case POS_S:
         *oy = tx->height + POS_OFFSET;
         break;

      default:
         *oy = tx->height / 2;
   }
   switch (pos & 0xc)
   {
      case POS_E:
         *ox = 0 + POS_OFFSET;
         break;

      case POS_W:
         *ox = -tx->width - POS_OFFSET;
         break;

      default:
         *ox = -tx->width / 2;
   }
}


static cairo_surface_t *cro_cut_out(int x, int y, double r)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;

   sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, round(r), round(r));
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create background surface: %s",
            cairo_status_to_string(cairo_surface_status(sfc)));
      return NULL;
   }
   ctx = cairo_create(sfc);
   cairo_set_source_surface(ctx, sfc_, -x - r / 2, -y - r / 2);
   cairo_paint(ctx);
   cairo_destroy(ctx);

   return sfc;
}


static cairo_surface_t *cro_plane(int w, int h, int x, int col)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;

   sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create surface: %s",
            cairo_status_to_string(cairo_surface_status(sfc)));
      return NULL;
   }

   ctx = cairo_create(sfc);
   cro_set_source_color(ctx, col);
   cairo_rectangle(ctx, x, 0, w - x, h);
   cairo_fill(ctx);
   cairo_destroy(ctx);

   return sfc;
}


static inline double min(double a, double b)
{
   return a < b ? a : b;
}


static inline double max(double a, double b)
{
   return a > b ? a : b;
}


static double color_max(double r, double g, double b)
{
   return max(max(r, g), b);
}


static double color_min(double r, double g, double b)
{
   return min(min(r, g), b);
}

static double color_to_gray(double r, double g, double b)
{
   return (color_max(r, g, b) + color_min(r, g, b)) / 2;
}


static double pix_avg(cairo_surface_t *sfc)
{
   unsigned char *p;
   uint32_t pixel;
   double avg;
   int x, y, mx, my;

   p = cairo_image_surface_get_data(sfc);
   mx = cairo_image_surface_get_width(sfc);
   my = cairo_image_surface_get_height(sfc);
   avg = 0;

   for (y = 0; y < my; y++)
   {
      for (x = 0; x < mx; x++)
      {
         pixel = *(((uint32_t*) p) + x);
         avg += (double) (pixel & 0xff) / 255;
      }
      p += cairo_image_surface_get_stride(sfc);
   }

   return avg / (mx * my);
}


/*! 
 * @param mode If set to 0 it is rotated centered otherwise it is rotated
 * around the center of the left edge.
 */
static double diff_coord(cairo_surface_t *bg, cairo_surface_t *fg, double a)
{
   cairo_surface_t *dst;
   cairo_t *ctx;
   double d, x, y;

   x = cairo_image_surface_get_width(fg);
   y = cairo_image_surface_get_height(fg);
   dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, x, y);
   ctx = cairo_create(dst);

   // drehpunkt festlegen
   cairo_save(ctx);
   cairo_translate(ctx, x / 2, y / 2);
   // winkel, ccw, ost = 0
   cairo_rotate(ctx, a);
   // ausschneiden
   cairo_set_source_surface(ctx, bg, cairo_image_surface_get_width(bg) / -2, cairo_image_surface_get_height(bg) / -2);
   cairo_paint(ctx);
   cairo_restore(ctx);

   // difference mit zielfarbe bilden
   //cairo_set_source_rgb(ctx, 0, 1, 1);
   cairo_set_source_surface(ctx, fg, 0, 0);
   cairo_set_operator(ctx, CAIRO_OPERATOR_DIFFERENCE);
   cairo_paint(ctx);
   // oder was anderes darüber malen
   //cairo_rectangle(ctx, PT2PX(2.5), PT2PX(2.5), PT2PX(15), PT2PX(5));
   //cairo_fill(ctx);

   // auf graustufen convertieren
   cairo_set_source_rgb(ctx, 1, 1, 1);
   cairo_set_operator(ctx, CAIRO_OPERATOR_HSL_COLOR);
   cairo_paint(ctx);

   cairo_destroy(ctx);

   d = pix_avg(dst);

   char buf[64];
   snprintf(buf, sizeof(buf), "diff-%03d.png", (int) (a * 180 / M_PI));
   cairo_surface_write_to_png(dst, buf);
   // pixeldifferenz ausgeben
   printf("avg = %f, angle = %s\n", d, buf);

   cairo_surface_destroy(dst);
   return d;
}


static int cap_coord(const struct actCaption *cap, const struct coord *c, const bstring_t *str)
{
   cairo_text_extents_t tx;
   cairo_surface_t *sfc, *pat;
   char buf[str->len + 1];
   double x, y, a, r;

   cairo_save(cap->ctx);
   geo2pxf(c->lon, c->lat, &x, &y);
   x = rdata_px_unit(x, U_PT);
   y = rdata_px_unit(y, U_PT);
   log_debug("translate to %.2f, %.2f", x, y);
   cairo_translate(cap->ctx, x, y);
 
   memcpy(buf, str->buf, str->len);
   buf[str->len] = '\0';
   if (cap->pos & POS_UC)
      strupper(buf);
   log_debug("text \"%s\"", buf);

   cairo_set_font_size(cap->ctx, mm2unit(cap->size));
   cairo_text_extents(cap->ctx, buf, &tx);

   if (isnan(cap->angle))
   {
      if (cap->pos & 0xf)
      {
         r = hypot(tx.width, tx.height) * 2;
         if ((pat = cro_plane(tx.width * 2, tx.height, tx.width, cap->col)) == NULL)
            return -1;
      }
      else
      {
         r = hypot(tx.width, tx.height);
         if ((pat = cro_plane(tx.width, tx.height, 0, cap->col)) == NULL)
            return -1;
      }

      if ((sfc = cro_cut_out(x, y, r)) == NULL)
         return -1;

      //find_angle_phase1(&tx, cap->pos);
   }
   else
   {
      a = DEG2RAD(360 - cap->angle);
      pos_offset(&tx, cap->pos, &x, &y);
   }

   log_debug("move to %.2f, %.2f", x, y);
   cairo_rotate(cap->ctx, a);
   cairo_move_to(cap->ctx, x, y);
   cairo_show_text(cap->ctx, buf);
   cairo_restore(cap->ctx);

   return 0;
}


static int cap_way(const struct actCaption *cap, osm_way_t *w, const bstring_t *str)
{
   struct actCaption tmp_cap;
   struct coord c;
   double ar;

   // FIXME: captions on open polygons missing
   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return 0;

   memcpy(&tmp_cap, cap, sizeof(tmp_cap));
   if (tmp_cap.size == 0.0)
   {
      tmp_cap.size = 100 * sqrt(fabs(ar) / rdata_square_nm());
#define MIN_AUTO_SIZE 0.7
#define MAX_AUTO_SIZE 12.0
      if (tmp_cap.size < MIN_AUTO_SIZE) tmp_cap.size = MIN_AUTO_SIZE;
      if (tmp_cap.size > MAX_AUTO_SIZE) tmp_cap.size = MAX_AUTO_SIZE;
      //log_debug("r->rule.cap.size = %f (%f 1/1000)", r->rule.cap.size, r->rule.cap.size / 100 * 1000);
   }

   return cap_coord(&tmp_cap, &c, str);
}


int act_cap_main(smrule_t *r, osm_obj_t *o)
{
   struct coord c;
   int n;

   if ((n = match_attr(o, ((struct actCaption*) r->data)->key, NULL)) == -1)
   {
      //log_debug("node %ld has no caption tag '%s'", nd->nd.id, rl->rule.cap.key);
      return 0;
   }

   switch (o->type)
   {
      case OSM_NODE:
         c.lon = ((osm_node_t*) o)->lon;
         c.lat = ((osm_node_t*) o)->lat;
         return cap_coord(r->data, &c, &o->otag[n].v);

      case OSM_WAY:
         return cap_way(r->data, (osm_way_t*) o, &o->otag[n].v);
   }

   return 1;
}


int act_cap_fini(smrule_t *r)
{
   struct actCaption *cap = r->data;

   cairo_pop_group_to_source(cap->ctx);
   cairo_paint(cap->ctx);
   cairo_destroy(cap->ctx);

   free(cap);
   r->data = NULL;

   return 0;
}

 
#endif
