#include <pebble.h>

#define BG_COLOR GColorDukeBlue
#define CONCENTRATION_COLOR GColorBrilliantRose
#define REST_COLOR GColorCyan

#define WAKEUP_REASON_CONCENTRATION_END 0
#define WAKEUP_REASON_REST_END 1

#define MODE_CONCENTRATION 0
#define MODE_REST 1

// hands
#define HAND_MARGIN  15
#define ANTIALIASING true
#define METER_THICKNESS 20
#define HAND_RADIUS 80

#define CONCENTRATION_TIME (60*25+10)
#define REST_TIME (60*5)

#define MENU_CELL_HEIGHT 50
#define MENU_ITEM_RESTART 0
#define MENU_ITEM_STOP 1

typedef struct {
  int hours;
  int minutes;
} Time;

typedef struct {
  char name[16];
} MenuInfo;
MenuInfo menu_array[] = {
  {"Restart"},
  {"Stop"},
};

// keys for persist values
enum {
  PERSIST_WAKEUP,
  PERSIST_POMODORO_MODE,
  PERSIST_METER_HOUR,
  PERSIST_METER_MIN
};

// windows and layers 
static Window *s_main_window, *s_menu_window;
static Layer *s_main_layer;
static MenuLayer *s_menu_layer;

// hands
static GPoint s_center;
static Time s_meter_time;

static TextLayer *s_left_time_label;

// wakeup
static WakeupId s_wakeup_id = -1;
static time_t s_wakeup_timestamp = 0;

static bool s_is_running = false;

static void reset_wakeup() {
  wakeup_cancel(s_wakeup_id);
  s_wakeup_id = -1;
  s_wakeup_timestamp = 0;
  persist_delete(PERSIST_WAKEUP);
}

static void start_pomodoro() {
  s_is_running = true;
  vibes_long_pulse();
  reset_wakeup();
  
  srand(time(NULL));
  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  
  time_t wakeup_time = time(NULL) + CONCENTRATION_TIME;
  s_wakeup_id = wakeup_schedule(wakeup_time, WAKEUP_REASON_CONCENTRATION_END, true);
  persist_write_int(PERSIST_WAKEUP, s_wakeup_id);
    
  persist_write_int(PERSIST_METER_HOUR, time_now->tm_hour);
  persist_write_int(PERSIST_METER_MIN, time_now->tm_min);
  s_meter_time.hours = persist_read_int(PERSIST_METER_HOUR);
  s_meter_time.minutes = persist_read_int(PERSIST_METER_MIN);
  
  text_layer_set_text_color(s_left_time_label, CONCENTRATION_COLOR);
}

static void start_pomodoro_rest() {
  persist_write_int(PERSIST_POMODORO_MODE, MODE_CONCENTRATION);
  vibes_double_pulse();
  
  time_t wakeup_time = time(NULL) + REST_TIME;
  s_wakeup_id = wakeup_schedule(wakeup_time, WAKEUP_REASON_REST_END, true);
  if (s_wakeup_id <= 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "FAILED to scheduling");
  }
  persist_write_int(PERSIST_WAKEUP, s_wakeup_id);
  
  text_layer_set_text_color(s_left_time_label, REST_COLOR);
}

static void stop_pomodoro() {
  s_is_running = false;
  reset_wakeup();
  text_layer_set_text(s_left_time_label, "00:00");
}

static void show_round_meter(GContext *ctx, GRect *bounds) {
  // radial 
  GRect frame = grect_inset((*bounds), GEdgeInsets(0));
  int minute_angle = (int)(360.0 * s_meter_time.minutes / 60.0);
  int32_t start_break = minute_angle + 360 * (CONCENTRATION_TIME) / 60 / 60; // 150 == 360 * 25 / 60
  int32_t end_break = start_break + 360 * (REST_TIME) / 60 / 60; // 30 == 360 * 5 / 60
  graphics_context_set_fill_color(ctx, CONCENTRATION_COLOR);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, METER_THICKNESS, DEG_TO_TRIGANGLE(minute_angle), DEG_TO_TRIGANGLE(start_break));
  graphics_context_set_fill_color(ctx, REST_COLOR);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, METER_THICKNESS, DEG_TO_TRIGANGLE(start_break), DEG_TO_TRIGANGLE(end_break));  

  // hour dots
  static int s_hour_dot_radius = 2;
  frame = grect_inset(frame, GEdgeInsets(5 * s_hour_dot_radius));
  for(int i = 0; i < 12; i++) {
    int hour_angle = i * 360 / 12;
    GPoint pos = gpoint_from_polar(frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(hour_angle));

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, pos, s_hour_dot_radius);
  }
}

