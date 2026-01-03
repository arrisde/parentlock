#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h> // For sync()
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <png.h>
#include <sys/mman.h>
#include <libgen.h>

#include <sys/types.h>
#include <dirent.h>
#include "muxshare.h"
#include "muparentlock.h"
#include "ui/ui_muparentlock.h"
#include "../common/log.h"
#include "../common/init.h"
#include "../common/common.h"
#include "../common/ui_common.h"
#include "../common/parentlock.h"


struct mux_parentlock parentlock;

#define MUX_PARENTAUTH "/tmp/mux_parentauth" // Muxparentlock Config Authorization
#define MUX_PARENTLOCK_TRACKING "/tmp/mux_parenttrack" // Muxparentlock uptime tracking file (volatile)
#define MAX_PATH_LEN 256

const char* app_dir = NULL;
char nv_counter_file[MAX_PATH_LEN] = {};
char config_file[MAX_PATH_LEN] = {};
char five_min_warn_icon[MAX_PATH_LEN] = {};

volatile int  exit_required = 0;
int           timetracker_running = 0; 
const char *  p_code = NULL;

// This is signal safe copy file method that works across file system, unlike rename
int copy_file(const char * old, const char * new)
{
    int source = open(old, O_RDONLY, 0);
    int dest = open(new, O_WRONLY | O_CREAT /*| O_TRUNC*/, 0644);
    if (source == -1 || dest == -1) return -1;

    char buf[256] = {};
    size_t size = 0;
    while ((size = read(source, buf, sizeof(buf))) > 0)
        write(dest, buf, size);

    close(source);
    close(dest);
    fsync(dest);

    return size;
}
extern void mux_signal_stop(void);
void sighandler(int signumber)
{
    if (signumber == SIGTERM || signumber == SIGINT || signumber == SIGCONT)
    {
        // Need to copy the temporal file to non-volatile storage now, rename is a safe call from signal handler
        int ret = copy_file(MUX_PARENTLOCK_TRACKING, nv_counter_file); 
        if (ret == -1) {
            write(1, "Failed\n", sizeof("Failed\n") - 1); 
        }
        else {
            write(1, "Copied ", sizeof("Copied ") - 1);
            write(1, MUX_PARENTLOCK_TRACKING, sizeof(MUX_PARENTLOCK_TRACKING) - 1);
            write(1, " to ", sizeof(" to ") - 1);
            write(1, nv_counter_file, strlen(nv_counter_file));
            write(1, "\n", 1);
        }

        exit_required = 1;
        mux_signal_stop(); // Calling mux_input_stop here isn't safe since it tries to join threads, but mux_signal_stop is (or should be)
    }
    if (signumber == SIGINT)
    {
        write(1, "Exiting\n", sizeof("Exiting\n") - 1);
    }
}

void shutdown_now()
{
    system("/opt/muos/script/mux/quit.sh poweroff frontend");
}

#define UI_COUNT 4
static lv_obj_t *ui_objects[UI_COUNT];
static lv_obj_t *ui_mux_panels[2];
int exit_status_muxparentlock = 0;

static void init_navigation_group() {
    ui_objects[0] = ui_rolComboOne;
    ui_objects[1] = ui_rolComboTwo;
    ui_objects[2] = ui_rolComboThree;
    ui_objects[3] = ui_rolComboFour;

    ui_group = lv_group_create();

    for (unsigned int i = 0; i < sizeof(ui_objects) / sizeof(ui_objects[0]); i++) {
        lv_group_add_obj(ui_group, ui_objects[i]);
    }
}

static void handle_confirm(void) {
    play_sound(SND_CONFIRM, 0);

    char b1[2], b2[2], b3[2], b4[2];
    uint32_t bs = sizeof(b1);

    lv_roller_get_selected_str(ui_rolComboOne, b1, bs);
    lv_roller_get_selected_str(ui_rolComboTwo, b2, bs);
    lv_roller_get_selected_str(ui_rolComboThree, b3, bs);
    lv_roller_get_selected_str(ui_rolComboFour, b4, bs);

    char try_code[13];
    sprintf(try_code, "%s%s%s%s", b1, b2, b3, b4);

    if (strcasecmp(try_code, p_code) == 0) {
        exit_status_muxparentlock = 1;
        close_input();
        mux_input_stop();
    }
}

static void handle_shutdown(void) {

    exit_status_muxparentlock = 2;
    close_input();
    mux_input_stop();
}
    
static void handle_up(void) {
    play_sound(SND_NAVIGATE, 0);

    struct _lv_obj_t *element_focused = lv_group_get_focused(ui_group);
    lv_roller_set_selected(element_focused,
                           lv_roller_get_selected(element_focused) - 1,
                           LV_ANIM_ON);
}

static void handle_down(void) {
    play_sound(SND_NAVIGATE, 0);

    struct _lv_obj_t *element_focused = lv_group_get_focused(ui_group);
    lv_roller_set_selected(element_focused,
                           lv_roller_get_selected(element_focused) + 1,
                           LV_ANIM_ON);
}

static void handle_left(void) {
    first_open ? (first_open = 0) : play_sound(SND_NAVIGATE, 0);
    nav_prev(ui_group, 1);
}

static void handle_right(void) {
    first_open ? (first_open = 0) : play_sound(SND_NAVIGATE, 0);
    nav_next(ui_group, 1);
}

