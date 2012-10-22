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
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <ctype.h>

#include "smrender_dev.h"
#include "smcoast.h"


int log_tags(int level, osm_obj_t *o)
{
   int len, i;
   char *buf, *s;

   for (i = 0, len = 0, buf = NULL; i < o->tag_cnt; i++)
   {
      len += o->otag[i].k.len + o->otag[i].v.len + 3;
      s = buf;
      if ((buf = realloc(s, len)) == NULL)
      {
         log_msg(LOG_ERR, "malloc() failed in log_tags(): %s", strerror(errno));
         free(s);
         return -1;
      }
      if (i)
         strcat(buf, ", ");
      else
         *buf = '\0';
      snprintf(buf + strlen(buf), len - strlen(buf), "%.*s=%.*s",
            o->otag[i].k.len, o->otag[i].k.buf, o->otag[i].v.len, o->otag[i].v.buf);

   }
   log_msg(level, "obj(%d, %ld): %s", o->type, (long) o->id, buf);
   free(buf);

   return 0;
}


#ifdef HAVE_GD
void init_main_image(struct rdata *rd, const char *bg)
{
   // preparing image
   if ((rd->img = gdImageCreateTrueColor(rd->w, rd->h)) == NULL)
      log_msg(LOG_ERR, "could not create image"), exit(EXIT_FAILURE);
   rd->col[WHITE] = gdImageColorAllocate(rd->img, 255, 255, 254);
   rd->col[BLACK] = gdImageColorAllocate(rd->img, 0, 0, 0);
   rd->col[YELLOW] = gdImageColorAllocate(rd->img, 231,209,74);
   rd->col[BLUE] = gdImageColorAllocate(rd->img, 137, 199, 178);
   rd->col[MAGENTA] = gdImageColorAllocate(rd->img, 120, 8, 44);
   rd->col[BROWN] = gdImageColorAllocate(rd->img, 154, 42, 2);
   rd->col[BGCOLOR] = bg == NULL ? 0x00ffffff : parse_color(rd, bg);
   log_msg(LOG_DEBUG, "background color is set to 0x%08x", rd->col[BGCOLOR]);
   gdImageFill(rd->img, 0, 0, rd->col[BGCOLOR]);

   if (!gdFTUseFontConfig(1))
      log_msg(LOG_NOTICE, "fontconfig library not available");
}


void reduce_resolution(struct rdata *rd)
{
   gdImage *img;

   log_msg(LOG_INFO, "resampling rendered image");
   img = gdImageCreateTrueColor(rd->fw, rd->fh);
   gdImageCopyResampled(img, rd->img, 0, 0, 0, 0, gdImageSX(img), gdImageSY(img), gdImageSX(rd->img), gdImageSY(rd->img));
   gdImageDestroy(rd->img);
   rd->img = img;
}


void save_main_image(struct rdata *rd, FILE *f)
{
   log_msg(LOG_INFO, "saving image");
   gdImagePngEx(rd->img, f, 9);
   //gdImageDestroy(rd->img);
}


/*! Convert latitude and longitude coordinates into x and y coordinates of
 * pixel image.
 */
void mk_paper_coords(double lat, double lon, struct rdata *rd, int *x, int *y)
{
   *x = round(        (                         lon    - rd->bb.ll.lon) * rd->w / rd->wc);
   *y = round(rd->h * (0.5 - (asinh(tan(DEG2RAD(lat))) - rd->lath)         / rd->lath_len));
}


void rot_rect(const struct rdata *rd, int x, int y, double a, int br[])
{
   gdPoint p[5];
   int i;

   rot_pos(br[0] - x, br[1] - y, a, &p[0].x, &p[0].y);
   rot_pos(br[2] - x, br[3] - y, a, &p[1].x, &p[1].y);
   rot_pos(br[4] - x, br[5] - y, a, &p[2].x, &p[2].y);
   rot_pos(br[6] - x, br[7] - y, a, &p[3].x, &p[3].y);

   for (i = 0; i < 4; i++)
   {
      p[i].x += x;
      p[i].y = y - p[i].y;
   }

   p[4] = p[0];

   gdImagePolygon(rd->img, p, 5, rd->col[BLACK]);
}


double weight_angle(double a, double phase, double weight)
{
   return 0.5 * (cos((a + phase) * 2) + 1) * (1 - weight) + weight;
}


