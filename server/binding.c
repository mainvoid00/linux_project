/* libdevice.so 런타임 동적 로딩 — dlopen/dlsym 함수 포인터 바인딩 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <syslog.h>
#include "server.h"

static void *g_lib;

/* server.h 에 extern 으로 선언된 함수 포인터들의 실제 정의 */
int  (*dev_init)(void);
void (*dev_cleanup)(void);
int  (*led_on)(void);
int  (*led_off)(void);
int  (*led_bright)(int);
int  (*buzzer_tone)(int);
int  (*buzzer_off)(void);
int  (*cds_read)(void);
int  (*cds_set_threshold)(int);
int  (*cds_get_threshold)(void);
int  (*fnd_display)(int);
int  (*fnd_clear)(void);

static void bind_symbols(void)
{
    dev_init    = dlsym(g_lib, "device_init");
    dev_cleanup = dlsym(g_lib, "device_cleanup");
    led_on      = dlsym(g_lib, "led_on");
    led_off     = dlsym(g_lib, "led_off");
    led_bright  = dlsym(g_lib, "led_bright");
    buzzer_tone = dlsym(g_lib, "buzzer_tone");
    buzzer_off  = dlsym(g_lib, "buzzer_off");
    cds_read    = dlsym(g_lib, "cds_read");
    cds_set_threshold = dlsym(g_lib, "cds_set_threshold");
    cds_get_threshold = dlsym(g_lib, "cds_get_threshold");
    fnd_display = dlsym(g_lib, "fnd_display");
    fnd_clear   = dlsym(g_lib, "fnd_clear");
}

int lib_load(const char *abspath)
{
    g_lib = dlopen(abspath, RTLD_NOW);
    if (!g_lib) { syslog(LOG_ERR, "dlopen: %s", dlerror()); return -1; }

    bind_symbols();
    if (!dev_init || !dev_cleanup || !led_on || !led_off || !led_bright ||
        !buzzer_tone || !buzzer_off || !cds_read || !cds_set_threshold ||
        !cds_get_threshold || !fnd_display || !fnd_clear) {
        syslog(LOG_ERR, "dlsym: missing symbol");
        return -1;
    }
    return 0;
}

void lib_unload(void)
{
    if (g_lib)
        dlclose(g_lib);
}
