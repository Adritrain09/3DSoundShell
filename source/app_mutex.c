#include "app_mutex.h"
#include "audio.h"
#include "navidrome.h"
#include "recorder.h"

static AppOwner s_current_owner = APP_OWNER_NONE;

void app_mutex_claim(AppOwner owner) {
    if (s_current_owner == owner) return;

    if (owner == APP_OWNER_LOCAL_PLAYER) {
        nd_clear_playing();
        if (recorder_is_active()) {
            recorder_leave();
        }
    } else if (owner == APP_OWNER_NAVIDROME) {
        audio_pause();
        if (recorder_is_active()) {
            recorder_leave();
        }
    } else if (owner == APP_OWNER_RECORDER) {
        audio_pause();
        nd_clear_playing();
    }

    s_current_owner = owner;
}

AppOwner app_mutex_get_owner(void) {
    return s_current_owner;
}