double color_frequency_w(struct rdata *rd, int x, int y, int w, int h, const struct auto_rot *rot)
{
   double a, ma = 0;
   int m = 0, mm = 0;

   // auto detect angle
   for (a = 0; a < 360; a += ANGLE_DIFF)
      {
         m = col_freq(rd, x, y, w, h, DEG2RAD(a), rot->autocol)
            * weight_angle(DEG2RAD(a), DEG2RAD(rot->phase), rot->weight);
         if (mm < m)
         {
            mm = m;
            ma = a;
         }
      }
   return ma;
}


double color_frequency(struct rdata *rd, int x, int y, int w, int h, int col)
{
   struct auto_rot rot = {0, col, 1};

   return color_frequency_w(rd, x, y, w, h, &rot);
}
 

int act_cap_ini(smrule_t *r)
{
   struct actCaption cap;
   struct rdata *rd;
   char *s;

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
   rd = get_rdata();
   if ((s = get_param("color", NULL, r->act)) != NULL)
      cap.col = parse_color(rd, s);
   if ((s = get_param("angle", &cap.angle, r->act)) != NULL)
   {
      if (!strcmp(s, "auto"))
      {
         cap.angle = NAN;
         cap.rot.autocol = rd->col[BGCOLOR];
         if ((s = get_param("auto-color", NULL, r->act)) != NULL)
         {
            cap.rot.autocol = parse_color(rd, s);
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

   if ((r->data = malloc(sizeof(cap))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   log_msg(LOG_DEBUG, "%04x, %08x, '%s', '%s', %.1f, %.1f, {%.1f, %08x, %.1f}",
         cap.pos, cap.col, cap.font, cap.key, cap.size, cap.angle,
         cap.rot.phase, cap.rot.autocol, cap.rot.weight);
   memcpy(r->data, &cap, sizeof(cap));
   return 0;
}


int act_cap_way_ini(smrule_t *r)
{
   if (r->oo->type != OSM_WAY)
   {
      log_msg(LOG_ERR, "cap_way only applicable on ways");
      return -1;
   }
   return act_cap_ini(r);
}


int act_cap_node_ini(smrule_t *r)
{
   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_ERR, "cap_way only applicable on nodes");
      return -1;
   }
   return act_cap_ini(r);
}



#define POS_OFFSET MM2PX(1.3)
#define MAX_OFFSET MM2PX(2.0)
#define DIVX 3


int act_cap_node_main(smrule_t *r, osm_node_t *n)
{
   struct actCaption *cap = r->data;
   struct rdata *rd = get_rdata();
   int br[8], m, c;
   char *s, *v;
   gdFTStringExtra fte;
   int x, y, rx, ry, ox, oy, off;
   double ma;

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

   // 0x80000000 is or'd into to get non-antialiased black
   c = !rd->ovs ? cap->col : -cap->col | 0x80000000;
   mk_paper_coords(n->lat, n->lon, rd, &x, &y);
   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;
   gdImageStringFTEx(NULL, br, c, cap->font, cap->size * 2.8699, 0, x, y, v, &fte);

   if (isnan(cap->angle))
   {
#ifdef NEW_AUTOROT
      // FIXME: this is not finished yet!
      struct diff_vec *dv;
      gdImage *cap_img;
      int n;

      if ((cap_img = gdImageCreateTrueColor(br[4] - br[0], br[1] - br[5])) == NULL)
      {
         log_msg(LOG_ERR, "gdImageCreateTrueColor() failed");
         free(v);
         return -1;
      }
      gdImageSaveAlpha(cap_img, 1);
      gdImageAlphaBlending(cap_img, 0);
      gdImageStringFTEx(cap_img, br, c, cap->font, cap->size * 2.8699, 0, x, y, v, &fte);
      dv = get_diff_vec(rd->img, cap_img, x, y, &n);

#else
      ma = color_frequency_w(rd, x, y, br[4] - br[0] + MAX_OFFSET, br[1] - br[5], &cap->rot);
      //FIXME: WHITE?
      off = cf_dist(rd, x, y, br[4] - br[0], br[1] - br[5], DEG2RAD(ma), rd->col[WHITE], MAX_OFFSET);
#endif

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
   }
   else
   {
      ma = cap->angle;

      switch (cap->pos & 3)
      {
         case POS_N:
            oy = 0;
            oy = (br[7] - br[3]) / DIVX;
            break;

         case POS_S:
            oy = br[3] - br[7];
            break;

         default:
            oy = (br[3] - br[7]) / DIVX;
      }
      switch (cap->pos & 12)
      {
         case POS_E:
            ox = 0;
            break;

         case POS_W:
            ox = br[0] - br[2];
            break;

         default:
            ox = (br[0] - br[2]) / DIVX;
      }
   }

  //rot_rect(rd, x, y, DEG2RAD(ma), br);

   rot_pos(ox, oy, DEG2RAD(ma), &rx, &ry);

   if ((s = gdImageStringFTEx(rd->img, br, c, cap->font, cap->size * 2.8699, DEG2RAD(ma), x + rx, y - ry, v, &fte)) != NULL)
      log_msg(LOG_ERR, "error rendering caption: %s", s);

   free(v);
   return 0;
}


int act_cap_way_main(smrule_t *r, osm_way_t *w)
{
   struct actCaption *cap = r->data;
   struct rdata *rd = get_rdata();
   struct coord c;
   double ar, size;
   osm_node_t *n;
   int e;

   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return 0;

   n = malloc_node(w->obj.tag_cnt);
   memcpy(n->obj.otag, w->obj.otag, sizeof(struct otag) * w->obj.tag_cnt);
   n->lat = c.lat;
   n->lon = c.lon;

   size = cap->size;
   if (cap->size == 0.0)
   {
      cap->size = 100 * sqrt(fabs(ar) / (rd->mean_lat_len * rd->hc * 3600));
#define MIN_AUTO_SIZE 0.7
#define MAX_AUTO_SIZE 12.0
      if (cap->size < MIN_AUTO_SIZE) cap->size = MIN_AUTO_SIZE;
      if (cap->size > MAX_AUTO_SIZE) cap->size = MAX_AUTO_SIZE;
      //log_debug("r->rule.cap.size = %f (%f 1/1000)", r->rule.cap.size, r->rule.cap.size / 100 * 1000);
   }

   e = act_cap_node_main(r, n);
   cap->size = size;
   free_obj((osm_obj_t*) n);

   return e;
}


int act_cap_main(smrule_t *r, osm_obj_t *o)
{
   switch (o->type)
   {
      case OSM_NODE:
         return act_cap_node_main(r, (osm_node_t*) o);

      case OSM_WAY:
         return act_cap_way_main(r, (osm_way_t*) o);
   }
   log_msg(LOG_WARN, "type %d not implemented yet", o->type);
   return -1;
}


int act_cap_fini(smrule_t *r)
{
   if (r->data != NULL)
   {
      free(r->data);
      r->data = NULL;
   }
   return 0;
}


int act_cap_way_fini(smrule_t *r)
{
   return act_cap_fini(r);
}


int act_cap_node_fini(smrule_t *r)
{
   return act_cap_fini(r);
}


int poly_mpcoords(osm_way_t *w, struct rdata *rd, gdPoint *p)
{
   int i;
   osm_node_t *n;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
         return -1;

      mk_paper_coords(n->lat, n->lon, rd, &p[i].x, &p[i].y);
   }
   return 0;
}


int set_style(gdImage *img, int style, int col)
{
#define MAX_STYLE_BUF 300
#define STYLE_SHORT_LEN 0.4
#define STYLE_LONG_LEN 1.2
   struct rdata *rd = get_rdata();
   int sdef[MAX_STYLE_BUF];
   int len, i;

   if (style == DRAW_SOLID)
      return col;

   if (MM2PX(STYLE_LONG_LEN) + MM2PX(STYLE_SHORT_LEN) >= MAX_STYLE_BUF)
   {
      log_msg(LOG_CRIT, "style buffer to small for %d dpi, increase MAX_STYLE_BUF", (int) rd->dpi);
      return col;
   }

   switch (style)
   {
      case DRAW_DOTTED:
         len = 0;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = col;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = gdTransparent;
         break;

      case DRAW_DASHED:
         len = 0;
         for (i = 0; i < MM2PX(STYLE_LONG_LEN); i++, len++)
            sdef[len] = col;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = gdTransparent;
         break;

      default:
         log_msg(LOG_EMERG, "unknown drawing style %d!", style);
         return col;
   }

   gdImageSetStyle(img, sdef, len);
   return gdStyled;
}


int get_color(const struct rdata *rd, int r, int g, int b, int a)
{
   log_msg(LOG_DEBUG, "get_color(..., %d, %d, %d, %d)", r, g, b, a);
   return gdImageColorResolveAlpha(rd->img, r, g, b, a);
}


int gdImageGetThickness(const gdImage *img)
{
   return img->thick;
}


int act_img_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
   struct actImage img;
   gdImage *tmp_img;
   char *name, *s;
   FILE *f;
   int c;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_WARN, "img() only applicable to nodes");
      return -1;
   }

   if ((name = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return -1;
   }

   if ((f = fopen(name, "r")) == NULL)
   {
      log_msg(LOG_WARN, "cannot open file %s: %s", name, strerror(errno));
      return -1;
   }

   memset(&img, 0, sizeof(img));

   tmp_img = gdImageCreateFromPng(f);
   (void) fclose(f);

   if (tmp_img == NULL)
   {
      log_msg(LOG_WARN, "could not read PNG from %s", name);
      return -1;
   }

   if (rd->ovs > 1)
   {
      if ((img.img = gdImageCreateTrueColor(gdImageSX(tmp_img) * rd->ovs, gdImageSY(tmp_img) * rd->ovs)) == NULL)
      {
         log_msg(LOG_WARN, "could not create resized true color image");
         return -1;
      }
      c = gdImageColorAllocate(img.img, 255, 255, 255);
      gdImageColorTransparent(img.img, c);
      gdImageFill(img.img, 0, 0, c);
      gdImageAlphaBlending(img.img, 0);
      gdImageCopyResized(img.img, tmp_img, 0, 0, 0, 0, gdImageSX(img.img), gdImageSY(img.img), gdImageSX(tmp_img), gdImageSY(tmp_img));
      gdImageDestroy(tmp_img);
   }
   else
      img.img = tmp_img;

   if ((name = get_param("angle", &img.angle, r->act)) != NULL)
   {
      if (!strcmp(name, "auto"))
      {
         img.angle = NAN;
         img.rot.autocol = rd->col[BGCOLOR];
         if ((s = get_param("auto-color", NULL, r->act)) != NULL)
         {
            img.rot.autocol = parse_color(rd, s);
         }
         if ((s = get_param("weight", &img.rot.weight, r->act)) == NULL)
            img.rot.weight = 1;
         (void) get_param("phase", &img.rot.phase, r->act);
      }
   }
   
   if ((r->data = malloc(sizeof(img))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memcpy(r->data, &img, sizeof(img));

   return 0;
}


int act_img_main(smrule_t *r, osm_node_t *n)
{
   struct actImage *img = r->data;
   struct rdata *rd = get_rdata();
   int hx, hy, x, y;
   double a;

   mk_paper_coords(n->lat, n->lon, rd, &x, &y);
   hx = gdImageSX(img->img) / 2;
   hy = gdImageSY(img->img) / 2;
   a = isnan(img->angle) ? color_frequency_w(rd, x, y, hx, hy, &img->rot) : img->angle;

   gdImageCopyRotated(rd->img, img->img, x, y, 0, 0,
         gdImageSX(img->img), gdImageSY(img->img), round(a));

   return 0;
}


int act_img_fini(smrule_t *r)
{
   struct actImage *img = r->data;

   if (img != NULL && img->img != NULL)
      gdImageDestroy(img->img);

   free (img);
   r->data = NULL;

   return 0;
}


/*! Print string into image at a desired position with correct alignment.
 *  @param rd Pointer to struct rdata.
 *  @param x X position within image; the alignment is referred to these coordinates.
 *  @param y Y position (see above).
 *  @param pos Alignment: this is a combination (logical or) of horizontal and
 *  vertical positioning parameters. The vertical alignment shall be one of
 *  POS_N, POS_S, and P_OSM; the horizontal parameter is one of POS_E, POS_W,
 *  and POS_C.
 *  @param col Color of string.
 *  @param ftsize Fontsize in milimeters.
 *  @param ft Pointer to font file.
 *  @param s Pointer to string buffer.
 *  @return The function returns 0 on success, -1 otherwise.
 */
#if 0
int img_print(const struct rdata *rd, int x, int y, int pos, int col, double ftsize, const char *ft, const char *s)
{
   char *err;
   int br[8], ox, oy;
   gdFTStringExtra fte;

   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;

   gdImageStringFTEx(NULL, br, col, (char*) ft, MM2PT(ftsize), 0, 0, 0, (char*) s, &fte);

   switch (pos & 3)
   {
      case POS_N:
         oy = 0;
         break;

      case POS_S:
         oy = br[1] - br[5];
         break;

      default:
         oy = (br[1] - br[5]) / 2;
   }
   switch (pos & 12)
   {
      case POS_E:
         ox = 0;
         break;
         
      case POS_W:
         ox = br[0] - br[4];
         break;

      default:
         ox = (br[0] - br[4]) / 2;
   }

   err = gdImageStringFTEx(rd->img, br, col, (char*) ft, MM2PT(ftsize), 0, x + ox, y + oy, (char*) s, &fte);
   if (err != NULL)
   {
      log_warn("gdImageStringFTEx error: '%s'", err);
      return -1;
   }

   return 0;
}
#endif


double rot_pos(int x, int y, double a, int *rx, int *ry)
{
   double r, b;

   r = sqrt(x * x + y * y);
   b = atan2((double) y, (double) x);
   *rx = round(r * cos(a - b));
   *ry = round(r * sin(a - b));

   return r;
}


int cf_dist(struct rdata *rd, int x, int y, int w, int h, double a, int col, int mdist)
{
   int rx, ry, d, freq, max_freq = 0, dist = 0;

   for (d = 0; d < mdist; d++)
   {
      rot_pos(d, 0, a, &rx, &ry);
      freq = col_freq(rd, x + rx, y - ry, w, h, a, col);
      if (max_freq < freq)
      {
         max_freq = freq;
         dist = d;
      }
   }

   return dist;
}


int col_freq(struct rdata *rd, int x, int y, int w, int h, double a, int col)
{
#define COL_MASK 0xfcfcfcfc
   int x1, y1, rx, ry, c = 0;

   col &= COL_MASK;
   for (y1 = -h / 2; y1 < h / 2; y1++)
      for (x1 = 0; x1 < w; x1++)
      {
         rot_pos(x1, y1, a, &rx, &ry);
         c += (col == (gdImageGetPixel(rd->img, x + rx, y - ry) & COL_MASK));
      }

   return c;
}


int act_draw_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
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
      d->fill.col = parse_color(rd, s);
      d->fill.used = 1;
   }
   if (get_param("width", &d->fill.width, r->act) == NULL)
      d->fill.width = 0;
   d->fill.style = parse_style(get_param("style", NULL, r->act));

   // parse border settings
   if ((s = get_param("bcolor", NULL, r->act)) != NULL)
   {
      d->border.col = parse_color(rd, s);
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

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

   return 0;
}


int act_draw_main(smrule_t *r, osm_obj_t *o)
{
   struct actDraw *d = r->data;
   osm_way_t *w;
   int i;

   if (o->type == OSM_WAY)
   {
      if (!d->collect_open && !is_closed_poly((osm_way_t*) o))
         return 0;

      return gather_poly0((osm_way_t*) o, &d->wl);
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

         (void) gather_poly0(w, &d->wl);
      }
      return 0;
   }

   log_msg(LOG_WARN, "draw() may not be applied to object type %d", o->type);
   return 1;
}


