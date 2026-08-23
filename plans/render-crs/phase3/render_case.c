// render-crs フェーズ 3 の PoC レンダラ。docs/snippets/c/still-image.c の
// 骨格に、カメラ設定（--mercator）と mln_map_set_render_crs（--render-crs）を
// 足したもの。バックエンドは Metal 決め打ち（行列は CPU/UBO 側で完結する
// 設計なのでバックエンド非依存。開発機は macOS arm64）。
//
// 使い方（run_cases.py が組み立てる）:
//   render_case <style_url> <cache_path> <width> <height> <ratio> <out.ppm> \
//     mercator <lon> <lat> <zoom>
//   render_case <style_url> <cache_path> <width> <height> <ratio> <out.ppm> \
//     render-crs <zone> <center_e> <center_n> <mpp> <rotation>
//
// 出力は P6 PPM（アルファは落とす。背景不透明なので情報は失われない）。

#include <maplibre_native_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void* MTLCreateSystemDefaultDevice(void);

typedef enum still_image_state {
  STILL_IMAGE_PENDING,
  STILL_IMAGE_FINISHED,
  STILL_IMAGE_FAILED,
} still_image_state;

static still_image_state drain_still_image_events(
  mln_runtime runtime, mln_map map
) {
  still_image_state state = STILL_IMAGE_PENDING;

  mln_runtime_event_batch batch = mln_runtime_event_batch_default();
  if (mln_runtime_drain_events(runtime, 0, &batch) != MLN_STATUS_OK) {
    return STILL_IMAGE_FAILED;
  }

  for (size_t index = 0; index < batch.event_count; index++) {
    const char* bytes = (const char*)batch.events + index * batch.event_size;
    const mln_runtime_event* event = (const mln_runtime_event*)bytes;
    if (event->source != map) continue;
    if (event->type == MLN_RUNTIME_EVENT_MAP_STILL_IMAGE_FINISHED) {
      state = STILL_IMAGE_FINISHED;
    } else if (
      event->type == MLN_RUNTIME_EVENT_MAP_STILL_IMAGE_FAILED ||
      event->type == MLN_RUNTIME_EVENT_MAP_LOADING_FAILED
    ) {
      fprintf(stderr, "render_case: still image failed (event)\n");
      return STILL_IMAGE_FAILED;
    }
  }

  return state;
}

static bool await_still_image(
  mln_runtime runtime, mln_map map, mln_render_session session
) {
  bool finished = false;
  bool rendered = false;
  // 大判 + 初回タイルフェッチがあり得るので余裕を持たせる。
  const time_t deadline = time(NULL) + 120;

  while (!(finished && rendered) && time(NULL) < deadline) {
    mln_runtime_pump(runtime, 10, -1);
    const still_image_state state = drain_still_image_events(runtime, map);
    if (state == STILL_IMAGE_FAILED) return false;
    if (state == STILL_IMAGE_FINISHED) finished = true;
    mln_render_result result = MLN_RENDER_RESULT_NO_UPDATE;
    bool needs_repaint = false;
    if (
      mln_render_session_render_update(session, &result, &needs_repaint) ==
      MLN_STATUS_OK
    ) {
      rendered = rendered || result == MLN_RENDER_RESULT_RENDERED;
    }
  }

  if (!(finished && rendered)) {
    fprintf(stderr, "render_case: timed out waiting for the still image\n");
  }
  return finished && rendered;
}

