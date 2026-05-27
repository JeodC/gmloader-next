#include <stddef.h>
#include "platform.h"
#include "gml_plugin_api.h"
#include "video.h"

static int video_draw_adapter(void *buf)
{
    return video_draw_internal(buf) ? 1 : 0;
}

static const gml_video_backend_t g_backend = {
    .init         = video_init,
    .process      = video_process,
    .open         = video_open_internal,
    .close        = video_close_internal,
    .draw         = video_draw_adapter,
    .set_volume   = video_set_volume_internal,
    .seek_to      = video_seek_to_internal,
    .enable_loop  = video_enable_loop_internal,
    .pause        = video_pause_internal,
    .resume       = video_resume_internal,
    .status       = video_status_internal,
    .get_status   = video_get_status_internal,
    .get_format   = video_get_format_internal,
    .get_width    = video_get_width_internal,
    .get_height   = video_get_height_internal,
    .get_duration = video_get_duration_internal,
    .get_position = video_get_position_internal,
    .get_volume   = video_get_volume_internal,
    .is_looping   = video_is_looping_internal,
};

extern "C" __attribute__((visibility("default")))
int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json)
{
    (void)config_json;
    if (!api || !api->register_video_backend)
        return -1;
    api->register_video_backend(&g_backend);
    return 0;
}
