#include <pebble.h>
#include "pomodoro.h"

#define NUM_MENU_SECTIONS 2
#define NUM_MENU_ICONS 3
#define NUM_FIRST_MENU_ITEMS 3
#define NUM_SECOND_MENU_ITEMS 1

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *s_main_window, *s_menu_window;
static Layer *s_canvas_layer;
static MenuLayer *s_menu_layer;
static TextLayer *s_error_text_layer, *s_tea_text_layer;
static char s_tea_text[32];

static GPoint s_center;
static Time s_last_time, s_meter_time;
static int s_radius = 80;

static TextLayer *s_left_label;
static uint16_t s_left_time = 0;

#define WAKEUP_REASON 0
#define PERSIST_KEY_WAKEUP_ID 42
static WakeupId s_wakeup_id;

static void wakeup_handler(WakeupId id, int32_t reason) {
  // The app has woken!
  text_layer_set_text(s_left_label, "ENDDD");

  // Delete the ID
  persist_delete(PERSIST_KEY_WAKEUP_ID);
  
  static const uint32_t const segments[] = { 201, 100, 200, 100, 400 };
  VibePattern pat = {
    .durations = segments,
    .num_segments = ARRAY_LENGTH(segments),
  };
  vibes_enqueue_custom_pattern(pat);
}

static void show_round_meter(GContext *ctx, GRect *bounds) {
  // radial 
  GRect frame = grect_inset((*bounds), GEdgeInsets(0));
  int minute_angle = (int)(360.0 * s_meter_time.minutes / 60.0);
  // (GContext * ctx, GRect rect, GOvalScaleMode scale_mode, uint16_t inset_thickness, int32_t angle_start, int32_t angle_end)
  int32_t start_break = minute_angle + 150; // 150 == 360 * 25 / 60
  int32_t end_break = start_break + 30; // 30 == 360 * 5 / 60
  graphics_context_set_fill_color(ctx, GColorRajah);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, METER_THICKNESS, DEG_TO_TRIGANGLE(minute_angle), DEG_TO_TRIGANGLE(start_break));
  graphics_context_set_fill_color(ctx, GColorMintGreen);
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