static bool write_ppm(const char* path, mln_render_session session) {
  mln_texture_image_info info = mln_texture_image_info_default();
  mln_texture_read_premultiplied_rgba8(session, NULL, 0, &info);

  uint8_t* pixels = malloc(info.byte_length);
  if (pixels == NULL) return false;
  if (
    mln_texture_read_premultiplied_rgba8(
      session, pixels, info.byte_length, &info
    ) != MLN_STATUS_OK
  ) {
    free(pixels);
    fprintf(stderr, "render_case: texture readback failed\n");
    return false;
  }

  FILE* out = fopen(path, "wb");
  if (out == NULL) {
    free(pixels);
    return false;
  }
  fprintf(out, "P6\n%u %u\n255\n", info.width, info.height);
  for (uint32_t row = 0; row < info.height; row++) {
    const uint8_t* line = pixels + (size_t)row * info.stride;
    for (uint32_t col = 0; col < info.width; col++) {
      fwrite(line + (size_t)col * 4, 1, 3, out);
    }
  }
  fclose(out);
  free(pixels);
  printf("wrote %s (%ux%u)\n", path, info.width, info.height);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 8) {
    fprintf(stderr, "render_case: not enough arguments\n");
    return 2;
  }
  const char* style_url = argv[1];
  const char* cache_path = argv[2];
  const uint32_t width = (uint32_t)strtoul(argv[3], NULL, 10);
  const uint32_t height = (uint32_t)strtoul(argv[4], NULL, 10);
  const double ratio = strtod(argv[5], NULL);
  const char* out_path = argv[6];
  const char* mode = argv[7];

  mln_runtime_options runtime_options = mln_runtime_options_default();
  runtime_options.cache_path = cache_path;

  mln_runtime runtime = MLN_HANDLE_NULL;
  if (mln_runtime_create(&runtime_options, &runtime) != MLN_STATUS_OK) {
    fprintf(stderr, "render_case: runtime creation failed\n");
    return 1;
  }

  int exit_code = 1;
  mln_map map = MLN_HANDLE_NULL;
  mln_render_session session = MLN_HANDLE_NULL;

  mln_map_options map_options = mln_map_options_default();
  map_options.width = width;
  map_options.height = height;
  map_options.scale_factor = ratio;
  map_options.map_mode = MLN_MAP_MODE_STATIC;

  if (mln_map_create(runtime, &map_options, &map) != MLN_STATUS_OK) {
    fprintf(stderr, "render_case: map creation failed\n");
    goto cleanup;
  }
  mln_map_set_event_mask(
    map, MLN_RUNTIME_EVENT_MASK_MAP_STILL_IMAGE_FINISHED |
           MLN_RUNTIME_EVENT_MASK_MAP_STILL_IMAGE_FAILED |
           MLN_RUNTIME_EVENT_MASK_MAP_LOADING_FAILED
  );

  mln_metal_owned_texture_descriptor descriptor =
    mln_metal_owned_texture_descriptor_default();
  descriptor.extent.width = width;
  descriptor.extent.height = height;
  descriptor.extent.scale_factor = ratio;
  descriptor.context.device = MTLCreateSystemDefaultDevice();
  if (descriptor.context.device == NULL) {
    fprintf(stderr, "render_case: no Metal device\n");
    goto cleanup;
  }
  if (
    mln_metal_owned_texture_attach(map, &descriptor, &session) != MLN_STATUS_OK
  ) {
    fprintf(stderr, "render_case: texture attach failed\n");
    goto cleanup;
  }

  if (strcmp(mode, "mercator") == 0 && argc >= 11) {
    mln_camera_options camera = mln_camera_options_default();
    camera.fields = MLN_CAMERA_OPTION_CENTER | MLN_CAMERA_OPTION_ZOOM |
                    MLN_CAMERA_OPTION_BEARING | MLN_CAMERA_OPTION_PITCH;
    camera.longitude = strtod(argv[8], NULL);
    camera.latitude = strtod(argv[9], NULL);
    camera.zoom = strtod(argv[10], NULL);
    camera.bearing = 0.0;
    camera.pitch = 0.0;
    if (mln_map_jump_to(map, &camera) != MLN_STATUS_OK) {
      fprintf(stderr, "render_case: jump_to failed\n");
      goto cleanup;
    }
  } else if (strcmp(mode, "render-crs") == 0 && argc >= 13) {
    mln_render_crs_options options = mln_render_crs_options_default();
    options.crs_kind = MLN_CRS_JAPAN_PLANE_RECTANGULAR;
    options.zone = (uint32_t)strtoul(argv[8], NULL, 10);
    options.center_easting = strtod(argv[9], NULL);
    options.center_northing = strtod(argv[10], NULL);
    options.meters_per_pixel = strtod(argv[11], NULL);
    options.rotation = strtod(argv[12], NULL);
    if (mln_map_set_render_crs(map, &options) != MLN_STATUS_OK) {
      fprintf(stderr, "render_case: set_render_crs failed\n");
      goto cleanup;
    }
  } else {
    fprintf(stderr, "render_case: unknown mode or missing arguments\n");
    goto cleanup;
  }

  if (mln_map_set_style_url(map, style_url) != MLN_STATUS_OK) {
    fprintf(stderr, "render_case: set_style_url failed\n");
    goto cleanup;
  }
  if (mln_map_request_still_image(map) != MLN_STATUS_OK) {
    fprintf(stderr, "render_case: request_still_image failed\n");
    goto cleanup;
  }
  if (
    await_still_image(runtime, map, session) && write_ppm(out_path, session)
  ) {
    exit_code = 0;
  }

cleanup:
  if (session != MLN_HANDLE_NULL) mln_render_session_destroy(session);
  if (map != MLN_HANDLE_NULL) mln_map_destroy(map);
  mln_runtime_destroy(runtime);
  return exit_code;
}
