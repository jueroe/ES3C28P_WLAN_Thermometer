// background.h
//
// Definitions and data structures for the background images used in the CYD thermometer application.
//

#pragma once

#include <stdint.h>
#include "gfx_color.h"

/* Mount point of the littlefs partition "storage" (see STORAGE_BASE_PATH in
 * storage.h) and the folder inside it.  The contents of data/ in the project
 * directory become the root of this file system, so the images live at
 * /littlefs/backgrounds/<season>/<time of day>/<name>. */
#define PATH_TO_BACKGROUND_IMAGES "/littlefs/backgrounds/"
#define AUTUMN_SUBPATH "autumn/"
#define WINTER_SUBPATH "winter/"
#define SPRING_SUBPATH "spring/"
#define SUMMER_SUBPATH "summer/"

typedef struct {
    const char *filename;
    uint16_t textcolor; /* colour of the overlaid text, GFX_RGB565() value from gfx_color.h */
} Background;

// 4 seasons, 4 day times, 3 backgrounds per day time for random selection.
// This results in a total of 4 * 4 * 3 = 48 backgrounds.
// The backgrounds are stored in the littlefs partition and are loaded at runtime.

/* Defined here for simplicity; `static const` keeps the table private to each
 * translation unit instead of producing a duplicate symbol at link time. */
static const Background backgrounds[4][4][3] = {
    { // first block "spring"
        { { "flowers-field.jpg", GFX_YELLOW }, { "north-lake-colorful.jpg", GFX_YELLOW }, { "yellow-balloon.jpg", GFX_BLACK } }, //daytime 0 "morning"
        { { "blossoms-blooming.jpg", GFX_BLACK }, { "canyon-arizona-reflection.jpg", GFX_BLACK }, { "daisy-field.jpg", GFX_BLACK } }, //daytime 1 "day"
        { { "lake-mirror-reflection.jpg", GFX_BLACK }, { "moon-rising-mountains.jpg", GFX_BLACK }, { "sunset-lavender.jpg", GFX_BLACK } }, //daytime 2 "evening"
        { { "boy-boat-glowing-sea.jpg", GFX_WHITE }, { "moonlit-lighthouse.jpg", GFX_WHITE }, { "the-boxhead-wanderer.jpg", GFX_WHITE } }  //daytime 3 "night"
    },
    { //second block "summer"
        { { "fairy-glen-sunrise.jpg", GFX_BLACK }, { "sunrise-lapland.jpg", GFX_BLACK }, { "sunset-hdr-color.jpg", GFX_YELLOW } }, //daytime 0 "morning"
        { { "canyon-sky-sandstone.jpg", GFX_MAGENTA }, { "dragon-ball-trunks.jpg", GFX_BLACK }, { "peaceful-daypixel-beach.jpg", GFX_BLACK } }, //daytime 1 "day"
        { { "lake-atitlan.jpg", GFX_BLUE }, { "ocean-sunset.jpg", GFX_BLUE }, { "refantazio-game.jpg", GFX_BLACK } }, //daytime 2 "evening"
        { { "boat-night-minimal-nature.jpg", GFX_WHITE }, { "boy-in-boat-glowing-sea.jpg", GFX_WHITE }, { "couple-with-lanterns.jpg", GFX_WHITE } }  //daytime 3 "night"
    },
    { //third block "autumn"
        { { "concrette-lighthouse.jpg", GFX_BLACK }, { "rainbow-mountains-peru.jpg", GFX_WHITE }, { "sunrise-greenland.jpg", GFX_WHITE } }, //daytime 0 "morning"
        { { "wild-flowers-desert.jpg", GFX_WHITE }, { "yellow-air-balloon.jpg", GFX_BLACK }, { "yoho-national-park.jpg", GFX_GREEN } }, //daytime 1 "day"
        { { "girl-beach-bike.jpg", GFX_YELLOW }, { "lavender-evening.jpg", GFX_WHITE }, { "retro-future-skyline.jpg", GFX_WHITE } }, //daytime 2 "evening"
        { { "half-moon-mountains.jpg", GFX_WHITE }, { "purple-galaxy-mountain.jpg", GFX_WHITE }, { "wolf-howling-moon.jpg", GFX_WHITE } }  //daytime 3 "night"
    },
    { //fourth block "winter"
        { { "northern-lights.jpg", GFX_WHITE }, { "snow-capped-park.jpg", GFX_WHITE }, { "sunrise-swiss.jpg", GFX_YELLOW } }, //daytime 0 "morning"
        { { "berry-branches-frost.jpg", GFX_BLACK }, { "mer-de-glace.jpg", GFX_BLACK }, { "schnee-sonne-berge.jpg", GFX_BLACK } }, //daytime 1 "day"
        { { "night-serenity.jpg", GFX_WHITE }, { "starlight-church-valley.jpg", GFX_WHITE }, { "winter-scene-lake.jpg", GFX_WHITE } }, //daytime 2 "evening"
        { { "beneath-the-meteor-shower.jpg", GFX_ORANGE }, { "northern-lights-mountain.jpg", GFX_WHITE }, { "starry-night-serenity.jpg", GFX_WHITE } }  //daytime 3 "night"
    }
};