static void update_proc(Layer *layer, GContext *ctx) {
  //APP_LOG(APP_LOG_LEVEL_WARNING, "UPDATE_PROC");
  
  GRect bounds = layer_get_bounds(s_canvas_layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  show_round_meter(ctx, &bounds);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  Time mode_time = s_last_time;
  
  APP_LOG(APP_LOG_LEVEL_ERROR, "mode_time.minutes");
  APP_LOG(APP_LOG_LEVEL_ERROR, "%d", mode_time.minutes);
  
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

void main_window_sec_update(int hours, int minutes, int seconds) {
  s_last_time.hours = hours;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = minutes;

  // show left time by minutes and sec
  s_left_time -= 1;
  static char s_left_time_buf[16];
  snprintf(s_left_time_buf, sizeof(s_left_time_buf), "%02d:%02d", s_left_time / 60, s_left_time % 60);
  text_layer_set_text(s_left_label, s_left_time_buf);
  
  // Redraw
  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}


static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  //Check the event is not already scheduled
  if (!wakeup_query(s_wakeup_id, NULL)) {
    // Current time + 30 seconds
    time_t future_time = time(NULL) + 5;

    // Schedule wakeup event and keep the WakeupId
    s_wakeup_id = wakeup_schedule(future_time, WAKEUP_REASON, true);
    persist_write_int(PERSIST_KEY_WAKEUP_ID, s_wakeup_id);

    // Prepare for waking up later
    //text_layer_set_text(s_output_layer, "This app will now wake up in 30 seconds.\n\nClose me!");
    APP_LOG(APP_LOG_LEVEL_WARNING, "TIME START");
    
  } else {
    // Check existing wakeup
    //check_wakeup();
    APP_LOG(APP_LOG_LEVEL_WARNING, "Existing wakeup?");
  }
}

static void click_config_provider(void *context) {
  // Register the ClickHandlers
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return NUM_MENU_SECTIONS;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case 0:
      return NUM_FIRST_MENU_ITEMS;
    case 1:
      return NUM_SECOND_MENU_ITEMS;
    default:
      return 0;
  }
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // Determine which section we're working with
  switch (section_index) {
    case 0:
        // Draw title text in the section header
        menu_cell_basic_header_draw(ctx, cell_layer, "Some example items");
      break;
    case 1:
        // Draw title text in the section header
        menu_cell_basic_header_draw(ctx, cell_layer, "One more");
      break;
  }
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  // Determine which section we're going to draw in
  switch (cell_index->section) {
    case 0:
      // Use the row to specify which item we'll draw
      switch (cell_index->row) {
        case 0:
          // This is a basic menu item with a title and subtitle
          menu_cell_basic_draw(ctx, cell_layer, "Basic Item", "With a subtitle", NULL);
          break;
        case 1:
          // This is a basic menu icon with a cycling icon
          menu_cell_basic_draw(ctx, cell_layer, "Basic Item2", "With a subtitle", NULL);
          break;
        case 2: 
          menu_cell_basic_draw(ctx, cell_layer, "Basic Item2", "With a subtitle", NULL);
          break;
      }
      break;
    case 1:
      switch (cell_index->row) {
        case 0:
          {
          // There is title draw for something more simple than a basic menu item
#ifdef PBL_RECT
          menu_cell_title_draw(ctx, cell_layer, "Final Item");
#else
          GSize size = layer_get_frame(cell_layer).size;
          graphics_draw_text(ctx, "Final Item", fonts_get_system_font(FONT_KEY_GOTHIC_28),
                             GRect(0, 0, size.w, size.h), GTextOverflowModeTrailingEllipsis, 
                             GTextAlignmentCenter, NULL);
#endif
          }
          break;
      }
  }
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  // Use the row to specify which item will receive the select action
  switch (cell_index->row) {
    // This is the menu item with the cycling icon
    case 1:
      // Cycle the icon
      //s_current_icon = (s_current_icon + 1) % NUM_MENU_ICONS;
      // After changing the icon, mark the layer to have it updated
      //layer_mark_dirty(menu_layer_get_layer(menu_layer));
      break;
  }
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) { 
  if (menu_layer_is_index_selected(menu_layer, cell_index)) {
    switch (cell_index->row) {
      case 0:
        return MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT;
        break;
      default:
        return MENU_CELL_ROUND_FOCUSED_TALL_CELL_HEIGHT;
    }
  } else {
    return MENU_CELL_ROUND_UNFOCUSED_SHORT_CELL_HEIGHT;
  }
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_center = grect_center_point(&bounds);

  s_canvas_layer = layer_create(bounds);
  layer_add_child(window_layer, s_canvas_layer);
  
  // left time label
  s_left_label = text_layer_create(GRect(46, 102, 110, 40));
  text_layer_set_background_color(s_left_label, GColorClear);
  text_layer_set_text_color(s_left_label, GColorBrilliantRose);
  text_layer_set_font(s_left_label, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  layer_add_child(window_layer, text_layer_get_layer(s_left_label));

  layer_set_update_proc(s_canvas_layer, update_proc);

  // Create the menu layer
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = PBL_IF_RECT_ELSE(menu_get_header_height_callback, NULL),
    .draw_header = PBL_IF_RECT_ELSE(menu_draw_header_callback, NULL),
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .get_cell_height = PBL_IF_ROUND_ELSE(get_cell_height_callback, NULL),
  });

  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(s_menu_layer, window);

  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

void main_window_push() {
  s_left_time = 60 * 25 + 10;
 
  srand(time(NULL));
  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  s_meter_time.hours = time_now->tm_hour;
  s_meter_time.minutes = time_now->tm_min;
  
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_background_color(s_main_window, BG_COLOR);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_main_window, true);
    

  // Was this a wakeup launch?
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "APP_LAUNCH_WAKEUP");
    // The app was started by a wakeup
    WakeupId id = 0;
    int32_t reason = 0;

    // Get details and handle the wakeup
    wakeup_get_launch_event(&id, &reason);
    wakeup_handler(id, reason);
  
  //} else if (wakeup_scheduled) {
  //  window_stack_push(s_main_window, false);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "APP_LAUNCH_ELSE");
  //} else {
    // Check whether a wakeup will occur soon
    // Get the ID
    /*
    s_wakeup_id = persist_read_int(PERSIST_KEY_WAKEUP_ID);

    if (s_wakeup_id > 0) {
      // There is a wakeup scheduled soon
      time_t timestamp = 0;
      wakeup_query(s_wakeup_id, &timestamp);
      int seconds_remaining = timestamp - time(NULL);

      // Show how many seconds to go
      static char s_buffer[64];
      snprintf(s_buffer, sizeof(s_buffer), "The event is already scheduled for %d seconds from now!", seconds_remaining);
      APP_LOG(APP_LOG_LEVEL_WARNING, "remaining sec:%s", s_buffer);
    }
    */
  }
  
  // timer
  // Subscribe to Wakeup API
  //wakeup_service_subscribe(wakeup_handler);
}

