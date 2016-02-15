#include <pebble.h>
#include "config.h"

#define POMODORO_TIME (60*25+10)
#define POMODORO_REST_TIME (60*5)

#define POMODORO_COLOR GColorBrilliantRose
#define POMODORO_REST_COLOR GColorCyan

static Window *s_main_window, *s_menu_window;
static Layer *s_main_layer;
static MenuLayer *s_menu_layer;
static TextLayer *s_error_text_layer;

static GPoint s_center;
static Time s_last_time, s_meter_time;
static int s_radius = 80;

static TextLayer *s_left_label;
static uint16_t s_left_time = 0;
static bool s_rest_time = false;

#define WAKEUP_REASON 0
#define PERSIST_KEY_WAKEUP_ID 42
static WakeupId s_wakeup_id;

/*
 * Main Window Settings
 */
static void reset_time(bool start_pomodoro) {
  srand(time(NULL));
  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  if(start_pomodoro) {
    s_left_time = POMODORO_TIME;
    s_meter_time.hours = time_now->tm_hour;
    s_meter_time.minutes = time_now->tm_min;
    text_layer_set_text_color(s_left_label, POMODORO_COLOR);
  } else {
    s_left_time = POMODORO_REST_TIME;
    text_layer_set_text_color(s_left_label, POMODORO_REST_COLOR);
  }
}

static void show_round_meter(GContext *ctx, GRect *bounds) {
  // radial 
  GRect frame = grect_inset((*bounds), GEdgeInsets(0));
  int minute_angle = (int)(360.0 * s_meter_time.minutes / 60.0);
  // (GContext * ctx, GRect rect, GOvalScaleMode scale_mode, uint16_t inset_thickness, int32_t angle_start, int32_t angle_end)
  int32_t start_break = minute_angle + 360 * (POMODORO_TIME) / 60 / 60; // 150 == 360 * 25 / 60
  int32_t end_break = start_break + 360 * (POMODORO_REST_TIME) / 60 / 60; // 30 == 360 * 5 / 60
  //graphics_context_set_fill_color(ctx, GColorRajah);
  graphics_context_set_fill_color(ctx, POMODORO_COLOR);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, METER_THICKNESS, DEG_TO_TRIGANGLE(minute_angle), DEG_TO_TRIGANGLE(start_break));
  graphics_context_set_fill_color(ctx, POMODORO_REST_COLOR);
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
  show_round_meter(ctx, &bounds);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  Time mode_time = s_last_time;
  
  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;

  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60 ) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };
  
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 4);
  // Draw hands with positive length only
  if(s_radius > 2 * HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, hour_hand);
  }
  if(s_radius > HAND_MARGIN) {
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

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  window_set_click_config_provider(window, main_click_config_provider);

  s_center = grect_center_point(&bounds);

  s_main_layer = layer_create(bounds);
  layer_add_child(window_layer, s_main_layer);
  
  // left time label
  s_left_label = text_layer_create(GRect(46, 102, 110, 40));
  text_layer_set_background_color(s_left_label, GColorClear);
  text_layer_set_text_color(s_left_label, POMODORO_COLOR);
  text_layer_set_font(s_left_label, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  layer_add_child(window_layer, text_layer_get_layer(s_left_label));

  layer_set_update_proc(s_main_layer, update_main_proc);
  
  // set wakeup
  APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup set!");
  time_t future_time = time(NULL) + 60 * 25 + 10;
  s_wakeup_id = wakeup_schedule(future_time, WAKEUP_REASON, true);
  persist_write_int(PERSIST_KEY_WAKEUP_ID, s_wakeup_id);
}

static void window_unload(Window *window) {
  layer_destroy(s_main_layer);
}

static void sec_tick_handler(struct tm *time_now, TimeUnits changed) {
  s_last_time.hours = time_now->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = time_now->tm_min;

  // show left time by minutes and sec
  s_left_time -= 1;
  if(s_left_time <= 0) {
    if(s_rest_time) {
      // start pomodoro
      s_rest_time = false;
      vibes_double_pulse();
      reset_time(true);
    } else {
      // take rest
      s_rest_time = true;
      vibes_long_pulse();
      reset_time(false);
    }
  }
  
  static char s_left_time_buf[16];
  snprintf(s_left_time_buf, sizeof(s_left_time_buf), "%02d:%02d", s_left_time / 60, s_left_time % 60);
  text_layer_set_text(s_left_label, s_left_time_buf);
  
  // Redraw
  if(s_main_layer) {
    layer_mark_dirty(s_main_layer);
  }
}

/*
 * Menu Settings
 */
static void menu_select_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index, void *callback_context) {
  switch(cell_index->row) {
    case 0:
      reset_time(true);
      break;
    case 1:
      reset_time(true);
      break;
  }
  window_stack_pop(false);
}

static uint16_t get_menu_sections_count_callback(struct MenuLayer *menulayer, uint16_t section_index, 
                                            void *callback_context) {
  int count = sizeof(menu_array) / sizeof(MenuInfo);
  return count;
}

static int16_t get_menu_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, 
                                        void *callback_context) {
  return 50;
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

  /*
  s_error_text_layer = text_layer_create((GRect) { .origin = {0, 44}, .size = {bounds.size.w, 60}});
  text_layer_set_text(s_error_text_layer, "Cannot\nschedule");
  text_layer_set_text_alignment(s_error_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_error_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(s_error_text_layer, GColorWhite);
  text_layer_set_background_color(s_error_text_layer, GColorBlack);
  layer_set_hidden(text_layer_get_layer(s_error_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_error_text_layer));
  */
}

static void menu_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_error_text_layer);
}

/*
static void wakeup_handler(WakeupId id, int32_t reason) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_handler...");
  vibes_long_pulse();
  text_layer_set_text(s_left_label, "wakupe");

  // Delete the ID
  persist_delete(PERSIST_KEY_WAKEUP_ID);
}
*/

static void init() {
  s_left_time = POMODORO_TIME;
  srand(time(NULL));
  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  s_meter_time.hours = time_now->tm_hour;
  s_meter_time.minutes = time_now->tm_min;
  
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, main_click_config_provider);
  window_set_background_color(s_main_window, BG_COLOR);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  tick_timer_service_subscribe(SECOND_UNIT, sec_tick_handler);
  
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers){
    .load = menu_window_load,
    .unload = menu_window_unload,
  });
  
  // Subscribe to Wakeup API
  //wakeup_service_subscribe(wakeup_handler);

  // Was this a wakeup launch?
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "APP_LAUNCH_WAKEUP");
    /*
    // The app was started by a wakeup
    WakeupId id = 0;
    int32_t reason = 0;

    // Get details and handle the wakeup
    wakeup_get_launch_event(&id, &reason);
    wakeup_handler(id, reason);
    */
  } else {
    window_stack_push(s_main_window, true);
  }
}

static void deinit() {
  tick_timer_service_unsubscribe();
}

int main() {
  init();
  app_event_loop();
  deinit();
}