void poly_fill(struct rdata *rd, gdImage *img, osm_way_t *w, int fg, int bg, int cw, int thick)
{
   int e, t;
   gdPoint p[w->ref_cnt];

   if ((e = poly_mpcoords(w, rd, p)))
   {
      log_msg(LOG_CRIT, "poly_mpcoords returned %d, skipping", e);
      return;
   }

   if (is_closed_poly(w))
   {
      gdImageFilledPolygon(img, p, w->ref_cnt, cw ? bg : !rd->ovs ? gdAntiAliased : fg);
   }
   else
   {
      t = gdImageGetThickness(img);
      gdImageSetThickness(img, thick);
      gdImageOpenPolygon(img, p, w->ref_cnt, rd->ovs ? fg : thick > 1 ? fg : gdAntiAliased);
      gdImageSetThickness(img, t);
   }
}


void poly_border(struct rdata *rd, gdImage *img, osm_way_t *w, int fg, int ct, int ot, int style)
{
   int e, t, c;
   gdPoint p[w->ref_cnt];

   if ((e = poly_mpcoords(w, rd, p)))
   {
      log_msg(LOG_CRIT, "poly_mpcoords returned %d, skipping", e);
      return;
   }

   t = gdImageGetThickness(img);
   if (is_closed_poly(w))
   {
      gdImageSetThickness(img, ct);
      c = set_style(img, style, rd->ovs ? fg : ct > 1 ? fg : gdAntiAliased);
      gdImagePolygon(img, p, w->ref_cnt, c);
   }
   else
   {
      gdImageSetThickness(img, ot);
      c = set_style(img, style, rd->ovs ? fg : ot > 1 ? fg : gdAntiAliased);
      gdImageOpenPolygon(img, p, w->ref_cnt, c);
   }
   gdImageSetThickness(img, t);
}


