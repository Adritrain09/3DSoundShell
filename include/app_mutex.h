#ifndef APP_MUTEX_H
#define APP_MUTEX_H

#include <stdbool.h>

typedef enum {
    APP_OWNER_NONE = 0,
    APP_OWNER_LOCAL_PLAYER,
    APP_OWNER_NAVIDROME,
    APP_OWNER_RECORDER
} AppOwner;

void app_mutex_claim(AppOwner owner);
AppOwner app_mutex_get_owner(void);

#endif // APP_MUTEX_H
