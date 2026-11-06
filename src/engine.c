#include "engine.h"

E_Debug_Hook e_debug_hook = NULL;
void* e_debug_user = NULL;

E_Vec2f transform_vertex(E_Vec2f vertex, const E_Vec2f pos, const E_Vec2f dir)
{
  E_Vec2f r = {vertex.x - pos.x, vertex.y - pos.y};
  return (E_Vec2f){r.x * dir.y - r.y * dir.x, r.x * dir.x + r.y * dir.y};
}

float vertex_distance(E_Vec2f v1, E_Vec2f v2)
{
  float dx = v2.x - v1.x;
  float dy = v2.y - v1.y;
  return sqrtf(dx * dx + dy * dy);
}
#define E_SHADE_FALLOFF 0.4f
#define E_SHADE_STEPS_PER_UNIT 16
#define E_SHADE_TABLE_SIZE 1024

static uint32_t shade_table[E_SHADE_TABLE_SIZE];

static void build_shade_table(void)
{
  for (int i = 0; i < E_SHADE_TABLE_SIZE; i++)
  {
    float z = (i + 0.5f) / E_SHADE_STEPS_PER_UNIT;
    shade_table[i] = (uint32_t)(256.0f / (1.0f + z * E_SHADE_FALLOFF));
  }
}

int e_init(E_Map* map)
{
  build_shade_table();

  for (int i = 0; i < map->texture_count; i++)
  {
    if (!map->textures[i].bitmap && E_Map_Texture_Load(&map->textures[i]) != 0)
    {
      return -1;
    }
  }

  return 0;
}

static uint32_t depth_shade(float z)
{
  int i = (int)(z * E_SHADE_STEPS_PER_UNIT);
  if (i < 0) i = 0;
  if (i >= E_SHADE_TABLE_SIZE) i = E_SHADE_TABLE_SIZE - 1;
  return shade_table[i];
}

static uint32_t darken(uint32_t c, uint32_t s)
{
  uint32_t rb = ((c & 0x00FF00FF) * s >> 8) & 0x00FF00FF;
  uint32_t g = ((c & 0x0000FF00) * s >> 8) & 0x0000FF00;
  return (c & 0xFF000000) | rb | g;
}

int point_in_sector(const E_Map* map, const E_Map_Sector* sector, const E_Vec2f point)
{
  for (int w = sector->first_wall; w < sector->first_wall + sector->wall_count; w++)
  {
    const E_Map_Wall* const wall = map->walls + w;
    const E_Vec2f v1 = map->vertices[wall->v1];
    const E_Vec2f v2 = map->vertices[wall->v2];
    if ((v2.x - v1.x) * (point.y - v1.y) - (v2.y - v1.y) * (point.x - v1.x) < 0) 
    {
      return 0;
    }
  }
  return 1;
}
E_Map_Sector* get_camera_sector(const E_Map* map, const E_Vec2f pos)
{
  for (int s = 0; s < map->sector_count; s++)
  {
    if (point_in_sector(map, map->sectors + s, pos))
    {
      return map->sectors + s;
    }
  }
  return NULL;
}


E_Map_Sector* point_sector(const E_Map* map, const E_Vec2f point)
{
  for (int s = 0; s < map->sector_count; s++)
  {
    E_Map_Sector* sector = map->sectors + s;
    if (point_in_sector(map, sector, point))
    {
      return sector;
    }
  }
  return NULL;
}

struct Column_Range
{
  int left;
  int right;
};
typedef struct Column_Range Column_Range;

struct Sector_Queue_Node
{
  E_Map_Sector* sector;
  Column_Range range;
  struct Sector_Queue_Node* next;
};
typedef struct Sector_Queue_Node Sector_Queue_Node;

struct Sector_Queue
{
  Sector_Queue_Node* head;
  Sector_Queue_Node* tail;
};
typedef struct Sector_Queue Sector_Queue;

void enqueue(Sector_Queue* queue, E_Map_Sector* sector, Column_Range range)
{
  Sector_Queue_Node* new_node = (Sector_Queue_Node*)malloc(sizeof(Sector_Queue_Node));
  if (!new_node)
  {
    fprintf(stderr, "Error creating new node in enqueue\n");
    return;
  }

  new_node->sector = sector;
  new_node->range = range;
  new_node->next = NULL;
  
  if (queue->tail)
  {
    queue->tail->next = new_node;
  }
  else
  {
    queue->head = new_node;
  }
  queue->tail = new_node;
}

