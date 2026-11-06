#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>

#include "e_map.h"

typedef struct
{
  int v1, v2;
  int sector;
  int neighbour;
  int tex;
} Raw_Wall;

static void report(const char *path, const char *data, const char *pos,
                   const char *fmt, ...)
{
  va_list ap;

  fprintf(stderr, "%s", path);

  if (data && pos) {
    int line = 1, col = 1;

    for (const char *c = data; c < pos; c++) {
      if (*c == '\n') {
        line++;
        col = 1;
      } else {
        col++;
      }
    }

    fprintf(stderr, ":%d:%d", line, col);
  }

  fprintf(stderr, ": ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

static void skip_ws(const char **p)
{
  while (isspace((unsigned char)**p))
    ++*p;
}

static int expect(const char **p, char c)
{
  skip_ws(p);

  if (**p != c)
    return -1;

  ++*p;
  return 0;
}

static int parse_int(const char **p, int *value)
{
  char *end;

  skip_ws(p);
  errno = 0;
  long n = strtol(*p, &end, 10);

  if (end == *p || errno || n < INT_MIN || n > INT_MAX)
    return -1;

  *value = (int)n;
  *p = end;
  return 0;
}

static int parse_float(const char **p, float *value)
{
  char *end;

  skip_ws(p);
  errno = 0;
  float n = strtof(*p, &end);

  if (end == *p || errno)
    return -1;

  *value = n;
  *p = end;
  return 0;
}

static int parse_string(const char **p, char **value)
{
  if (expect(p, '"'))
    return -1;

  const char *start = *p;

  while (**p && **p != '"')
    ++*p;

  if (!**p)
    return -1;

  size_t len = (size_t)(*p - start);
  char *s = malloc(len + 1);

  if (!s)
    return -1;

  memcpy(s, start, len);
  s[len] = '\0';

  ++*p;
  *value = s;
  return 0;
}

static int append(void **array, int *count, size_t size, const void *item)
{
  if (*count == INT_MAX || (size_t)*count + 1 > SIZE_MAX / size)
    return -1;

  void *tmp = realloc(*array, ((size_t)*count + 1) * size);

  if (!tmp)
    return -1;

  *array = tmp;
  memcpy((char *)*array + (size_t)*count * size, item, size);
  ++*count;
  return 0;
}

static char *read_file(const char *path, size_t *size)
{
  FILE *file = fopen(path, "rb");
  char *data = NULL;
  long length;

  if (!file) {
    report(path, NULL, NULL, "%s", strerror(errno));
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) {
    report(path, NULL, NULL, "cannot determine file size");
    goto done;
  }

  data = malloc((size_t)length + 1);
  if (!data) {
    report(path, NULL, NULL, "out of memory");
    goto done;
  }

  if (fread(data, 1, (size_t)length, file) != (size_t)length) {
    report(path, NULL, NULL, "read error");
    free(data);
    data = NULL;
    goto done;
  }

  data[length] = '\0';

  if (size)
    *size = (size_t)length;

done:
  fclose(file);
  return data;
}

void E_Map_Free(E_Map *map)
{
  for (int i = 0; i < map->texture_count; i++) {
    free((void *)map->textures[i].path);
    free(map->textures[i].bitmap);
  }

  free(map->vertices);
  free(map->walls);
  free(map->sectors);
  free(map->textures);
  free(map->path);

  memset(map, 0, sizeof(*map));
}

static uint32_t read_u32(const unsigned char *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const unsigned char *p)
{
  return (uint16_t)(p[0] | p[1] << 8);
}

static int channel_shift(uint32_t mask, int *shift)
{
  *shift = 0;

  if (!mask)
    return -1;

  while (!(mask & 1)) {
    mask >>= 1;
    ++*shift;
  }

  return mask == 0xFF ? 0 : -1;
}

int E_Map_Texture_Load(E_Map_Texture *texture)
{
  if (texture->bitmap)
    return 0;

  const char *path = texture->path;
  size_t size;
  unsigned char *data = (unsigned char *)read_file(path, &size);
  uint32_t *pixels = NULL;

  if (!data)
    return -1;

  if (size < 54 || data[0] != 'B' || data[1] != 'M') {
    report(path, NULL, NULL, "not a BMP file");
    goto fail;
  }

  uint32_t offset = read_u32(data + 10);
  uint32_t header_size = read_u32(data + 14);
  int32_t width = (int32_t)read_u32(data + 18);
  int32_t height = (int32_t)read_u32(data + 22);
  uint16_t bpp = read_u16(data + 28);
  uint32_t compression = read_u32(data + 30);
  int top_down = height < 0;

  if (top_down)
    height = -height;

  if (header_size < 40) {
    report(path, NULL, NULL, "unsupported BMP header (old OS/2 format)");
    goto fail;
  }

  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    report(path, NULL, NULL, "bad image size %dx%d", (int)width, (int)height);
    goto fail;
  }

  if (!((bpp == 24 && compression == 0) ||
        (bpp == 32 && (compression == 0 || compression == 3)))) {
    report(path, NULL, NULL, "unsupported BMP format (%d-bit, compression %d); "
           "save as uncompressed 24 or 32-bit", (int)bpp, (int)compression);
    goto fail;
  }

  int r_shift = 16, g_shift = 8, b_shift = 0;

  if (compression == 3) {
    if (size < 66 ||
        channel_shift(read_u32(data + 54), &r_shift) ||
        channel_shift(read_u32(data + 58), &g_shift) ||
        channel_shift(read_u32(data + 62), &b_shift)) {
      report(path, NULL, NULL, "unsupported BMP colour masks (need 8 bits per channel)");
      goto fail;
    }
  }

  size_t row_size = ((size_t)bpp * (size_t)width + 31) / 32 * 4;

  if (offset > size || (size - offset) / row_size < (size_t)height) {
    report(path, NULL, NULL, "file is truncated");
    goto fail;
  }

  pixels = malloc((size_t)width * (size_t)height * sizeof(*pixels));
  if (!pixels) {
    report(path, NULL, NULL, "out of memory");
    goto fail;
  }

  for (int y = 0; y < height; y++) {
    const unsigned char *row = data + offset + row_size * (size_t)(top_down ? y : height - 1 - y);
    uint32_t *out = pixels + (size_t)y * (size_t)width;

    for (int x = 0; x < width; x++) {
      uint32_t r, g, b;

      if (bpp == 24) {
        b = row[x * 3];
        g = row[x * 3 + 1];
        r = row[x * 3 + 2];
      } else {
        uint32_t p = read_u32(row + x * 4);
        r = (p >> r_shift) & 0xFF;
        g = (p >> g_shift) & 0xFF;
        b = (p >> b_shift) & 0xFF;
      }

      out[x] = 0xFF000000u | r << 16 | g << 8 | b;
    }
  }

  free(data);
  texture->bitmap = pixels;
  texture->width = width;
  texture->height = height;
  return 0;

fail:
  free(data);
  free(pixels);
  return -1;
}

#define SYNTAX(...) do { report(map_path, data, p, __VA_ARGS__); goto fail; } while (0)

#define EXPECT(expr, what) do { if ((expr) != 0) SYNTAX("expected %s", what); } while (0)

#define INVALID(...) do { report(map_path, NULL, NULL, __VA_ARGS__); goto fail; } while (0)

#define OOM() INVALID("out of memory")

static int parse_map(E_Map *map, const char *map_path)
{
  E_Map tmp = {0};
  Raw_Wall *raw = NULL;
  int raw_count = 0;
  int *src = NULL;
  int *fill = NULL;
  char *data = NULL;
  const char *p = NULL;
  unsigned seen = 0;

  data = read_file(map_path, NULL);
  if (!data)
    goto fail;

  tmp.path = malloc(strlen(map_path) + 1);
  if (!tmp.path)
    OOM();
  strcpy(tmp.path, map_path);

  p = data;

  while (1) {
    skip_ws(&p);

    if (!*p)
      break;

    EXPECT(expect(&p, '{'), "'{' to start a block");

    char type = *p;
    unsigned bit;

    switch (type) {
      case 'V': bit = 1; break;
      case 'W': bit = 2; break;
      case 'S': bit = 4; break;
      case 'T': bit = 8; break;
      default:  SYNTAX("unknown block type (expected V, W, S or T)");
    }

    if (seen & bit)
      SYNTAX("duplicate %c block", type);

    seen |= bit;
    p++;
    EXPECT(expect(&p, ':'), "':'");

    while (1) {
      skip_ws(&p);

      if (*p == '}') {
        p++;
        break;
      }

      switch (type) {
        case 'V': {
          E_Map_Vertex v;

          EXPECT(expect(&p, '('), "'('");
          EXPECT(parse_float(&p, &v.x), "vertex x");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_float(&p, &v.y), "vertex y");
          EXPECT(expect(&p, ')'), "')'");

          if (append((void **)&tmp.vertices, &tmp.vertex_count, sizeof(v), &v))
            OOM();
          break;
        }

        case 'W': {
          Raw_Wall w;

          EXPECT(expect(&p, '('), "'('");
          EXPECT(parse_int(&p, &w.v1), "wall v1");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &w.v2), "wall v2");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &w.sector), "wall sector");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &w.neighbour), "wall neighbour");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &w.tex), "wall texture");
          EXPECT(expect(&p, ')'), "')'");

          if (append((void **)&raw, &raw_count, sizeof(w), &w))
            OOM();
          break;
        }

        case 'S': {
          E_Map_Sector s = {0};

          EXPECT(expect(&p, '('), "'('");
          EXPECT(parse_float(&p, &s.floor_z), "sector floor height");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_float(&p, &s.ceiling_z), "sector ceiling height");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &s.floor_tex), "sector floor texture");
          EXPECT(expect(&p, ','), "','");
          EXPECT(parse_int(&p, &s.ceiling_tex), "sector ceiling texture");
          EXPECT(expect(&p, ')'), "')'");

          if (append((void **)&tmp.sectors, &tmp.sector_count, sizeof(s), &s))
            OOM();
          break;
        }

        case 'T': {
          E_Map_Texture t = {0};
          char *path;

          EXPECT(parse_string(&p, &path), "quoted texture path");
          t.path = path;

          if (append((void **)&tmp.textures, &tmp.texture_count, sizeof(t), &t)) {
            free(path);
            OOM();
          }
          break;
        }
      }

      skip_ws(&p);

      if (*p == ',') {
        p++;
        skip_ws(&p);

        if (*p == '}')
          SYNTAX("trailing ',' before '}'");
      } else if (*p != '}') {
        SYNTAX("expected ',' or '}'");
      }
    }
  }

  if (!(seen & 1)) INVALID("missing V (vertices) block");
  if (!(seen & 2)) INVALID("missing W (walls) block");
  if (!(seen & 4)) INVALID("missing S (sectors) block");
  if (!(seen & 8)) INVALID("missing T (textures) block");

  for (int i = 0; i < tmp.sector_count; i++) {
    E_Map_Sector *s = &tmp.sectors[i];

    if (s->floor_z >= s->ceiling_z)
      INVALID("sector %d: floor (%g) must be below ceiling (%g)",
              i, s->floor_z, s->ceiling_z);

    if (s->floor_tex < 0 || s->floor_tex >= tmp.texture_count)
      INVALID("sector %d: floor texture %d does not exist", i, s->floor_tex);

    if (s->ceiling_tex < 0 || s->ceiling_tex >= tmp.texture_count)
      INVALID("sector %d: ceiling texture %d does not exist", i, s->ceiling_tex);

  }

  for (int i = 0; i < raw_count; i++) {
    Raw_Wall *w = &raw[i];

    if (w->v1 < 0 || w->v1 >= tmp.vertex_count)
      INVALID("wall %d: vertex %d does not exist", i, w->v1);

    if (w->v2 < 0 || w->v2 >= tmp.vertex_count)
      INVALID("wall %d: vertex %d does not exist", i, w->v2);

    if (w->v1 == w->v2)
      INVALID("wall %d: both ends are vertex %d", i, w->v1);

    if (w->sector < 0 || w->sector >= tmp.sector_count)
      INVALID("wall %d: sector %d does not exist", i, w->sector);

    if (w->neighbour < -1 || w->neighbour >= tmp.sector_count)
      INVALID("wall %d: neighbour sector %d does not exist", i, w->neighbour);

    if (w->neighbour == w->sector)
      INVALID("wall %d: neighbour is its own sector (%d)", i, w->sector);

    if (w->tex < 0 || w->tex >= tmp.texture_count)
      INVALID("wall %d: texture %d does not exist", i, w->tex);
  }

  for (int i = 0; i < raw_count; i++)
    tmp.sectors[raw[i].sector].wall_count++;

  for (int s = 0, next = 0; s < tmp.sector_count; s++) {
    tmp.sectors[s].first_wall = next;
    next += tmp.sectors[s].wall_count;
  }

  tmp.wall_count = raw_count;
  tmp.walls = malloc((size_t)raw_count * sizeof(*tmp.walls) + 1);
  src = malloc((size_t)raw_count * sizeof(*src) + 1);
  fill = calloc((size_t)tmp.sector_count + 1, sizeof(*fill));
  if (!tmp.walls || !src || !fill)
    OOM();

  for (int i = 0; i < raw_count; i++) {
    int s = raw[i].sector;
    int j = tmp.sectors[s].first_wall + fill[s]++;

    tmp.walls[j].v1 = raw[i].v1;
    tmp.walls[j].v2 = raw[i].v2;
    tmp.walls[j].neighbour = raw[i].neighbour;
    tmp.walls[j].tex = raw[i].tex;
    src[j] = i;
  }

  for (int s = 0; s < tmp.sector_count; s++) {
    const E_Map_Sector *sec = &tmp.sectors[s];
    const E_Map_Wall *walls = &tmp.walls[sec->first_wall];
    int n = sec->wall_count;

    if (n < 3)
      INVALID("sector %d: has %d wall%s, needs at least 3",
              s, n, n == 1 ? "" : "s");

    for (int i = 0; i < n; i++) {
      int starts_here = 0, ends_here = 0;

      for (int j = 0; j < n; j++) {
        if (walls[j].v1 == walls[i].v2) starts_here++;
        if (walls[j].v2 == walls[i].v2) ends_here++;
      }

      if (starts_here != 1 || ends_here != 1)
        INVALID("sector %d: walls do not form a single closed loop at vertex %d "
                "(%d wall%s start there, %d end there)",
                s, walls[i].v2, starts_here, starts_here == 1 ? "" : "s",
                ends_here);
    }

    for (int i = 0; i < n; i++) {
      E_Map_Vertex a = tmp.vertices[walls[i].v1];
      E_Map_Vertex b = tmp.vertices[walls[i].v2];
      double abx = (double)b.x - a.x, aby = (double)b.y - a.y;

      for (int j = 0; j < n; j++) {
        E_Map_Vertex c = tmp.vertices[walls[j].v1];
        double acx = (double)c.x - a.x, acy = (double)c.y - a.y;
        double cross = abx * acy - aby * acx;

        if (cross < 0 &&
            cross * cross > 1e-12 * (abx * abx + aby * aby) * (acx * acx + acy * acy))
          INVALID("sector %d: not convex, or walls run clockwise "
                  "(vertex %d is outside wall %d)",
                  s, walls[j].v1, src[sec->first_wall + i]);
      }
    }
  }

  for (int i = 0; i < tmp.wall_count; i++) {
    const E_Map_Wall *w = &tmp.walls[i];

    if (w->neighbour < 0)
      continue;

    int owner = 0;
    while (i >= tmp.sectors[owner].first_wall + tmp.sectors[owner].wall_count)
      owner++;

    const E_Map_Sector *other = &tmp.sectors[w->neighbour];
    int found = 0;

    for (int j = 0; j < other->wall_count; j++) {
      const E_Map_Wall *o = &tmp.walls[other->first_wall + j];

      if (o->v1 == w->v2 && o->v2 == w->v1 && o->neighbour == owner) {
        found = 1;
        break;
      }
    }

    if (!found)
      INVALID("wall %d: portal from sector %d to %d needs a matching wall "
              "(%d, %d, %d, %d, tex) in sector %d",
              src[i], owner, w->neighbour,
              w->v2, w->v1, w->neighbour, owner, w->neighbour);
  }

  free(raw);
  free(src);
  free(fill);
  free(data);
  *map = tmp;
  return 0;

fail:
  free(raw);
  free(src);
  free(fill);
  free(data);
  E_Map_Free(&tmp);
  return -1;
}

#undef SYNTAX
#undef EXPECT
#undef INVALID
#undef OOM

int E_Map_Import(E_Map* map, const char* map_path, int map_id)
{
  if (parse_map(map, map_path))
    return -1;

  map->id = map_id;
  return 0;
}