void dfree(struct actDraw *d)
{
   free(d->wl);
   free(d);
}


int act_draw_fini(smrule_t *r)
{
   struct actDraw *d = r->data;
   struct rdata *rd;
   struct coord c;
   gdImage *img;
   int fg, bg, closed_thick, open_thick;
   int i;
 
   if (!d->wl->ref_cnt)
   {
      log_debug("emtpy waylist");
      dfree(r->data);
      r->data = NULL;
      return 1;
   }


   if (d->directional)
   {
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         if (is_closed_poly(d->wl->ref[i].w))
         {
            poly_area(d->wl->ref[i].w, &c, &d->wl->ref[i].area);
            if (d->wl->ref[i].area < 0)
            {
               d->wl->ref[i].area = fabs(d->wl->ref[i].area);
               d->wl->ref[i].cw = d->directional;
            }
         }
      }
      qsort(d->wl->ref, d->wl->ref_cnt, sizeof(struct poly), (int(*)(const void *, const void *)) compare_poly_area);
   }

   rd = get_rdata();
   bg = rd->col[BGCOLOR];
   img = gdImageCreateTrueColor(gdImageSX(rd->img), gdImageSY(rd->img));
   bg = gdImageColorAllocate(img,
         gdImageRed(rd->img, rd->col[BGCOLOR]),
         gdImageGreen(rd->img, rd->col[BGCOLOR]),
         gdImageBlue(rd->img, rd->col[BGCOLOR]));
   gdImageColorTransparent(img, bg);

   if (d->fill.used)
   {
      fg = d->fill.col;
      gdImageSetAntiAliased(img, fg);
      gdImageFilledRectangle(img, 0, 0, gdImageSX(img), gdImageSY(img), !d->wl->ref[0].cw ? bg : rd->ovs ? fg : gdAntiAliased);
      for (i = 0; i < d->wl->ref_cnt; i++)
         poly_fill(rd, img, d->wl->ref[i].w, fg, bg, d->wl->ref[i].cw, d->fill.width > 0 ? MM2PX(d->fill.width) : (rd->ovs ? rd->ovs : 1));

      gdImageCopy(rd->img, img, 0, 0, 0, 0, gdImageSX(img), gdImageSY(img));
   }

   if (d->border.used)
   {
      fg = d->border.col;
      gdImageSetAntiAliased(img, fg);
      gdImageFilledRectangle(img, 0, 0, gdImageSX(img), gdImageSY(img), bg);

      if ((closed_thick = MM2PX(round(d->border.width * 2))) < 1)
         closed_thick = rd->ovs ? rd->ovs : 1;
      if ((open_thick = MM2PX(round(d->border.width * 2 + d->fill.width))) < 1)
         open_thick = rd->ovs ? rd->ovs : 1;

      for (i = 0; i < d->wl->ref_cnt; i++)
         poly_border(rd, img, d->wl->ref[i].w, fg, closed_thick, open_thick, d->border.style);

      gdImageCopy(rd->img, img, 0, 0, 0, 0, gdImageSX(img), gdImageSY(img));
   }

   gdImageDestroy(img);
   dfree(r->data);
   r->data = NULL;
   return 0;
}


int get_pixel(struct rdata *rd, int x, int y)
{
   return gdImageGetPixel(rd->img, x, y);
}


#else

void init_main_image(struct rdata *rd, const char *bg)
{
}


void reduce_resolution(struct rdata *rd)
{
}


void save_main_image(struct rdata *rd, FILE *f)
{
}


int get_color(const struct rdata *rd, int r, int g, int b, int a)
{
   return 0;
}


int get_pixel(struct rdata *rd, int x, int y)
{
   return 0;
}

#endif