Sector_Queue_Node dequeue(Sector_Queue* queue)
{
  if (!queue->head)
  {
    fprintf(stderr, "queue is empty\n");
    return (Sector_Queue_Node){0};
  }
  
  Sector_Queue_Node* node = queue->head;
  E_Map_Sector* sector = node->sector;
  Column_Range range = node->range;
  Sector_Queue_Node return_node = {sector, range, NULL};
  queue->head = node->next;
  if (!queue->head)
  {
    queue->tail = NULL;
  }

  free(node);
  return return_node;
}

#define NEAR_PLANE_DEPTH 0.01f
#define TEXELS_PER_UNIT 64

struct Flat_Spans
{
  int* ceiling_top;
  int* ceiling_bottom;
  int* floor_top;
  int* floor_bottom;
};
typedef struct Flat_Spans Flat_Spans;

struct Render_Context
{
  const E_Map* map;
  uint32_t* framebuffer;
  int buf_width;
  int buf_height;

  E_Vec2f pos;
  E_Vec2f dir;
  E_Vec2f right;
  float camera_z;

  float focal;
  float r_focal;
  float horizon;

  int* y_top;
  int* y_bottom;

  float* floor_z_scalar;
  float* ceiling_z_scalar;

  uint32_t* floor_shade;
  uint32_t* ceiling_shade;

  Flat_Spans spans;
};
typedef struct Render_Context Render_Context;

struct Projected_Wall
{
  float v1_x;
  float v2_x;
  float r_width;

  float v1_iz;
  float v2_iz;
  float v1_uz;
  float v2_uz;

  float v1_ceil_y;
  float v2_ceil_y;
  float v1_floor_y;
  float v2_floor_y;

  float v1_neighbour_ceil_y;
  float v2_neighbour_ceil_y;
  float v1_neighbour_floor_y;
  float v2_neighbour_floor_y;

  int x_start;
  int x_end;
};
typedef struct Projected_Wall Projected_Wall;

enum
{
  WALL_HIDDEN,
  WALL_VISIBLE,
  WALL_AT_EYE
};

static float lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