static void update_main_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(s_main_layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  if(s_is_running) {
    show_round_meter(ctx, &bounds);
  }

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  Time mode_time;
  srand(time(NULL));
  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  mode_time.hours = time_now->tm_hour;
  mode_time.minutes = time_now->tm_min;
  
  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;

  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60 ) * (int32_t)(HAND_RADIUS - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(HAND_RADIUS - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(HAND_RADIUS - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(HAND_RADIUS - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };
  
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 4);
  // Draw hands with positive length only
  if(HAND_RADIUS > 2 * HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, hour_hand);
  }
  if(HAND_RADIUS > HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, minute_hand);
  }
 
  // center dot
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, s_center, 7);
}

static void main_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_push(s_menu_window, false);
}

static void main_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, main_select_click_handler);
}

static void count_timer_handler(void *data) {
  if(s_is_running) {
    if (s_wakeup_timestamp == 0) {
      // get the wakeup timestamp for showing a countdown
      wakeup_query(s_wakeup_id, &s_wakeup_timestamp);
    }
  
    int countdown = s_wakeup_timestamp - time(NULL);
    static char s_left_time_buf[16];
    snprintf(s_left_time_buf, sizeof(s_left_time_buf), "%02d:%02d", countdown/60, countdown%60);
    text_layer_set_text(s_left_time_label, s_left_time_buf);
  }
  layer_mark_dirty(text_layer_get_layer(s_left_time_label));
  app_timer_register(1000, count_timer_handler, data);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  window_set_click_config_provider(window, main_click_config_provider);

  s_center = grect_center_point(&bounds);

  s_main_layer = layer_create(bounds);
  layer_add_child(window_layer, s_main_layer);
  
  // left time label
  s_left_time_label = text_layer_create(GRect(46, 102, 110, 40));
  text_layer_set_background_color(s_left_time_label, GColorClear);
  if(persist_read_int(MODE_REST)) {
    text_layer_set_text_color(s_left_time_label, REST_COLOR);
  } else {
    text_layer_set_text_color(s_left_time_label, CONCENTRATION_COLOR);
  }
  text_layer_set_font(s_left_time_label, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  layer_add_child(window_layer, text_layer_get_layer(s_left_time_label));

  layer_set_update_proc(s_main_layer, update_main_proc);
  
  // fire timer
  app_timer_register(0, count_timer_handler, NULL);
}

static void window_unload(Window *window) {
  layer_destroy(s_main_layer);
}

static void menu_select_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index, void *callback_context) {
  switch(cell_index->row) {
    case MENU_ITEM_RESTART:
      start_pomodoro();
      break;
    case MENU_ITEM_STOP:
      stop_pomodoro();
      break;
  }
  
  // back to last window
  window_stack_pop(false);
}

static uint16_t get_menu_sections_count_callback(struct MenuLayer *menulayer, uint16_t section_index, 
                                            void *callback_context) {
  return sizeof(menu_array) / sizeof(MenuInfo);
}

static int16_t get_menu_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, 
                                        void *callback_context) {
  return MENU_CELL_HEIGHT;
}

static void draw_menu_row_handler(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, 
                             void *callback_context) {
  char* name = menu_array[cell_index->row].name;
  menu_cell_basic_draw(ctx, cell_layer, name, NULL, NULL);
}

static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_menu_sections_count_callback,
    .get_cell_height = get_menu_cell_height_callback,
    .draw_row = draw_menu_row_handler,
    .select_click = menu_select_callback
  }); 
  menu_layer_set_click_config_onto_window(s_menu_layer,	window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void menu_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

static void wakeup_handler(WakeupId id, int32_t reason) {
  // force to reset wakeup handler
  reset_wakeup();
  
  switch(reason) {
    case WAKEUP_REASON_CONCENTRATION_END:
      start_pomodoro_rest();
      break;
    case WAKEUP_REASON_REST_END:
      start_pomodoro();
      break;
  }
}

static void init() {
  bool wakeup_scheduled = false;
  
  // check whether wakeup handler is set
  if (persist_exists(PERSIST_WAKEUP)) {
    s_wakeup_id = persist_read_int(PERSIST_WAKEUP);
    if (wakeup_query(s_wakeup_id, NULL)) {
      wakeup_scheduled = true;
    } else {
      // force reset
      reset_wakeup();
    }
  }
    
  // resave value after resume
  if(wakeup_scheduled) {
    s_is_running = true;
  } else {
    start_pomodoro();
  }
  
  if(persist_exists(PERSIST_METER_HOUR))
    s_meter_time.hours = persist_read_int(PERSIST_METER_HOUR);
  if(persist_exists(PERSIST_METER_MIN))
    s_meter_time.minutes = persist_read_int(PERSIST_METER_MIN);
  
  // set up windows
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, main_click_config_provider);
  window_set_background_color(s_main_window, BG_COLOR);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers){
    .load = menu_window_load,
    .unload = menu_window_unload,
  });
  
  // register services
  wakeup_service_subscribe(wakeup_handler);

  // show foreground window
  window_stack_push(s_main_window, true);
}

static void deinit() {
}

int main() {
  init();
  app_event_loop();
  deinit();
}
