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
    fprintf(stderr, "queueis empty\n");
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
  E_Vec2f right = (E_Vec2f){dir.y, -dir.x};
  float focal = (buf_width/2)/tan(fov/2);
  float r_focal = 1 / focal;


  float shift = focal * tanf(pitch);
  float max_shift = buf_height * 0.4f;
  if (shift > max_shift) shift = max_shift;
  if (shift < -max_shift) shift = -max_shift;
  float horizon = buf_height * 0.5f + shift;
  
  Sector_Queue sector_queue = {0};
  //uint8_t* column_drawn = calloc((buf_width + 7) / 8, 1);
  int y_top[buf_width];
  int y_bottom[buf_width];
  for (int i = 0; i < buf_width; i++)
  {
    y_top[i] = 0;
    y_bottom[i] = buf_height;
  }

  if (camera_sector == NULL)
  {
    camera_sector = point_sector(map, pos);
    if (camera_sector == NULL)
    {
      fprintf(stderr, "camera in non-existent sector");
      return;
    }
  }
  enqueue(&sector_queue, camera_sector, (Column_Range){x_s, x_e});

  float floor_z_scalar[buf_height];
  for (int y = 0; y < buf_height; y++)
  {
    floor_z_scalar[y] = focal / ((y+0.5f) - horizon);
  }
  float ceiling_z_scalar[buf_height];
  for (int y = 0; y < buf_height; y++)
  {
    ceiling_z_scalar[y] = focal / (horizon - (y + 0.5f));
  }
  while (sector_queue.head)
  {
    const Sector_Queue_Node sector_data = dequeue(&sector_queue);
    const E_Map_Sector* sector = sector_data.sector;
    const Column_Range range = sector_data.range;

    const E_Map_Texture* floor_tex = &map->textures[sector->floor_tex];
    const E_Map_Texture* ceil_tex = &map->textures[sector->ceiling_tex];
    for (int w = sector->first_wall; w < sector->first_wall + sector->wall_count; w++)
    {
      const E_Map_Wall* wall = map->walls + w;
      const E_Map_Texture* wall_tex = &map->textures[wall->tex];
      const E_Map_Vertex v1 = map->vertices[wall->v1];
      const E_Map_Vertex v2 = map->vertices[wall->v2];
      E_Map_Vertex v1_t = transform_vertex(v1, pos, dir);
      E_Map_Vertex v2_t = transform_vertex(v2, pos, dir);

      float v1_u = vertex_distance(v1, v2);
      float v2_u = 0;
      
      if (v1_t.y < NEAR_PLANE_DEPTH && v2_t.y < NEAR_PLANE_DEPTH)
      {
        if (wall->neighbour != -1 &&
            v1_t.y > -NEAR_PLANE_DEPTH && v2_t.y > -NEAR_PLANE_DEPTH &&
            v1_t.x > 0 && v2_t.x < 0)
        {
          enqueue(&sector_queue, map->sectors + wall->neighbour, range);
        }
        continue;
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
      
      float v1_iz = 1 / v1_t.y;
      float v1_uz = v1_u / v1_t.y;
      float v2_iz = 1 / v2_t.y;
      float v2_uz = v2_u / v2_t.y;

      const float v1_screen_x = buf_width * 0.5f + focal * v1_t.x / v1_t.y;
      const float v2_screen_x = buf_width * 0.5f + focal * v2_t.x / v2_t.y;
      if (v1_screen_x <= v2_screen_x) continue;
        

      const float v1_screen_ceil_y = horizon - focal * (sector->ceiling_z - camera_z) / v1_t.y;
      const float v2_screen_ceil_y = horizon - focal * (sector->ceiling_z - camera_z) / v2_t.y;
      const float v1_screen_floor_y = horizon - focal * (sector->floor_z - camera_z) / v1_t.y;
      const float v2_screen_floor_y = horizon - focal * (sector->floor_z - camera_z) / v2_t.y;

      int x_start = v2_clipped ? range.left : (int)ceilf(v2_screen_x);
      int x_end = v1_clipped ? range.right : (int)ceilf(v1_screen_x);
      if (x_start < range.left) x_start = range.left;
      if (x_end > range.right) x_end = range.right;
      if (x_start >= x_end) continue;
      
      float v1_screen_neighbour_ceil_y = 0.0f;
      float v2_screen_neighbour_ceil_y = 0.0f;
      float v1_screen_neighbour_floor_y = 0.0f;
      float v2_screen_neighbour_floor_y = 0.0f;
      if (wall->neighbour != -1)
      {
        E_Map_Sector* neighbour = map->sectors+wall->neighbour;
        v1_screen_neighbour_ceil_y = horizon - focal * (neighbour->ceiling_z - camera_z) / v1_t.y;
        v2_screen_neighbour_ceil_y = horizon - focal * (neighbour->ceiling_z - camera_z) / v2_t.y;
        v1_screen_neighbour_floor_y = horizon - focal * (neighbour->floor_z - camera_z) / v1_t.y;
        v2_screen_neighbour_floor_y = horizon - focal * (neighbour->floor_z - camera_z) / v2_t.y;
        enqueue(&sector_queue, neighbour, (Column_Range){x_start, x_end});
      }

      float r_v1_v2_screen_x = 1 / (v1_screen_x - v2_screen_x);
      
        float base_x = pos.x * 64 + 65536.0f;
        float base_y = pos.y * 64 + 65536.0f;
      for (int x = x_start; x < x_end; x++)
      {
        if (y_top[x] >= y_bottom[x]) continue;

        float t = (x - v2_screen_x) * r_v1_v2_screen_x;

        float x_iz = v2_iz + (v1_iz - v2_iz) * t;
        float x_uz = v2_uz + (v1_uz - v2_uz) * t;
        float x_u = x_uz / x_iz;
        int tx = (int)(x_u * 64);
        tx = tx & (wall_tex->width - 1);
        float x_z = 1 / x_iz;
        float v_step = x_z * 64 * r_focal;
        uint32_t wall_shade = depth_shade(x_z);

        float ceil_y = v2_screen_ceil_y + (v1_screen_ceil_y - v2_screen_ceil_y) * t;
        float floor_y = v2_screen_floor_y + (v1_screen_floor_y - v2_screen_floor_y) * t;

        int y_ceil = (int)ceilf(ceil_y);
        int y_floor = (int)ceilf(floor_y);
        if (y_ceil < y_top[x]) y_ceil = y_top[x];
        if (y_ceil > y_bottom[x]) y_ceil = y_bottom[x];
        if (y_floor < y_top[x]) y_floor = y_top[x];
        if (y_floor > y_bottom[x]) y_floor = y_bottom[x];
        //static const uint32_t wall_colours[] = {0xFFC04040, 0xFF40C040, 0xFF4040C0, 0xFFC0C040, 0xFFC040C0, 0xFF40C0C0};
        //const uint32_t wall_colour = wall_colours[wall->tex % 6];
        float sz = (x + 0.5f - buf_width * 0.5f) * r_focal;
        float ray_x = (right.x*sz + dir.x)*64;
        float ray_y = (right.y*sz + dir.y)*64;
        for (int y = y_floor; y < y_bottom[x]; y++)
        {
          float z = (camera_z - sector->floor_z) * floor_z_scalar[y];
          int tx = (int)(base_x + ray_x * z) & (floor_tex->width - 1);
          int ty = (int)(base_y + ray_y * z) & (floor_tex->height - 1);
          framebuffer[y * buf_width + x] = darken(floor_tex->bitmap[ty * floor_tex->width + tx], depth_shade(z));
        }
        for (int y = y_top[x]; y < y_ceil; y++)
        {
          float z = (sector->ceiling_z - camera_z) * ceiling_z_scalar[y];
          int tx = (int)(base_x + ray_x * z) & (ceil_tex->width - 1);
          int ty = (int)(base_y + ray_y * z) & (ceil_tex->height - 1);
          framebuffer[y * buf_width + x] = darken(ceil_tex->bitmap[ty * ceil_tex->width + tx], depth_shade(z));
        }
        if (wall->neighbour == -1)
        {
          float v = (y_ceil + 0.5f - ceil_y) * v_step;
          for (int y = y_ceil; y < y_floor; y++)
          {
            framebuffer[y * buf_width + x] = 
              darken(wall_tex->bitmap[((int)v & (wall_tex->height -1)) * wall_tex->width + tx], wall_shade);
            v+=v_step;
          }
          y_top[x] = y_bottom[x];
        }
        else
        {
          float ceil_neighbour_y = v2_screen_neighbour_ceil_y + (v1_screen_neighbour_ceil_y - v2_screen_neighbour_ceil_y) * t;
          float floor_neighbour_y = v2_screen_neighbour_floor_y + (v1_screen_neighbour_floor_y - v2_screen_neighbour_floor_y) * t;
          int y_ceil_neighbour = (int)ceilf(ceil_neighbour_y);
          int y_floor_neighbour = (int)ceilf(floor_neighbour_y);
          if (y_ceil_neighbour < y_ceil) y_ceil_neighbour = y_ceil;
          if (y_ceil_neighbour > y_floor) y_ceil_neighbour = y_floor;
          if (y_floor_neighbour < y_ceil) y_floor_neighbour = y_ceil;
          if (y_floor_neighbour > y_floor) y_floor_neighbour = y_floor;
          float v = (y_ceil + 0.5f - ceil_y) * v_step;
          for (int y = y_ceil; y < y_ceil_neighbour; y++)
          {
            framebuffer[y * buf_width + x] =
              darken(wall_tex->bitmap[((int)v & (wall_tex->height - 1)) * wall_tex->width + tx], wall_shade);
            v+=v_step;
          }
          v = (y_floor_neighbour + 0.5f - ceil_y) * v_step;
          for (int y = y_floor_neighbour; y < y_floor; y++)
          {
            framebuffer[y * buf_width + x] = 
              darken(wall_tex->bitmap[((int)v & (wall_tex->height - 1)) * wall_tex->width + tx], wall_shade);
            v+=v_step;
          }

          y_top[x] = y_ceil_neighbour;
          y_bottom[x] = y_floor_neighbour;
        }
        if (e_debug_hook) e_debug_hook(e_debug_user);
      }
    }
  }
}