static int clamp_int(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float project_y(const Render_Context* render_context, float height, float depth)
{
  return render_context->horizon - render_context->focal * (height - render_context->camera_z) / depth;
}

static int project_wall
(
  const Render_Context* render_context,
  const E_Map_Sector* sector,
  const E_Map_Wall* wall,
  Column_Range range,
  Projected_Wall* pw
)
{
  const E_Map_Vertex v1 = render_context->map->vertices[wall->v1];
  const E_Map_Vertex v2 = render_context->map->vertices[wall->v2];
  E_Map_Vertex v1_t = transform_vertex(v1, render_context->pos, render_context->dir);
  E_Map_Vertex v2_t = transform_vertex(v2, render_context->pos, render_context->dir);

  float v1_u = vertex_distance(v1, v2);
  float v2_u = 0.0f;

  if (v1_t.y < NEAR_PLANE_DEPTH && v2_t.y < NEAR_PLANE_DEPTH)
  {
    if (wall->neighbour != -1 &&
        v1_t.y > -NEAR_PLANE_DEPTH && v2_t.y > -NEAR_PLANE_DEPTH &&
        v1_t.x > 0 && v2_t.x < 0)
    {
      return WALL_AT_EYE;
    }
    return WALL_HIDDEN;
  }

  int v1_clipped = 0;
  int v2_clipped = 0;
  if (v1_t.y < NEAR_PLANE_DEPTH)
  {
    float t = (NEAR_PLANE_DEPTH - v1_t.y) / (v2_t.y - v1_t.y);
    v1_u = v1_u + (v2_u - v1_u) * t;
    v1_t.x = v1_t.x + (v2_t.x - v1_t.x) * t;
    v1_t.y = NEAR_PLANE_DEPTH;
    v1_clipped = 1;
  }
  if (v2_t.y < NEAR_PLANE_DEPTH)
  {
    float t = (NEAR_PLANE_DEPTH - v2_t.y) / (v1_t.y - v2_t.y);
    v2_u = v2_u + (v1_u - v2_u) * t;
    v2_t.x = v2_t.x + (v1_t.x - v2_t.x) * t;
    v2_t.y = NEAR_PLANE_DEPTH;
    v2_clipped = 1;
  }

  pw->v1_iz = 1 / v1_t.y;
  pw->v1_uz = v1_u / v1_t.y;
  pw->v2_iz = 1 / v2_t.y;
  pw->v2_uz = v2_u / v2_t.y;

  pw->v1_x = render_context->buf_width * 0.5f + render_context->focal * v1_t.x / v1_t.y;
  pw->v2_x = render_context->buf_width * 0.5f + render_context->focal * v2_t.x / v2_t.y;
  if (pw->v1_x <= pw->v2_x) return WALL_HIDDEN;

  pw->v1_ceil_y = project_y(render_context, sector->ceiling_z, v1_t.y);
  pw->v2_ceil_y = project_y(render_context, sector->ceiling_z, v2_t.y);
  pw->v1_floor_y = project_y(render_context, sector->floor_z, v1_t.y);
  pw->v2_floor_y = project_y(render_context, sector->floor_z, v2_t.y);

  pw->x_start = v2_clipped ? range.left : (int)ceilf(pw->v2_x);
  pw->x_end = v1_clipped ? range.right : (int)ceilf(pw->v1_x);
  if (pw->x_start < range.left) pw->x_start = range.left;
  if (pw->x_end > range.right) pw->x_end = range.right;
  if (pw->x_start >= pw->x_end) return WALL_HIDDEN;

  if (wall->neighbour != -1)
  {
    const E_Map_Sector* neighbour = render_context->map->sectors + wall->neighbour;
    pw->v1_neighbour_ceil_y = project_y(render_context, neighbour->ceiling_z, v1_t.y);
    pw->v2_neighbour_ceil_y = project_y(render_context, neighbour->ceiling_z, v2_t.y);
    pw->v1_neighbour_floor_y = project_y(render_context, neighbour->floor_z, v1_t.y);
    pw->v2_neighbour_floor_y = project_y(render_context, neighbour->floor_z, v2_t.y);
  }

  pw->r_width = 1 / (pw->v1_x - pw->v2_x);
  return WALL_VISIBLE;
}

static void draw_wall_slice
(
  uint32_t* restrict framebuffer,
  int buf_width,
  int x,
  int y_start,
  int y_end,
  const E_Map_Texture* tex,
  int tx,
  float v,
  float v_step,
  uint32_t shade
)
{
  for (int y = y_start; y < y_end; y++)
  {
    framebuffer[y * buf_width + x] = darken(tex->bitmap[((int)v & (tex->height - 1)) * tex->width + tx], shade);
    v += v_step;
  }
}

static void draw_wall_column(const Render_Context* render_context, const E_Map_Wall* wall, const Projected_Wall* pw, int x)
{
  int* y_top = render_context->y_top;
  int* y_bottom = render_context->y_bottom;
  if (y_top[x] >= y_bottom[x]) return;

  const E_Map_Texture* wall_tex = &render_context->map->textures[wall->tex];

  float t = (x - pw->v2_x) * pw->r_width;
  float x_iz = lerp(pw->v2_iz, pw->v1_iz, t);
  float x_uz = lerp(pw->v2_uz, pw->v1_uz, t);
  float x_u = x_uz / x_iz;
  int tx = (int)(x_u * TEXELS_PER_UNIT) & (wall_tex->width - 1);
  float x_z = 1 / x_iz;
  float v_step = x_z * TEXELS_PER_UNIT * render_context->r_focal;
  uint32_t wall_shade = depth_shade(x_z);

  float ceil_y = lerp(pw->v2_ceil_y, pw->v1_ceil_y, t);
  float floor_y = lerp(pw->v2_floor_y, pw->v1_floor_y, t);
  int y_ceil = clamp_int((int)ceilf(ceil_y), y_top[x], y_bottom[x]);
  int y_floor = clamp_int((int)ceilf(floor_y), y_top[x], y_bottom[x]);

  render_context->spans.ceiling_top[x] = y_top[x];
  render_context->spans.ceiling_bottom[x] = y_ceil;
  render_context->spans.floor_top[x] = y_floor;
  render_context->spans.floor_bottom[x] = y_bottom[x];

  if (wall->neighbour == -1)
  {
    float v = (y_ceil + 0.5f - ceil_y) * v_step;
    draw_wall_slice(render_context->framebuffer, render_context->buf_width, x, y_ceil, y_floor, wall_tex, tx, v, v_step, wall_shade);
    y_top[x] = y_bottom[x];
  }
  else
  {
    float neighbour_ceil_y = lerp(pw->v2_neighbour_ceil_y, pw->v1_neighbour_ceil_y, t);
    float neighbour_floor_y = lerp(pw->v2_neighbour_floor_y, pw->v1_neighbour_floor_y, t);
    int y_ceil_neighbour = clamp_int((int)ceilf(neighbour_ceil_y), y_ceil, y_floor);
    int y_floor_neighbour = clamp_int((int)ceilf(neighbour_floor_y), y_ceil, y_floor);

    float v = (y_ceil + 0.5f - ceil_y) * v_step;
    draw_wall_slice(render_context->framebuffer, render_context->buf_width, x, y_ceil, y_ceil_neighbour, wall_tex, tx, v, v_step, wall_shade);

    v = (y_floor_neighbour + 0.5f - ceil_y) * v_step;
    draw_wall_slice(render_context->framebuffer, render_context->buf_width, x, y_floor_neighbour, y_floor, wall_tex, tx, v, v_step, wall_shade);

    y_top[x] = y_ceil_neighbour;
    y_bottom[x] = y_floor_neighbour;
  }

  if (e_debug_hook) e_debug_hook(e_debug_user);
}

static void draw_flats(const Render_Context* render_context, const E_Map_Sector* sector, Column_Range range)
{
  int floor_min = render_context->buf_height, floor_max = 0;
  int ceil_min = render_context->buf_height, ceil_max = 0;
  for (int x = range.left; x < range.right; x++)
  {
    if (render_context->spans.floor_top[x] < render_context->spans.floor_bottom[x])
    {
      if (render_context->spans.floor_top[x] < floor_min) floor_min = render_context->spans.floor_top[x];
      if (render_context->spans.floor_bottom[x] > floor_max) floor_max = render_context->spans.floor_bottom[x];
    }
    if (render_context->spans.ceiling_top[x] < render_context->spans.ceiling_bottom[x])
    {
      if (render_context->spans.ceiling_top[x] < ceil_min) ceil_min = render_context->spans.ceiling_top[x];
      if (render_context->spans.ceiling_bottom[x] > ceil_max) ceil_max = render_context->spans.ceiling_bottom[x];
    }
  }

  float floor_height = render_context->camera_z - sector->floor_z;
  float ceiling_height = sector->ceiling_z - render_context->camera_z;
  for (int y = floor_min; y < floor_max; y++)
  {
    render_context->floor_shade[y] = depth_shade(floor_height * render_context->floor_z_scalar[y]);
  }
  for (int y = ceil_min; y < ceil_max; y++)
  {
    render_context->ceiling_shade[y] = depth_shade(ceiling_height * render_context->ceiling_z_scalar[y]);
  }
  const E_Map_Texture* floor_tex = &render_context->map->textures[sector->floor_tex];
  const E_Map_Texture* ceil_tex = &render_context->map->textures[sector->ceiling_tex];
  float base_x = render_context->pos.x * 64 + 65536.0f;
  float base_y = render_context->pos.y * 64 + 65536.0f;

  uint32_t* restrict framebuffer = render_context->framebuffer;
  const int buf_width = render_context->buf_width;

  const uint32_t* floor_bitmap = floor_tex->bitmap;
  const int floor_w = floor_tex->width;
  const int floor_mask_w = floor_tex->width - 1;
  const int floor_mask_h = floor_tex->height - 1;

  const uint32_t* ceil_bitmap = ceil_tex->bitmap;
  const int ceil_w = ceil_tex->width;
  const int ceil_mask_w = ceil_tex->width - 1;
  const int ceil_mask_h = ceil_tex->height - 1;

  for (int x = range.left; x < range.right; x++)
  {
    float sz = (x + 0.5f - buf_width * 0.5f) * render_context->r_focal;
    float ray_x = (render_context->right.x*sz + render_context->dir.x)*64;
    float ray_y = (render_context->right.y*sz + render_context->dir.y)*64;
    for (int y = render_context->spans.floor_top[x]; y < render_context->spans.floor_bottom[x]; y++)
    {
      float z = floor_height * render_context->floor_z_scalar[y];
      int tx = (int)(base_x + ray_x * z) & floor_mask_w;
      int ty = (int)(base_y + ray_y * z) & floor_mask_h;
      framebuffer[y * buf_width + x] = darken(floor_bitmap[ty * floor_w + tx], render_context->floor_shade[y]);
    }
    for (int y = render_context->spans.ceiling_top[x]; y < render_context->spans.ceiling_bottom[x]; y++)
    {
      float z = ceiling_height * render_context->ceiling_z_scalar[y];
      int tx = (int)(base_x + ray_x * z) & ceil_mask_w;
      int ty = (int)(base_y + ray_y * z) & ceil_mask_h;
      framebuffer[y * buf_width + x] = darken(ceil_bitmap[ty * ceil_w + tx], render_context->ceiling_shade[y]);
    }
  }
}
static void render_sector(Render_Context* render_context, const E_Map_Sector* sector, Column_Range range, Sector_Queue* queue)
{
  for (int x = range.left; x < range.right; x++)
  {
    render_context->spans.ceiling_top[x] = 0;
    render_context->spans.ceiling_bottom[x] = 0;
    render_context->spans.floor_top[x] = 0;
    render_context->spans.floor_bottom[x] = 0;
  }

  for (int w = sector->first_wall; w < sector->first_wall + sector->wall_count; w++)
  {
    const E_Map_Wall* wall = render_context->map->walls + w;
    Projected_Wall pw = {0};

    int result = project_wall(render_context, sector, wall, range, &pw);
    if (result == WALL_HIDDEN) continue;

    if (result == WALL_AT_EYE)
    {
      enqueue(queue, render_context->map->sectors + wall->neighbour, range);
      continue;
    }

    if (wall->neighbour != -1)
    {
      enqueue(queue, render_context->map->sectors + wall->neighbour, (Column_Range){pw.x_start, pw.x_end});
    }

    for (int x = pw.x_start; x < pw.x_end; x++)
    {
      draw_wall_column(render_context, wall, &pw, x);
    }
  }

  draw_flats(render_context, sector, range);
}

void e_render
(
  const E_Map* map,
  E_Vec2f pos,
  E_Vec2f dir,
  float camera_z,
  float fov,
  int x_s,
  int x_e,
  E_Map_Sector* camera_sector, //optional, leave NULL if no starting sector known
  uint32_t* restrict framebuffer,
  int buf_width,
  int buf_height,
  float pitch
)
{
  if (camera_sector == NULL)
  {
    camera_sector = point_sector(map, pos);
    if (camera_sector == NULL)
    {
      fprintf(stderr, "camera in non-existent sector\n");
      return;
    }
  }

  Render_Context render_context = {0};
  render_context.map = map;
  render_context.framebuffer = framebuffer;
  render_context.buf_width = buf_width;
  render_context.buf_height = buf_height;
  render_context.pos = pos;
  render_context.dir = dir;
  render_context.right = (E_Vec2f){dir.y, -dir.x};
  render_context.camera_z = camera_z;
  render_context.focal = (buf_width / 2) / tan(fov / 2);
  render_context.r_focal = 1 / render_context.focal;

  float shift = render_context.focal * tanf(pitch);
  float max_shift = buf_height * 0.4f;
  if (shift > max_shift) shift = max_shift;
  if (shift < -max_shift) shift = -max_shift;
  render_context.horizon = buf_height * 0.5f + shift;

  int y_top[buf_width];
  int y_bottom[buf_width];
  for (int x = 0; x < buf_width; x++)
  {
    y_top[x] = 0;
    y_bottom[x] = buf_height;
  }
  render_context.y_top = y_top;
  render_context.y_bottom = y_bottom;

  float floor_z_scalar[buf_height];
  float ceiling_z_scalar[buf_height];
  for (int y = 0; y < buf_height; y++)
  {
    floor_z_scalar[y] = render_context.focal / ((y + 0.5f) - render_context.horizon);
    ceiling_z_scalar[y] = render_context.focal / (render_context.horizon - (y + 0.5f));
  }
  render_context.floor_z_scalar = floor_z_scalar;
  render_context.ceiling_z_scalar = ceiling_z_scalar;

  uint32_t floor_shade[buf_height];
  uint32_t ceiling_shade[buf_height];
  render_context.floor_shade = floor_shade;
  render_context.ceiling_shade = ceiling_shade;
  
  int ceiling_top[buf_width];
  int ceiling_bottom[buf_width];
  int floor_top[buf_width];
  int floor_bottom[buf_width];
  render_context.spans = (Flat_Spans){ceiling_top, ceiling_bottom, floor_top, floor_bottom};

  Sector_Queue queue = {0};
  enqueue(&queue, camera_sector, (Column_Range){x_s, x_e});
  while (queue.head)
  {
    Sector_Queue_Node node = dequeue(&queue);
    render_sector(&render_context, node.sector, node.range, &queue);
  }
}