static void init_elements() {
    ui_mux_panels[0] = ui_pnlFooter;
    ui_mux_panels[1] = ui_pnlHeader;

    adjust_panel_priority(ui_mux_panels, sizeof(ui_mux_panels) / sizeof(ui_mux_panels[0]));

    if (bar_footer) lv_obj_set_style_bg_opa(ui_pnlFooter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (bar_header) lv_obj_set_style_bg_opa(ui_pnlHeader, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_label_set_text(ui_lblPreviewHeader, "");
    lv_label_set_text(ui_lblPreviewHeaderGlyph, "");

    process_visual_element(CLOCK, ui_lblDatetime);
    process_visual_element(BLUETOOTH, ui_staBluetooth);
    process_visual_element(NETWORK, ui_staNetwork);
    process_visual_element(BATTERY, ui_staCapacity);

    lv_label_set_text(ui_lblNavA, lang.GENERIC.SELECT);
    
    lv_obj_t *nav_hide[] = {
            ui_lblNavAGlyph,
            ui_lblNavA
    };

    for (int i = 0; i < sizeof(nav_hide) / sizeof(nav_hide[0]); i++) {
        lv_obj_clear_flag(nav_hide[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(nav_hide[i], LV_OBJ_FLAG_FLOATING);
    }

    kiosk_image = lv_img_create(ui_screen);
    load_kiosk_image(ui_screen, kiosk_image);

    overlay_image = lv_img_create(ui_screen);
    load_overlay_image(ui_screen, overlay_image);
}

#define ArrSz(X)	sizeof(X)/sizeof(X[0])
void apply_parentlock_theme(lv_obj_t *ui_rolComboOne, lv_obj_t *ui_rolComboTwo, lv_obj_t *ui_rolComboThree, lv_obj_t *ui_rolComboFour) {
    lv_obj_t * elements[] = {
        ui_rolComboOne,
        ui_rolComboTwo,
        ui_rolComboThree,
        ui_rolComboFour,
    };
    for (size_t i = 0; i < ArrSz(elements); i++) {
        lv_obj_set_style_text_color(elements[i], lv_color_hex(theme.ROLL.TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(elements[i], lv_color_hex(theme.ROLL.SELECT_TEXT), LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(elements[i], theme.ROLL.TEXT_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(elements[i], theme.ROLL.SELECT_TEXT_ALPHA, LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(elements[i], lv_color_hex(theme.ROLL.BACKGROUND), LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(elements[i], theme.ROLL.BACKGROUND_ALPHA, LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(elements[i], lv_color_hex(theme.ROLL.SELECT_BACKGROUND), LV_PART_SELECTED | LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(elements[i], theme.ROLL.SELECT_BACKGROUND_ALPHA, LV_PART_SELECTED | LV_STATE_FOCUSED);
        lv_obj_set_style_radius(elements[i], theme.ROLL.RADIUS, LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(elements[i], theme.ROLL.SELECT_RADIUS, LV_PART_SELECTED | LV_STATE_FOCUSED);
        lv_obj_set_style_radius(elements[i], theme.ROLL.BORDER_RADIUS, LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(elements[i], lv_color_hex(theme.ROLL.BORDER_COLOUR), LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_outline_opa(elements[i], theme.ROLL.BORDER_ALPHA, LV_PART_MAIN | LV_STATE_FOCUSED);
    }
}

void init_audio() {
    int r = 10;
    while (r-- > 0) {
        if (init_audio_backend()) {
            init_fe_snd(&fe_snd, config.SETTINGS.GENERAL.SOUND, 0);
            init_fe_bgm(&fe_bgm, config.SETTINGS.GENERAL.BGM, 0);

            break;
        }

        // start at 50ms then double exponent - stop at 1s max
        // tbqh if it isn't initialised in this time something
        // is really wrong with the device audio initialisation...
        useconds_t delay = (50000U << r);
        if (delay > 1000000U) delay = 1000000U;
        usleep(delay);
    }

    if (!file_exist(CHIME_DONE) && config.SETTINGS.GENERAL.CHIME) play_sound(SND_STARTUP, 0);
    write_text_to_file(CHIME_DONE, "w", CHAR, "");
}


int muparentlock_main() {
    init_theme(0, 0);
    init_display();

    init_audio();

    load_parentlock(&parentlock, &device, config_file);

    p_code = parentlock.CODE.UNLOCK;

    if (strcasecmp(p_code, "0000") == 0) {
        return 1;
    }

    init_theme(0, 0);

    init_ui_common_screen(&theme, &device, &lang, strlen(parentlock.MESSAGE.UNLOCK) ? parentlock.MESSAGE.UNLOCK : lang.MUXPASS.TITLE);
    init_muxparentlock(ui_pnlContent);
    init_elements();


    lv_obj_set_user_data(ui_screen, "muparentlock");
    lv_label_set_text(ui_lblDatetime, get_datetime());

    apply_parentlock_theme(ui_rolComboOne, ui_rolComboTwo, ui_rolComboThree, ui_rolComboFour);

    load_wallpaper(ui_screen, NULL, ui_pnlWall, ui_imgWall, GENERAL);
    load_font_text(ui_screen);

    init_fonts();
    init_navigation_group();

    load_kiosk(&kiosk);

    init_timer(NULL, NULL);

    mux_input_options input_opts = {
            .swap_axis = (theme.MISC.NAVIGATION_TYPE == 1),
            .press_handler = {
                    [MUX_INPUT_A] = handle_confirm,
                    [MUX_INPUT_DPAD_UP] = handle_up,
                    [MUX_INPUT_DPAD_DOWN] = handle_down,
                    [MUX_INPUT_DPAD_LEFT] = handle_left,
                    [MUX_INPUT_DPAD_RIGHT] = handle_right,
                    [MUX_INPUT_POWER_SHORT] = handle_shutdown,
            },
            .hold_handler = {
                    [MUX_INPUT_DPAD_UP] = handle_up,
                    [MUX_INPUT_DPAD_DOWN] = handle_down,
                    [MUX_INPUT_DPAD_LEFT] = handle_left,
                    [MUX_INPUT_DPAD_RIGHT] = handle_right,
            }
    };
    init_input(&input_opts, true);
    
    // Grab the input so the other PID don't see the event
    ioctl(input_opts.general_fd, EVIOCGRAB, 1);

    mux_input_task(&input_opts);
    
    // Ungrab the input so the other PID can resume seeing the events
    ioctl(input_opts.general_fd, EVIOCGRAB, 0);
    LOG_DEBUG("muparentlock", "Exiting from lockscreen with %d", exit_status_muxparentlock)

    return exit_status_muxparentlock;
}

// Scan files in dir, read any symbolic link and check if it matches the given filename
static bool has_file_in_dir (const char *const dir_to_scan, const char * compared_filename) {
    DIR * fd_dir = opendir (dir_to_scan);
    if (fd_dir == NULL) return false;

    struct dirent * fd_ent = readdir (fd_dir);
    while (fd_ent != NULL)
    {
        if (fd_ent->d_type == DT_LNK) {
            char fd_symlnk[PATH_MAX];
            snprintf (fd_symlnk, sizeof (fd_symlnk), "%s/%s", dir_to_scan, fd_ent->d_name);

            char fd_target[PATH_MAX + 1];
            size_t fd_target_len = readlink (fd_symlnk, fd_target, sizeof (fd_target) - 1);
            if (fd_target_len > 0) {
                fd_target[fd_target_len] = '\0';
                if (!strcmp (fd_target, compared_filename)) {
                    closedir(fd_dir);
                    return true;
                }
            }
        }
        fd_ent = readdir (fd_dir);
    }

    closedir (fd_dir);
    return false;
}

static pid_t find_pid_using (const char * filename, pid_t previous) {
    DIR * proc_dir = opendir ("/proc");
    if (proc_dir == NULL) return 0;

    struct dirent * proc_ent = readdir (proc_dir);
    int found_prev = previous ? 0 : 1;
    while (proc_ent != NULL)
    {
        char junk;
        pid_t pid;
        if ((proc_ent->d_type == DT_DIR) && (sscanf (proc_ent->d_name, "%d%c", &pid, &junk) == 1))
        {
            char fd_dir_to_scan[PATH_MAX];
            snprintf (fd_dir_to_scan, sizeof (fd_dir_to_scan), "/proc/%s/fd", proc_ent->d_name);
            if (has_file_in_dir(fd_dir_to_scan, filename)) {
                if (found_prev) return pid;
                if (pid == previous) found_prev = 1;
            }
        }

        proc_ent = readdir (proc_dir);
    }

    closedir (proc_dir);
    return 0;
}

static pid_t kill_users_of(const char * filename, int sig) {
    pid_t pid = find_pid_using(filename, 0);
    while (pid)
    {
        if (pid != getpid()) kill(pid, sig);
        pid = find_pid_using(filename, pid);
    }
    return pid;
}

typedef struct {
    int fd;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    size_t size;
    void * mem;
} fb_info;

int open_fb(fb_info * fb) {
    if (fb == NULL) return -1;

    fb->fd = open(device.SCREEN.DEVICE, O_RDWR);
    if (fb->fd < 0) {
        perror("Error opening framebuffer device");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0) {
        perror("Error retrieving fixed screen info");
        close(fb->fd);
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        perror("Error retrieving variable screen info");
        close(fb->fd);
        return -1;
    }

    fb->size = fb->finfo.smem_len;
    fb->mem = mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        perror("Error mapping framebuffer memory");
        close(fb->fd);
        return -1;
    }
    return 0;
}

int destroy_fb(fb_info * fb) {
    if (fb == NULL) return -1;

    munmap(fb->mem, fb->size);
    close(fb->fd);
    return 0;
}


void get_active_area_framebuffer(fb_info * fb, size_t * offset, size_t * size, size_t * stride, int reask) {
    if (reask && ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        perror("Error retrieving variable screen info");
    }
    *stride = fb->finfo.line_length / (fb->vinfo.bits_per_pixel / 8);
    *offset = fb->vinfo.xoffset + (fb->vinfo.yoffset * *stride);
    *size   = *stride * fb->vinfo.yres;
}

int overlay_framebuffer(const char * overlay_filename, const int pos_x, const int pos_y, const int delay) {
    // Then perform the overlay action here
    FILE *fp = fopen(overlay_filename, "rb");
    if (fp == NULL) {
        perror("Error opening the given filename");
        return -1;
    }

    // Read PNG file now
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (setjmp(png_jmpbuf(png))) {
        perror("Error creating the png structure");
        return -1;
    }
    png_init_io(png, fp);

    png_infop info = png_create_info_struct(png);
    png_read_info(png, info);
    uint32_t width = png_get_image_width(png, info);
    uint32_t height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // Convert to RGBA
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE || color_type == PNG_COLOR_TYPE_RGB) {
      png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    uint8_t * rgba = malloc(height * width * 4);
    png_bytep* row_pointers = malloc(height * sizeof(png_bytep));
    for(uint32_t y = 0; y < height; y++)
      row_pointers[y] = (png_bytep)(rgba + y * width * 4);

    png_read_image(png, row_pointers);
    free(row_pointers);
    fclose(fp);

    fb_info fb;

    if (open_fb(&fb) < 0) return -1;

    // Draw to only the active area in the framebuffer
    size_t offset = 0, size = 0, stride = 0;
    get_active_area_framebuffer(&fb, &offset, &size, &stride, 0);

    // Pause process if asked to
    kill_users_of(device.SCREEN.DEVICE, SIGSTOP);

    // Save the current framebuffer content to restore afterward
    uint32_t * saved_pixels = malloc(fb.size);
    if (saved_pixels == NULL)
    {
        destroy_fb(&fb);
        free(rgba);
        perror("Not enough memory to save the current frame buffer");
        return -1;
    }
    memcpy(saved_pixels, fb.mem, fb.size);

    uint32_t * out = ((uint32_t*)fb.mem) + offset, * in = saved_pixels + offset, * content = (uint32_t*)rgba;
    out += stride * pos_y; in += stride * pos_y;
    for (size_t y = pos_y; y < (height + pos_y) && y < fb.vinfo.yres; ++y) {
        for (size_t x = pos_x; x < (width + pos_x) && x < fb.vinfo.xres; ++x) {
            uint32_t c = content[x - pos_x], i = in[x];
            uint8_t a = (c & 0xFF000000) >> 24;
            uint8_t b = (c & 0x00FF0000) >> 16;
            uint8_t g = (c & 0x0000FF00) >> 8;
            uint8_t r = (c & 0x000000FF) >> 0;

            uint8_t ia = (i >> fb.vinfo.transp.offset) & ((1 << fb.vinfo.transp.length) - 1);
            uint8_t ir = (i >> fb.vinfo.red.offset) & ((1 << fb.vinfo.red.length) - 1);
            uint8_t ig = (i >> fb.vinfo.green.offset) & ((1 << fb.vinfo.green.length) - 1);
            uint8_t ib = (i >> fb.vinfo.blue.offset) & ((1 << fb.vinfo.blue.length) - 1);

            // Alpha blend pixels now
            r = (r * a + ir * (255 - a)) / 255;
            g = (g * a + ig * (255 - a)) / 255;
            b = (b * a + ib * (255 - a)) / 255;

            // And output them
            c = ((ia & ((1 << fb.vinfo.transp.length) - 1)) << fb.vinfo.transp.offset)
                | ((r & ((1 << fb.vinfo.red.length) - 1)) << fb.vinfo.red.offset)
                | ((g & ((1 << fb.vinfo.green.length) - 1)) << fb.vinfo.green.offset)
                | ((b & ((1 << fb.vinfo.blue.length) - 1)) << fb.vinfo.blue.offset);

            out[x] = c;
        }
        content += width;
        in += stride;
        out += stride;
    }

    sleep(delay);

    // Finally restore the framebuffer to what it was before
    memcpy(fb.mem, saved_pixels, fb.size);
    kill_users_of(device.SCREEN.DEVICE, SIGCONT);

    free(rgba);
    free(saved_pixels);
    destroy_fb(&fb);
    printf("Drawn overlay %s(%ux%u) at %d,%d.\n", overlay_filename, width, height, pos_x, pos_y);
    return 0;
}


// This is always called from a different process or thread
// thus, the main thread should be stopped to prevent dual control of the screen and input
static int triggerLock()
{
    // Upon boot, the screen is fighting for who wants to draw on it, so let's allow the other application perform its work first before stopping it
    sleep(5); 
    pid_t child_pid = kill_users_of(device.SCREEN.DEVICE, SIGSTOP);

    // Find user of /dev/fb0 so we can pause it
    LOG_DEBUG("muparentlock", "Triggering parental lock for child PID: %d", child_pid)
    // Need to stop the current process
//	if (!kill(child_pid, SIGSTOP)) 
    {
        // Backup current framebuffer content
        fb_info fb;
        if (open_fb(&fb) < 0) return -1;

        uint32_t * saved_pixels = malloc(fb.size);
        if (saved_pixels == NULL)
        {
            destroy_fb(&fb);
            perror("Not enough memory to save the current frame buffer");
            return -1;
        }
        memcpy(saved_pixels, fb.mem, fb.size);
    
        // Clear framebuffer
        memset(fb.mem, 0, fb.size);

        // Draw the lock screen
        int ret = muparentlock_main();
        LOG_DEBUG("muparentlock", "Lock screen done with %d", ret)

        if (ret == 1) {

            write_text_to_file(MUX_PARENTAUTH, "w", CHAR, "");

            // Ok, code is correct, let's restore the framebuffer here
            memcpy(fb.mem, saved_pixels, fb.size);
            destroy_fb(&fb);
            kill_users_of(device.SCREEN.DEVICE, SIGCONT);
            return 0;
        } else if (ret == 2) {
            LOG_INFO("muparentlock", "Shutting down now")
            kill_users_of(device.SCREEN.DEVICE, SIGTERM);
            sleep(2);
            kill_users_of(device.SCREEN.DEVICE, SIGKILL);
            memset(fb.mem, 0, fb.size);
            shutdown_now();
            return 0;
        }
    }
    perror("Error showing lock screen");
    return 1; 
}


void muxparentlock_savetracker(void)
{
    if (file_exist(MUX_PARENTLOCK_TRACKING)) {
        copy_file(MUX_PARENTLOCK_TRACKING, nv_counter_file);
        sync();
    }
}

static int get_actual_counter_file(char * counter_file, size_t arr_size)
{
    // First try the volatile version (since we prefer to save in priority to volatile mount to avoid wear on the SD card)
    if (file_exist(MUX_PARENTLOCK_TRACKING)) {
        strncpy(counter_file, MUX_PARENTLOCK_TRACKING, arr_size);
        return 0;
    }
    // Else try the non volatile version from the SD card	
    strncpy(counter_file, nv_counter_file, arr_size);
    return 0;
}

static void warn5MinLeft(char* icon_file)
{
    // Display the 5mn left sign for 5s 
    overlay_framebuffer(icon_file, 224, 144, 5);
}

static int process(void) 
{
    struct timespec boot, cur;
    time_t lastBoot = 0;
    unsigned additionalTime = 0, maxTimeForToday = 86400, fiveMinBefore = 86400;
    struct tm current;

    // If parent unlocked beforehand, let's avoid the whole tracking process
    // This file will be removed upon reboot or when leaving the config page 
    if (file_exist(MUX_PARENTAUTH)) return 0;
    // Save startup time (using monotonic clock that's not counting while the device is suspended)
    clock_gettime( CLOCK_MONOTONIC, &boot );

    // Load the last boot time file, to avoid gremlins from shutting down the device to reset the counter
    char counter_file[MAX_BUFFER_SIZE];
    if (get_actual_counter_file(counter_file, sizeof(counter_file))) return triggerLock();

    bool counter_file_valid = false;
    // We need to know what day of week we are
    time_t now = time(NULL);
    if (!localtime_r(&now, &current)) return triggerLock();

    if (file_exist(counter_file)) {
        
        struct tm previous;
        char * prev_run = read_line_char_from(counter_file, 1);

        if (strlen(prev_run) == 0) {
            LOG_WARN("muparentlock", "Found invalid time tracker state in %s", counter_file)
            // invalid time tracker state file usually stems from erratic reset 
            // -> could be an attempt to temper with parent controls, so lock for today
            additionalTime = maxTimeForToday;
        }
        else {
            LOG_INFO("muparentlock", "Loading time tracker from %s: %s", counter_file, prev_run)

            if (sscanf(prev_run, "%ld %u", &lastBoot, &additionalTime) == 2) { 
                if (!localtime_r(&lastBoot, &previous)) return triggerLock();

                // Get current day of week and check if it's valid
                if (previous.tm_wday != current.tm_wday || previous.tm_mday != current.tm_mday || previous.tm_mon != current.tm_mon) {
                    // current day is different from day of last time tracker state 
                    // -> reset available time
                    additionalTime = 0;
                }
                else {
                    counter_file_valid = true;
                }
            }
            else {
                LOG_WARN("muparentlock", "Found invalid time tracker state in %s", counter_file)
                // invalid time tracker state file usually stems from erratic restart 
                // -> could be an attempt to temper with parent controls, so lock for today
                additionalTime = maxTimeForToday;
            }
            free(prev_run);
        }
    }

    lastBoot = now;

    // Read configuration now to know what's the maximum allowed time for today
    load_parentlock(&parentlock, &device, config_file);
    {
        unsigned monday, tuesday, wednesday, thursday, friday, saturday, sunday;
        sscanf(parentlock.TIMES.SUNDAY, "%u", &sunday);
        sscanf(parentlock.TIMES.MONDAY, "%u", &monday);
        sscanf(parentlock.TIMES.TUESDAY, "%u", &tuesday);
        sscanf(parentlock.TIMES.WEDNESDAY, "%u", &wednesday);
        sscanf(parentlock.TIMES.THURSDAY, "%u", &thursday);
        sscanf(parentlock.TIMES.FRIDAY, "%u", &friday);
        sscanf(parentlock.TIMES.SATURDAY, "%u", &saturday);

        LOG_INFO("muparentlock", "Parent lock times: Monday %umn, Tuesday %umn, Wednesday %umn, Thursday: %umn, Friday: %umn, Saturday: %umn, Sunday: %umn",
                monday, tuesday, wednesday, thursday, friday, saturday, sunday)

    }


    switch (current.tm_wday)
    {
    case 0: // Sunday
        sscanf(parentlock.TIMES.SUNDAY, "%u", &maxTimeForToday);
        break;
    case 1: // Monday
        sscanf(parentlock.TIMES.MONDAY, "%u", &maxTimeForToday);
        break;
    case 2: // Tuesday
        sscanf(parentlock.TIMES.TUESDAY, "%u", &maxTimeForToday);
        break;
    case 3: // Wednesday
        sscanf(parentlock.TIMES.WEDNESDAY, "%u", &maxTimeForToday);
        break;
    case 4: // Thursday
        sscanf(parentlock.TIMES.THURSDAY, "%u", &maxTimeForToday);
        break;
    case 5: // Friday
        sscanf(parentlock.TIMES.FRIDAY, "%u", &maxTimeForToday);
        break;
    case 6: // Saturday
        sscanf(parentlock.TIMES.SATURDAY, "%u", &maxTimeForToday);
        break;
    default: return triggerLock();
    }

    // If 0: no limit else need to convert from min to sec
    if (!maxTimeForToday) {
        maxTimeForToday = 86400;
        fiveMinBefore = 86400;
    } else {
        maxTimeForToday = maxTimeForToday * 60;
        fiveMinBefore = maxTimeForToday - 300;
    }

    LOG_DEBUG("muparentlock", "Parent lock process created, maxTimeForToday %u/addtime %u", maxTimeForToday, additionalTime)

    // If we've already spent all time, let's lock too
    if (additionalTime >= maxTimeForToday) {
        if (!counter_file_valid) {
            // counter file invalid or not up-to-date, so rewrite both volatile and persistent version
            FILE * file = fopen(MUX_PARENTLOCK_TRACKING, "w");
            if (file) {
                fprintf(file, "%ld %u\n", lastBoot, additionalTime);
                fclose(file);
                copy_file(MUX_PARENTLOCK_TRACKING, nv_counter_file); 
            }
        }

        return triggerLock();
    }

    // Main process is dumb here, we are sleeping for 1mn and take the time, 
    // and write it to the counter or trigger the parental lock
    while (!exit_required)
    {
        unsigned elapsed = 0;
        // Wait for 1mn here or the console shutdown
        sleep(60);

        clock_gettime( CLOCK_MONOTONIC, &cur );

        elapsed = cur.tv_sec - boot.tv_sec + additionalTime;

        FILE * file = fopen(MUX_PARENTLOCK_TRACKING, "w");
        if (!file) return triggerLock();
        fprintf(file, "%ld %u\n", lastBoot, elapsed);
        fclose(file);

        if (elapsed >= maxTimeForToday) {
            copy_file(MUX_PARENTLOCK_TRACKING, nv_counter_file); 
            return triggerLock();
        }
        else if (elapsed >= fiveMinBefore && elapsed <= fiveMinBefore + 60) { 
            warn5MinLeft(five_min_warn_icon); fiveMinBefore = 86400; 
        }
    }
    return 0;
}

void load_device_old(struct mux_device *device) {
    char buffer[MAX_BUFFER_SIZE];

#define DEV_INT_FIELD(field, path)                                       \
    do {                                                                 \
        snprintf(buffer, sizeof(buffer), (RUN_PATH "device/%s"), path); \
        field = (int)({                                                  \
            char *ep;                                                    \
            long val = strtol(read_all_char_from(buffer), &ep, 10);      \
            *ep ? 0 : val;                                               \
        });                                                              \
    } while (0);

#define DEV_FLO_FIELD(field, path)                                       \
    do {                                                                 \
        snprintf(buffer, sizeof(buffer), (RUN_PATH "device/%s"), path); \
        field = (float)({                                                \
            char *ep;                                                    \
            double val = strtod(read_all_char_from(buffer), &ep);        \
            *ep ? 1.0 : val;                                             \
        });                                                              \
    } while (0);

#define DEV_STR_FIELD(field, path)                                       \
    do {                                                                 \
        snprintf(buffer, sizeof(buffer), (RUN_PATH "device/%s"), path); \
        strncpy(field, read_all_char_from(buffer), MAX_BUFFER_SIZE - 1); \
        field[MAX_BUFFER_SIZE - 1] = '\0';                               \
    } while (0);

#define DEV_MNT_FIELD(field, path)                                            \
    DEV_INT_FIELD(device->STORAGE.field.PARTITION, "storage/" path "/num"  ); \
    DEV_STR_FIELD(device->STORAGE.field.DEVICE,    "storage/" path "/dev"  ); \
    DEV_STR_FIELD(device->STORAGE.field.SEPARATOR, "storage/" path "/sep"  ); \
    DEV_STR_FIELD(device->STORAGE.field.MOUNT,     "storage/" path "/mount"); \
    DEV_STR_FIELD(device->STORAGE.field.TYPE,      "storage/" path "/type" ); \
    DEV_STR_FIELD(device->STORAGE.field.LABEL,     "storage/" path "/label");

#define DEV_ALG_FIELD(input, field, method, path)                                              \
    DEV_INT_FIELD(device->input.ANALOG.field.UP,    "input/" method "/analog/" path "/up"   ); \
    DEV_INT_FIELD(device->input.ANALOG.field.DOWN,  "input/" method "/analog/" path "/down" ); \
    DEV_INT_FIELD(device->input.ANALOG.field.LEFT,  "input/" method "/analog/" path "/left" ); \
    DEV_INT_FIELD(device->input.ANALOG.field.RIGHT, "input/" method "/analog/" path "/right"); \
    DEV_INT_FIELD(device->input.ANALOG.field.CLICK, "input/" method "/analog/" path "/click");

#define DEV_DPA_FIELD(input, method)                                        \
    DEV_INT_FIELD(device->input.DPAD.UP,    "input/" method "/dpad/up"   ); \
    DEV_INT_FIELD(device->input.DPAD.DOWN,  "input/" method "/dpad/down" ); \
    DEV_INT_FIELD(device->input.DPAD.LEFT,  "input/" method "/dpad/left" ); \
    DEV_INT_FIELD(device->input.DPAD.RIGHT, "input/" method "/dpad/right");

#define DEV_BTN_FIELD(input, method)                                                        \
    DEV_INT_FIELD(device->input.BUTTON.A,           "input/" method "/button/a"          ); \
    DEV_INT_FIELD(device->input.BUTTON.B,           "input/" method "/button/b"          ); \
    DEV_INT_FIELD(device->input.BUTTON.C,           "input/" method "/button/c"          ); \
    DEV_INT_FIELD(device->input.BUTTON.X,           "input/" method "/button/x"          ); \
    DEV_INT_FIELD(device->input.BUTTON.Y,           "input/" method "/button/y"          ); \
    DEV_INT_FIELD(device->input.BUTTON.Z,           "input/" method "/button/z"          ); \
    DEV_INT_FIELD(device->input.BUTTON.L1,          "input/" method "/button/l1"         ); \
    DEV_INT_FIELD(device->input.BUTTON.L2,          "input/" method "/button/l2"         ); \
    DEV_INT_FIELD(device->input.BUTTON.L3,          "input/" method "/button/l3"         ); \
    DEV_INT_FIELD(device->input.BUTTON.R1,          "input/" method "/button/r1"         ); \
    DEV_INT_FIELD(device->input.BUTTON.R2,          "input/" method "/button/r2"         ); \
    DEV_INT_FIELD(device->input.BUTTON.R3,          "input/" method "/button/r3"         ); \
    DEV_INT_FIELD(device->input.BUTTON.MENU_SHORT,  "input/" method "/button/menu_short" ); \
    DEV_INT_FIELD(device->input.BUTTON.MENU_LONG,   "input/" method "/button/menu_long"  ); \
    DEV_INT_FIELD(device->input.BUTTON.SELECT,      "input/" method "/button/select"     ); \
    DEV_INT_FIELD(device->input.BUTTON.START,       "input/" method "/button/start"      ); \
    DEV_INT_FIELD(device->input.BUTTON.SWITCH,      "input/" method "/button/switch"     ); \
    DEV_INT_FIELD(device->input.BUTTON.POWER_SHORT, "input/" method "/button/power_short"); \
    DEV_INT_FIELD(device->input.BUTTON.POWER_LONG,  "input/" method "/button/power_long" ); \
    DEV_INT_FIELD(device->input.BUTTON.VOLUME_UP,   "input/" method "/button/vol_up"     ); \
    DEV_INT_FIELD(device->input.BUTTON.VOLUME_DOWN, "input/" method "/button/vol_down"   );

    DEV_INT_FIELD(device->DEVICE.HAS_NETWORK, "board/network")
    DEV_INT_FIELD(device->DEVICE.HAS_BLUETOOTH, "board/bluetooth")
    DEV_INT_FIELD(device->DEVICE.HAS_PORTMASTER, "board/portmaster")
    DEV_INT_FIELD(device->DEVICE.HAS_LID, "board/lid")
    DEV_INT_FIELD(device->DEVICE.HAS_HDMI, "board/hdmi")
    DEV_INT_FIELD(device->DEVICE.EVENT, "board/event")
    DEV_INT_FIELD(device->DEVICE.DEBUGFS, "board/debugfs")
    DEV_STR_FIELD(device->DEVICE.NAME, "board/name")
    DEV_STR_FIELD(device->DEVICE.RTC_CLOCK, "board/rtc_clock")
    DEV_STR_FIELD(device->DEVICE.RTC_WAKE, "board/rtc_wake")
    DEV_STR_FIELD(device->DEVICE.LED, "board/led")

    DEV_INT_FIELD(device->AUDIO.MIN, "audio/min")
    DEV_INT_FIELD(device->AUDIO.MAX, "audio/max")

    DEV_INT_FIELD(device->MUX.WIDTH, "mux/width")
    DEV_INT_FIELD(device->MUX.HEIGHT, "mux/height")

    DEV_MNT_FIELD(BOOT, "boot")
    DEV_MNT_FIELD(ROM, "rom")
    DEV_MNT_FIELD(ROOT, "root")
    DEV_MNT_FIELD(SDCARD, "sdcard")
    DEV_MNT_FIELD(USB, "usb")

    DEV_STR_FIELD(device->CPU.DEFAULT, "cpu/default")
    DEV_STR_FIELD(device->CPU.AVAILABLE, "cpu/available")
    DEV_STR_FIELD(device->CPU.GOVERNOR, "cpu/governor")
    DEV_STR_FIELD(device->CPU.SCALER, "cpu/scaler")

    DEV_STR_FIELD(device->NETWORK.MODULE, "network/module")
    DEV_STR_FIELD(device->NETWORK.NAME, "network/name")
    DEV_STR_FIELD(device->NETWORK.TYPE, "network/type")
    DEV_STR_FIELD(device->NETWORK.INTERFACE, "network/iface")
    DEV_STR_FIELD(device->NETWORK.STATE, "network/state")

    DEV_INT_FIELD(device->SCREEN.BRIGHT, "screen/bright")
    DEV_INT_FIELD(device->SCREEN.WAIT, "screen/wait")
    DEV_STR_FIELD(device->SCREEN.DEVICE, "screen/device")
    DEV_STR_FIELD(device->SCREEN.HDMI, "screen/hdmi")
    DEV_INT_FIELD(device->SCREEN.WIDTH, "screen/width")
    DEV_INT_FIELD(device->SCREEN.HEIGHT, "screen/height")

    DEV_INT_FIELD(device->SCREEN.ROTATE, "screen/rotate")
    if (file_exist(RUN_PATH "device/screen/s_rotate")) DEV_INT_FIELD(device->SCREEN.ROTATE, "screen/s_rotate");

    DEV_FLO_FIELD(device->SCREEN.ZOOM, "screen/zoom")
    if (file_exist(RUN_PATH "device/screen/s_zoom")) DEV_FLO_FIELD(device->SCREEN.ZOOM, "screen/s_zoom");

    DEV_INT_FIELD(device->SCREEN.INTERNAL.WIDTH, "screen/internal/width")
    DEV_INT_FIELD(device->SCREEN.INTERNAL.HEIGHT, "screen/internal/height")
    DEV_INT_FIELD(device->SCREEN.EXTERNAL.WIDTH, "screen/external/width")
    DEV_INT_FIELD(device->SCREEN.EXTERNAL.HEIGHT, "screen/external/height")

    DEV_INT_FIELD(device->SDL.SCALER, "sdl/scaler")
    DEV_INT_FIELD(device->SDL.ROTATE, "sdl/rotate")

    DEV_STR_FIELD(device->BATTERY.CAPACITY, "battery/capacity")
    DEV_STR_FIELD(device->BATTERY.HEALTH, "battery/health")
    DEV_STR_FIELD(device->BATTERY.VOLTAGE, "battery/voltage")
    DEV_STR_FIELD(device->BATTERY.CHARGER, "battery/charger")

    DEV_INT_FIELD(device->INPUT_EVENT.AXIS, "input/axis")
    DEV_STR_FIELD(device->INPUT_EVENT.JOY_GENERAL, "input/general")
    DEV_STR_FIELD(device->INPUT_EVENT.JOY_POWER, "input/power")
    DEV_STR_FIELD(device->INPUT_EVENT.JOY_VOLUME, "input/volume")
    DEV_STR_FIELD(device->INPUT_EVENT.JOY_EXTRA, "input/extra")

    DEV_INT_FIELD(device->INPUT_CODE.DPAD.UP, "input/code/dpad/up")
    DEV_INT_FIELD(device->INPUT_CODE.DPAD.DOWN, "input/code/dpad/down")
    DEV_INT_FIELD(device->INPUT_CODE.DPAD.LEFT, "input/code/dpad/left")
    DEV_INT_FIELD(device->INPUT_CODE.DPAD.RIGHT, "input/code/dpad/right")

    DEV_ALG_FIELD(INPUT_CODE, LEFT, "code", "left")
    DEV_ALG_FIELD(INPUT_CODE, RIGHT, "code", "right")

    DEV_DPA_FIELD(INPUT_CODE, "code")
    DEV_BTN_FIELD(INPUT_CODE, "code")

    DEV_ALG_FIELD(INPUT_TYPE, LEFT, "type", "left")
    DEV_ALG_FIELD(INPUT_TYPE, RIGHT, "type", "right")

    DEV_DPA_FIELD(INPUT_TYPE, "type")
    DEV_BTN_FIELD(INPUT_TYPE, "type")

#undef DEV_INT_FIELD
#undef DEV_STR_FIELD
#undef DEV_MNT_FIELD
#undef DEV_ALG_FIELD
#undef DEV_DPA_FIELD
#undef DEV_BTN_FIELD
}

#undef CFG_INT_FIELD
#define CFG_INT_FIELD(field, path, def)                              \
    snprintf(buffer, sizeof(buffer), (RUN_PATH "global/%s"), path); \
    field = (int)({                                                  \
        char *ep;                                                    \
        long val = strtol(read_all_char_from(buffer), &ep, 10);      \
        *ep ? def : val;                                             \
    });

#undef CFG_STR_FIELD
#define CFG_STR_FIELD(field, path, def)                                     \
    snprintf(buffer, sizeof(buffer), (RUN_PATH "global/%s"), path);        \
    strncpy(field, read_all_char_from(buffer) ?: def, MAX_BUFFER_SIZE - 1); \
    field[MAX_BUFFER_SIZE - 1] = '\0';

void load_config_old(struct mux_config *config) {
    char buffer[MAX_BUFFER_SIZE];

    CFG_INT_FIELD(config->BOOT.FACTORY_RESET, "boot/factory_reset", 0)
    CFG_INT_FIELD(config->BOOT.DEVICE_MODE, "boot/device_mode", 0)

    CFG_INT_FIELD(config->CLOCK.NOTATION, "clock/notation", 0)
    CFG_STR_FIELD(config->CLOCK.POOL, "clock/pool", "pool.ntp.org")

    CFG_INT_FIELD(config->NETWORK.TYPE, "network/type", 0)
    CFG_STR_FIELD(config->NETWORK.INTERFACE, "network/interface", "wlan0")
    CFG_STR_FIELD(config->NETWORK.SSID, "network/ssid", "")
    CFG_STR_FIELD(config->NETWORK.PASS, "network/pass", "")
    CFG_INT_FIELD(config->NETWORK.SCAN, "network/scan", 0)
    CFG_STR_FIELD(config->NETWORK.ADDRESS, "network/address", "192.168.0.123")
    CFG_STR_FIELD(config->NETWORK.GATEWAY, "network/gateway", "192.168.0.1")
    CFG_STR_FIELD(config->NETWORK.SUBNET, "network/subnet", "24")
    CFG_STR_FIELD(config->NETWORK.DNS, "network/dns", "1.1.1.1")

    CFG_INT_FIELD(config->SETTINGS.ADVANCED.ACCELERATE, "settings/advanced/accelerate", 96)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.SWAP, "settings/advanced/swap", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.THERMAL, "settings/advanced/thermal", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.FONT, "settings/advanced/font", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.OFFSET, "settings/advanced/offset", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.LOCK, "settings/advanced/lock", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.LED, "settings/advanced/led", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.THEME, "settings/advanced/random_theme", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.RETROWAIT, "settings/advanced/retrowait", 0)
    CFG_STR_FIELD(config->SETTINGS.ADVANCED.USBFUNCTION, "settings/advanced/usb_function", "none")
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.VERBOSE, "settings/advanced/verbose", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.RUMBLE, "settings/advanced/rumble", 0)
    CFG_STR_FIELD(config->SETTINGS.ADVANCED.VOLUME, "settings/advanced/volume", "previous")
    CFG_STR_FIELD(config->SETTINGS.ADVANCED.BRIGHTNESS, "settings/advanced/brightness", "previous")
    CFG_STR_FIELD(config->SETTINGS.ADVANCED.STATE, "settings/advanced/state", "mem")
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.USERINIT, "settings/advanced/user_init", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.DPADSWAP, "settings/advanced/dpad_swap", 1)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.OVERDRIVE, "settings/advanced/overdrive", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.SWAPFILE, "settings/advanced/swapfile", 0)
    CFG_INT_FIELD(config->SETTINGS.ADVANCED.ZRAMFILE, "settings/advanced/zramfile", 0)
    CFG_STR_FIELD(config->SETTINGS.ADVANCED.CARDMODE, "settings/advanced/cardmode", "deadline")

    CFG_INT_FIELD(config->SETTINGS.GENERAL.HIDDEN, "settings/general/hidden", 0)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.SOUND, "settings/general/sound", 0)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.CHIME, "settings/general/chime", 0)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.BGM, "settings/general/bgm", 0)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.COLOUR, "settings/general/colour", 32)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.BRIGHTNESS, "settings/general/brightness", 90)
    CFG_INT_FIELD(config->SETTINGS.GENERAL.VOLUME, "settings/general/volume", 75)
    CFG_STR_FIELD(config->SETTINGS.GENERAL.STARTUP, "settings/general/startup", "launcher")
    CFG_STR_FIELD(config->SETTINGS.GENERAL.LANGUAGE, "settings/general/language", "English")
    CFG_INT_FIELD(config->SETTINGS.GENERAL.THEME_RESOLUTION, "settings/general/theme_resolution", 0)
    switch (config->SETTINGS.GENERAL.THEME_RESOLUTION) {
        case 1:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 640;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 480;
            break;
        case 2:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 720;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 480;
            break;
        case 3:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 720;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 576;
            break;
        case 4:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 720;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 720;
            break;
        case 5:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 1024;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 768;
            break;
        case 6:
            config->SETTINGS.GENERAL.THEME_RESOLUTION_WIDTH = 1280;
            config->SETTINGS.GENERAL.THEME_RESOLUTION_HEIGHT = 720;
            break;
    }

    CFG_INT_FIELD(config->SETTINGS.HDMI.RESOLUTION, "settings/hdmi/resolution", 0)
    CFG_INT_FIELD(config->SETTINGS.HDMI.SPACE, "settings/hdmi/space", 0)
    CFG_INT_FIELD(config->SETTINGS.HDMI.DEPTH, "settings/hdmi/depth", 0)
    CFG_INT_FIELD(config->SETTINGS.HDMI.RANGE, "settings/hdmi/range", 0)
    CFG_INT_FIELD(config->SETTINGS.HDMI.SCAN, "settings/hdmi/scan", 0)
    CFG_INT_FIELD(config->SETTINGS.HDMI.AUDIO, "settings/hdmi/audio", 0)

    CFG_INT_FIELD(config->SETTINGS.POWER.LOW_BATTERY, "settings/power/low_battery", 0)
    CFG_INT_FIELD(config->SETTINGS.POWER.SHUTDOWN, "settings/power/shutdown", 0)
    CFG_INT_FIELD(config->SETTINGS.POWER.IDLE_DISPLAY, "settings/power/idle_display", 0)
    CFG_INT_FIELD(config->SETTINGS.POWER.IDLE_SLEEP, "settings/power/idle_sleep", 0)

    CFG_INT_FIELD(config->VISUAL.BATTERY, "visual/battery", 1)
    CFG_INT_FIELD(config->VISUAL.NETWORK, "visual/network", 0)
    CFG_INT_FIELD(config->VISUAL.BLUETOOTH, "visual/bluetooth", 0)
    CFG_INT_FIELD(config->VISUAL.CLOCK, "visual/clock", 1)
    CFG_INT_FIELD(config->VISUAL.OVERLAY_IMAGE, "visual/overlayimage", 1)
    CFG_INT_FIELD(config->VISUAL.OVERLAY_TRANSPARENCY, "visual/overlaytransparency", 85)
    CFG_INT_FIELD(config->VISUAL.BOX_ART, "visual/boxart", 0)
    CFG_INT_FIELD(config->VISUAL.BOX_ART_ALIGN, "visual/boxartalign", 0)
    CFG_INT_FIELD(config->VISUAL.NAME, "visual/name", 0)
    CFG_INT_FIELD(config->VISUAL.DASH, "visual/dash", 0)
    CFG_INT_FIELD(config->VISUAL.FRIENDLYFOLDER, "visual/friendlyfolder", 1)
    CFG_INT_FIELD(config->VISUAL.THETITLEFORMAT, "visual/thetitleformat", 0)
    CFG_INT_FIELD(config->VISUAL.TITLEINCLUDEROOTDRIVE, "visual/titleincluderootdrive", 0)
    CFG_INT_FIELD(config->VISUAL.FOLDERITEMCOUNT, "visual/folderitemcount", 0)
    CFG_INT_FIELD(config->VISUAL.FOLDEREMPTY, "visual/folderempty", 0)
    CFG_INT_FIELD(config->VISUAL.COUNTERFOLDER, "visual/counterfolder", 1)
    CFG_INT_FIELD(config->VISUAL.COUNTERFILE, "visual/counterfile", 1)
    CFG_INT_FIELD(config->VISUAL.BACKGROUNDANIMATION, "visual/backgroundanimation", 0)
    CFG_INT_FIELD(config->VISUAL.LAUNCHSPLASH, "visual/launchsplash", 0)
    CFG_INT_FIELD(config->VISUAL.BLACKFADE, "visual/blackfade", 1)

    CFG_INT_FIELD(config->WEB.SSHD, "web/sshd", 1)
    CFG_INT_FIELD(config->WEB.SFTPGO, "web/sftpgo", 0)
    CFG_INT_FIELD(config->WEB.TTYD, "web/ttyd", 0)
    CFG_INT_FIELD(config->WEB.SYNCTHING, "web/syncthing", 0)
    CFG_INT_FIELD(config->WEB.RSLSYNC, "web/rslsync", 0)
    CFG_INT_FIELD(config->WEB.NTP, "web/ntp", 1)
    CFG_INT_FIELD(config->WEB.TAILSCALED, "web/tailscaled", 0)
}


int main(int argc, const char * argv[])
{
    if (file_exist(CONF_DEVICE_PATH "screen/device"))
    {
        load_device(&device);
        load_config(&config);
    }
    else 
    {	// This uglyness is due to MustardOS shifting files around in incompatible format
        load_device_old(&device);
        load_config_old(&config);
    }

    char buf[MAX_PATH_LEN] = {};
    size_t exe_path_len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    app_dir = dirname(buf);
    LOG_INFO("muparentlock", "App directory: %s", app_dir);

    snprintf(five_min_warn_icon, sizeof(five_min_warn_icon), "%s/5mnLeft.png", app_dir);
    snprintf(config_file, sizeof(config_file), "%s/parent_lock.ini", app_dir);
    
    int written = snprintf(nv_counter_file, sizeof(nv_counter_file), "%s/parent_ctr.txt", app_dir);
    if (written < 0 || (size_t) written >= sizeof(nv_counter_file)) {
        perror("Cannot create counter file path");
        return 1;
    }
    LOG_DEBUG("muparentlock", "counter file is expected at %s", nv_counter_file);

    if (file_exist(MUX_PARENTAUTH)) 
    {	// The code was entered, so let's prevent showing the lock screen again 
        return 0;
    }
    // Ok, run now
    signal(SIGTERM, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGCONT, sighandler);

    LOG_DEBUG("muparentlock", "Creating parent lock process")
    return process();
}
